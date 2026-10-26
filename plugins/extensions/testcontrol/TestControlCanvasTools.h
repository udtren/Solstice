/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TESTCONTROLCANVASTOOLS_H
#define TESTCONTROLCANVASTOOLS_H

#include <QObject>

#include <functional>

#include "TestControlMcpServer.h"

class TestControlInputInjector;

/**
 * The MCP tools that act on the running application: documents, tools,
 * synthetic strokes, idle waits and state/pixel queries.
 *
 * Every handler resolves the current main window, view and image when it
 * runs, never at registration, and fails with a message when there is none.
 * Handlers run on the GUI thread, one at a time (TestControlMcpServer).
 */
class TestControlCanvasTools : public QObject
{
    Q_OBJECT
public:
    explicit TestControlCanvasTools(QObject *parent = nullptr);
    ~TestControlCanvasTools() override;

    void registerTools(TestControlMcpServer *server);

    static QString instructions();

private:
    using Arguments = QJsonObject;
    using Reply = TestControlMcpServer::ToolReply;

    void sessionInfo(const Arguments &arguments, const Reply &reply);
    void documentNew(const Arguments &arguments, const Reply &reply);
    void toolActivate(const Arguments &arguments, const Reply &reply);
    void inputStroke(const Arguments &arguments, const Reply &reply);
    void waitIdle(const Arguments &arguments, const Reply &reply);
    void queryState(const Arguments &arguments, const Reply &reply);
    void actionTrigger(const Arguments &arguments, const Reply &reply);
    void canvasCapture(const Arguments &arguments, const Reply &reply);

    /// Calls @p done(true) once @p condition holds, or done(false) after @p timeoutMs.
    /// With @p stablePolls > 1 the condition must hold on that many consecutive
    /// polls, 20 ms apart, so events queued in between are processed too.
    void pollUntil(std::function<bool()> condition,
                   int timeoutMs,
                   std::function<void(bool, qint64)> done,
                   int stablePolls = 1);

    TestControlInputInjector *m_injector;
};

#endif // TESTCONTROLCANVASTOOLS_H
