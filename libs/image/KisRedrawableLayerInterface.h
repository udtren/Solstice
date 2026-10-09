/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISREDRAWABLELAYERINTERFACE_H
#define KISREDRAWABLELAYERINTERFACE_H

#include <QTransform>

#include "kritaimage_export.h"

class KUndo2Command;

/**
 * A paint layer that can draw its content again for a transform instead of
 * resampling its pixels (Solstice: the brush stroke layer, see
 * docs/agent/brush-stroke-layer-plan.md). KisTransformProcessingVisitor asks
 * it before transforming the layer's pixels.
 */
class KRITAIMAGE_EXPORT KisRedrawableLayerInterface
{
public:
    virtual ~KisRedrawableLayerInterface();

    /**
     * A command that leaves the layer as @p transform applied to its content,
     * drawn again, or nullptr when the layer cannot do it (the pixels are
     * transformed as usual then). Called while the image is being processed.
     */
    virtual KUndo2Command *createTransformRedrawCommand(const QTransform &transform) = 0;
};

#endif // KISREDRAWABLELAYERINTERFACE_H
