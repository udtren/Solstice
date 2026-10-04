/* SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef KISGPUDABCOMPOSITOR_H
#define KISGPUDABCOMPOSITOR_H
#include "KisGpuVulkanFunctions.h"
#include "kritagpu_export.h"
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>
#include <memory>
class KisGpuContext;
class KisGpuCommandList;
class KisGpuComputePipeline;
class KisGpuBuffer;
class KRITAGPU_EXPORT KisGpuDabCompositor
{
public:
    static constexpr quint64 MaxUploadBytes = quint64(64) << 20;
    enum class CompositeMode : quint32 {
        Normal,
        AlphaDarkenHard,
        AlphaDarkenCreamy,
        Erase,
        Multiply,
        Screen,
        Add,
        Subtract,
        Darken,
        Lighten,
        Difference,
        Overlay,
        HardLight,
        Exclusion,
        LinearBurn,
        LinearLight,
        PinLight,
        SoftLightSvg,
        SoftLightPhotoshop,
        ColorDodge,
        ColorBurn,
        Divide,
        VividLight,
        HardMix,
        HardMixPhotoshop,
        HardMixSofterPhotoshop,
        GrainMerge,
        GrainExtract,
        Negation,
        Allanon,
        Hue,
        Saturation,
        Color,
        Luminosity,
        DarkerColor,
        LighterColor,
        Count
    };
    struct Dab {
        const void *pixels; // RGBA32F, or RGBA16F when pixelSize is 8
        QPoint origin;
        QSize size;
        float opacity;
        float flow = 1.0f;
        float averageOpacity = 0.0f;
        quint32 mirrorFlags = 0; // source-coordinate reflection: horizontal=1, vertical=2
        QRect clip; // document coordinates; null means the entire dab
    };
    struct Mask {
        const quint8 *pixels;
        QRect bounds; // contiguous 8-bit coverage in document coordinates
    };
    ~KisGpuDabCompositor();
    static std::unique_ptr<KisGpuDabCompositor> create(KisGpuContext &context, QString *error = nullptr);
    /// Required reusable allocation, rounded to 256 KiB; zero on invalid/oversized input.
    static quint64 requiredUploadBytes(int tileCount,
                                       const QVector<Dab> &dabs,
                                       const Mask *mask = nullptr,
                                       QString *error = nullptr,
                                       int pixelSize = 16);
    quint64 uploadBytes() const;
    /// Caller must first wait for the last recording to finish.
    void releaseUpload();
    /// RGBA32F modes, or the supported major RGBA16F modes. Wait before reusing this object.
    /// F16 channelMask bit 4 distinguishes explicit flags from an empty set.
    /// The host-visible source/table/mask buffer is capped at 64 MiB.
    /// Optional origins select sparse, distinct destination tiles instead of a
    /// rectangular grid. There must be exactly one document origin per address.
    bool record(KisGpuCommandList &commands,
                const QVector<VkDeviceAddress> &tiles,
                int gridWidth,
                QPoint origin,
                const QVector<Dab> &dabs,
                CompositeMode mode,
                const Mask *mask = nullptr,
                quint32 channelMask = 0xf,
                QString *error = nullptr,
                const QVector<QPoint> &tileOrigins = {},
                int pixelSize = 16);

private:
    explicit KisGpuDabCompositor(KisGpuContext &context);
    KisGpuContext &m_context;
    std::unique_ptr<KisGpuComputePipeline> m_pipeline;
    std::unique_ptr<KisGpuComputePipeline> m_extendedPipeline;
    std::unique_ptr<KisGpuComputePipeline> m_halfPipeline;
    std::unique_ptr<KisGpuComputePipeline> m_halfExtendedPipeline;
    std::unique_ptr<KisGpuBuffer> m_upload;
};
#endif
