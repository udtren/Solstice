/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "KisColorReferenceFilter.h"
#include "KisColorReferenceConfigWidget.h"
#include "KisColorReferenceResources.h"
#include <KoChannelInfo.h>
#include <KoColorSpaceTraits.h>
#include <KoUpdater.h>
#include <cmath>
#include <cstring>
#include <filter/kis_filter_category_ids.h>
#include <filter/kis_filter_configuration.h>
#include <kis_paint_device.h>
#include <kis_painter.h>
#include <kis_random_accessor_ng.h>
#include <kis_sequential_iterator.h>
#include <memory>

KisColorReferenceFilter::KisColorReferenceFilter()
    : KisFilter(KoID("solstice_color_reference", i18n("Color Reference")),
                FiltersCategoryAdjustId,
                i18n("Color &Reference..."))
{
    setColorSpaceIndependence(FULLY_INDEPENDENT);
    setSupportsThreading(true);
    setSupportsAdjustmentLayers(true);
    setSupportsPainting(false);
}
KisFilterConfigurationSP KisColorReferenceFilter::defaultConfiguration(KisResourcesInterfaceSP resources) const
{
    auto config = factoryConfiguration(resources);
    config->setProperty("source", 0);
    config->setProperty("amount", 100.0);
    config->setProperty("keepBrightness", true);
    config->setProperty("referenceId", "preset:0");
    const QString image = ColorReference::encode(ColorReference::fromImage(ColorReference::presetImage(0)));
    const auto gradient = ColorReference::defaultGradient();
    config->setProperty("lut", image);
    config->setProperty("imageLut", image);
    config->setProperty("gradientLut", ColorReference::encode(ColorReference::fromGradient(gradient)));
    config->setProperty("gradientXML", ColorReference::gradientXml(gradient));
    return config;
}
KisConfigWidget *
KisColorReferenceFilter::createConfigurationWidget(QWidget *parent, const KisPaintDeviceSP device, bool) const
{
    return new KisColorReferenceConfigWidget(parent, device);
}
void KisColorReferenceFilter::processImpl(KisPaintDeviceSP device,
                                          const QRect &rect,
                                          const KisFilterConfigurationSP config,
                                          KoUpdater *progress) const
{
    KIS_SAFE_ASSERT_RECOVER_RETURN(device && config);
    const auto cancelled = [&](int percent) {
        if (!progress)
            return false;
        progress->setProgress(percent);
        return progress->interrupted();
    };
    const double requested = config->getDouble("amount", 100);
    const double amount = std::isfinite(requested) ? qBound(0.0, requested, 100.0) / 100 : 0;
    if (rect.isEmpty() || amount == 0 || cancelled(0))
        return;
    const auto table = ColorReference::decode(config->getString("lut"));
    if (table.isEmpty())
        return; // Malformed/unknown data must never replace saved artwork.
    const auto *cs = device->colorSpace(), *work = ColorReference::workingSpace();
    KIS_SAFE_ASSERT_RECOVER_RETURN(work);
    const auto channels = cs->channels();
    const auto flags = config->channelFlags();
    QVector<const KoChannelInfo *> enabled;
    for (int i = 0; i < channels.size(); ++i)
        if (channels[i]->channelType() == KoChannelInfo::COLOR
            && (flags.isEmpty() || (i < flags.size() && flags.testBit(i))))
            enabled.append(channels[i]);
    if (enabled.isEmpty())
        return;
    // Own converters per invocation: the color space's cached converter is not
    // safe to share between concurrent filter patches.
    std::unique_ptr<KoColorConversionTransformation> toRgb(
        cs->createColorConverter(work,
                                 KoColorConversionTransformation::internalRenderingIntent(),
                                 KoColorConversionTransformation::internalConversionFlags()));
    std::unique_ptr<KoColorConversionTransformation> fromRgb(
        work->createColorConverter(cs,
                                   KoColorConversionTransformation::internalRenderingIntent(),
                                   KoColorConversionTransformation::internalConversionFlags()));
    KIS_SAFE_ASSERT_RECOVER_RETURN(toRgb && fromRgb);
    const bool keep = config->getBool("keepBrightness", true);
    KisPaintDeviceSP output = new KisPaintDevice(cs);
    output->setX(device->x());
    output->setY(device->y());
    output->setDefaultPixel(device->defaultPixel());
    auto source = device->createRandomConstAccessorNG();
    QByteArray encoded(cs->pixelSize(), '\0');
    {
        KisSequentialIterator it(output, rect);
        while (it.nextPixel()) {
            if (it.x() == rect.x() && cancelled(98 * (it.y() - rect.y()) / rect.height()))
                return;
            source->moveTo(it.x(), it.y());
            const auto *p = source->oldRawData();
            std::memcpy(it.rawData(), p, cs->pixelSize());
            const double alpha = cs->opacityF(p);
            if (!(alpha > 0) || !std::isfinite(alpha))
                continue;
            KoRgbF32Traits::Pixel rgb;
            toRgb->transform(p, reinterpret_cast<quint8 *>(&rgb), 1);
            const ColorReference::RGB original{rgb.red, rgb.green, rgb.blue};
            if (!std::isfinite(original[0]) || !std::isfinite(original[1]) || !std::isfinite(original[2]))
                continue;
            const double l = ColorReference::brightness(original);
            auto mapped = ColorReference::lookup(table, l);
            if (keep)
                mapped = ColorReference::keepBrightness(mapped, l);
            rgb.red = float(original[0] + amount * (mapped[0] - original[0]));
            rgb.green = float(original[1] + amount * (mapped[1] - original[1]));
            rgb.blue = float(original[2] + amount * (mapped[2] - original[2]));
            fromRgb->transform(reinterpret_cast<const quint8 *>(&rgb), reinterpret_cast<quint8 *>(encoded.data()), 1);
            for (const auto *ch : enabled)
                std::memcpy(it.rawData() + ch->pos(), encoded.constData() + ch->pos(), ch->size());
        }
    }
    source.clear();
    if (cancelled(99))
        return;
    KisPainter::copyAreaOptimized(rect.topLeft(), output, device, rect);
    if (progress)
        progress->setProgress(100);
}
