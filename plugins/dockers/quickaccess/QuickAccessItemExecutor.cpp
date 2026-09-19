/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "QuickAccessItemExecutor.h"

#include <KisMainWindow.h>
#include <KisResourceServerProvider.h>
#include <KisViewManager.h>
#include <KoColor.h>
#include <KoCompositeOpRegistry.h>
#include <brushengine/kis_paintop_preset.h>
#include <kactioncollection.h>
#include <kis_action.h>
#include <kis_action_manager.h>
#include <kis_canvas2.h>
#include <kis_canvas_controller.h>
#include <kis_canvas_resource_provider.h>
#include <kis_paintop_box.h>
#include <klocalizedstring.h>

#include <QColor>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>

#ifdef HAVE_QUICKACCESS_PYTHON
#include <Python.h>
#endif

namespace QuickAccess
{

ItemExecutor::ItemExecutor(QObject *parent)
    : QObject(parent)
{
}

void ItemExecutor::setCanvas(KisCanvas2 *canvas)
{
    m_canvas = canvas;
}

bool ItemExecutor::execute(const Item &item, QString *error)
{
    if (!m_canvas || !m_canvas->viewManager()) {
        if (error)
            *error = i18n("No canvas is active.");
        return false;
    }
    switch (item.type) {
    case ItemType::Brush:
        return executeBrush(item, error);
    case ItemType::Action:
        return executeAction(item, error);
    case ItemType::DockerToggle:
        return executeDockerToggle(item, error);
    case ItemType::Color:
        return executeColor(item, error);
    case ItemType::BrushSize:
        return executeBrushSize(item, error);
    case ItemType::BrushBlendMode:
        return executeBrushBlendMode(item, error);
    case ItemType::Script:
        return executeScript(item, error);
    case ItemType::Label:
    case ItemType::Separator:
        return true;
    }
    return false;
}

bool ItemExecutor::executeScript(const Item &item, QString *error)
{
    const QString path = item.payload.value(QStringLiteral("script_path")).toString();
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = i18n("Python script '%1' could not be opened.", path);
        return false;
    }
#ifdef HAVE_QUICKACCESS_PYTHON
    if (!Py_IsInitialized()) {
        if (error)
            *error = i18n("Krita's Python engine is not initialized. Enable Python support and restart Krita.");
        return false;
    }
    const QByteArray source = file.readAll();
    const PyGILState_STATE gil = PyGILState_Ensure();
    PyObject *globals = PyDict_New();
    PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());
    PyObject *name = PyUnicode_FromString("__main__");
    PyObject *fileName = PyUnicode_FromString(QFile::encodeName(QFileInfo(path).absoluteFilePath()).constData());
    PyDict_SetItemString(globals, "__name__", name);
    PyDict_SetItemString(globals, "__file__", fileName);
    Py_DECREF(name);
    Py_DECREF(fileName);
    if (PyObject *kritaModule = PyImport_ImportModule("krita")) {
        if (PyObject *kritaClass = PyObject_GetAttrString(kritaModule, "Krita")) {
            PyDict_SetItemString(globals, "Krita", kritaClass);
            Py_DECREF(kritaClass);
        }
        Py_DECREF(kritaModule);
    }

    PyObject *result = PyRun_StringFlags(source.constData(), Py_file_input, globals, globals, nullptr);
    bool ok = result != nullptr;
    if (result) {
        Py_DECREF(result);
    } else {
        PyObject *type = nullptr;
        PyObject *value = nullptr;
        PyObject *traceback = nullptr;
        PyErr_Fetch(&type, &value, &traceback);
        PyErr_NormalizeException(&type, &value, &traceback);
        PyObject *message = value ? PyObject_Str(value) : nullptr;
        if (error)
            *error = i18n("Python script failed: %1",
                          message ? QString::fromUtf8(PyUnicode_AsUTF8(message)) : i18n("Unknown Python error"));
        Py_XDECREF(message);
        Py_XDECREF(type);
        Py_XDECREF(value);
        Py_XDECREF(traceback);
    }
    Py_DECREF(globals);
    PyGILState_Release(gil);
    return ok;
#else
    if (error)
        *error = i18n("This Krita build does not include Python support.");
    return false;
#endif
}

bool ItemExecutor::executeBrush(const Item &item, QString *error)
{
    const QString name = item.payload.value(QStringLiteral("brush_name")).toString();
    const auto resources =
        KisResourceServerProvider::instance()->paintOpPresetServer()->resourceModel()->resourcesForName(name);
    if (resources.isEmpty()) {
        if (error)
            *error = i18n("Brush preset '%1' could not be found.", name);
        return false;
    }
    m_canvas->viewManager()->paintOpBox()->resourceSelected(resources.constFirst());
    return true;
}

bool ItemExecutor::executeAction(const Item &item, QString *error)
{
    const QString id = item.payload.value(QStringLiteral("action_id")).toString();
    QAction *action = m_canvas->viewManager()->actionManager()->actionByName(id);
    if (!action && m_canvas->canvasController())
        action = m_canvas->canvasController()->actionCollection()->action(id);
    if (!action) {
        if (error)
            *error = i18n("Action '%1' could not be found.", id);
        return false;
    }
    if (!action->isEnabled()) {
        if (error)
            *error = i18n("Action '%1' is currently unavailable.", id);
        return false;
    }
    action->trigger();
    return true;
}

bool ItemExecutor::executeDockerToggle(const Item &item, QString *error)
{
    const QString id = item.payload.value(QStringLiteral("docker_id")).toString();
    KisMainWindow *mainWindow = m_canvas->viewManager()->mainWindow();
    QDockWidget *docker = mainWindow->findChild<QDockWidget *>(id);
    if (!docker) {
        const auto dockers = mainWindow->findChildren<QDockWidget *>();
        for (QDockWidget *candidate : dockers) {
            if (candidate->windowTitle().compare(id, Qt::CaseInsensitive) == 0) {
                docker = candidate;
                break;
            }
        }
    }
    if (!docker) {
        if (error)
            *error = i18n("Docker '%1' could not be found.", id);
        return false;
    }
    docker->toggleViewAction()->trigger();
    return true;
}

bool ItemExecutor::executeColor(const Item &item, QString *error)
{
    const QColor value(item.payload.value(QStringLiteral("color")).toString());
    if (!value.isValid()) {
        if (error)
            *error = i18n("The configured color is invalid.");
        return false;
    }
    KoColor color = m_canvas->resourceManager()->foregroundColor();
    color.fromQColor(value);
    m_canvas->resourceManager()->setForegroundColor(color);
    return true;
}

bool ItemExecutor::executeBrushSize(const Item &item, QString *error)
{
    bool valid = false;
    const qreal size = item.payload.value(QStringLiteral("text")).toString().toDouble(&valid);
    if (!valid || size <= 0.0) {
        if (error)
            *error = i18n("The configured brush size is invalid.");
        return false;
    }
    m_canvas->viewManager()->canvasResourceProvider()->setSize(size);
    return true;
}

bool ItemExecutor::executeBrushBlendMode(const Item &item, QString *error)
{
    const QString id = item.payload.value(QStringLiteral("text")).toString();
    if (KoCompositeOpRegistry::instance().getKoID(id).id().isEmpty()) {
        if (error)
            *error = i18n("Blending mode '%1' could not be found.", id);
        return false;
    }
    m_canvas->viewManager()->canvasResourceProvider()->setCurrentCompositeOp(id);
    return true;
}

} // namespace QuickAccess
