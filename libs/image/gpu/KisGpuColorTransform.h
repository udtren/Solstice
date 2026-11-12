/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUCOLORTRANSFORM_H
#define KISGPUCOLORTRANSFORM_H

#include <QSharedPointer>
#include <QVector>

#include "kritaimage_export.h"

class KoColorSpace;
class KoColorTransformation;

/**
 * Solstice (GPU engine, phase 4.115, docs/agent/gpu-engine.md): a color
 * transformation filter in a form the GPU engine can compute, with the
 * CPU's result. KisColorTransformationFilter::createGpuColorTransform()
 * returns one for the filters and configurations that have such a form.
 *
 * Always compiled; it only describes the transformation.
 */
class KRITAIMAGE_EXPORT KisGpuColorTransform
{
public:
    enum Kind {
        /// KisDesaturateAdjustment: arithmetic the GPU repeats step by step.
        Desaturate,
        /// One table per channel, indexed by the bit pattern of the half
        /// value (RGBA F16 only): the output channel depends on nothing but
        /// the same input channel.
        ChannelTables
    };

    static constexpr int ChannelTableSize = 65536;

    /**
     * The desaturate filter of @p type (0 lightness, 1 BT.709, 2 BT.601,
     * 3 average, 4 minimum, 5 maximum) in @p cs. Null unless @p cs is
     * RGBA F32/F16 and the type is one of these.
     */
    static QSharedPointer<const KisGpuColorTransform> desaturate(const KoColorSpace *cs, int type);

    /**
     * Channel tables of @p transformation, which the caller knows to treat
     * every channel on its own (per-channel curves, for instance). Every
     * table entry is computed by @p transformation itself, so looking a
     * pixel up gives exactly what transform() gives.
     *
     * Null unless @p cs is RGBA F16, or when sample pixels with unrelated
     * channel values show that the channels are not independent after all.
     */
    static QSharedPointer<const KisGpuColorTransform> channelTables(const KoColorSpace *cs,
                                                                    const KoColorTransformation *transformation);

    Kind kind() const
    {
        return m_kind;
    }

    int desaturateType() const
    {
        return m_desaturateType;
    }

    /// 4 * ChannelTableSize half bit patterns: the R, G, B and A tables.
    const quint16 *tables() const
    {
        return m_tables.constData();
    }

    /// Unique for every set of tables in this process.
    quint64 tablesId() const
    {
        return m_tablesId;
    }

    /// What the GPU computes for @p nPixels pixels (ChannelTables only; tests).
    void applyTables(const quint16 *src, quint16 *dst, int nPixels) const;

private:
    KisGpuColorTransform() = default;

    Kind m_kind = Desaturate;
    int m_desaturateType = 0;
    QVector<quint16> m_tables;
    quint64 m_tablesId = 0;
};

using KisGpuColorTransformSP = QSharedPointer<const KisGpuColorTransform>;

#endif // KISGPUCOLORTRANSFORM_H
