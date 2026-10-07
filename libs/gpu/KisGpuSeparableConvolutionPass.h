/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUSEPARABLECONVOLUTIONPASS_H
#define KISGPUSEPARABLECONVOLUTIONPASS_H

#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>

#include <memory>

#include "KisGpuTilePool.h"
#include "KisGpuVulkanFunctions.h"
#include "kritagpu_export.h"

class KisGpuBuffer;
class KisGpuCommandList;
class KisGpuComputePipeline;
class KisGpuContext;

/**
 * A separable convolution on the GPU (phase 4.98,
 * shaders/separable_convolution.comp): the Gaussian blur of
 * KisGaussianKernel::applyGaussian(), equivalent to the CPU FFT convolution
 * within rounding (sums in doubles, premultiplied alpha, the same clamping
 * and alpha handling when the result is written).
 *
 * The destination tile grid is processed in bands of tile rows: a horizontal
 * pass into a device-local double intermediate buffer, then a vertical pass
 * into the destination tiles. Every destination pixel is written: the
 * convolution inside applyRect, the source pixel elsewhere and in channels
 * that are not convolved.
 *
 * Requires shaderFloat64. Tables live in internal buffers: do not record
 * again before the previous recording has completed.
 */
class KRITAGPU_EXPORT KisGpuSeparableConvolutionPass
{
public:
    struct Pass {
        QVector<double> horizontal; ///< 2 * halfWidth + 1 weights
        QVector<double> vertical; ///< 2 * halfHeight + 1 weights
        double invFactor = 1.0; ///< 1 / the 2D kernel's sum
        QRect applyRect;
        QRect dataRect; ///< the clamp rect when repeatBorder
        bool repeatBorder = false;
        bool convolved[4] = {true, true, true, true}; ///< per channel, tile memory order (alpha last)
        QPoint srcGridOrigin; ///< image coordinates of the first source tile
        int srcGridWidth = 0;
        int srcGridHeight = 0;
        QVector<VkDeviceAddress> srcTiles; ///< row-major; 0 reads the default pixel
        QPoint dstGridOrigin;
        int dstGridWidth = 0;
        int dstGridHeight = 0;
        QVector<VkDeviceAddress> dstTiles;
        float defaultPixel[4] = {0.0f, 0.0f, 0.0f, 0.0f}; ///< as float, tile memory order
    };

    ~KisGpuSeparableConvolutionPass();

    /// nullptr without shaderFloat64 or on pipeline failure.
    static std::unique_ptr<KisGpuSeparableConvolutionPass>
    create(KisGpuContext &context, KisGpuTileFormat format, QString *errorMessage = nullptr);

    bool record(KisGpuCommandList &commands, const Pass &pass, QString *errorMessage = nullptr);

    /// Bands recorded by the last record() (tests, diagnostics).
    int lastBandCount() const;

private:
    explicit KisGpuSeparableConvolutionPass(KisGpuContext &context);

    KisGpuContext &m_context;
    std::shared_ptr<KisGpuComputePipeline> m_horizontal; // KisGpuContext::sharedComputePipeline
    std::shared_ptr<KisGpuComputePipeline> m_vertical;
    std::unique_ptr<KisGpuBuffer> m_tables;
    std::unique_ptr<KisGpuBuffer> m_intermediate;
    int m_lastBandCount = 0;
};

#endif // KISGPUSEPARABLECONVOLUTIONPASS_H
