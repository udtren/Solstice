/* SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "KisPaintTrace.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRect>
#include <QSaveFile>
#include <QThread>
#include <QVector>
#include <atomic>
#include <chrono>

namespace
{
constexpr int eventLimit = 262144;
constexpr int strokeLimit = 1024;
thread_local quint64 inputId = 0;
thread_local quint64 jobId = 0;
thread_local quint64 flowId = 0;
struct Event {
    const char *name;
    quintptr owner;
    quintptr related;
    quintptr thread;
    qint64 start;
    qint64 duration;
    quint64 id;
    quint64 parent;
    quint64 job;
    QRect rect;
    int lod;
};

qint64 now()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

struct Recorder {
    Recorder()
        : origin(now())
        , pid(QCoreApplication::applicationPid())
        , filename(QFileInfo(QString::fromLocal8Bit(qgetenv("KRITA_PAINT_TRACE"))).absoluteFilePath()
                   + QStringLiteral(".%1.json").arg(pid))
        , projection(QString::fromLocal8Bit(qgetenv("KRITA_GPU_PROJECTION")))
        , brush(QString::fromLocal8Bit(qgetenv("KRITA_GPU_BRUSH")))
    {
        events.reserve(eventLimit);
    }
    ~Recorder()
    {
        if (!write())
            qWarning() << "Paint trace could not be saved to" << filename;
    }
    void append(const char *name,
                const void *owner,
                const void *related,
                qint64 start,
                qint64 duration,
                quint64 id = 0,
                quint64 parent = 0,
                quint64 job = 0,
                const QRect &rect = {},
                int lod = -1)
    {
        const Event event{name,
                          quintptr(owner),
                          quintptr(related),
                          quintptr(QThread::currentThreadId()),
                          start,
                          duration,
                          id,
                          parent,
                          job,
                          rect,
                          lod};
        QMutexLocker lock(&mutex);
        if (events.size() == eventLimit) {
            ++dropped;
            return;
        }
        events.append(event);
    }
    bool write()
    {
        // Serialize snapshots, but never hold the recording lock during I/O.
        QMutexLocker exportLock(&exportMutex);
        QVector<Event> snapshot;
        QJsonArray strokeSnapshot;
        quint64 lost;
        {
            QMutexLocker lock(&mutex);
            snapshot = events;
            strokeSnapshot = strokes;
            lost = dropped;
        }
        QJsonArray array;
        for (const Event &event : snapshot) {
            QJsonObject row{{"name", QLatin1String(event.name)},
                            {"cat", "paint"},
                            {"ph", event.duration < 0 ? "i" : "X"},
                            {"ts", double(event.start - origin) / 1000.0},
                            {"pid", double(pid)},
                            {"tid", double(event.thread)},
                            {"args",
                             QJsonObject{{"owner", QString::number(event.owner, 16)},
                                         {"related", QString::number(event.related, 16)}}}};
            if (event.duration < 0)
                row.insert("s", "t");
            else
                row.insert("dur", double(event.duration) / 1000.0);
            if (event.id) {
                auto args = row["args"].toObject();
                args.insert("id", QString::number(event.id));
                args.insert("parent", QString::number(event.parent));
                row.insert("args", args);
            }
            if (event.job) {
                auto args = row["args"].toObject();
                args.insert("job", QString::number(event.job));
                row.insert("args", args);
            }
            if (event.lod >= 0) {
                auto args = row["args"].toObject();
                args.insert("rect",
                            QJsonArray{event.rect.x(), event.rect.y(), event.rect.width(), event.rect.height()});
                args.insert("lod", event.lod);
                row.insert("args", args);
            }
            array.append(row);
        }
        const QJsonObject root{{"traceEvents", array},
                               {"displayTimeUnit", "ms"},
                               {"metadata",
                                QJsonObject{{"schema", 2},
                                            {"clock", "steady_clock"},
                                            {"time_unit", "microseconds"},
                                            {"event_limit", eventLimit},
                                            {"dropped_events", double(lost)},
                                            {"projection_env", projection},
                                            {"brush_env", brush},
                                            {"stroke_conditions", strokeSnapshot},
                                            {"presentation", "Qt frameSwapped; not physical scanout"}}}};
        QSaveFile file(filename);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
        return file.write(bytes) == bytes.size() && file.commit();
    }
    const qint64 origin;
    const qint64 pid;
    const QString filename;
    const QString projection;
    const QString brush;
    QMutex mutex;
    QMutex exportMutex;
    QVector<Event> events;
    QJsonArray strokes;
    quint64 dropped = 0;
};

Recorder &recorder()
{
    static Recorder result;
    return result;
}
} // namespace

bool KisPaintTrace::enabled()
{
    static const bool result = !qgetenv("KRITA_PAINT_TRACE").isEmpty();
    return result;
}

void KisPaintTrace::instant(const char *name, const void *owner, const void *related)
{
    if (!enabled())
        return;
    Recorder &r = recorder();
    r.append(name, owner, related, now(), -1, 0, 0, jobId);
}

quint64 KisPaintTrace::nextId()
{
    if (!enabled())
        return 0;
    static std::atomic<quint64> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

void KisPaintTrace::link(const char *name, const void *owner, quint64 id, quint64 parent)
{
    if (!enabled() || !id)
        return;
    Recorder &r = recorder();
    r.append(name, owner, nullptr, now(), -1, id, parent);
}

void KisPaintTrace::markIncomplete()
{
    if (!enabled())
        return;
    Recorder &r = recorder();
    QMutexLocker lock(&r.mutex);
    ++r.dropped;
}

void KisPaintTrace::rectangle(const char *name, const void *owner, quint64 id, const QRect &rect, int lod)
{
    if (!enabled() || !id)
        return;
    if (lod < 0) {
        markIncomplete();
        return;
    }
    Recorder &r = recorder();
    r.append(name, owner, nullptr, now(), -1, id, 0, jobId, rect, lod);
}

void KisPaintTrace::strokeConditions(const void *canvas, quint64 input, const QJsonObject &conditions)
{
    if (!enabled() || !input)
        return;
    Recorder &r = recorder();
    QMutexLocker lock(&r.mutex);
    if (r.strokes.size() == strokeLimit) {
        ++r.dropped;
        return;
    }
    r.strokes.append(QJsonObject{{"input", QString::number(input)},
                                 {"canvas", QString::number(quintptr(canvas), 16)},
                                 {"conditions", conditions}});
}

quint64 KisPaintTrace::currentInput()
{
    return enabled() ? inputId : 0;
}

quint64 KisPaintTrace::currentJob()
{
    return enabled() ? jobId : 0;
}

quint64 KisPaintTrace::currentCause()
{
    return enabled() ? (jobId ? jobId : inputId) : 0;
}

quint64 KisPaintTrace::currentFlow()
{
    return enabled() ? flowId : 0;
}

KisPaintTrace::FlowScope::FlowScope(quint64 id)
    : m_active(enabled())
{
    if (m_active) {
        m_previous = flowId;
        flowId = id;
    }
}

KisPaintTrace::FlowScope::~FlowScope()
{
    if (m_active)
        flowId = m_previous;
}

KisPaintTrace::JobScope::JobScope(quint64 id)
    : m_id(enabled() ? id : 0)
{
    if (m_id) {
        m_previous = jobId;
        jobId = m_id;
        link("job.started", nullptr, m_id);
    }
}

KisPaintTrace::JobScope::~JobScope()
{
    if (m_id) {
        link("job.finished", nullptr, m_id);
        jobId = m_previous;
    }
}

KisPaintTrace::InputScope::InputScope(const char *name, const void *owner)
{
    if (!enabled())
        return;
    m_active = true;
    m_previous = inputId;
    m_id = name ? nextId() : 0;
    inputId = m_id;
    link(name, owner, m_id);
}

KisPaintTrace::InputScope::~InputScope()
{
    if (m_active)
        inputId = m_previous;
}

bool KisPaintTrace::flush()
{
    return !enabled() || recorder().write();
}

KisPaintTrace::Scope::Scope(const char *name, const void *owner, const void *related, quint64 id)
    : m_name(name)
    , m_owner(owner)
    , m_related(related)
{
    if (enabled()) {
        recorder();
        m_start = now();
        m_job = jobId;
        m_id = id;
    }
}

KisPaintTrace::Scope::~Scope()
{
    if (m_start >= 0)
        recorder().append(m_name, m_owner, m_related, m_start, now() - m_start, m_id, 0, m_job);
}
