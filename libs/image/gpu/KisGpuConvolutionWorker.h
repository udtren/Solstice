/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUCONVOLUTIONWORKER_H
#define KISGPUCONVOLUTIONWORKER_H

#include <QBitArray>
#include <QRect>
#include <QVector>

#include "kis_convolution_painter.h"
#include "kis_types.h"
#include "kritaimage_export.h"

/**
 * GPU engine (Solstice, phase 4.98): separable convolutions on the GPU, used
 * by KisGaussianKernel::applyGaussian() (Gaussian blur, Unsharp Mask,
 * Gaussian High Pass). Equivalent to the CPU FFT convolution within
 * rounding, not bit-identical (docs/agent/gpu-engine.md).
 *
 * The convolution reads a snapshot of the device's old data (as the CPU
 * convolution reads oldRawData()) and writes into a temporary device, which
 * is then copied into @p rect on the CPU. Several calls on disjoint rects of
 * one device may therefore run concurrently, like a filter stroke's patches.
 */
class KRITAIMAGE_EXPORT KisGpuConvolutionWorker
{
public:
    /// Cheap checks: engine, device format, LOD, wrap-around, shaderFloat64.
    static bool canRun(KisPaintDeviceSP device);

    /**
     * Convolves @p rect of @p device in place with the kernel
     * vertical * horizontal, divided by @p factor (the sum of that matrix).
     * Both weight vectors have an odd size; the center is the middle weight.
     * Pixels outside @p rect are not changed.
     *
     * On false the device is unchanged and the caller runs the CPU
     * convolution.
     */
    static bool applySeparable(KisPaintDeviceSP device,
                               const QRect &rect,
                               const QVector<double> &horizontal,
                               const QVector<double> &vertical,
                               double factor,
                               const QBitArray &channelFlags,
                               KisConvolutionBorderOp borderOp);

    /// Enabled with the GPU engine; KRITA_GPU_CONVOLUTION=0 disables it.
    static bool isEnabled();
    /// Number of convolutions that ran on the GPU (tests, diagnostics).
    static quint64 runCount();
};

#endif // KISGPUCONVOLUTIONWORKER_H
