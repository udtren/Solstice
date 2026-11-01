/*
 *  SPDX-FileCopyrightText: 2026 Solstice contributors
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisThroughLayerEraserTargets.h"

#include "kis_group_layer.h"
#include "kis_node.h"
#include "kis_paint_layer.h"

namespace
{
void collectFrom(KisNodeSP node, const KisNodeList &selectedNodes, bool included, bool isRoot, KisNodeList *targets)
{
    if ((!node->visible(false) && !isRoot) || node->userLocked()) {
        return;
    }

    included = included || selectedNodes.contains(node);

    if (KisPaintLayer *layer = dynamic_cast<KisPaintLayer *>(node.data())) {
        if (included && layer->paintDevice() && !layer->alphaLocked()) {
            *targets << node;
        }
    } else if (dynamic_cast<KisGroupLayer *>(node.data())) {
        for (KisNodeSP child = node->lastChild(); child; child = child->prevSibling()) {
            collectFrom(child, selectedNodes, included, false, targets);
        }
    }
}
} // namespace

KisNodeList KisThroughLayerEraserTargets::collect(KisNodeSP root, const KisNodeList &selectedNodes, bool selectedOnly)
{
    KisNodeList targets;
    if (root) {
        collectFrom(root, selectedNodes, !selectedOnly, true, &targets);
    }
    return targets;
}
