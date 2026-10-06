# SPDX-FileCopyrightText: 2026 Krita contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Partition verified wall intervals at the last required upload issue marker."""
import collections
import math
import statistics


def summarize_projection_preparation(events, pipeline, timing):
    """Join executed walker spans to their explicitly linked ready updates."""
    merges, updates = collections.defaultdict(list), collections.defaultdict(list)
    for event in events:
        args = event.get("args", {})
        if event["name"] == "projection.merge":
            merges[args.get("id")].append(event)
        elif event["name"] == "update.ready":
            updates[args.get("parent")].append(event)
    audit = {row["input"]: row for row in pipeline["input_audit"]}
    by_stroke = collections.defaultdict(set)
    for sample in timing["inputs"]:
        by_stroke[sample["stroke"]].update(audit[sample["input"]]["downstream_walkers"])
    required = set().union(*by_stroke.values()) if by_stroke else set()
    rows, exclusions = [], collections.Counter()
    for walker in sorted(required):
        if len(merges[walker]) != 1 or len(updates[walker]) != 1:
            exclusions["missing_or_ambiguous_merge_or_ready"] += 1
            continue
        merge, ready = merges[walker][0], updates[walker][0]
        start, duration, end = merge.get("ts"), merge.get("dur"), ready.get("ts")
        if (merge.get("ph") != "X" or
                any(type(value) not in (int, float) or not math.isfinite(value) or value < 0
                    for value in (start, duration, end))):
            exclusions["invalid_interval"] += 1
            continue
        if end < start + duration:
            exclusions["ready_before_merge_end"] += 1
            continue
        rows.append({"walker": walker, "update": ready["args"].get("id"),
                     "merge_cpu_ms": duration / 1000,
                     "merge_end_to_ready_ms": (end - start - duration) / 1000})
    strokes = []
    for stroke in timing["strokes"]:
        selected = [row for row in rows if row["walker"] in by_stroke[stroke["stroke"]]]
        entry = {"stroke": stroke["stroke"], "conditions": stroke["conditions"], "samples": len(selected)}
        for key in ("merge_cpu_ms", "merge_end_to_ready_ms"):
            values = sorted(row[key] for row in selected)
            entry[key] = {"median": statistics.median(values) if values else None,
                          "p95": values[math.ceil(len(values) * .95) - 1] if values else None}
        strokes.append(entry)
    return {"samples": len(rows), "exclusion_counts": dict(exclusions), "walkers": rows, "strokes": strokes,
            "limitations": "Explicit walker-to-update joins only; ambiguous multiple ready updates excluded. CPU merge spans can include GPU submission/waits but do not measure GPU execution. Merge-end to ready includes notification, canvas preparation and synchronization, not transfer-only time. Walkers are deduplicated globally and within each stroke, but may be shared across strokes. Warm-up exclusion is external. Not an additive per-input critical-path decomposition."}


def summarize_ready_to_issue(events, pipeline, timing):
    """Measure direct update-ready to issue intervals, once per verified upload."""
    records = collections.defaultdict(list)
    for event in events:
        if event["name"] in ("update.ready", "update.upload_issued"):
            records[(event["name"], event.get("args", {}).get("id"))].append(event)
    audit = {row["input"]: row for row in pipeline["input_audit"]}
    required = {identity for sample in timing["inputs"]
                for identity in audit[sample["input"]]["downstream_uploads"]}
    rows, exclusions = [], collections.Counter()
    for identity in sorted(required):
        issues = records[("update.upload_issued", identity)]
        if len(issues) != 1:
            exclusions["missing_or_ambiguous_issue"] += 1
            continue
        issue = issues[0]
        parent = issue.get("args", {}).get("parent")
        ready = records[("update.ready", parent)] if parent not in (None, "0") else []
        if len(ready) != 1:
            exclusions["missing_or_ambiguous_ready"] += 1
            continue
        start, end = ready[0].get("ts"), issue.get("ts")
        if any(type(value) not in (int, float) or not math.isfinite(value) or value < 0
               for value in (start, end)) or end < start:
            exclusions["invalid_interval"] += 1
            continue
        rows.append({"upload": identity, "update": parent, "ready_to_issue_ms": (end - start) / 1000})
    values = sorted(row["ready_to_issue_ms"] for row in rows)
    return {"samples": len(rows), "exclusion_counts": dict(exclusions), "uploads": rows,
            "median_ms": statistics.median(values) if values else None,
            "p95_ms": values[math.ceil(len(values) * .95) - 1] if values else None,
            "limitations": "Unique uploads linked to verified inputs, including warm-ups unless callers filter them. Direct parent update only: excludes earlier merged/superseded update residence. Interval includes compressor/event-loop waiting and upload processing, not pure queue or GPU time. Update-ready can precede later merged inputs; this is not an input-latency partition. Do not sum intervals across overlapping updates."}


def summarize_overhead(events, pipeline, timing):
    issued = collections.defaultdict(list)
    for event in events:
        if event["name"] == "update.upload_issued":
            issued[event.get("args", {}).get("id")].append(event.get("ts"))
    audit = {row["input"]: row for row in pipeline["input_audit"]}
    rows, exclusions = [], collections.Counter()
    for sample in timing["inputs"]:
        ids = audit[sample["input"]]["downstream_uploads"]
        stamps = []
        for identity in ids:
            matches = issued.get(identity, [])
            if len(matches) != 1:
                break
            value = matches[0]
            if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
                break
            stamps.append(value)
        if not ids or len(stamps) != len(ids):
            exclusions["missing_or_ambiguous_upload_issue"] += 1
            continue
        start, end = sample["received_at_us"], sample["last_required_swap_at_us"]
        if min(stamps) < start or max(stamps) > end:
            exclusions["upload_outside_verified_interval"] += 1
            continue
        boundary = max(stamps)
        rows.append({"input": sample["input"], "stroke": sample["stroke"],
                     "last_upload_issued_at_us": boundary,
                     "before_last_upload_ms": (boundary - start) / 1000,
                     "after_last_upload_ms": (end - boundary) / 1000})
    strokes = []
    for stroke in timing["strokes"]:
        selected = [row for row in rows if row["stroke"] == stroke["stroke"]]
        entry = {"stroke": stroke["stroke"], "conditions": stroke["conditions"], "samples": len(selected)}
        for key in ("before_last_upload_ms", "after_last_upload_ms"):
            values = sorted(row[key] for row in selected)
            entry[key] = {"median": statistics.median(values) if values else None,
                          "p95": values[math.ceil(len(values) * .95) - 1] if values else None}
        strokes.append(entry)
    return {"samples": len(rows), "excluded_timed_inputs": sum(exclusions.values()),
            "exclusion_counts": dict(exclusions), "inputs": rows, "strokes": strokes,
            "limitations": "Only previously verified timing samples; last required upload issue partitions each input's wall interval exactly. Components' medians/percentiles need not add to total statistics. Before includes input/job scheduling, drawing, preparation, uploads and waits; after includes remaining painting/presentation/Qt acknowledgment. Neither is GPU execution time, transfer-only cost or a critical-path attribution. Shared batches correlate inputs. Unrelated uploads are ignored; warm-up exclusion is external and explicit."}
