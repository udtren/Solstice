/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "KisBrushStrokePreviewCache.h"
#include "KisBrushStrokePreviewRenderer.h"
#include <KisDocument.h>
#include <KisGlobalResourcesInterface.h>
#include <KisPart.h>
#include <KisResourceLocator.h>
#include <KisResourceModel.h>
#include <KisResourceModelProvider.h>
#include <KisResourceStorage.h>
#include <KisSolsticePaths.h>
#include <KoResourceLoadResult.h>
#include <QApplication>
#include <QCache>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent>
#include <kis_image.h>
#include <kis_paintop_preset.h>

namespace
{
struct DiskResult {
    bool hit = false;
    QImage image;
};
QString digest(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
} // namespace

struct KisBrushStrokePreviewCache::Private {
    QString directory;
    QHash<QObject *, QList<Request>> consumers;
    QHash<QString, QString> keys;
    QCache<QString, QImage> images{300};
    QSet<QString> empty;
    QSet<KisImage *> observed;
    QSet<KisDocument *> documents;
    QTimer timer;
    KisBrushStrokePreviewRenderer renderer;
    QFutureWatcher<DiskResult> disk;
    Request active;
    QString activeKey;
    KisPaintOpPresetSP preset;
    quint64 generation = 0, activeGeneration = 0;
    bool initialized = false, writing = false, diskBusy = false;
};

QString KisBrushStrokePreviewCache::Request::id() const
{
    return digest((location + QChar(0) + filename + QChar(0) + md5).toUtf8());
}
KisBrushStrokePreviewCache::Request KisBrushStrokePreviewCache::Request::fromIndex(const QModelIndex &index)
{
    return {index.data(Qt::UserRole + KisAbstractResourceModel::Location).toString(),
            index.data(Qt::UserRole + KisAbstractResourceModel::Filename).toString(),
            index.data(Qt::UserRole + KisAbstractResourceModel::MD5).toString()};
}
KisBrushStrokePreviewCache *KisBrushStrokePreviewCache::instance()
{
    static auto *cache =
        new KisBrushStrokePreviewCache(KisSolsticePaths::cacheDir() + "/brush-stroke-previews/v1", qApp);
    return cache;
}
KisBrushStrokePreviewCache::KisBrushStrokePreviewCache(const QString &directory, QObject *parent)
    : QObject(parent)
    , m_d(new Private)
{
    m_d->directory = directory;
    m_d->timer.setInterval(50);
    connect(&m_d->timer, &QTimer::timeout, this, &KisBrushStrokePreviewCache::process);
    connect(&m_d->renderer, &KisBrushStrokePreviewRenderer::finished, this, &KisBrushStrokePreviewCache::finish);
    connect(&m_d->disk, &QFutureWatcher<DiskResult>::finished, this, [this]() {
        m_d->diskBusy = false;
        if (m_d->writing) {
            m_d->writing = false;
            return;
        }
        if (m_d->activeGeneration != m_d->generation || !wanted(m_d->active.id())) {
            m_d->preset.clear();
            return;
        }
        const auto result = m_d->disk.result();
        if (result.hit) {
            if (result.image.isNull())
                m_d->empty.insert(m_d->activeKey);
            else
                m_d->images.insert(m_d->activeKey, new QImage(result.image));
            m_d->preset.clear();
            Q_EMIT previewReady();
        }
        // A miss is left pending until process() confirms every canvas is idle.
    });
}
KisBrushStrokePreviewCache::~KisBrushStrokePreviewCache() = default;

void KisBrushStrokePreviewCache::setRequests(QObject *consumer, const QList<Request> &requests)
{
    if (!m_d->consumers.contains(consumer))
        connect(consumer, &QObject::destroyed, this, [this, consumer]() {
            removeConsumer(consumer);
        });
    m_d->consumers.insert(consumer, requests);
    m_d->timer.start();
    if (m_d->renderer.isRunning() && !wanted(m_d->active.id()))
        m_d->renderer.cancel();
}
void KisBrushStrokePreviewCache::removeConsumer(QObject *consumer)
{
    m_d->consumers.remove(consumer);
    if (!wanted(m_d->active.id()))
        m_d->renderer.cancel();
}
bool KisBrushStrokePreviewCache::wanted(const QString &id) const
{
    for (const auto &requests : m_d->consumers)
        for (const auto &request : requests)
            if (request.id() == id)
                return true;
    return false;
}
QImage KisBrushStrokePreviewCache::preview(const Request &request) const
{
    const auto *image = m_d->images.object(m_d->keys.value(request.id()));
    return image ? *image : QImage();
}
bool KisBrushStrokePreviewCache::ready(const Request &request) const
{
    const auto key = m_d->keys.value(request.id());
    return m_d->images.contains(key) || m_d->empty.contains(key);
}
bool KisBrushStrokePreviewCache::isBusy() const
{
    return m_d->diskBusy || m_d->renderer.isRunning();
}
void KisBrushStrokePreviewCache::invalidate()
{
    ++m_d->generation;
    m_d->keys.clear();
    m_d->images.clear();
    m_d->empty.clear();
    m_d->renderer.cancel();
    Q_EMIT previewReady();
}
QString KisBrushStrokePreviewCache::cacheKey(KisPaintOpPresetSP preset, const QString &savedMd5)
{
    QStringList dependencies;
    auto resources = KisGlobalResourcesInterface::instance();
    const auto links = preset->linkedResources(resources) + preset->embeddedResources(resources);
    for (const auto &link : links) {
        const auto resource = link.resource();
        const auto signature = link.signature();
        dependencies << signature.type + ':' + (resource ? resource->md5Sum() : signature.md5sum);
    }
    dependencies.sort();
    return digest((savedMd5 + '|' + dependencies.join('|')).toUtf8()) + "-480x160";
}
void KisBrushStrokePreviewCache::prune(const QString &directory)
{
    QDir dir(directory);
    QDir parent = dir;
    if (parent.cdUp() && parent.dirName() == "brush-stroke-previews") {
        const QRegularExpression version("^v[0-9]+$");
        for (const auto &entry : parent.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
            if (entry.fileName() != dir.dirName() && version.match(entry.fileName()).hasMatch())
                QDir(entry.absoluteFilePath()).removeRecursively();
        }
    }
    const auto files = dir.entryInfoList({"*.png", "*.none"}, QDir::Files | QDir::NoSymLinks, QDir::Time);
    qint64 bytes = 0;
    const auto cutoff = QDateTime::currentDateTimeUtc().addDays(-60);
    for (const auto &file : files) {
        bytes += file.size();
        if (file.lastModified() < cutoff || bytes > 256LL * 1024 * 1024)
            dir.remove(file.fileName());
    }
}
void KisBrushStrokePreviewCache::process()
{
    if (!m_d->initialized) {
        m_d->initialized = true;
        connect(KisPart::instance(), &KisPart::sigDocumentAdded, this, &KisBrushStrokePreviewCache::observeDocument);
        // Created lazily by a visible chooser, after the main window exists.
        for (const QString &type :
             {ResourceType::PaintOpPresets, ResourceType::Brushes, ResourceType::Patterns, ResourceType::Gradients}) {
            auto model = KisResourceModelProvider::resourceModel(type);
            connect(model,
                    &QAbstractItemModel::dataChanged,
                    this,
                    [this](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
                        // Unsaved edits only affect the dirty/thumbnail roles.
                        if (roles.isEmpty() || roles.contains(Qt::UserRole + KisAbstractResourceModel::MD5))
                            invalidate();
                    });
            connect(model, &QAbstractItemModel::modelReset, this, &KisBrushStrokePreviewCache::invalidate);
            connect(model, &QAbstractItemModel::rowsRemoved, this, &KisBrushStrokePreviewCache::invalidate);
            connect(model, &QAbstractItemModel::rowsInserted, this, &KisBrushStrokePreviewCache::invalidate);
        }
        m_d->diskBusy = m_d->writing = true;
        const auto directory = m_d->directory;
        m_d->disk.setFuture(QtConcurrent::run([directory]() {
            prune(directory);
            return DiskResult{};
        }));
    }
    bool busy = false;
    for (const auto &document : KisPart::instance()->documents()) {
        if (!document)
            continue;
        observeDocument(document.data());
        if (!document->image())
            continue;
        auto image = document->image();
        observeImage(image);
        busy |= !image->isIdle();
    }
    if (busy || m_d->consumers.isEmpty()) {
        m_d->renderer.cancel();
        if (m_d->consumers.isEmpty() && !m_d->renderer.isRunning() && !m_d->diskBusy)
            m_d->timer.stop();
        return;
    }
    if (m_d->renderer.isRunning() || m_d->diskBusy)
        return;
    if (m_d->preset) {
        if (wanted(m_d->active.id()) && m_d->activeGeneration == m_d->generation) {
            const auto preset = m_d->preset;
            m_d->preset.clear();
            m_d->renderer.start(preset);
            Q_EMIT renderStarted();
            return;
        }
        m_d->preset.clear();
    }
    for (const auto &requests : m_d->consumers) {
        for (auto it = requests.crbegin(); it != requests.crend(); ++it) {
            if (ready(*it))
                continue;
            m_d->active = *it;
            m_d->activeGeneration = m_d->generation;
            auto *locator = KisResourceLocator::instance();
            m_d->preset = locator->resourceSnapshot(it->location, ResourceType::PaintOpPresets, it->filename, it->md5)
                              .dynamicCast<KisPaintOpPreset>();
            // A stale/deleted resource may remain in a queued view for one tick.
            // Do not persist a failure for a resource whose saved bytes changed.
            if (!m_d->preset) {
                m_d->keys[it->id()] = it->id();
                m_d->empty.insert(it->id());
                Q_EMIT previewReady();
                return;
            }
            m_d->activeKey = cacheKey(m_d->preset, it->md5);
            m_d->keys[it->id()] = m_d->activeKey;
            const auto path = m_d->directory + '/' + m_d->activeKey;
            m_d->diskBusy = true;
            m_d->disk.setFuture(QtConcurrent::run([path]() {
                if (QFile::exists(path + ".none")) {
                    QFile file(path + ".none");
                    if (file.open(QIODevice::ReadOnly))
                        file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
                    return DiskResult{true, {}};
                }
                QImage image(path + ".png");
                if (image.size() != QSize(480, 160))
                    return DiskResult{};
                QFile file(path + ".png");
                if (file.open(QIODevice::ReadOnly))
                    file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
                return DiskResult{true, image};
            }));
            return;
        }
    }
}
void KisBrushStrokePreviewCache::observeImage(KisImageSP image)
{
    if (!image || m_d->observed.contains(image.data()))
        return;
    m_d->observed.insert(image.data());
    connect(image.data(), &KisImage::sigStrokeStarted, this, [this]() {
        m_d->renderer.cancel();
    });
    connect(image.data(), &QObject::destroyed, this, [this, ptr = image.data()]() {
        m_d->observed.remove(ptr);
    });
}
void KisBrushStrokePreviewCache::observeDocument(KisDocument *document)
{
    if (!document)
        return;
    observeImage(document->image());
    if (m_d->documents.contains(document))
        return;
    m_d->documents.insert(document);
    connect(document, &KisDocument::sigLoadingFinished, this, [this, document]() {
        observeImage(document->image());
    });
    connect(document, &QObject::destroyed, this, [this, document]() {
        m_d->documents.remove(document);
    });
}
void KisBrushStrokePreviewCache::finish(const QImage &image, bool cancelled)
{
    if (cancelled || m_d->activeGeneration != m_d->generation || !wanted(m_d->active.id()))
        return;
    auto *locator = KisResourceLocator::instance();
    if (!locator->resourceSnapshot(m_d->active.location,
                                   ResourceType::PaintOpPresets,
                                   m_d->active.filename,
                                   m_d->active.md5))
        return;
    if (image.isNull())
        m_d->empty.insert(m_d->activeKey);
    else
        m_d->images.insert(m_d->activeKey, new QImage(image));
    const QString directory = m_d->directory;
    const QString path = directory + '/' + m_d->activeKey + (image.isNull() ? ".none" : ".png");
    m_d->writing = m_d->diskBusy = true;
    m_d->disk.setFuture(QtConcurrent::run([directory, path, image]() {
        QDir().mkpath(directory);
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly) && (image.isNull() || image.save(&file, "PNG")))
            file.commit();
        prune(directory);
        return DiskResult{};
    }));
    Q_EMIT previewReady();
}
