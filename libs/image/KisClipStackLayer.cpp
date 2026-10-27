/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisClipStackLayer.h"

#include <KoColorSpace.h>
#include <KoColorSpaceConstants.h>

#include "kis_assert.h"
#include "kis_default_bounds.h"
#include "kis_image.h"
#include "kis_paint_device.h"

struct KisClipStackLayer::Private {
    KisNodeWSP base;
    KisPaintDeviceSP paintDevice;
};

KisClipStackLayer::KisClipStackLayer(KisLayer *base)
    : KisLayer(base->image(), QStringLiteral("clip stack"), OPACITY_OPAQUE_U8)
    , m_d(new Private)
{
    m_d->base = base;

    KisImageSP image = this->image().toStrongRef();
    const KoColorSpace *colorSpace = image ? image->colorSpace() : base->colorSpace();
    m_d->paintDevice = new KisPaintDevice(this, colorSpace, new KisDefaultBounds(this->image()));
    m_d->paintDevice->setProjectionDevice(true);
}

KisClipStackLayer::~KisClipStackLayer()
{
}

KisLayerSP KisClipStackLayer::base() const
{
    KisNodeSP base = m_d->base;
    return KisLayerSP(qobject_cast<KisLayer *>(base.data()));
}

KisNodeSP KisClipStackLayer::clone() const
{
    // The stack belongs to its base and is never copied with it.
    KIS_SAFE_ASSERT_RECOVER_NOOP(false && "a clip stack is never cloned");
    return KisNodeSP();
}

bool KisClipStackLayer::allowAsChild(KisNodeSP node) const
{
    Q_UNUSED(node);
    return false;
}

KisPaintDeviceSP KisClipStackLayer::original() const
{
    return m_d->paintDevice;
}

KisPaintDeviceSP KisClipStackLayer::paintDevice() const
{
    return KisPaintDeviceSP();
}

void KisClipStackLayer::syncColorSpace()
{
    KisImageSP image = this->image().toStrongRef();
    if (!image || *m_d->paintDevice->colorSpace() == *image->colorSpace()) {
        return;
    }

    m_d->paintDevice->clear();
    m_d->paintDevice->convertTo(image->colorSpace());
}
