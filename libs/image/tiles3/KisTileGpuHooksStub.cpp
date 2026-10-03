/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Built instead of gpu/KisGpuTileBackend.cpp when the GPU engine (kritagpu)
// is not available. No tile data ever gets a KisTileGpuState in such builds.

#include "KisTileGpuState.h"

#include <kis_assert.h>

void KisTileGpuHooks::ensureCpuValid(KisTileData *td)
{
    Q_UNUSED(td);
    KIS_SAFE_ASSERT_RECOVER_NOOP(false && "GPU tile state without the GPU engine");
}

void KisTileGpuHooks::destroyState(KisTileGpuState *state, qint32 pixelSize)
{
    Q_UNUSED(pixelSize);
    delete state;
}

bool KisTileGpuHooks::tryEvict(KisTileData *td)
{
    Q_UNUSED(td);
    return false;
}
