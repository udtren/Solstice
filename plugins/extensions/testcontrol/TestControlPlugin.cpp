/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "TestControlPlugin.h"

#include <QCoreApplication>

#include <KritaVersionWrapper.h>
#include <kis_debug.h>
#include <kpluginfactory.h>

#include "TestControlCanvasTools.h"
#include "TestControlHttpServer.h"
#include "TestControlMcpServer.h"

K_PLUGIN_FACTORY_WITH_JSON(TestControlPluginFactory, "kritatestcontrol.json", registerPlugin<TestControlPlugin>();)

namespace
{
constexpr int MinTokenLength = 16;
}

TestControlPlugin::TestControlPlugin(QObject *parent, const QVariantList &)
    : QObject(parent)
{
    // The constructor runs while the first main window is being built: it
    // only reads the environment and opens the socket. Tools resolve the
    // window, view and image when a request arrives.
    if (!qEnvironmentVariableIsSet("SOLSTICE_TEST_CONTROL_PORT")) {
        return;
    }

    bool ok = false;
    const int port = qEnvironmentVariableIntValue("SOLSTICE_TEST_CONTROL_PORT", &ok);
    if (!ok || port < 1024 || port > 65535) {
        warnPlugins << "Test control: SOLSTICE_TEST_CONTROL_PORT must be a port from 1024 to 65535; not started";
        return;
    }

    const QByteArray token = qgetenv("SOLSTICE_TEST_CONTROL_TOKEN");
    if (token.size() < MinTokenLength) {
        warnPlugins << "Test control: SOLSTICE_TEST_CONTROL_TOKEN must have at least" << MinTokenLength
                    << "characters; not started";
        return;
    }

    m_mcp = new TestControlMcpServer(QStringLiteral("solstice-test-control"),
                                     KritaVersionWrapper::solsticeVersionString(),
                                     this);
    m_mcp->setInstructions(TestControlCanvasTools::instructions());

    m_tools = new TestControlCanvasTools(this);
    m_tools->registerTools(m_mcp);

    TestControlMcpServer *mcp = m_mcp;
    m_http = new TestControlHttpServer(
        token,
        [mcp](const QByteArray &body, const TestControlHttpServer::Reply &reply) {
            mcp->handleMessage(body, reply);
        },
        this);

    if (!m_http->listen(quint16(port))) {
        warnPlugins << qPrintable(
            QStringLiteral("Test control: cannot listen on 127.0.0.1:%1: %2").arg(port).arg(m_http->errorString()));
        return;
    }

    connect(qApp, &QCoreApplication::aboutToQuit, m_http, &TestControlHttpServer::close);
    infoPlugins << qPrintable(QStringLiteral("Test control: MCP endpoint at http://127.0.0.1:%1/mcp").arg(port));
}

TestControlPlugin::~TestControlPlugin() = default;

#include "TestControlPlugin.moc"
