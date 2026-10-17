/* SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <KisGlobalResourcesInterface.h>
#include <KisReferenceImage.h>
#include <KoColor.h>
#include <QDomDocument>
#include <filter/kis_filter.h>
#include <filter/kis_filter_registry.h>
#include <kis_coordinates_converter.h>
#include <kis_paint_device.h>
#include <simpletest.h>

class TestReferenceFilter : public KisFilter
{
public:
    TestReferenceFilter(QString id, bool multiply)
        : KisFilter(KoID(id, id), KoID("test", "test"), id)
        , m_multiply(multiply)
    {
        setSupportsAdjustmentLayers(true);
    }
    void
    processImpl(KisPaintDeviceSP device, const QRect &rect, const KisFilterConfigurationSP, KoUpdater *) const override
    {
        QColor color = device->convertToQImage(nullptr, rect).pixelColor(0, 0);
        color.setRed(m_multiply ? color.red() * 2 : color.red() + 20);
        device->fill(rect, KoColor(color, device->colorSpace()));
    }

private:
    bool m_multiply;
};

class KisReferenceImageFiltersTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        KisFilterRegistry::instance()->add(new TestReferenceFilter("reference-test-add", false));
        KisFilterRegistry::instance()->add(new TestReferenceFilter("reference-test-multiply", true));
    }
    void orderedRendering()
    {
        QImage source(8, 8, QImage::Format_ARGB32);
        source.fill(QColor(30, 40, 50));
        QVector<KisReferenceImage::FilterEffect> effects;
        for (const QString &id : {QString("reference-test-add"), QString("reference-test-multiply")}) {
            auto config =
                KisFilterRegistry::instance()->value(id)->factoryConfiguration(KisGlobalResourcesInterface::instance());
            effects.append({id, config->toXML(), true});
        }
        QCOMPARE(KisReferenceImage::renderFilterEffects(source, effects).pixelColor(0, 0).red(), 100);
        effects.swapItemsAt(0, 1);
        QCOMPARE(KisReferenceImage::renderFilterEffects(source, effects).pixelColor(0, 0).red(), 80);
        effects[0].enabled = false;
        QCOMPARE(KisReferenceImage::renderFilterEffects(source, effects).pixelColor(0, 0).red(), 50);
        QCOMPARE(source.pixelColor(0, 0).red(), 30);
    }
    void persistenceAndPreviewIsolation()
    {
        KisReferenceImage image;
        image.setSize(QSizeF(120, 240));
        image.setTransformation(QTransform().translate(32, 64).rotate(30));
        const QVector<KisReferenceImage::FilterEffect> effects = {{"missing-filter", "<params>&saved</params>", false},
                                                                  {"levels", "<params/>", true}};
        image.setFilterEffects(effects, false);
        image.previewFilterEffects({}, true);
        QDomDocument xml;
        auto root = xml.createElement("root");
        xml.appendChild(root);
        image.saveXml(xml, root, 0);
        QScopedPointer<KisReferenceImage> restored(KisReferenceImage::fromXml(root.firstChildElement()));
        QCOMPARE(restored->filterEffects().size(), 2);
        QCOMPARE(restored->filterEffects()[0].configuration, effects[0].configuration);
        QVERIFY(!restored->filterEffects()[0].enabled);
        QVERIFY(!restored->filtersEnabled());
        QCOMPARE(restored->size(), image.size());
        const QPointF originalPoint = image.transformation().map(QPointF(10, 20));
        const QPointF restoredPoint = restored->transformation().map(QPointF(10, 20));
        QVERIFY(QLineF(originalPoint, restoredPoint).length() < 0.0001);
        QScopedPointer<KisReferenceImage> clone(static_cast<KisReferenceImage *>(image.cloneShape()));
        clone->setFilterEffects({}, true);
        QCOMPARE(image.filterEffects().size(), 2);
    }
    void legacyLoading()
    {
        QDomDocument xml;
        QVERIFY(xml.setContent(QString("<referenceimage width='5' height='6'/>")));
        QScopedPointer<KisReferenceImage> image(KisReferenceImage::fromXml(xml.documentElement()));
        QVERIFY(image->filterEffects().isEmpty());
        QVERIFY(image->filtersEnabled());
    }
    void asynchronousInvalidation()
    {
        QImage source(8, 8, QImage::Format_ARGB32);
        source.fill(QColor(30, 40, 50));
        KisCoordinatesConverter converter;
        converter.setImageResolution(1.0, 1.0);
        QScopedPointer<KisReferenceImage> image(KisReferenceImage::fromQImage(converter, source));
        QVERIFY(image);
        auto config = KisFilterRegistry::instance()
                          ->value("reference-test-add")
                          ->factoryConfiguration(KisGlobalResourcesInterface::instance());
        QVector<KisReferenceImage::FilterEffect> effects = {{"reference-test-add", config->toXML(), true}};
        image->setFilterEffects(effects, true);
        image->getPixel(QPointF(0, 0)); // Start an asynchronous job.
        image->setFilterEffects({}, false); // Its result must not re-enable the effect.
        QTest::qWait(100);
        QCOMPARE(image->getPixel(QPointF(0, 0)).red(), 30);
        image->setFilterEffects(effects, true);
        QTRY_COMPARE(image->getPixel(QPointF(0, 0)).red(), 50);
        QCOMPARE(image->getImage(), source);
        image->previewFilterEffects({}, false);
        QCOMPARE(image->getPixel(QPointF(0, 0)).red(), 30);
        image->setFilterEffects(effects, true);
        image->getPixel(QPointF(0, 0));
        image.reset(); // The worker must not retain a pointer to the shape.
        QTest::qWait(100);
    }
};
SIMPLE_TEST_MAIN(KisReferenceImageFiltersTest)
#include "KisReferenceImageFiltersTest.moc"
