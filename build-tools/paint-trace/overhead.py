# SPDX-FileCopyrightText: 2026 Krita contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Partition verified wall intervals at the last required upload issue marker."""
import collections
import math
import statistics


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
