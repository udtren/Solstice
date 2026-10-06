/* SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef KIS_PAINT_TRACE_H
#define KIS_PAINT_TRACE_H

#include "kritaimage_export.h"
#include <QtGlobal>

class QJsonObject;
class QRect;

/** Opt-in CPU timeline. No GPU waits or writes on the measured path.
 * KRITA_PAINT_TRACE is an output filename prefix; each process adds its PID.
 * Names must be string literals. Identities are opaque, non-owning pointers.
 * Durations may overlap/nest and must not be summed as elapsed latency.
 */
namespace KisPaintTrace
{
KRITAIMAGE_EXPORT bool enabled();
KRITAIMAGE_EXPORT void instant(const char *name, const void *owner = nullptr, const void *related = nullptr);
/// Record a completed external host interval using the same steady_clock epoch.
/// Call after releasing measured locks; this function records no new interval.
KRITAIMAGE_EXPORT void
externalSpan(const char *name, const void *owner, const void *related, qint64 startNs, qint64 endNs, quint64 id);
/// Process-unique IDs; zero when disabled. Unlike pointer identities, never reused.
KRITAIMAGE_EXPORT quint64 nextId();
KRITAIMAGE_EXPORT void link(const char *name, const void *owner, quint64 id, quint64 parent = 0);
KRITAIMAGE_EXPORT void markIncomplete();
/// Bounded, opt-in snapshot of GUI brush conditions at an accepted stroke start.
KRITAIMAGE_EXPORT void strokeConditions(const void *canvas, quint64 input, const QJsonObject &conditions);
/// Rectangle coordinates are in the named stage's image space at the given LOD.
KRITAIMAGE_EXPORT void rectangle(const char *name, const void *owner, quint64 id, const QRect &rect, int lod);
KRITAIMAGE_EXPORT quint64 currentInput();
KRITAIMAGE_EXPORT quint64 currentJob();
KRITAIMAGE_EXPORT quint64 currentCause();
KRITAIMAGE_EXPORT quint64 currentFlow();

/// Explicit dirty/update provenance; never implicitly carried by timers/jobs.
class KRITAIMAGE_EXPORT FlowScope
{
public:
    explicit FlowScope(quint64 id);
    ~FlowScope();
    FlowScope(const FlowScope &) = delete;
    FlowScope &operator=(const FlowScope &) = delete;

private:
    bool m_active;
    quint64 m_previous = 0;
};

/// Carries an explicitly scheduled job identity onto its executing thread.
class KRITAIMAGE_EXPORT JobScope
{
public:
    explicit JobScope(quint64 id);
    ~JobScope();
    JobScope(const JobScope &) = delete;
    JobScope &operator=(const JobScope &) = delete;

private:
    quint64 m_id;
    quint64 m_previous = 0;
};

/// GUI dispatch scope. Reentrant events restore the enclosing input on return.
class KRITAIMAGE_EXPORT InputScope
{
public:
    InputScope(const char *name, const void *owner);
    ~InputScope();
    InputScope(const InputScope &) = delete;
    InputScope &operator=(const InputScope &) = delete;

private:
    bool m_active = false;
    quint64 m_id = 0;
    quint64 m_previous = 0;
};
/// Atomic snapshot export; also called on normal process teardown.
KRITAIMAGE_EXPORT bool flush();

class KRITAIMAGE_EXPORT Scope
{
public:
    explicit Scope(const char *name, const void *owner = nullptr, const void *related = nullptr, quint64 id = 0);
    ~Scope();
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    const char *m_name;
    const void *m_owner;
    const void *m_related;
    qint64 m_start = -1;
    quint64 m_job = 0;
    quint64 m_id = 0;
};
} // namespace KisPaintTrace
#endif
