# SPDX-FileCopyrightText: 2026 Krita contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Timing of verified command descendants, not physical input-to-pixel latency."""
import collections
import math
import statistics


def summarize_timing(events, pipeline, strokes, geometry, readiness):
    received = collections.defaultdict(list)
    for event in events:
        if event["name"].startswith(("input.mouse_", "input.tablet_")):
            received[event.get("args", {}).get("id")].append(event.get("ts"))
    uploads = collections.defaultdict(list)
    for row in geometry.get("transfers", {}).get("uploads", []):
        uploads[row["upload"]].append(row)
    audit = {row["input"]: row for row in pipeline["input_audit"]}
    rows, excluded = [], collections.Counter()
    for ready in readiness["inputs"]:
        reasons = list(ready["exclusions"])
        identity = ready["input"]
        starts = received.get(identity, [])
        valid = lambda value: type(value) in (int, float) and math.isfinite(value) and value >= 0
        if len(starts) != 1 or not valid(starts[0]):
            reasons.append("missing_or_ambiguous_input_timestamp")
        ends = []
        for upload in audit[identity]["downstream_uploads"]:
            matches = uploads[upload]
            if (len(matches) != 1 or matches[0]["status"] != "covered_to_swapped_commands"
                    or not valid(matches[0].get("swapped_at_us"))):
                reasons.append("missing_or_ambiguous_swap_timestamp")
            else:
                ends.append(matches[0]["swapped_at_us"])
        if not ends:
            reasons.append("no_timed_uploads")
        if len(starts) == 1 and valid(starts[0]) and ends and min(ends) < starts[0]:
            reasons.append("swap_precedes_input")
        if reasons:
            excluded.update(set(reasons))
            continue
        rows.append({"input": identity, "stroke": ready["stroke"],
                     "received_at_us": starts[0], "last_required_swap_at_us": max(ends),
                     "elapsed_ms": (max(ends) - starts[0]) / 1000})
    summaries = []
    for record in strokes["records"]:
        values = sorted(row["elapsed_ms"] for row in rows if row["stroke"] == record["input"])
        summaries.append({"stroke": record["input"], "conditions": record["conditions"],
                          "samples": len(values),
                          "median_ms": statistics.median(values) if values else None,
                          "p95_ms": values[math.ceil(len(values) * .95) - 1] if values else None,
                          "max_ms": max(values) if values else None})
    return {"metric": "Qt input receipt to last required recorded command swap",
            "samples": len(rows), "excluded_inputs": len(readiness["inputs"]) - len(rows),
            "exclusion_counts": dict(excluded), "strokes": summaries, "inputs": rows,
            "limitations": "Only joined-check inputs with one recorded receipt timestamp are included. All descendant upload acknowledgments are required. Shared batches/frames correlate samples; percentiles are nearest-rank within each stroke, not independent trials. This is not physical scanout, pixel survival, full interpolation dependency or effective GPU-path proof. Do not pool processes or compare unmatched conditions."}
