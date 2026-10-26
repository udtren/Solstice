/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "TestControlCanvasTools.h"

#include <QAction>
#include <QBuffer>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QPixmap>
#include <QSysInfo>
#include <QTimer>

#include <KisDocument.h>
#include <KisMainWindow.h>
#include <KisPart.h>
#include <KisViewManager.h>
#include <KoColor.h>
#include <KoColorModelStandardIds.h>
#include <KoColorProfile.h>
#include <KoColorSpace.h>
#include <KoColorSpaceRegistry.h>
#include <KoToolManager.h>
#include <KoToolRegistry.h>
#include <KritaVersionWrapper.h>
#include <kactioncollection.h>
#include <kis_canvas2.h>
#include <kis_config.h>
#include <kis_coordinates_converter.h>
#include <kis_image.h>
#include <kis_node.h>
#include <kis_paint_device.h>
#include <kis_pixel_selection.h>
#include <kis_selection.h>
#include <kundo2stack.h>

#include "TestControlInputInjector.h"

namespace
{
constexpr int MaxStrokePoints = 10000;
constexpr int DefaultPointSpacingMs = 10;
constexpr qint64 MaxStrokeDurationMs = 120000;
constexpr qint64 MaxHashedBytes = qint64(512) * 1024 * 1024;

KisMainWindow *currentWindow()
{
    return KisPart::instance()->currentMainwindow();
}

KisViewManager *currentViewManager()
{
    KisMainWindow *window = currentWindow();
    return window ? window->viewManager() : nullptr;
}

QJsonObject rectToJson(const QRect &rect)
{
    return {{"x", rect.x()}, {"y", rect.y()}, {"width", rect.width()}, {"height", rect.height()}};
}

QString sha256(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

/// Reads @p rect of @p device; returns false if the rect is too large to hash.
bool readDevice(KisPaintDeviceSP device, const QRect &rect, QByteArray *bytes)
{
    const qint64 size = qint64(rect.width()) * rect.height() * device->pixelSize();
    if (size > MaxHashedBytes) {
        return false;
    }
    bytes->resize(int(size));
    device->readBytes(reinterpret_cast<quint8 *>(bytes->data()), rect);
    return true;
}

QJsonValue documentInfo(KisViewManager *viewManager)
{
    KisImageSP image;
    if (viewManager) {
        image = viewManager->image();
    }
    if (!image) {
        return QJsonValue::Null;
    }
    const KoColorSpace *colorSpace = image->colorSpace();
    return QJsonObject{
        {"width", image->width()},
        {"height", image->height()},
        {"colorModel", colorSpace->colorModelId().id()},
        {"colorDepth", colorSpace->colorDepthId().id()},
        {"profile", colorSpace->profile() ? colorSpace->profile()->name() : QString()},
    };
}

QJsonValue canvasInfo(KisViewManager *viewManager)
{
    KisCanvas2 *canvas = viewManager ? viewManager->canvasBase() : nullptr;
    QWidget *widget = canvas ? canvas->canvasWidget() : nullptr;
    if (!widget) {
        return QJsonValue::Null;
    }
    return QJsonObject{
        {"widgetClass", QString::fromLatin1(widget->metaObject()->className())},
        {"width", widget->width()},
        {"height", widget->height()},
        {"hasFocus", widget->hasFocus()},
        {"windowActive", widget->isActiveWindow()},
    };
}

QJsonObject objectSchema(const QJsonObject &properties, const QStringList &required = QStringList())
{
    QJsonObject schema{{"type", "object"}, {"properties", properties}, {"additionalProperties", false}};
    if (!required.isEmpty()) {
        schema.insert("required", QJsonArray::fromStringList(required));
    }
    return schema;
}

QJsonObject schemaProperty(const QString &type, const QString &description)
{
    return {{"type", type}, {"description", description}};
}

QJsonObject enumProperty(const QStringList &values, const QString &description)
{
    return {{"type", "string"}, {"enum", QJsonArray::fromStringList(values)}, {"description", description}};
}

QJsonObject environmentInfo()
{
    QJsonObject result;
    for (const char *name : {"KRITA_GPU_PROJECTION", "KRITA_GPU_BRUSH", "KRITA_PAINT_TRACE", "SOLSTICE_PROFILE_ROOT"}) {
        if (qEnvironmentVariableIsSet(name)) {
            result.insert(QString::fromLatin1(name), qEnvironmentVariable(name));
        }
    }
    return result;
}
} // namespace

TestControlCanvasTools::TestControlCanvasTools(QObject *parent)
    : QObject(parent)
    , m_injector(new TestControlInputInjector(this))
{
}

TestControlCanvasTools::~TestControlCanvasTools() = default;

QString TestControlCanvasTools::instructions()
{
    return QStringLiteral(
        "Drives a running Solstice through synthetic input for testing. "
        "Typical flow: session_info, document_new, tool_activate (e.g. KritaShape/KisToolBrush, "
        "KisToolSelectRectangular), input_stroke, wait_idle, query_state. Always call wait_idle after input "
        "or actions before reading state. Coordinates are image pixels unless coordinates is 'widget'. "
        "Calls run one at a time. Results are evidence for a test, not proof of real tablet behavior.");
}

void TestControlCanvasTools::registerTools(TestControlMcpServer *server)
{
    using namespace std::placeholders;

    server->addTool("session_info",
                    "Report versions, the active document, canvas widget, active tool and GPU environment.",
                    objectSchema({}),
                    std::bind(&TestControlCanvasTools::sessionInfo, this, _1, _2));

    server->addTool("document_new",
                    "Create an RGBA document with one transparent paint layer on a white canvas and show it in "
                    "the current window.",
                    objectSchema({
                        {"width", schemaProperty("integer", "Width in pixels (1-16384, default 800).")},
                        {"height", schemaProperty("integer", "Height in pixels (1-16384, default 600).")},
                        {"colorDepth", enumProperty({"U8", "U16", "F16", "F32"}, "Channel depth (default U8).")},
                    }),
                    std::bind(&TestControlCanvasTools::documentNew, this, _1, _2));

    server->addTool("tool_activate",
                    "Activate a tool by its factory ID, e.g. KritaShape/KisToolBrush or KisToolSelectRectangular. "
                    "An unknown ID fails and lists the available IDs.",
                    objectSchema({{"id", schemaProperty("string", "Tool factory ID.")}}, {"id"}),
                    std::bind(&TestControlCanvasTools::toolActivate, this, _1, _2));

    const QJsonObject pointSchema = objectSchema(
        {
            {"x", schemaProperty("number", "X coordinate.")},
            {"y", schemaProperty("number", "Y coordinate.")},
            {"pressure", schemaProperty("number", "Pressure 0-1 (tablet only, default 1).")},
            {"xTilt", schemaProperty("number", "X tilt in degrees, -60 to 60 (tablet only).")},
            {"yTilt", schemaProperty("number", "Y tilt in degrees, -60 to 60 (tablet only).")},
            {"t", schemaProperty("number", "Time offset in ms (0-120000); must not decrease.")},
        },
        {"x", "y"});

    server->addTool(
        "input_stroke",
        "Play one press-move-release stroke into the canvas through the input manager, so the "
        "active tool handles it. Points without t are spaced 10 ms apart. Call wait_idle afterwards.",
        objectSchema(
            {
                {"points",
                 QJsonObject{{"type", "array"},
                             {"items", pointSchema},
                             {"minItems", 1},
                             {"maxItems", MaxStrokePoints},
                             {"description", "Stroke points; the first is the press."}}},
                {"device", enumProperty({"mouse", "tablet"}, "Input device (default mouse).")},
                {"coordinates", enumProperty({"image", "widget"}, "Coordinate space of the points (default image).")},
                {"button", enumProperty({"left", "right", "middle"}, "Button (default left).")},
                {"realtime",
                 schemaProperty("boolean",
                                "Send each point at its time offset (default true); false sends them back "
                                "to back.")},
            },
            {"points"}),
        std::bind(&TestControlCanvasTools::inputStroke, this, _1, _2));

    server->addTool("wait_idle",
                    "Wait until the image has no queued or running strokes and stays idle for three polls "
                    "20 ms apart, so follow-up work queued to the GUI thread (such as a new selection mask) "
                    "has run too.",
                    objectSchema({{"timeoutMs", schemaProperty("integer", "Timeout (default 10000, max 120000).")}}),
                    std::bind(&TestControlCanvasTools::waitIdle, this, _1, _2));

    server->addTool("query_state",
                    "Report the active tool, document, undo stack, selection bounds/pixel count/hash and the "
                    "active layer's exact bounds and SHA-256 pixel hash. Fails while the image is busy.",
                    objectSchema({{"includeLayerHash",
                                   schemaProperty("boolean", "Hash the active layer's pixels (default true).")}}),
                    std::bind(&TestControlCanvasTools::queryState, this, _1, _2));

    server->addTool("action_trigger",
                    "Trigger a main window action by name, e.g. edit_undo, edit_redo, deselect, select_all.",
                    objectSchema({{"name", schemaProperty("string", "Action name.")}}, {"name"}),
                    std::bind(&TestControlCanvasTools::actionTrigger, this, _1, _2));

    server->addTool("canvas_capture",
                    "Return a PNG image of the canvas widget as displayed (diagnostic; not a pixel reference).",
                    objectSchema({{"maxSize", schemaProperty("integer", "Longest side in pixels (default 1024).")}}),
                    std::bind(&TestControlCanvasTools::canvasCapture, this, _1, _2));
}

void TestControlCanvasTools::sessionInfo(const Arguments &, const Reply &reply)
{
    KisViewManager *viewManager = currentViewManager();
    reply(TestControlMcpServer::ToolResult::success({
        {"solsticeVersion", KritaVersionWrapper::solsticeVersionString(true)},
        {"kritaCompatibilityVersion", KritaVersionWrapper::versionString(true)},
        {"qtVersion", QString::fromLatin1(qVersion())},
        {"platform", QSysInfo::prettyProductName()},
        {"mainWindows", KisPart::instance()->mainwindowCount()},
        {"activeTool", KoToolManager::instance()->activeToolId()},
        {"document", documentInfo(viewManager)},
        {"canvas", canvasInfo(viewManager)},
        {"environment", environmentInfo()},
    }));
}

void TestControlCanvasTools::documentNew(const Arguments &arguments, const Reply &reply)
{
    const int width = arguments.value("width").toInt(800);
    const int height = arguments.value("height").toInt(600);
    if (width < 1 || width > 16384 || height < 1 || height > 16384) {
        reply(TestControlMcpServer::ToolResult::failure("width and height must be between 1 and 16384"));
        return;
    }

    const QString depth = arguments.value("colorDepth").toString("U8");
    KoID depthId;
    if (depth == QLatin1String("U8")) {
        depthId = Integer8BitsColorDepthID;
    } else if (depth == QLatin1String("U16")) {
        depthId = Integer16BitsColorDepthID;
    } else if (depth == QLatin1String("F16")) {
        depthId = Float16BitsColorDepthID;
    } else if (depth == QLatin1String("F32")) {
        depthId = Float32BitsColorDepthID;
    } else {
        reply(TestControlMcpServer::ToolResult::failure("Unknown colorDepth: " + depth));
        return;
    }

    if (!currentWindow()) {
        reply(TestControlMcpServer::ToolResult::failure("No main window"));
        return;
    }

    const KoColorSpace *colorSpace = KoColorSpaceRegistry::instance()->colorSpace(RGBAColorModelID.id(), depthId.id());
    if (!colorSpace) {
        reply(TestControlMcpServer::ToolResult::failure("Color space RGBA/" + depth + " is not available"));
        return;
    }

    KisDocument *document = KisPart::instance()->createDocument();
    // An empty name avoids a new translatable string; the view shows the
    // usual unnamed-document title.
    const bool created = document->newImage(QString(),
                                            width,
                                            height,
                                            colorSpace,
                                            KoColor(QColor(Qt::white), colorSpace),
                                            KisConfig::CANVAS_COLOR,
                                            1,
                                            QString(),
                                            300.0 / 72.0);
    if (!created) {
        delete document;
        reply(TestControlMcpServer::ToolResult::failure("KisDocument::newImage() failed"));
        return;
    }

    QPointer<KisDocument> guard(document);
    KisPart::instance()->startCustomDocument(document);

    pollUntil(
        [guard]() {
            KisViewManager *viewManager = currentViewManager();
            return guard && viewManager && viewManager->document() == guard.data() && viewManager->canvasBase();
        },
        5000,
        [reply](bool shown, qint64) {
            KisViewManager *viewManager = currentViewManager();
            if (!shown) {
                reply(TestControlMcpServer::ToolResult::failure("The new document did not become the active view"));
                return;
            }
            reply(TestControlMcpServer::ToolResult::success({
                {"document", documentInfo(viewManager)},
                {"canvas", canvasInfo(viewManager)},
                {"activeTool", KoToolManager::instance()->activeToolId()},
            }));
        });
}

void TestControlCanvasTools::toolActivate(const Arguments &arguments, const Reply &reply)
{
    const QString id = arguments.value("id").toString();
    if (!KoToolRegistry::instance()->contains(id)) {
        QStringList ids = KoToolRegistry::instance()->keys();
        ids.sort();
        reply(TestControlMcpServer::ToolResult::failure("Unknown tool ID: " + id,
                                                        {{"availableTools", QJsonArray::fromStringList(ids)}}));
        return;
    }
    if (!currentViewManager() || !currentViewManager()->canvasBase()) {
        reply(TestControlMcpServer::ToolResult::failure("No canvas; create or open a document first"));
        return;
    }

    KoToolManager::instance()->switchToolRequested(id);

    pollUntil(
        [id]() {
            return KoToolManager::instance()->activeToolId() == id;
        },
        2000,
        [reply, id](bool active, qint64) {
            const QString activeTool = KoToolManager::instance()->activeToolId();
            if (!active) {
                reply(TestControlMcpServer::ToolResult::failure("The tool did not become active",
                                                                {{"requested", id}, {"activeTool", activeTool}}));
                return;
            }
            reply(TestControlMcpServer::ToolResult::success({{"activeTool", activeTool}}));
        });
}

void TestControlCanvasTools::inputStroke(const Arguments &arguments, const Reply &reply)
{
    KisViewManager *viewManager = currentViewManager();
    KisCanvas2 *canvas = viewManager ? viewManager->canvasBase() : nullptr;
    QWidget *widget = canvas ? canvas->canvasWidget() : nullptr;
    if (!widget) {
        reply(TestControlMcpServer::ToolResult::failure("No canvas; create or open a document first"));
        return;
    }

    const QJsonArray jsonPoints = arguments.value("points").toArray();
    if (jsonPoints.isEmpty() || jsonPoints.size() > MaxStrokePoints) {
        reply(TestControlMcpServer::ToolResult::failure(
            QStringLiteral("points must hold 1 to %1 points").arg(MaxStrokePoints)));
        return;
    }

    const QString deviceName = arguments.value("device").toString("mouse");
    const QString coordinates = arguments.value("coordinates").toString("image");
    const QString buttonName = arguments.value("button").toString("left");
    const bool realtime = arguments.value("realtime").toBool(true);

    if (deviceName != QLatin1String("mouse") && deviceName != QLatin1String("tablet")) {
        reply(TestControlMcpServer::ToolResult::failure("Unknown device: " + deviceName));
        return;
    }
    if (coordinates != QLatin1String("image") && coordinates != QLatin1String("widget")) {
        reply(TestControlMcpServer::ToolResult::failure("Unknown coordinates: " + coordinates));
        return;
    }

    Qt::MouseButton button = Qt::LeftButton;
    if (buttonName == QLatin1String("right")) {
        button = Qt::RightButton;
    } else if (buttonName == QLatin1String("middle")) {
        button = Qt::MiddleButton;
    } else if (buttonName != QLatin1String("left")) {
        reply(TestControlMcpServer::ToolResult::failure("Unknown button: " + buttonName));
        return;
    }

    bool anyTime = false;
    for (const QJsonValue &value : jsonPoints) {
        anyTime |= value.toObject().contains("t");
    }

    const KisCoordinatesConverter *converter = canvas->coordinatesConverter();
    const QRectF widgetRect(widget->rect());
    QVector<TestControlInputInjector::Point> points;
    int outside = 0;

    for (int i = 0; i < jsonPoints.size(); ++i) {
        const QJsonObject object = jsonPoints[i].toObject();
        if (!object.value("x").isDouble() || !object.value("y").isDouble()) {
            reply(TestControlMcpServer::ToolResult::failure(QStringLiteral("Point %1 needs numeric x and y").arg(i)));
            return;
        }
        if (anyTime && !object.value("t").isDouble()) {
            reply(TestControlMcpServer::ToolResult::failure(
                QStringLiteral("Point %1 has no t; give t for every point or for none").arg(i)));
            return;
        }

        const QPointF source(object.value("x").toDouble(), object.value("y").toDouble());
        TestControlInputInjector::Point point;
        point.position = coordinates == QLatin1String("image") ? converter->imageToWidget(source) : source;
        point.pressure = qBound(0.0, object.value("pressure").toDouble(1.0), 1.0);
        point.xTilt = qBound(-60.0, object.value("xTilt").toDouble(0.0), 60.0);
        point.yTilt = qBound(-60.0, object.value("yTilt").toDouble(0.0), 60.0);
        point.timeMs = anyTime ? qint64(object.value("t").toDouble()) : qint64(i) * DefaultPointSpacingMs;
        if (point.timeMs < 0 || point.timeMs > MaxStrokeDurationMs) {
            reply(TestControlMcpServer::ToolResult::failure(
                QStringLiteral("Point %1: t must be between 0 and %2 ms").arg(i).arg(MaxStrokeDurationMs)));
            return;
        }
        if (!widgetRect.contains(point.position)) {
            outside++;
        }
        points.append(point);
    }

    const TestControlInputInjector::Device device = deviceName == QLatin1String("tablet")
        ? TestControlInputInjector::Device::Tablet
        : TestControlInputInjector::Device::Mouse;

    m_injector->playStroke(widget,
                           device,
                           button,
                           points,
                           realtime,
                           [reply, outside](const TestControlInputInjector::Report &report) {
                               const QJsonObject details{
                                   {"eventsSent", report.eventsSent},
                                   {"pressAccepted", report.pressAccepted},
                                   {"releaseAccepted", report.releaseAccepted},
                                   {"durationMs", report.durationMs},
                                   {"pointsOutsideCanvas", outside},
                                   {"activeTool", KoToolManager::instance()->activeToolId()},
                               };
                               if (!report.error.isEmpty()) {
                                   reply(TestControlMcpServer::ToolResult::failure(report.error, details));
                               } else {
                                   reply(TestControlMcpServer::ToolResult::success(details));
                               }
                           });
}

void TestControlCanvasTools::waitIdle(const Arguments &arguments, const Reply &reply)
{
    KisViewManager *viewManager = currentViewManager();
    KisImageSP image;
    if (viewManager) {
        image = viewManager->image();
    }
    if (!image) {
        reply(TestControlMcpServer::ToolResult::failure("No image; create or open a document first"));
        return;
    }

    const int timeoutMs = qBound(0, arguments.value("timeoutMs").toInt(10000), 120000);
    pollUntil(
        [image]() mutable {
            return image->isIdle();
        },
        timeoutMs,
        [reply](bool idle, qint64 waitedMs) {
            if (idle) {
                reply(TestControlMcpServer::ToolResult::success({{"idle", true}, {"waitedMs", waitedMs}}));
            } else {
                reply(TestControlMcpServer::ToolResult::failure("The image did not become idle",
                                                                {{"idle", false}, {"waitedMs", waitedMs}}));
            }
        },
        // An image that reports idle once can still have work queued to the
        // GUI thread: a selection made on a document without a global
        // selection was read as empty in about one of 25 runs.
        3);
}

void TestControlCanvasTools::queryState(const Arguments &arguments, const Reply &reply)
{
    const bool includeLayerHash = arguments.value("includeLayerHash").toBool(true);

    QJsonObject state{{"activeTool", KoToolManager::instance()->activeToolId()}};
    KisViewManager *viewManager = currentViewManager();
    KisImageSP image;
    if (viewManager) {
        image = viewManager->image();
    }
    state.insert("document", documentInfo(viewManager));
    if (!image) {
        reply(TestControlMcpServer::ToolResult::success(state));
        return;
    }

    KisDocument *document = viewManager->document();
    if (document && document->undoStack()) {
        KUndo2Stack *stack = document->undoStack();
        const int index = stack->index();
        state.insert("undo",
                     QJsonObject{
                         {"count", stack->count()},
                         {"index", index},
                         {"lastCommand", index > 0 ? stack->text(index - 1) : QString()},
                     });
    }

    if (!image->tryBarrierLock(true)) {
        reply(TestControlMcpServer::ToolResult::failure("The image is busy; call wait_idle first", state));
        return;
    }

    KisSelectionSP selection = viewManager->selection();
    if (selection) {
        const QRect bounds = selection->selectedExactRect();
        QJsonObject selectionInfo{{"active", true}, {"bounds", rectToJson(bounds)}};
        KisPixelSelectionSP pixels = selection->projection();
        QByteArray bytes;
        if (pixels && !bounds.isEmpty() && readDevice(pixels, bounds, &bytes)) {
            const int pixelSize = pixels->pixelSize();
            qint64 selected = 0;
            for (int i = 0; i < bytes.size(); i += pixelSize) {
                bool any = false;
                for (int c = 0; c < pixelSize; ++c) {
                    any |= bytes[i + c] != 0;
                }
                selected += any;
            }
            selectionInfo.insert("selectedPixels", selected);
            selectionInfo.insert("sha256", sha256(bytes));
        }
        state.insert("selection", selectionInfo);
    } else {
        state.insert("selection", QJsonObject{{"active", false}});
    }

    KisNodeSP node = viewManager->activeNode();
    if (node) {
        QJsonObject layer{
            {"name", node->name()},
            {"class", QString::fromLatin1(node->metaObject()->className())},
        };
        KisPaintDeviceSP device = node->paintDevice();
        if (device) {
            const QRect bounds = device->exactBounds();
            layer.insert("exactBounds", rectToJson(bounds));
            layer.insert("colorSpace", device->colorSpace()->id());
            QByteArray bytes;
            if (includeLayerHash && !bounds.isEmpty() && readDevice(device, bounds, &bytes)) {
                layer.insert("sha256", sha256(bytes));
            }
        }
        state.insert("activeLayer", layer);
    }

    image->unlock();
    reply(TestControlMcpServer::ToolResult::success(state));
}

void TestControlCanvasTools::actionTrigger(const Arguments &arguments, const Reply &reply)
{
    const QString name = arguments.value("name").toString();
    KisMainWindow *window = currentWindow();
    QAction *action = window ? window->actionCollection()->action(name) : nullptr;
    if (!action) {
        reply(TestControlMcpServer::ToolResult::failure("Unknown action: " + name));
        return;
    }
    if (!action->isEnabled()) {
        reply(TestControlMcpServer::ToolResult::failure("The action is disabled: " + name));
        return;
    }

    action->trigger();

    QPointer<QAction> guard(action);
    QTimer::singleShot(0, this, [reply, guard, name]() {
        QJsonObject result{{"triggered", name}};
        if (guard && guard->isCheckable()) {
            result.insert("checked", guard->isChecked());
        }
        reply(TestControlMcpServer::ToolResult::success(result));
    });
}

void TestControlCanvasTools::canvasCapture(const Arguments &arguments, const Reply &reply)
{
    KisViewManager *viewManager = currentViewManager();
    KisCanvas2 *canvas = viewManager ? viewManager->canvasBase() : nullptr;
    QWidget *widget = canvas ? canvas->canvasWidget() : nullptr;
    if (!widget) {
        reply(TestControlMcpServer::ToolResult::failure("No canvas; create or open a document first"));
        return;
    }

    QImage image = widget->grab().toImage();
    const int maxSize = qBound(16, arguments.value("maxSize").toInt(1024), 4096);
    if (image.width() > maxSize || image.height() > maxSize) {
        image = image.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (image.isNull() || !image.save(&buffer, "PNG")) {
        reply(TestControlMcpServer::ToolResult::failure("Could not capture the canvas widget"));
        return;
    }

    TestControlMcpServer::ToolResult result = TestControlMcpServer::ToolResult::success({
        {"width", image.width()},
        {"height", image.height()},
        {"widgetWidth", widget->width()},
        {"widgetHeight", widget->height()},
    });
    result.extraContent.append(QJsonObject{
        {"type", "image"},
        {"data", QString::fromLatin1(png.toBase64())},
        {"mimeType", "image/png"},
    });
    reply(result);
}

void TestControlCanvasTools::pollUntil(std::function<bool()> condition,
                                       int timeoutMs,
                                       std::function<void(bool, qint64)> done,
                                       int stablePolls)
{
    QElapsedTimer clock;
    clock.start();
    int satisfiedPolls = condition() ? 1 : 0;
    if (satisfiedPolls >= stablePolls) {
        done(true, 0);
        return;
    }

    QTimer *timer = new QTimer(this);
    timer->setInterval(20);
    connect(timer,
            &QTimer::timeout,
            this,
            [timer, clock, condition, timeoutMs, done, stablePolls, satisfiedPolls]() mutable {
                satisfiedPolls = condition() ? satisfiedPolls + 1 : 0;
                const bool satisfied = satisfiedPolls >= stablePolls;
                if (!satisfied && clock.elapsed() < timeoutMs) {
                    return;
                }
                timer->stop();
                timer->deleteLater();
                done(satisfied, clock.elapsed());
            });
    timer->start();
}
