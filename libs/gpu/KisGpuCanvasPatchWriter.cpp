/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuCanvasPatchWriter.h"

#include "KisGpuBuffer.h"
#include "KisGpuCommandList.h"
#include "KisGpuComputePipeline.h"
#include "KisGpuContext.h"

#include <cstring>

#include <kis_debug.h>

namespace
{
const quint32 CanvasPatchesF32ToF32[] = {
#include "canvas_patches_f32_f32.spv.inc"
};
const quint32 CanvasPatchesF32ToF16[] = {
#include "canvas_patches_f32_f16.spv.inc"
};
const quint32 CanvasPatchesF16ToF32[] = {
#include "canvas_patches_f16_f32.spv.inc"
};
const quint32 CanvasPatchesF16ToF16[] = {
#include "canvas_patches_f16_f16.spv.inc"
};

struct PushConstants {
    VkDeviceAddress srcTiles;
    VkDeviceAddress patches;
    VkDeviceAddress dst;
    VkDeviceAddress srcCurves;
    VkDeviceAddress dstCurves;
    quint32 gridWidth;
    quint32 patchCount;
    quint32 mode;
    quint32 curveSize;
    quint32 padding[2]; // vec4 members are 16-byte aligned in the shader
    float matrixRow0[4];
    float matrixRow1[4];
    float matrixRow2[4];
};
static_assert(sizeof(PushConstants) == 112, "must match the shader push constant block");

struct GpuPatch {
    qint32 srcOrigin[2];
    qint32 centerSize[2];
    qint32 margin[2];
    qint32 bufferSize[2];
    quint32 dstOffset;
    quint32 padding[3];
};
static_assert(sizeof(GpuPatch) == 48, "must match the shader Patch struct");

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}
} // namespace

KisGpuCanvasPatchWriter::KisGpuCanvasPatchWriter(KisGpuContext &context)
    : m_context(context)
{
}

KisGpuCanvasPatchWriter::~KisGpuCanvasPatchWriter() = default;

std::unique_ptr<KisGpuCanvasPatchWriter> KisGpuCanvasPatchWriter::create(KisGpuContext &context,
                                                                         KisGpuTileFormat sourceFormat,
                                                                         bool halfFloatOutput,
                                                                         QString *errorMessage)
{
    std::unique_ptr<KisGpuCanvasPatchWriter> writer(new KisGpuCanvasPatchWriter(context));
    writer->m_halfFloatOutput = halfFloatOutput;

    const bool halfSource = sourceFormat == KisGpuTileFormat::RGBA16F;
    const quint32 *spirv = nullptr;
    size_t spirvSize = 0;
    if (halfSource) {
        spirv = halfFloatOutput ? CanvasPatchesF16ToF16 : CanvasPatchesF16ToF32;
        spirvSize = halfFloatOutput ? sizeof(CanvasPatchesF16ToF16) : sizeof(CanvasPatchesF16ToF32);
    } else {
        spirv = halfFloatOutput ? CanvasPatchesF32ToF16 : CanvasPatchesF32ToF32;
        spirvSize = halfFloatOutput ? sizeof(CanvasPatchesF32ToF16) : sizeof(CanvasPatchesF32ToF32);
    }
    writer->m_pipeline = KisGpuComputePipeline::create(context, spirv, spirvSize, sizeof(PushConstants), errorMessage);
    return writer->m_pipeline ? std::move(writer) : nullptr;
}

quint32 KisGpuCanvasPatchWriter::outputPixelSize() const
{
    return m_halfFloatOutput ? 8 : 16;
}

bool KisGpuCanvasPatchWriter::record(KisGpuCommandList &commands,
                                     const QVector<VkDeviceAddress> &sourceTiles,
                                     int gridWidth,
                                     const QVector<Patch> &patches,
                                     VkDeviceAddress output,
                                     const Conversion &conversion,
                                     QString *errorMessage)
{
    if (patches.isEmpty()) {
        return true;
    }
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(gridWidth > 0, false);
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(conversion.srcCurves.isEmpty()
                                             || conversion.srcCurves.size() == 3 * conversion.curveSize,
                                         false);
    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(conversion.dstCurves.isEmpty()
                                             || conversion.dstCurves.size() == 3 * conversion.curveSize,
                                         false);

    const VkDeviceSize tilesBytes = VkDeviceSize(sourceTiles.size()) * sizeof(VkDeviceAddress);
    const VkDeviceSize patchesOffset = alignUp(tilesBytes, 16);
    const VkDeviceSize patchesBytes = VkDeviceSize(patches.size()) * sizeof(GpuPatch);
    const VkDeviceSize srcCurvesOffset = alignUp(patchesOffset + patchesBytes, 16);
    const VkDeviceSize srcCurvesBytes = VkDeviceSize(conversion.srcCurves.size()) * sizeof(float);
    const VkDeviceSize dstCurvesOffset = alignUp(srcCurvesOffset + srcCurvesBytes, 16);
    const VkDeviceSize dstCurvesBytes = VkDeviceSize(conversion.dstCurves.size()) * sizeof(float);
    const VkDeviceSize totalBytes = dstCurvesOffset + dstCurvesBytes + 16;

    if (!m_tables || m_tables->size() < totalBytes) {
        m_tables = KisGpuBuffer::create(m_context, totalBytes, KisGpuBuffer::Location::Upload, errorMessage);
        if (!m_tables) {
            return false;
        }
    }

    quint8 *mapped = static_cast<quint8 *>(m_tables->mapped());
    std::memcpy(mapped, sourceTiles.constData(), size_t(tilesBytes));

    GpuPatch *gpuPatches = reinterpret_cast<GpuPatch *>(mapped + patchesOffset);
    int maxWidth = 0;
    int maxHeight = 0;
    for (int i = 0; i < patches.size(); i++) {
        const Patch &patch = patches[i];
        GpuPatch &gpu = gpuPatches[i];
        gpu.srcOrigin[0] = patch.srcOrigin.x();
        gpu.srcOrigin[1] = patch.srcOrigin.y();
        gpu.centerSize[0] = patch.centerSize.width();
        gpu.centerSize[1] = patch.centerSize.height();
        gpu.margin[0] = patch.margin.x();
        gpu.margin[1] = patch.margin.y();
        gpu.bufferSize[0] = patch.bufferSize.width();
        gpu.bufferSize[1] = patch.bufferSize.height();
        gpu.dstOffset = patch.dstOffset;
        gpu.padding[0] = gpu.padding[1] = gpu.padding[2] = 0;
        maxWidth = qMax(maxWidth, patch.bufferSize.width());
        maxHeight = qMax(maxHeight, patch.bufferSize.height());
    }
    if (srcCurvesBytes) {
        std::memcpy(mapped + srcCurvesOffset, conversion.srcCurves.constData(), size_t(srcCurvesBytes));
    }
    if (dstCurvesBytes) {
        std::memcpy(mapped + dstCurvesOffset, conversion.dstCurves.constData(), size_t(dstCurvesBytes));
    }

    PushConstants constants{};
    constants.srcTiles = m_tables->deviceAddress();
    constants.patches = m_tables->deviceAddress() + patchesOffset;
    constants.dst = output;
    constants.srcCurves = srcCurvesBytes ? m_tables->deviceAddress() + srcCurvesOffset : 0;
    constants.dstCurves = dstCurvesBytes ? m_tables->deviceAddress() + dstCurvesOffset : 0;
    constants.gridWidth = quint32(gridWidth);
    constants.patchCount = quint32(patches.size());
    constants.mode = quint32(conversion.mode);
    constants.curveSize = quint32(conversion.curveSize);
    for (int column = 0; column < 3; column++) {
        constants.matrixRow0[column] = conversion.matrix[column];
        constants.matrixRow1[column] = conversion.matrix[3 + column];
        constants.matrixRow2[column] = conversion.matrix[6 + column];
    }

    m_pipeline->dispatch(commands.commandBuffer(),
                         constants,
                         quint32((maxWidth + 15) / 16),
                         quint32((maxHeight + 15) / 16),
                         quint32(patches.size()));
    return true;
}
