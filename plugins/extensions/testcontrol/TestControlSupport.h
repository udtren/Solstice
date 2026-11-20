/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TESTCONTROLSUPPORT_H
#define TESTCONTROLSUPPORT_H

#include <QJsonObject>
#include <QJsonValue>
#include <QRect>
#include <QString>
#include <QStringList>

#include <functional>

#include <kis_types.h>

class KisMainWindow;
class KisViewManager;
class QObject;

/// Helpers shared by the test control tool classes.
namespace TestControlSupport
{
KisMainWindow *currentWindow();
KisViewManager *currentViewManager();

QJsonObject rectToJson(const QRect &rect);
QString sha256(const QByteArray &bytes);

/// Reads @p rect of @p device; returns false if the rect is too large to hash.
bool readDevice(KisPaintDeviceSP device, const QRect &rect, QByteArray *bytes);

/// Size, color model and profile of the current image, or null without one.
QJsonValue documentInfo(KisViewManager *viewManager);
/// Class, size and focus state of the current canvas widget, or null.
QJsonValue canvasInfo(KisViewManager *viewManager);

QJsonObject objectSchema(const QJsonObject &properties, const QStringList &required = QStringList());
QJsonObject schemaProperty(const QString &type, const QString &description);
QJsonObject enumProperty(const QStringList &values, const QString &description);

/**
 * Calls @p done(true) once @p condition holds, or done(false) after
 * @p timeoutMs. With @p stablePolls > 1 the condition must hold on that many
 * consecutive polls, 20 ms apart, so events queued in between are processed
 * too. The poll timer is a child of @p context.
 */
void pollUntil(QObject *context,
               std::function<bool()> condition,
               int timeoutMs,
               std::function<void(bool, qint64)> done,
               int stablePolls = 1);

/**
 * Takes @p image's barrier lock for reading and calls @p done(true, waitedMs);
 * the caller unlocks the image. Krita starts strokes of its own once the
 * image is idle (the idle tasks: layer thumbnails, Overview, Histogram), so
 * a read right after wait_idle can meet one. While the image is busy this
 * tries again every 20 ms for up to one second, then calls
 * @p done(false, waitedMs) without the lock. It never asks a stroke to end,
 * and events are processed while it waits: resolve the view again in @p done.
 */
void lockImageForRead(QObject *context, KisImageSP image, std::function<void(bool, qint64)> done);
} // namespace TestControlSupport

#endif // TESTCONTROLSUPPORT_H
