/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISREDRAWABLELAYERINTERFACE_H
#define KISREDRAWABLELAYERINTERFACE_H

#include <QTransform>

#include "kis_types.h"
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
     * A command that leaves the layer's paint device as @p transform applied
     * to its content, drawn again, or nullptr when the layer cannot do it
     * (the pixels are transformed as usual then). @p original holds the
     * content before the transform: the paint device itself, or a copy that
     * the Transform Tool keeps while the device shows its preview. Called
     * while the image is being processed.
     */
    virtual KUndo2Command *createTransformRedrawCommand(const QTransform &transform, KisPaintDeviceSP original) = 0;
};

#endif // KISREDRAWABLELAYERINTERFACE_H
