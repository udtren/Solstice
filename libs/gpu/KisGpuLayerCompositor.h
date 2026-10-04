/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPULAYERCOMPOSITOR_H
#define KISGPULAYERCOMPOSITOR_H

#include <QRect>
#include <QString>
#include <QVector>

#include <memory>

#include "KisGpuTilePool.h"
#include "KisGpuVulkanFunctions.h"
#include "kritagpu_export.h"

class KisGpuContext;
class KisGpuCommandList;
class KisGpuBuffer;
class KisGpuComputePipeline;

/// Blend modes of shaders/composite_layers.comp (values are shader constants).
enum class KisGpuBlendOp : quint32 {
    Over = 0,
    Multiply = 1,
    Screen = 2,
    Add = 3,
    Subtract = 4,
    Darken = 5,
    Lighten = 6,
    Difference = 7,
    Overlay = 8,
    HardLight = 9,
    Exclusion = 10,
    Erase = 11, // Brush compositing only; not exposed by the layer blend-op mapping.
    LinearBurn = 12,
    LinearLight = 13,
    PinLight = 14,
    SoftLightSvg = 15,
    SoftLightPhotoshop = 16,
    ColorDodge = 17,
    ColorBurn = 18,
    Divide = 19,
    VividLight = 20,
    HardMix = 21,
    HardMixPhotoshop = 22,
    HardMixSofterPhotoshop = 23,
    GrainMerge = 24,
    GrainExtract = 25,
    Negation = 26,
    Allanon = 27,
    Hue = 28,
    Saturation = 29,
    Color = 30,
    Luminosity = 31,
    DarkerColor = 32,
    LighterColor = 33,
    Count,
};

/// F16 brush modes covered by the dab and indirect-painting parity matrices.
inline bool kisGpuSupportsHalfBrushBlend(KisGpuBlendOp op)
{
    return op <= KisGpuBlendOp::PinLight || op == KisGpuBlendOp::SoftLightSvg || op == KisGpuBlendOp::ColorDodge
        || op == KisGpuBlendOp::ColorBurn || (op >= KisGpuBlendOp::Hue && op <= KisGpuBlendOp::Luminosity);
}

/**
 * Composites layers bottom to top onto destination tiles, writing only the
 * pixels inside a clip rect (GPU engine phase 2, projection).
 *
 * Tiles form a grid of gridWidth columns; tile t covers grid pixels
 * ((t % gridWidth) * 64, (t / gridWidth) * 64) .. +64. The clip rect is in
 * the same grid pixel coordinates.
 *
 * Tables live in an internal host-visible buffer: do not record a new pass
 * while the previous one is still executing.
 */
class KRITAGPU_EXPORT KisGpuLayerCompositor
{
public:
    struct Mask {
        const quint8 *data = nullptr;
        QRect bounds; // Tightly packed coverage bytes, in tile-grid pixel coordinates.
    };
    struct Layer {
        KisGpuBlendOp op = KisGpuBlendOp::Over;
        float opacity = 1.0f;
        bool alphaLocked = false;
        quint32 channelMask = 0xf; // RGBA bits; partial channels are used by brushes.
        bool halfBrush = false; // F16 Normal/Erase scalar arithmetic, for Wash only
        bool explicitChannelFlags = false;
    };

    ~KisGpuLayerCompositor();

    static std::unique_ptr<KisGpuLayerCompositor>
    create(KisGpuContext &context, KisGpuTileFormat format, QString *errorMessage = nullptr);

    /**
     * @param layerTiles layer-major, layerTiles[layer * tileCount + tile]; 0 skips the tile
     * @param dstTiles destination tiles (read and written)
     * @param mask optional single-layer coverage; copied into the
     * internal table buffer before return, with a 16 MiB limit
     */
    bool record(KisGpuCommandList &commands,
                const QVector<VkDeviceAddress> &layerTiles,
                const QVector<VkDeviceAddress> &dstTiles,
                const QVector<Layer> &layers,
                int gridWidth,
                const QRect &clip,
                QString *errorMessage = nullptr,
                const Mask *mask = nullptr);

private:
    explicit KisGpuLayerCompositor(KisGpuContext &context);

    KisGpuContext &m_context;
    std::unique_ptr<KisGpuComputePipeline> m_pipeline;
    std::unique_ptr<KisGpuComputePipeline> m_extendedPipeline;
    bool m_f16 = false;
    std::unique_ptr<KisGpuBuffer> m_tables;
};

#endif // KISGPULAYERCOMPOSITOR_H
