/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuCanvasUploader.h"

#include <KisGpuBuffer.h>
#include <KisGpuCommandList.h>
#include <KisGpuContext.h>
#include <KisGpuGLSharedBuffer.h>

#include <KoColorModelStandardIds.h>
#include <KoColorProfile.h>
#include <KoColorSpace.h>

#include <QFloat16>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSet>
#include <QtEndian>

#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

#include <kis_debug.h>

#include "gpu/KisGpuMergeBatch.h"
#include "gpu/KisGpuTileAccess.h"
#include "gpu/KisGpuTileBackend.h"
#include "kis_paint_device.h"
#include "opengl/kis_texture_tile_update_info.h"

namespace
{
constexpr GLenum PixelUnpackBuffer = 0x88EC;
constexpr int CurveSize = 4096;
constexpr VkDeviceSize MinimumBufferSize = VkDeviceSize(8) << 20;
constexpr VkDeviceSize PatchAlignment = 256;

/// -1: not checked yet, 0: unavailable, 1: available
std::atomic<int> s_glInterop{-1};
std::atomic<quint64> s_uploadCount{0};

// Diagnostics (KRITA_GPU_CANVAS_DEBUG=1).
std::atomic<int> s_debugMessages{0};
constexpr int MaxDebugMessages = 400;

bool takeDebugSlot()
{
    return s_debugMessages.fetch_add(1) < MaxDebugMessages;
}

/// Byte offset of the patch center pixel inside the patch's uploadGeometry() layout.
quint64 patchCenterByteOffset(const KisTextureTileUpdateInfo &tile, quint32 pixelSize)
{
    const KisTextureTileUpdateInfo::UploadGeometry geometry = tile.uploadGeometry();
    const QSize center = tile.realPatchSize();
    const int width = geometry.extended ? geometry.bufferSize.width() : center.width();
    const int x = geometry.left + center.width() / 2;
    const int y = geometry.top + center.height() / 2;
    return (quint64(y) * width + x) * pixelSize;
}

QString debugPixel(const quint8 *data, quint32 pixelSize)
{
    float values[4] = {0, 0, 0, 0};
    if (pixelSize == 16) {
        memcpy(values, data, 16);
    } else {
        qfloat16 half[4];
        memcpy(half, data, 8);
        for (int i = 0; i < 4; i++) {
            values[i] = float(half[i]);
        }
    }
    return QStringLiteral("%1 %2 %3 %4").arg(values[0]).arg(values[1]).arg(values[2]).arg(values[3]);
}

// All buffers, including checked-out updates and failed imports, count
// against the limit. Only the GL thread can retire imported free buffers.
QMutex s_bufferMutex;
std::vector<KisGpuGLSharedBuffer *> *s_freeBuffers = new std::vector<KisGpuGLSharedBuffer *>();
std::vector<KisGpuGLSharedBuffer *> *s_retiredBuffers = new std::vector<KisGpuGLSharedBuffer *>();
quint64 s_bufferBytes = 0;
bool s_trimBuffers = false;

quint64 &bufferBudget()
{
    static quint64 budget = []() {
        bool valid = false;
        const quint64 requested = qEnvironmentVariable("KRITA_GPU_CANVAS_BUDGET_MIB").toULongLong(&valid);
        return (valid && requested > 0 && requested <= 1048576 ? requested : 512) << 20;
    }();
    return budget; // s_bufferMutex held
}

void collectBuffers(bool trimAll = false)
{
    if (!QOpenGLContext::currentContext())
        return;
    QMutexLocker locker(&s_bufferMutex);
    quint64 freeBytes = 0;
    for (auto *buffer : *s_freeBuffers)
        freeBytes += buffer->size();
    if (trimAll || s_trimBuffers || freeBytes > bufferBudget() / 4) {
        s_retiredBuffers->insert(s_retiredBuffers->end(), s_freeBuffers->begin(), s_freeBuffers->end());
        s_freeBuffers->clear();
        s_trimBuffers = false;
    }
    auto it = s_retiredBuffers->begin();
    while (it != s_retiredBuffers->end()) {
        auto *buffer = *it;
        if (buffer->prepareForDestruction()) {
            s_bufferBytes -= buffer->size();
            delete buffer;
            it = s_retiredBuffers->erase(it);
        } else {
            ++it; // Wrong share group or a failed drain: keep it charged.
        }
    }
}

KisGpuGLSharedBuffer *acquireBuffer(KisGpuContext &context, VkDeviceSize size, QString *errorMessage)
{
    QMutexLocker locker(&s_bufferMutex);
    // Smallest free buffer that is large enough.
    auto best = s_freeBuffers->end();
    for (auto it = s_freeBuffers->begin(); it != s_freeBuffers->end(); ++it) {
        if ((*it)->size() >= size && (best == s_freeBuffers->end() || (*it)->size() < (*best)->size())) {
            best = it;
        }
    }
    if (best != s_freeBuffers->end()) {
        KisGpuGLSharedBuffer *buffer = *best;
        s_freeBuffers->erase(best);
        return buffer;
    }
    VkDeviceSize capacity = MinimumBufferSize;
    while (capacity < size) {
        capacity *= 2;
    }
    // Avoid rounding over the limit when the exact request still fits.
    const quint64 available = bufferBudget() > s_bufferBytes ? bufferBudget() - s_bufferBytes : 0;
    if (capacity > available)
        capacity = size;
    if (capacity > available) {
        s_trimBuffers = true;
        if (errorMessage)
            *errorMessage = QStringLiteral("GPU canvas buffer budget reached");
        return nullptr;
    }
    auto buffer = KisGpuGLSharedBuffer::create(context, capacity, errorMessage);
    if (buffer)
        s_bufferBytes += capacity;
    return buffer.release();
}

void releaseBuffer(KisGpuGLSharedBuffer *buffer, bool failed = false)
{
    QMutexLocker locker(&s_bufferMutex);
    (failed ? s_retiredBuffers : s_freeBuffers)->push_back(buffer);
}

/// Per-thread resources, reused across uploads; begin() waits for the
/// previous use, so the writer's table buffer is never overwritten in use.
struct WorkContext {
    explicit WorkContext(KisGpuContext &context)
        : commands(context)
    {
    }
    KisGpuCommandList commands;
    std::unique_ptr<KisGpuCanvasPatchWriter> writers[4];
};

QMutex s_contextsMutex;
std::vector<std::unique_ptr<WorkContext>> *s_freeContexts = new std::vector<std::unique_ptr<WorkContext>>();

std::unique_ptr<WorkContext> acquireContext(KisGpuContext &context)
{
    QMutexLocker locker(&s_contextsMutex);
    if (!s_freeContexts->empty()) {
        std::unique_ptr<WorkContext> work = std::move(s_freeContexts->back());
        s_freeContexts->pop_back();
        return work;
    }
    return std::unique_ptr<WorkContext>(new WorkContext(context));
}

void releaseContext(std::unique_ptr<WorkContext> work)
{
    {
        QMutexLocker locker(&s_contextsMutex);
        if (s_freeContexts->size() < 4) {
            s_freeContexts->push_back(std::move(work));
            return;
        }
    }
    // Writer tables must outlive recorded work. Do not destroy them before
    // the command list (declared first in WorkContext) has completed.
    work->commands.wait();
}

bool isRgbaFloat(const KoColorSpace *cs)
{
    return cs && cs->colorModelId() == RGBAColorModelID
        && (cs->colorDepthId() == Float32BitsColorDepthID || cs->colorDepthId() == Float16BitsColorDepthID);
}

/// Inverse of a row-major 3x3 matrix; false if singular.
bool invert3x3(const double m[9], double out[9])
{
    const double det =
        m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (qFuzzyIsNull(det)) {
        return false;
    }
    const double inv = 1.0 / det;
    out[0] = (m[4] * m[8] - m[5] * m[7]) * inv;
    out[1] = (m[2] * m[7] - m[1] * m[8]) * inv;
    out[2] = (m[1] * m[5] - m[2] * m[4]) * inv;
    out[3] = (m[5] * m[6] - m[3] * m[8]) * inv;
    out[4] = (m[0] * m[8] - m[2] * m[6]) * inv;
    out[5] = (m[2] * m[3] - m[0] * m[5]) * inv;
    out[6] = (m[3] * m[7] - m[4] * m[6]) * inv;
    out[7] = (m[1] * m[6] - m[0] * m[7]) * inv;
    out[8] = (m[0] * m[4] - m[1] * m[3]) * inv;
    return true;
}

void multiply3x3(const double a[9], const double b[9], double out[9])
{
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
            double value = 0.0;
            for (int k = 0; k < 3; k++) {
                value += a[row * 3 + k] * b[k * 3 + column];
            }
            out[row * 3 + column] = value;
        }
    }
}

const double D50[3] = {0.9642, 1.0, 0.8249}; // LCMS cmsD50_XYZ()

/// Bradford chromatic adaptation from white @p from to white @p to (what LCMS cmsAdaptToIlluminant uses).
bool bradford(const double from[3], const double to[3], double out[9])
{
    static const double cone[9] = {0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296};
    double coneInverse[9];
    if (!invert3x3(cone, coneInverse)) {
        return false;
    }
    double scale[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 3; i++) {
        double rhoFrom = 0.0;
        double rhoTo = 0.0;
        for (int k = 0; k < 3; k++) {
            rhoFrom += cone[i * 3 + k] * from[k];
            rhoTo += cone[i * 3 + k] * to[k];
        }
        if (qFuzzyIsNull(rhoFrom) || !std::isfinite(rhoFrom) || !std::isfinite(rhoTo)) {
            return false;
        }
        scale[i * 3 + i] = rhoTo / rhoFrom;
    }
    double scaled[9];
    multiply3x3(scale, cone, scaled);
    multiply3x3(coneInverse, scaled, out);
    return true;
}

/**
 * RGB -> PCS XYZ (D50) matrix, row-major: the colorant tags LCMS builds the
 * matrix-shaper transform from. LcmsColorProfileContainer stores them
 * adapted from D50 to the media white point (Bradford), so that adaptation
 * is undone here; profiles with different white points then share the PCS.
 * False if the result is not a plausible matrix-shaper (columns not summing
 * to the D50 white).
 */
bool colorantMatrix(const KoColorProfile *profile, double out[9])
{
    const QVector<qreal> c = profile->getColorantsXYZ(); // rX rY rZ gX gY gZ bX bY bZ
    const QVector<qreal> white = profile->getWhitePointXYZ();
    if (c.size() != 9 || white.size() != 3) {
        return false;
    }
    double stored[9];
    for (int row = 0; row < 3; row++) {
        stored[row * 3 + 0] = c[0 + row];
        stored[row * 3 + 1] = c[3 + row];
        stored[row * 3 + 2] = c[6 + row];
    }
    const double mediaWhite[3] = {white[0], white[1], white[2]};
    double toD50[9];
    if (!bradford(mediaWhite, D50, toD50)) {
        return false;
    }
    multiply3x3(toD50, stored, out);

    for (int row = 0; row < 3; row++) {
        const double sum = out[row * 3 + 0] + out[row * 3 + 1] + out[row * 3 + 2];
        if (!std::isfinite(sum) || std::abs(sum - D50[row]) > 0.01) {
            return false;
        }
    }
    return true;
}

/**
 * True if the ICC data of @p profile has LUT-based transform tags (AToB*,
 * BToA*, DToB*, BToD*). LCMS prefers those over the matrix-shaper tags, so
 * such profiles stay on the CPU path. Also true if the data is not readable.
 */
bool hasLutTags(const KoColorProfile *profile)
{
    const QByteArray data = profile->rawData();
    constexpr int HeaderSize = 128;
    if (data.size() < HeaderSize + 4) {
        return true;
    }
    const uchar *bytes = reinterpret_cast<const uchar *>(data.constData());
    const quint32 count = qFromBigEndian<quint32>(bytes + HeaderSize);
    if (count > quint32((data.size() - HeaderSize - 4) / 12)) {
        return true;
    }
    for (quint32 i = 0; i < count; i++) {
        const uchar *signature = bytes + HeaderSize + 4 + i * 12;
        const QByteArray tag(reinterpret_cast<const char *>(signature), 4);
        if (tag.startsWith("A2B") || tag.startsWith("B2A") || tag.startsWith("D2B") || tag.startsWith("B2D")) {
            return true;
        }
    }
    return false;
}

QVector<float> sampleCurves(const KoColorProfile *profile, bool linearize)
{
    QVector<float> curves(3 * CurveSize);
    for (int i = 0; i < CurveSize; i++) {
        const qreal v = qreal(i) / (CurveSize - 1);
        QVector<qreal> rgb = {v, v, v};
        if (linearize) {
            profile->linearizeFloatValue(rgb);
        } else {
            profile->delinearizeFloatValue(rgb);
        }
        curves[i] = float(rgb[0]);
        curves[CurveSize + i] = float(rgb[1]);
        curves[2 * CurveSize + i] = float(rgb[2]);
    }
    return curves;
}

QMutex s_conversionMutex;
QHash<QByteArray, KisGpuCanvasPatchWriter::Conversion> *s_conversions =
    new QHash<QByteArray, KisGpuCanvasPatchWriter::Conversion>();
QSet<QByteArray> *s_unsupportedConversions = new QSet<QByteArray>();
} // namespace

KisGpuCanvasUpload::KisGpuCanvasUpload(KisGpuGLSharedBuffer *buffer, quint64 byteSize)
    : m_buffer(buffer)
    , m_byteSize(byteSize)
{
}

KisGpuCanvasUpload::~KisGpuCanvasUpload()
{
    KIS_SAFE_ASSERT_RECOVER_NOOP(!m_acquired || m_released);
    releaseBuffer(m_buffer, m_failed);
}

quint32 KisGpuCanvasUpload::glBuffer()
{
    return m_buffer->glBuffer();
}

bool KisGpuCanvasUpload::glAcquire()
{
    if (!m_acquired && !m_failed) {
        if (m_buffer->glAcquire()) {
            m_acquired = true;
        } else {
            m_failed = true;
        }
    }
    return m_acquired;
}

bool KisGpuCanvasUpload::hasFailed() const
{
    return m_failed;
}

bool KisGpuCanvasUpload::readBack(std::vector<quint8> *data)
{
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(!m_acquired, false);
    KisGpuTileBackend *backend = KisGpuTileBackend::existingInstance();
    if (!backend || backend->hasFailed()) {
        return false;
    }
    KisGpuContext &context = backend->context();
    std::unique_ptr<KisGpuBuffer> readback =
        KisGpuBuffer::create(context, m_byteSize, KisGpuBuffer::Location::Readback);
    if (!readback) {
        return false;
    }
    std::unique_ptr<WorkContext> work = acquireContext(context);
    KisGpuCommandList &commands = work->commands;
    if (!commands.isValid()) {
        releaseContext(std::move(work));
        return false;
    }
    commands.begin();
    VkBufferCopy region{};
    region.size = m_byteSize;
    context.vk().vkCmdCopyBuffer(commands.commandBuffer(), m_buffer->vulkanBuffer(), readback->handle(), 1, &region);
    commands.barrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                     VK_ACCESS_2_TRANSFER_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_HOST_BIT,
                     VK_ACCESS_2_HOST_READ_BIT);
    // Waits for the Vulkan write (and consumes its signal, which GL never will).
    const quint64 value = commands.submit(m_buffer->vulkanWriteWaits(), {});
    m_buffer->finishVulkanRead(value != 0);
    const bool done = value && commands.wait();
    releaseContext(std::move(work));
    if (!done) {
        return false;
    }
    const quint8 *bytes = static_cast<const quint8 *>(readback->mapped());
    data->assign(bytes, bytes + m_byteSize);
    return true;
}

bool KisGpuCanvasUpload::isAcquired() const
{
    return m_acquired && !m_released;
}

void KisGpuCanvasUpload::glRelease()
{
    if (m_acquired && !m_released) {
        m_buffer->glRelease();
        m_released = true;
    }
}

KisGpuCanvasUploadBinder::KisGpuCanvasUploadBinder(KisGpuCanvasUpload *upload, QOpenGLFunctions *f)
    : m_f(f)
{
    m_f->glBindBuffer(PixelUnpackBuffer, upload->glBuffer());
}

KisGpuCanvasUploadBinder::~KisGpuCanvasUploadBinder()
{
    m_f->glBindBuffer(PixelUnpackBuffer, 0);
}

void KisGpuCanvasUploader::checkGLInterop()
{
    if (s_glInterop.load() >= 0 || !KisGpuMergeBatch::isEnabled()) {
        return;
    }
    KisGpuTileBackend *backend = KisGpuTileBackend::instance();
    QString reason;
    const bool available = backend && KisGpuGLSharedBuffer::testGLInterop(backend->context(), &reason);
    s_glInterop.store(available ? 1 : 0);
    if (!available) {
        warnUI << "GPU engine: the canvas reads the projection on the CPU:" << reason;
    } else if (debugEnabled()) {
        warnUI << "GPU canvas debug: interop self-test passed (1 MiB, two patterns), context"
               << QOpenGLContext::currentContext() << "share group" << QOpenGLContext::currentContext()->shareGroup();
    }
}

void KisGpuCanvasUploader::resetGLInteropForTesting()
{
    s_glInterop.store(-1);
}

void KisGpuCanvasUploader::setGLInteropAvailable(bool available)
{
    s_glInterop.store(available ? 1 : 0);
}

bool KisGpuCanvasUploader::isEnabled()
{
    if (s_glInterop.load() != 1 || !KisGpuMergeBatch::isEnabled()) {
        return false;
    }
    KisGpuTileBackend *backend = KisGpuTileBackend::existingInstance();
    return backend && !backend->hasFailed();
}

bool KisGpuCanvasUploader::debugEnabled()
{
    static const bool enabled = qEnvironmentVariableIntValue("KRITA_GPU_CANVAS_DEBUG") == 1;
    return enabled;
}

void KisGpuCanvasUploader::debugLogBuild(bool onGpu, const QString &reason, const QRect &rect)
{
    if (!debugEnabled() || !takeDebugSlot()) {
        return;
    }
    qWarning().noquote() << "GPU canvas debug: update" << rect << (onGpu ? "GPU path" : "CPU path") << "interop"
                         << s_glInterop.load() << "projection enabled" << KisGpuMergeBatch::isEnabled()
                         << "reason:" << reason;
}

bool KisGpuCanvasUploader::debugInspectBuffer(const KisTextureTileUpdateInfo &tile)
{
    KisGpuCanvasUpload *upload = tile.gpuUpload();
    if (!debugEnabled() || !upload || !upload->isAcquired()) {
        return false;
    }
    // GUI thread only: two tiles per upload.
    static const KisGpuCanvasUpload *lastUpload = nullptr;
    static int inspected = 0;
    if (upload != lastUpload) {
        lastUpload = upload;
        inspected = 0;
    }
    if (inspected++ >= 2 || !takeDebugSlot()) {
        return false;
    }
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if (!context) {
        qWarning() << "GPU canvas debug: no current GL context while uploading";
        return false;
    }
    using GetBufferSubData = void(QOPENGLF_APIENTRY *)(GLenum, GLintptr, GLsizeiptr, void *);
    using CopyBufferSubData = void(QOPENGLF_APIENTRY *)(GLenum, GLenum, GLintptr, GLintptr, GLsizeiptr);
    auto getBufferSubData = reinterpret_cast<GetBufferSubData>(context->getProcAddress("glGetBufferSubData"));
    auto copyBufferSubData = reinterpret_cast<CopyBufferSubData>(context->getProcAddress("glCopyBufferSubData"));
    QString value = QStringLiteral("unavailable");
    const quint32 pixelSize = tile.pixelSize();
    if (getBufferSubData && copyBufferSubData) {
        quint8 raw[16] = {};
        // Direct reads of imported memory may return a stale CPU shadow.
        // Inspect what the GPU sees, as the canvas texture upload does.
        constexpr GLenum copyRead = 0x8F36;
        constexpr GLenum copyWrite = 0x8F37;
        auto *f = context->functions();
        GLint previousRead = 0;
        GLint previousWrite = 0;
        f->glGetIntegerv(copyRead, &previousRead);
        f->glGetIntegerv(copyWrite, &previousWrite);
        GLuint readback = 0;
        f->glGenBuffers(1, &readback);
        f->glBindBuffer(copyWrite, readback);
        f->glBufferData(copyWrite, sizeof(raw), nullptr, GL_STREAM_READ);
        f->glBindBuffer(copyRead, upload->glBuffer());
        copyBufferSubData(copyRead,
                          copyWrite,
                          GLintptr(tile.gpuByteOffset() + patchCenterByteOffset(tile, pixelSize)),
                          0,
                          pixelSize);
        getBufferSubData(copyWrite, 0, pixelSize, raw);
        f->glDeleteBuffers(1, &readback);
        f->glBindBuffer(copyRead, GLuint(previousRead));
        f->glBindBuffer(copyWrite, GLuint(previousWrite));
        value = debugPixel(raw, pixelSize);
    }
    qWarning().noquote() << "GPU canvas debug: GL GPU-copy reads tile" << tile.tileCol() << tile.tileRow() << "lod"
                         << tile.patchLevelOfDetail() << "buffer" << upload->glBuffer() << "offset"
                         << tile.gpuByteOffset() << "center" << value << "GL error"
                         << context->functions()->glGetError();
    return true;
}

void KisGpuCanvasUploader::debugInspectTexture(const KisTextureTileUpdateInfo &tile, quint32 format, quint32 type)
{
    if (!debugEnabled() || !takeDebugSlot()) {
        return;
    }
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if (!context) {
        return;
    }
    const int level = tile.patchLevelOfDetail();
    using GetTexLevelParameteriv = void(QOPENGLF_APIENTRY *)(GLenum, GLint, GLenum, GLint *);
    using GetTexImage = void(QOPENGLF_APIENTRY *)(GLenum, GLint, GLenum, GLenum, void *);
    auto levelParameter = reinterpret_cast<GetTexLevelParameteriv>(context->getProcAddress("glGetTexLevelParameteriv"));
    auto getTexImage = reinterpret_cast<GetTexImage>(context->getProcAddress("glGetTexImage"));
    if (!levelParameter || !getTexImage) {
        return;
    }
    GLint width = 0;
    GLint height = 0;
    levelParameter(GL_TEXTURE_2D, level, 0x1000 /* GL_TEXTURE_WIDTH */, &width);
    levelParameter(GL_TEXTURE_2D, level, 0x1001 /* GL_TEXTURE_HEIGHT */, &height);
    if (width <= 0 || height <= 0) {
        return;
    }
    std::vector<float> pixels(size_t(width) * height * 4);
    getTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_FLOAT, pixels.data());
    const QPoint center =
        tile.realPatchOffset() + QPoint(tile.realPatchSize().width() / 2, tile.realPatchSize().height() / 2);
    const size_t index = (size_t(qBound(0, center.y(), height - 1)) * width + qBound(0, center.x(), width - 1)) * 4;
    qWarning().noquote() << "GPU canvas debug: texture of tile" << tile.tileCol() << tile.tileRow() << "level" << level
                         << "path" << (tile.gpuUpload() ? "GPU" : "CPU") << "texture" << width << "x" << height
                         << "format" << Qt::hex << format << type << Qt::dec << "center" << center << "value"
                         << pixels[index] << pixels[index + 1] << pixels[index + 2] << pixels[index + 3] << "GL error"
                         << context->functions()->glGetError();
}

quint64 KisGpuCanvasUploader::uploadCount()
{
    return s_uploadCount.load();
}

quint64 KisGpuCanvasUploader::reservedBufferBytes()
{
    QMutexLocker locker(&s_bufferMutex);
    return s_bufferBytes;
}

quint64 KisGpuCanvasUploader::bufferMemoryBudget()
{
    QMutexLocker locker(&s_bufferMutex);
    return bufferBudget();
}

quint64 KisGpuCanvasUploader::setBufferMemoryBudgetForTesting(quint64 bytes)
{
    QMutexLocker locker(&s_bufferMutex);
    const quint64 previous = bufferBudget();
    bufferBudget() = bytes;
    return previous;
}

void KisGpuCanvasUploader::trimBuffersForTesting()
{
    collectBuffers(true);
}

bool KisGpuCanvasUploader::conversionFor(const KoColorSpace *src,
                                         const KoColorSpace *dst,
                                         KoColorConversionTransformation::Intent intent,
                                         KisGpuCanvasPatchWriter::Conversion *conversion)
{
    if (!isRgbaFloat(src) || !isRgbaFloat(dst) || !src->profile() || !dst->profile()) {
        return false;
    }
    const KoColorProfile *srcProfile = src->profile();
    const KoColorProfile *dstProfile = dst->profile();

    if (*srcProfile == *dstProfile) {
        *conversion = KisGpuCanvasPatchWriter::Conversion();
        return true;
    }

    // Absolute colorimetric also adapts the white point.
    if (intent == KoColorConversionTransformation::IntentAbsoluteColorimetric) {
        return false;
    }

    const QByteArray key = srcProfile->uniqueId() + '|' + dstProfile->uniqueId();
    QMutexLocker locker(&s_conversionMutex);
    auto it = s_conversions->constFind(key);
    if (it != s_conversions->constEnd()) {
        *conversion = it.value();
        return true;
    }
    if (s_unsupportedConversions->contains(key)) {
        return false;
    }

    // Matrix-shaper profiles only (no LUT tags LCMS would use instead).
    double srcToXyz[9];
    double dstToXyz[9];
    double xyzToDst[9];
    if (!srcProfile->hasColorants() || !dstProfile->hasColorants() || !srcProfile->hasTRC() || !dstProfile->hasTRC()
        || hasLutTags(srcProfile) || hasLutTags(dstProfile) || !colorantMatrix(srcProfile, srcToXyz)
        || !colorantMatrix(dstProfile, dstToXyz) || !invert3x3(dstToXyz, xyzToDst)) {
        s_unsupportedConversions->insert(key);
        return false;
    }

    KisGpuCanvasPatchWriter::Conversion result;
    result.mode = KisGpuCanvasPatchWriter::Conversion::MatrixShaper;
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
            double value = 0.0;
            for (int k = 0; k < 3; k++) {
                value += xyzToDst[row * 3 + k] * srcToXyz[k * 3 + column];
            }
            result.matrix[row * 3 + column] = float(value);
        }
    }
    result.curveSize = CurveSize;
    if (!srcProfile->isLinear()) {
        result.srcCurves = sampleCurves(srcProfile, true);
    }
    if (!dstProfile->isLinear()) {
        result.dstCurves = sampleCurves(dstProfile, false);
    }

    s_conversions->insert(key, result);
    *conversion = result;
    return true;
}

bool KisGpuCanvasUploader::upload(KisPaintDeviceSP projection,
                                  const QVector<KisTextureTileUpdateInfoSP> &tiles,
                                  const KoColorSpace *dstColorSpace,
                                  KoColorConversionTransformation::Intent intent,
                                  KoColorConversionTransformation::ConversionFlags flags,
                                  QString *errorMessage)
{
    Q_UNUSED(flags); // matrix-shaper conversions ignore them (see docs/agent/gpu-engine.md)

    if (tiles.isEmpty()) {
        return true;
    }
    auto decline = [errorMessage](const QString &reason) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = reason;
        }
        return false;
    };
    if (!isEnabled()) {
        return decline(QStringLiteral("GPU canvas disabled"));
    }
    if (!KisGpuTileAccess::isSupported(projection, errorMessage)) {
        return decline(QStringLiteral("projection not supported"));
    }
    if (!isRgbaFloat(dstColorSpace)) {
        return decline(QStringLiteral("display color space %1 is not RGBA float")
                           .arg(dstColorSpace ? dstColorSpace->id() : QStringLiteral("none")));
    }
    KisGpuCanvasPatchWriter::Conversion conversion;
    if (!conversionFor(projection->colorSpace(), dstColorSpace, intent, &conversion)) {
        return decline(
            QStringLiteral("conversion %1 (%2) -> %3 (%4) not supported")
                .arg(projection->colorSpace()->id(),
                     projection->colorSpace()->profile() ? projection->colorSpace()->profile()->name() : QString(),
                     dstColorSpace->id(),
                     dstColorSpace->profile() ? dstColorSpace->profile()->name() : QString()));
    }

    KisGpuTileBackend *backend = KisGpuTileBackend::instance();
    KisGpuContext &context = backend->context();
    const bool halfSource = projection->pixelSize() == 8;
    const bool halfOutput = dstColorSpace->pixelSize() == 8;
    const quint32 outputPixelSize = dstColorSpace->pixelSize();

    // Source rect: union of the patch centers (in the current LOD plane).
    QRect sourceRect;
    for (const KisTextureTileUpdateInfoSP &tile : tiles) {
        sourceRect |= tile->realPatchRect();
    }

    std::unique_ptr<WorkContext> work = acquireContext(context);
    std::unique_ptr<KisGpuCanvasPatchWriter> &writer = work->writers[(halfSource ? 2 : 0) + (halfOutput ? 1 : 0)];
    if (!writer) {
        writer = KisGpuCanvasPatchWriter::create(context,
                                                 halfSource ? KisGpuTileFormat::RGBA16F : KisGpuTileFormat::RGBA32F,
                                                 halfOutput,
                                                 errorMessage);
    }
    if (!writer || !work->commands.isValid()) {
        releaseContext(std::move(work));
        return false;
    }

    KisGpuCommandList &commands = work->commands;
    commands.begin();
    KisGpuTileAccess access(projection, sourceRect, KisGpuTileAccess::ReadOnly);
    if (!access.prepare(commands, errorMessage)) {
        KisGpuTileAccess::finishUnsubmitted(commands, {&access});
        releaseContext(std::move(work));
        return false;
    }
    const QRect grid = access.tileGrid();
    const QPoint gridOrigin = access.tileOrigin(grid.left(), grid.top());

    QVector<KisGpuCanvasPatchWriter::Patch> patches;
    QVector<quint64> byteOffsets;
    VkDeviceSize totalBytes = 0;
    for (const KisTextureTileUpdateInfoSP &tile : tiles) {
        const KisTextureTileUpdateInfo::UploadGeometry geometry = tile->uploadGeometry();
        const QRect center = tile->realPatchRect();

        KisGpuCanvasPatchWriter::Patch patch;
        patch.srcOrigin = center.topLeft() - gridOrigin;
        patch.centerSize = center.size();
        patch.margin = QPoint(geometry.left, geometry.top);
        patch.bufferSize = geometry.extended ? geometry.bufferSize : center.size();
        patch.dstOffset = quint32(totalBytes / outputPixelSize);
        patches << patch;
        byteOffsets << totalBytes;

        const VkDeviceSize bytes = VkDeviceSize(patch.bufferSize.width()) * patch.bufferSize.height() * outputPixelSize;
        totalBytes += (bytes + PatchAlignment - 1) / PatchAlignment * PatchAlignment;
    }

    KisGpuGLSharedBuffer *buffer = acquireBuffer(context, totalBytes, errorMessage);
    if (!buffer) {
        KisGpuTileAccess::finishUnsubmitted(commands, {&access});
        releaseContext(std::move(work));
        return false;
    }

    commands.computeBarrier();
    if (!writer->record(commands,
                        access.addresses(),
                        grid.width(),
                        patches,
                        buffer->deviceAddress(),
                        conversion,
                        errorMessage)) {
        KisGpuTileAccess::finishUnsubmitted(commands, {&access});
        releaseBuffer(buffer);
        releaseContext(std::move(work));
        return false;
    }

    // Diagnostics: copy the center pixel of the first patches to the CPU.
    std::unique_ptr<KisGpuBuffer> probe;
    const int probeCount = debugEnabled() ? qMin(2, int(patches.size())) : 0;
    if (probeCount) {
        probe = KisGpuBuffer::create(context, VkDeviceSize(16) * probeCount, KisGpuBuffer::Location::Readback);
    }
    if (probe) {
        commands.computeBarrier();
        for (int i = 0; i < probeCount; i++) {
            VkBufferCopy region{};
            region.srcOffset = byteOffsets[i] + patchCenterByteOffset(*tiles[i], outputPixelSize);
            region.dstOffset = VkDeviceSize(16) * i;
            region.size = outputPixelSize;
            context.vk().vkCmdCopyBuffer(commands.commandBuffer(), buffer->vulkanBuffer(), probe->handle(), 1, &region);
        }
        commands.barrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                         VK_ACCESS_2_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_HOST_BIT,
                         VK_ACCESS_2_HOST_READ_BIT);
    }

    const quint64 value = KisGpuTileAccess::submitAndFinish(commands,
                                                            {&access},
                                                            buffer->vulkanWriteWaits(),
                                                            {buffer->vulkanDoneSignal()});
    buffer->finishVulkanWrite(value != 0);
    if (probe && value && commands.wait() && takeDebugSlot()) {
        QString text;
        for (int i = 0; i < probeCount; i++) {
            text += QStringLiteral(" tile %1,%2 lod %3: %4;")
                        .arg(tiles[i]->tileCol())
                        .arg(tiles[i]->tileRow())
                        .arg(tiles[i]->patchLevelOfDetail())
                        .arg(debugPixel(static_cast<const quint8 *>(probe->mapped()) + 16 * i, outputPixelSize));
        }
        qWarning().noquote() << "GPU canvas debug: Vulkan wrote" << patches.size() << "patches from" << grid.width()
                             << "x" << grid.height() << "source tiles, source" << projection->colorSpace()->id()
                             << "display" << dstColorSpace->id() << "conversion mode" << int(conversion.mode)
                             << "centers:" << text;
    }
    releaseContext(std::move(work));
    if (!value) {
        releaseBuffer(buffer);
        return false;
    }

    QSharedPointer<KisGpuCanvasUpload> upload(new KisGpuCanvasUpload(buffer, totalBytes));
    for (int i = 0; i < tiles.size(); i++) {
        tiles[i]->setGpuUpload(upload, byteOffsets[i], dstColorSpace);
    }
    s_uploadCount++;
    return true;
}

bool KisGpuCanvasUploader::acquire(const QVector<KisTextureTileUpdateInfoSP> &tiles)
{
    collectBuffers();
    bool ok = true;
    QSet<KisGpuCanvasUpload *> done;
    for (const KisTextureTileUpdateInfoSP &tile : tiles) {
        KisGpuCanvasUpload *upload = tile->gpuUpload();
        if (upload && !done.contains(upload)) {
            done.insert(upload);
            ok = upload->glAcquire() && ok;
        }
    }
    if (!ok && s_glInterop.exchange(0) == 1) {
        warnUI << "GPU engine: OpenGL cannot import a shared canvas buffer; the canvas reads the projection on the CPU";
    }
    return ok;
}

void KisGpuCanvasUploader::release(const QVector<KisTextureTileUpdateInfoSP> &tiles)
{
    QSet<KisGpuCanvasUpload *> done;
    for (const KisTextureTileUpdateInfoSP &tile : tiles) {
        KisGpuCanvasUpload *upload = tile->gpuUpload();
        if (upload && !done.contains(upload)) {
            done.insert(upload);
            upload->glRelease();
        }
    }
}

bool KisGpuCanvasUploader::readBackFailedUploads(const QVector<KisTextureTileUpdateInfoSP> &tiles)
{
    QHash<KisGpuCanvasUpload *, std::vector<quint8>> contents;
    for (const KisTextureTileUpdateInfoSP &tile : tiles) {
        KisGpuCanvasUpload *upload = tile->gpuUpload();
        if (!upload || !upload->hasFailed()) {
            continue;
        }
        auto it = contents.find(upload);
        if (it == contents.end()) {
            std::vector<quint8> data;
            if (!upload->readBack(&data)) {
                warnUI << "GPU engine: cannot read back a canvas update that OpenGL could not import";
                return false;
            }
            it = contents.insert(upload, std::move(data));
        }
        tile->replaceGpuUploadWithPixels(it.value().data());
    }
    return true;
}
