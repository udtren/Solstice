/*
 * SPDX-FileCopyrightText: 2026 Solstice contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisAbrPresetConverter.h"

#include <QDomDocument>
#include <QDomElement>
#include <QPainter>
#include <QRadialGradient>
#include <QtMath>

#include <KisBrushModel.h>
#include <KisGlobalResourcesInterface.h>
#include <KisResourceTypes.h>
#include <KoCompositeOpRegistry.h>
#include <KoID.h>
#include <KoResourceSignature.h>
#include <brushengine/kis_paintop_preset.h>
#include <brushengine/kis_paintop_registry.h>
#include <brushengine/kis_paintop_settings.h>
#include <kis_properties_configuration.h>

namespace
{
const QString DefaultCurve = QStringLiteral("0,0;1,1;");

QDomElement child(const QDomElement &parent, const QString &key)
{
    for (QDomElement e = parent.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
        if (e.attribute(QStringLiteral("key")) == key) {
            return e;
        }
    }
    return QDomElement();
}

bool isOn(const QDomElement &parent, const QString &key)
{
    return child(parent, key).attribute(QStringLiteral("value")) == QStringLiteral("1");
}

/// a number of any descriptor type; integers are signed 32-bit values that
/// the reader gives unsigned
qreal number(const QDomElement &parent, const QString &key, qreal fallback)
{
    const QDomElement e = child(parent, key);
    if (e.isNull()) {
        return fallback;
    }
    bool ok = false;
    if (e.attribute(QStringLiteral("type")) == QStringLiteral("Integer")) {
        const qlonglong value = e.attribute(QStringLiteral("value")).toLongLong(&ok);
        return ok ? qreal(qint32(quint32(value))) : fallback;
    }
    const qreal value = e.attribute(QStringLiteral("value")).toDouble(&ok);
    return ok ? value : fallback;
}

QString text(const QDomElement &parent, const QString &key)
{
    return child(parent, key).attribute(QStringLiteral("value"));
}

QString curveString(const QVector<QPointF> &points)
{
    QString result;
    for (const QPointF &p : points) {
        result += QString::number(qBound(0.0, p.x(), 1.0)) + QLatin1Char(',') + QString::number(qBound(0.0, p.y(), 1.0))
            + QLatin1Char(';');
    }
    return result;
}

/// A Photoshop dynamic (`brVr`): what controls a setting, its jitter, and
/// the least value it goes down to
struct Dynamic {
    int control = 0;
    int fadeSteps = 25;
    qreal jitter = 0.0;
    qreal minimum = 0.0;

    static Dynamic read(const QDomElement &e)
    {
        Dynamic d;
        if (!e.isNull()) {
            d.control = int(number(e, QStringLiteral("bVTy"), 0));
            d.fadeSteps = qBound(1, int(number(e, QStringLiteral("fStp"), 25)), 9999);
            d.jitter = qMax(0.0, number(e, QStringLiteral("jitter"), 0) / 100.0);
            d.minimum = qBound(0.0, number(e, QStringLiteral("Mnm "), 0) / 100.0, 1.0);
        }
        return d;
    }
    bool isUsed() const
    {
        return control != 0 || jitter > 0.0;
    }
};

struct Sensor {
    QString id;
    QString curve;
    int length = -1;
    bool lockedAngle = false;
};

/// Writes a curve option as the Pixel Brush stores it (the keys of
/// KisKritaSensorPack::write())
void writeCurveOption(KisPropertiesConfiguration *config,
                      const QString &prefix,
                      const QString &id,
                      bool checked,
                      qreal value,
                      QVector<Sensor> sensors)
{
    // an option always has a sensor: a constant one is pressure with a flat
    // curve
    if (sensors.isEmpty()) {
        sensors.append({QStringLiteral("pressure"), QStringLiteral("0,1;1,1;")});
    }
    QDomDocument doc(QStringLiteral("params"));
    QDomElement root = doc.createElement(QStringLiteral("params"));
    doc.appendChild(root);
    auto writeSensor = [&](QDomElement e, const Sensor &sensor) {
        e.setAttribute(QStringLiteral("id"), sensor.id);
        if (sensor.length >= 0) {
            e.setAttribute(QStringLiteral("periodic"), 0);
            e.setAttribute(QStringLiteral("length"), sensor.length);
        }
        if (sensor.id == QStringLiteral("drawingangle")) {
            e.setAttribute(QStringLiteral("fanCornersEnabled"), 0);
            e.setAttribute(QStringLiteral("fanCornersStep"), 30);
            e.setAttribute(QStringLiteral("angleOffset"), 0);
            e.setAttribute(QStringLiteral("lockedAngleMode"), sensor.lockedAngle ? 1 : 0);
        }
        if (!sensor.curve.isEmpty() && sensor.curve != DefaultCurve) {
            QDomElement curve = doc.createElement(QStringLiteral("curve"));
            curve.appendChild(doc.createTextNode(sensor.curve));
            e.appendChild(curve);
        }
    };
    if (sensors.size() == 1) {
        writeSensor(root, sensors.first());
    } else {
        if (!sensors.isEmpty()) {
            root.setAttribute(QStringLiteral("id"), QStringLiteral("sensorslist"));
        }
        for (const Sensor &sensor : sensors) {
            QDomElement e = doc.createElement(QStringLiteral("ChildSensor"));
            writeSensor(e, sensor);
            root.appendChild(e);
        }
    }
    config->setProperty(prefix + QStringLiteral("Pressure") + id, checked);
    config->setProperty(prefix + id + QStringLiteral("Sensor"), doc.toString());
    config->setProperty(prefix + id + QStringLiteral("UseCurve"), true);
    config->setProperty(prefix + id + QStringLiteral("UseSameCurve"), true);
    config->setProperty(prefix + id + QStringLiteral("Value"), value);
    config->setProperty(prefix + id + QStringLiteral("curveMode"), 0);
    config->setProperty(prefix + id + QStringLiteral("commonCurve"), DefaultCurve);
}

/// The sensors that make a setting follow a Photoshop dynamic, going from
/// @p minimum at the lowest input to full at the highest; jitter becomes a
/// random (fuzzy) sensor
QVector<Sensor> sensorsFor(const Dynamic &dynamic, qreal minimum, QMap<QString, int> *unsupported)
{
    QVector<Sensor> sensors;
    const QString rising = curveString({QPointF(0, minimum), QPointF(1, 1)});
    switch (dynamic.control) {
    case 1: // fade
        sensors.append({QStringLiteral("fade"), curveString({QPointF(0, 1), QPointF(1, minimum)}), dynamic.fadeSteps});
        break;
    case 2:
        sensors.append({QStringLiteral("pressure"), rising});
        break;
    case 3:
        sensors.append({QStringLiteral("declination"), rising});
        break;
    case 4:
        sensors.append({QStringLiteral("tangentialpressure"), rising});
        break;
    case 5:
        sensors.append({QStringLiteral("drawingangle"), QString(), -1, true});
        break;
    case 6:
        sensors.append({QStringLiteral("drawingangle"), QString()});
        break;
    case 7:
        (*unsupported)[QStringLiteral("initial rotation (read as rotation)")]++;
        Q_FALLTHROUGH();
    case 8:
        sensors.append({QStringLiteral("rotation"), QString()});
        break;
    default:
        break;
    }
    if (dynamic.jitter > 0.0) {
        sensors.append(
            {QStringLiteral("fuzzy"), curveString({QPointF(0, 1.0 - qMin(1.0, dynamic.jitter)), QPointF(1, 1)})});
    }
    return sensors;
}

QString compositeOpFor(const QString &mode)
{
    static const QHash<QString, QString> ops = {
        {QStringLiteral("Nrml"), COMPOSITE_OVER},
        {QStringLiteral("Dslv"), COMPOSITE_DISSOLVE},
        {QStringLiteral("Drkn"), COMPOSITE_DARKEN},
        {QStringLiteral("Mltp"), COMPOSITE_MULT},
        {QStringLiteral("CBrn"), COMPOSITE_BURN},
        {QStringLiteral("linearBurn"), COMPOSITE_LINEAR_BURN},
        {QStringLiteral("darkerColor"), COMPOSITE_DARKER_COLOR},
        {QStringLiteral("Lghn"), COMPOSITE_LIGHTEN},
        {QStringLiteral("Scrn"), COMPOSITE_SCREEN},
        {QStringLiteral("CDdg"), COMPOSITE_DODGE},
        {QStringLiteral("linearDodge"), COMPOSITE_LINEAR_DODGE},
        {QStringLiteral("lighterColor"), COMPOSITE_LIGHTER_COLOR},
        {QStringLiteral("Ovrl"), COMPOSITE_OVERLAY},
        {QStringLiteral("SftL"), COMPOSITE_SOFT_LIGHT_PHOTOSHOP},
        {QStringLiteral("HrdL"), COMPOSITE_HARD_LIGHT},
        {QStringLiteral("vividLight"), COMPOSITE_VIVID_LIGHT},
        {QStringLiteral("linearLight"), COMPOSITE_LINEAR_LIGHT},
        {QStringLiteral("pinLight"), COMPOSITE_PIN_LIGHT},
        {QStringLiteral("hardMix"), COMPOSITE_HARD_MIX_PHOTOSHOP},
        {QStringLiteral("Dfrn"), COMPOSITE_DIFF},
        {QStringLiteral("Xclu"), COMPOSITE_EXCLUSION},
        {QStringLiteral("Sbtr"), COMPOSITE_SUBTRACT},
        {QStringLiteral("divide"), COMPOSITE_DIVIDE},
        {QStringLiteral("H   "), COMPOSITE_HUE},
        {QStringLiteral("Strt"), COMPOSITE_SATURATION},
        {QStringLiteral("Clr "), COMPOSITE_COLOR},
        {QStringLiteral("Lmns"), COMPOSITE_LUMINIZE},
    };
    return ops.value(mode, COMPOSITE_OVER);
}

/// KisTextureOptionData::TexturingMode for a Photoshop texture mode
int texturingModeFor(const QString &mode)
{
    static const QHash<QString, int> modes = {
        {QStringLiteral("Mltp"), 0},
        {QStringLiteral("Sbtr"), 1},
        {QStringLiteral("Drkn"), 4},
        {QStringLiteral("Ovrl"), 5},
        {QStringLiteral("CDdg"), 6},
        {QStringLiteral("CBrn"), 7},
        {QStringLiteral("linearDodge"), 8},
        {QStringLiteral("linearBurn"), 9},
        {QStringLiteral("hardMix"), 10},
        {QStringLiteral("Hght"), 14},
        {QStringLiteral("linearHeight"), 15},
    };
    return modes.value(mode, 0);
}

/// The tip of a `Brsh` descriptor; false when its sampled tip is missing
bool readTip(const QDomElement &tip,
             const KisAbrPresetConverter::Sources &sources,
             KisBrushModel::BrushData *brush,
             qreal *diameter,
             KisAbrBrushSP *sampled)
{
    using namespace KisBrushModel;
    *diameter = qBound(1.0, number(tip, QStringLiteral("Dmtr"), 30), 5000.0);
    brush->common.angle = qDegreesToRadians(number(tip, QStringLiteral("Angl"), 0));
    brush->common.spacing = qBound(0.01, number(tip, QStringLiteral("Spcn"), 25) / 100.0, 10.0);
    brush->common.useAutoSpacing = false;

    if (tip.attribute(QStringLiteral("classId")) == QStringLiteral("computedBrush")) {
        brush->type = Auto;
        AutoBrushGeneratorData &generator = brush->autoBrush.generator;
        generator.diameter = *diameter;
        generator.ratio = qBound(0.01, number(tip, QStringLiteral("Rndn"), 100) / 100.0, 1.0);
        const qreal fade = 1.0 - qBound(0.0, number(tip, QStringLiteral("Hrdn"), 100) / 100.0, 1.0);
        generator.horizontalFade = fade;
        generator.verticalFade = fade;
        generator.shape = Circle;
        generator.type = Default;
        return true;
    }

    const KisAbrBrushSP abrTip = sources.tipsBySample.value(text(tip, QStringLiteral("sampledData")));
    if (!abrTip) {
        // a file with one tip and no identifiers
        if (sources.tipsBySample.size() != 1) {
            return false;
        }
    }
    const KisAbrBrushSP effectiveTip = abrTip ? abrTip : sources.tipsBySample.begin().value();
    *sampled = effectiveTip;
    brush->type = Predefined;
    PredefinedBrushData &predefined = brush->predefinedBrush;
    predefined.subtype = QStringLiteral("abr_brush");
    predefined.resourceSignature = KoResourceSignature(ResourceType::Brushes,
                                                       effectiveTip->md5Sum(),
                                                       effectiveTip->filename(),
                                                       effectiveTip->name());
    const QSize size = effectiveTip->brushTipImage().size();
    predefined.baseSize = size;
    predefined.scale = *diameter / qMax(1, qMax(size.width(), size.height()));
    predefined.application = ALPHAMASK;
    return true;
}

QImage thumbnailFor(const KisBrushModel::BrushData &brush, const KisAbrBrushSP &sampled)
{
    QImage image(200, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    if (sampled) {
        const QImage tip = sampled->brushTipImage().scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawImage(QPointF(100 - tip.width() / 2.0, 100 - tip.height() / 2.0), tip);
    } else {
        const qreal hardness = 1.0 - brush.autoBrush.generator.horizontalFade;
        QRadialGradient gradient(QPointF(100, 100), 80);
        gradient.setColorAt(0, Qt::black);
        gradient.setColorAt(qBound(0.0, hardness, 0.999), Qt::black);
        gradient.setColorAt(1, Qt::transparent);
        painter.setBrush(gradient);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(100, 100), 80, 80 * brush.autoBrush.generator.ratio);
    }
    return image;
}
} // namespace

KisAbrPresetConverter::Result KisAbrPresetConverter::convert(const QDomElement &root, const Sources &sources)
{
    Result result;
    KisPaintOpRegistry *registry = KisPaintOpRegistry::instance();
    const KoID paintbrush(QStringLiteral("paintbrush"));
    if (!registry || !registry->get(paintbrush.id())) {
        return result;
    }

    QVector<QDomElement> presets;
    const QDomNodeList nodes = root.elementsByTagName(QStringLiteral("node"));
    for (int i = 0; i < nodes.size(); i++) {
        const QDomElement e = nodes.at(i).toElement();
        if (e.attribute(QStringLiteral("classId")) == QStringLiteral("brushPreset")) {
            presets.append(e);
        }
    }

    int number_ = 0;
    for (const QDomElement &p : presets) {
        number_++;
        QMap<QString, int> &unsupported = result.unsupported;

        KisBrushModel::BrushData brush;
        qreal diameter = 30;
        KisAbrBrushSP sampled;
        if (!readTip(child(p, QStringLiteral("Brsh")), sources, &brush, &diameter, &sampled)) {
            result.skipped++;
            continue;
        }

        KisPaintOpSettingsSP settings = registry->createSettings(paintbrush, KisGlobalResourcesInterface::instance());
        if (!settings) {
            result.skipped++;
            continue;
        }
        settings->setProperty(QStringLiteral("paintop"), paintbrush.id());
        brush.write(settings.data());

        const QDomElement tip = child(p, QStringLiteral("Brsh"));
        if (isOn(tip, QStringLiteral("flipX")) || isOn(tip, QStringLiteral("flipY"))) {
            unsupported[QStringLiteral("flipped tip")]++;
        }

        // shape dynamics
        const bool tipDynamics = isOn(p, QStringLiteral("useTipDynamics"));
        const Dynamic size = tipDynamics ? Dynamic::read(child(p, QStringLiteral("szVr"))) : Dynamic();
        const qreal minimumSize = number(p, QStringLiteral("minimumDiameter"), 0) / 100.0;
        writeCurveOption(settings.data(),
                         QString(),
                         QStringLiteral("Size"),
                         size.isUsed(),
                         1.0,
                         sensorsFor(size, minimumSize, &unsupported));

        const Dynamic angle = tipDynamics ? Dynamic::read(child(p, QStringLiteral("angleDynamics"))) : Dynamic();
        {
            Dynamic rotation = angle;
            // the angle jitter is a share of 360 degrees: a fuzzy rotation
            // of that strength
            const qreal jitter = qMin(1.0, angle.jitter);
            rotation.jitter = 0;
            QVector<Sensor> sensors = sensorsFor(rotation, 0, &unsupported);
            if (jitter > 0) {
                sensors.append({QStringLiteral("fuzzy"), QString()});
            }
            writeCurveOption(settings.data(),
                             QString(),
                             QStringLiteral("Rotation"),
                             angle.isUsed(),
                             jitter > 0 && angle.control == 0 ? jitter : 1.0,
                             sensors);
        }

        const qreal roundness = qBound(0.01, number(tip, QStringLiteral("Rndn"), 100) / 100.0, 1.0);
        const Dynamic roundnessDynamic =
            tipDynamics ? Dynamic::read(child(p, QStringLiteral("roundnessDynamics"))) : Dynamic();
        {
            // a sampled tip's roundness squashes it; a computed tip has it
            const bool squash = brush.type == KisBrushModel::Predefined && roundness < 1.0;
            const qreal minimum = number(p, QStringLiteral("minimumRoundness"), 0) / 100.0;
            writeCurveOption(settings.data(),
                             QString(),
                             QStringLiteral("Ratio"),
                             squash || roundnessDynamic.isUsed(),
                             squash ? roundness : 1.0,
                             sensorsFor(roundnessDynamic, minimum, &unsupported));
        }
        if (tipDynamics && (isOn(p, QStringLiteral("flipX")) || isOn(p, QStringLiteral("flipY")))) {
            unsupported[QStringLiteral("flip jitter")]++;
        }

        // scattering
        const bool scatter = isOn(p, QStringLiteral("useScatter"));
        {
            const Dynamic amount = scatter ? Dynamic::read(child(p, QStringLiteral("scatterDynamics"))) : Dynamic();
            Dynamic control = amount;
            control.jitter = 0;
            writeCurveOption(settings.data(),
                             QString(),
                             QStringLiteral("Scatter"),
                             scatter && amount.jitter > 0,
                             qBound(0.0, amount.jitter, 5.0),
                             sensorsFor(control, amount.minimum, &unsupported));
            settings->setProperty(QStringLiteral("Scattering/AxisX"), true);
            settings->setProperty(QStringLiteral("Scattering/AxisY"), scatter && isOn(p, QStringLiteral("bothAxes")));
            if (scatter && number(p, QStringLiteral("Cnt "), 1) > 1) {
                unsupported[QStringLiteral("scatter count")]++;
            }
        }

        // transfer and the tool's opacity, flow and blend mode
        const QDomElement tool = child(p, QStringLiteral("toolOptions"));
        const bool transfer = isOn(p, QStringLiteral("usePaintDynamics"));
        const Dynamic opacity = transfer ? Dynamic::read(child(p, QStringLiteral("opVr"))) : Dynamic();
        const Dynamic flow = transfer ? Dynamic::read(child(p, QStringLiteral("prVr"))) : Dynamic();
        writeCurveOption(settings.data(),
                         QString(),
                         QStringLiteral("Opacity"),
                         true,
                         qBound(0.0, number(tool, QStringLiteral("Opct"), 100) / 100.0, 1.0),
                         sensorsFor(opacity, opacity.minimum, &unsupported));
        writeCurveOption(settings.data(),
                         QString(),
                         QStringLiteral("Flow"),
                         true,
                         qBound(0.0, number(tool, QStringLiteral("flow"), 100) / 100.0, 1.0),
                         sensorsFor(flow, flow.minimum, &unsupported));
        settings->setProperty(QStringLiteral("CompositeOp"), compositeOpFor(text(tool, QStringLiteral("Md  "))));
        // Photoshop's opacity limits a stroke and its flow builds up the
        // dabs: Wash
        settings->setProperty(QStringLiteral("PaintOpAction"), 2);

        // texture
        if (isOn(p, QStringLiteral("useTexture"))) {
            const QDomElement reference = child(p, QStringLiteral("Txtr"));
            KoPatternSP pattern = sources.patternsById.value(text(reference, QStringLiteral("Idnt")));
            if (!pattern) {
                unsupported[QStringLiteral("texture whose pattern is not in the file")]++;
            } else {
                const QString prefix = QStringLiteral("Texture/Pattern/");
                settings->setProperty(prefix + QStringLiteral("Enabled"), true);
                settings->setProperty(
                    prefix + QStringLiteral("PatternMD5"),
                    QString::fromLatin1(QByteArray::fromHex(pattern->md5Sum().toLatin1()).toBase64()));
                settings->setProperty(prefix + QStringLiteral("PatternMD5Sum"), pattern->md5Sum());
                settings->setProperty(prefix + QStringLiteral("PatternFileName"), pattern->filename());
                settings->setProperty(prefix + QStringLiteral("Name"), pattern->name());
                settings->setProperty(prefix + QStringLiteral("Scale"),
                                      qBound(0.01, number(p, QStringLiteral("textureScale"), 100) / 100.0, 10.0));
                // Solstice's brightness is subtracted from the pattern's
                // lightness; Photoshop's is added
                settings->setProperty(prefix + QStringLiteral("Brightness"),
                                      qBound(-1.0, -number(p, QStringLiteral("textureBrightness"), 0) / 150.0, 1.0));
                settings->setProperty(prefix + QStringLiteral("Contrast"),
                                      qBound(0.0, 1.0 + number(p, QStringLiteral("textureContrast"), 0) / 50.0, 2.0));
                settings->setProperty(prefix + QStringLiteral("NeutralPoint"), 0.5);
                settings->setProperty(prefix + QStringLiteral("OffsetX"), 0);
                settings->setProperty(prefix + QStringLiteral("OffsetY"), 0);
                settings->setProperty(prefix + QStringLiteral("isRandomOffsetX"), false);
                settings->setProperty(prefix + QStringLiteral("isRandomOffsetY"), false);
                const int texturingMode = texturingModeFor(text(p, QStringLiteral("textureBlendMode")));
                settings->setProperty(prefix + QStringLiteral("TexturingMode"), texturingMode);
                // Photoshop's subtract and height modes take paint away
                // where the pattern is dark; Solstice's take away the mask
                // value, so the pattern is inverted for them
                const bool depthMode = texturingMode == 1 || texturingMode == 14 || texturingMode == 15;
                settings->setProperty(prefix + QStringLiteral("UseSoftTexturing"), false);
                settings->setProperty(prefix + QStringLiteral("CutoffLeft"), 0);
                settings->setProperty(prefix + QStringLiteral("CutoffRight"), 255);
                settings->setProperty(prefix + QStringLiteral("CutoffPolicy"), 0);
                settings->setProperty(prefix + QStringLiteral("Invert"), isOn(p, QStringLiteral("InvT")) != depthMode);
                settings->setProperty(prefix + QStringLiteral("AutoInvertOnErase"), false);

                const Dynamic depth = Dynamic::read(child(p, QStringLiteral("textureDepthDynamics")));
                const qreal minimumDepth = number(p, QStringLiteral("minimumDepth"), 0) / 100.0;
                writeCurveOption(settings.data(),
                                 QString(),
                                 QStringLiteral("Texture/Strength/"),
                                 true,
                                 qBound(0.0, number(p, QStringLiteral("textureDepth"), 100) / 100.0, 1.0),
                                 sensorsFor(depth, minimumDepth, &unsupported));
                if (isOn(p, QStringLiteral("protectTexture"))) {
                    unsupported[QStringLiteral("protect texture")]++;
                }
            }
        }

        // dual brush: the masking brush
        const QDomElement dual = child(p, QStringLiteral("dualBrush"));
        if (isOn(dual, QStringLiteral("useDualBrush"))) {
            KisBrushModel::BrushData maskingBrush;
            qreal maskingDiameter = diameter;
            KisAbrBrushSP maskingSampled;
            if (!readTip(child(dual, QStringLiteral("Brsh")),
                         sources,
                         &maskingBrush,
                         &maskingDiameter,
                         &maskingSampled)) {
                unsupported[QStringLiteral("dual brush whose tip is not in the file")]++;
            } else {
                if (!child(dual, QStringLiteral("Spcn")).isNull()) {
                    maskingBrush.common.spacing = qBound(0.01, number(dual, QStringLiteral("Spcn"), 25) / 100.0, 10.0);
                }
                KisPropertiesConfiguration masking;
                maskingBrush.write(&masking);
                const QString prefix = QStringLiteral("MaskingBrush/Preset/");
                settings->setProperty(QStringLiteral("MaskingBrush/Enabled"), true);
                settings->setProperty(QStringLiteral("MaskingBrush/MaskingCompositeOp"),
                                      compositeOpFor(text(dual, QStringLiteral("BlnM"))));
                settings->setProperty(QStringLiteral("MaskingBrush/UseMasterSize"), true);
                settings->setProperty(QStringLiteral("MaskingBrush/MasterSizeCoeff"), maskingDiameter / diameter);
                settings->setProperty(prefix + QStringLiteral("paintop"), paintbrush.id());
                settings->setProperty(prefix + QStringLiteral("brush_definition"),
                                      masking.getString(QStringLiteral("brush_definition")));
                const bool dualScatter = isOn(dual, QStringLiteral("useScatter"));
                const Dynamic amount =
                    dualScatter ? Dynamic::read(child(dual, QStringLiteral("scatterDynamics"))) : Dynamic();
                writeCurveOption(settings.data(),
                                 prefix,
                                 QStringLiteral("Scatter"),
                                 dualScatter && amount.jitter > 0,
                                 qBound(0.0, amount.jitter, 5.0),
                                 {});
                settings->setProperty(prefix + QStringLiteral("Scattering/AxisX"), true);
                settings->setProperty(prefix + QStringLiteral("Scattering/AxisY"),
                                      dualScatter && isOn(dual, QStringLiteral("bothAxes")));
                if (dualScatter && number(dual, QStringLiteral("Cnt "), 1) > 1) {
                    unsupported[QStringLiteral("dual brush scatter count")]++;
                }
                if (isOn(dual, QStringLiteral("Flip"))) {
                    unsupported[QStringLiteral("dual brush flip")]++;
                }
            }
        }

        if (isOn(p, QStringLiteral("useColorDynamics"))) {
            unsupported[QStringLiteral("color dynamics")]++;
        }
        if (isOn(p, QStringLiteral("Wtdg"))) {
            unsupported[QStringLiteral("wet edges")]++;
        }
        if (isOn(p, QStringLiteral("Nose"))) {
            unsupported[QStringLiteral("noise")]++;
        }
        if (isOn(p, QStringLiteral("useBrushPose"))) {
            unsupported[QStringLiteral("brush pose")]++;
        }

        KisPaintOpPresetSP preset(new KisPaintOpPreset());
        preset->setSettings(settings);
        QString name = text(p, QStringLiteral("Nm  ")).trimmed();
        if (name.isEmpty()) {
            name = QStringLiteral("%1 %2").arg(sources.baseName).arg(number_);
        }
        preset->setName(name);
        const QString fileName = QStringLiteral("%1_preset_%2.kpp").arg(sources.baseName).arg(number_);
        preset->setFilename(fileName);
        preset->setImage(thumbnailFor(brush, sampled));
        preset->setValid(true);
        result.presets.insert(fileName, preset);
    }
    return result;
}

namespace
{
/// deeper folders are put in their parent at this depth
const int MaxFolderDepth = 32;

enum class HierarchyToken {
    Preset,
    Group,
    GroupEnd
};

/// The hierarchy is a list of tokens: a folder (`Grup`, with its name in
/// `Nm  `), the end of the innermost folder (`groupEnd`) and a preset
/// (`preset`). The token is the descriptor's class or, for a plain
/// descriptor, the key of its only child; an enumeration or text gives it
/// as its value. Anything else counts as a preset.
HierarchyToken hierarchyToken(const QDomElement &e)
{
    QStringList names;
    names << e.attribute(QStringLiteral("classId")) << e.attribute(QStringLiteral("value"));
    const QDomElement first = e.firstChildElement();
    if (!first.isNull() && first.nextSiblingElement().isNull()) {
        names << first.attribute(QStringLiteral("key"));
    }
    for (const QString &name : names) {
        const QString token = name.trimmed().toLower();
        if (token == QStringLiteral("groupend")) {
            return HierarchyToken::GroupEnd;
        }
        if (token == QStringLiteral("grup") || token == QStringLiteral("group")) {
            return HierarchyToken::Group;
        }
    }
    return HierarchyToken::Preset;
}

QString folderName(const QDomElement &group)
{
    const QDomNodeList nodes = group.elementsByTagName(QStringLiteral("node"));
    for (int i = 0; i < nodes.size(); i++) {
        const QDomElement e = nodes.at(i).toElement();
        if (e.attribute(QStringLiteral("key")) == QStringLiteral("Nm  ")
            && e.attribute(QStringLiteral("type")) == QStringLiteral("Text")) {
            return e.attribute(QStringLiteral("value")).trimmed();
        }
    }
    return QString();
}

struct HierarchyWalk {
    QStringList path;
    /// folders past MaxFolderDepth, which are not in the path
    int hiddenDepth = 0;
    QVector<QStringList> folders;

    void open(const QString &name)
    {
        if (path.size() < MaxFolderDepth) {
            path << (name.isEmpty() ? QStringLiteral("?") : name);
        } else {
            hiddenDepth++;
        }
    }

    void close()
    {
        // a stray end is ignored
        if (hiddenDepth > 0) {
            hiddenDepth--;
        } else if (!path.isEmpty()) {
            path.removeLast();
        }
    }

    void walk(const QDomElement &list)
    {
        for (QDomElement e = list.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            switch (hierarchyToken(e)) {
            case HierarchyToken::GroupEnd:
                close();
                break;
            case HierarchyToken::Group: {
                open(folderName(e));
                // a folder that holds its contents as a list closes itself
                QDomElement contents;
                for (QDomElement c = e.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
                    if (c.attribute(QStringLiteral("type")) == QStringLiteral("List")) {
                        contents = c;
                        break;
                    }
                }
                if (!contents.isNull()) {
                    walk(contents);
                    close();
                }
                break;
            }
            case HierarchyToken::Preset:
                folders << path;
                break;
            }
        }
    }
};
} // namespace

QVector<QStringList> KisAbrPresetConverter::presetFolders(const QDomElement &root)
{
    const QDomNodeList nodes = root.elementsByTagName(QStringLiteral("node"));
    for (int i = 0; i < nodes.size(); i++) {
        const QDomElement e = nodes.at(i).toElement();
        if (e.attribute(QStringLiteral("key")) == QStringLiteral("hierarchy")
            && e.attribute(QStringLiteral("type")) == QStringLiteral("List")) {
            // folders still open at the end close themselves
            HierarchyWalk walk;
            walk.walk(e);
            return walk.folders;
        }
    }
    return {};
}
