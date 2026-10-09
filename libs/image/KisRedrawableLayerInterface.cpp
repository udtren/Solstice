/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisRedrawableLayerInterface.h"

// out of line, so that the type information lives in kritaimage and
// dynamic_cast works across libraries
KisRedrawableLayerInterface::~KisRedrawableLayerInterface()
{
}
