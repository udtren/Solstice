# SPDX-FileCopyrightText: 2026 Krita contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Report observed cross-thread residency hold overlap, not causal attribution."""
import bisect
import collections
import math


def summarize_residency(events):
    holders = collections.defaultdict(list)
    waits, invalid = [], 0
    for event in events:
        name = event["name"]
        if name != "tile_submit.lock" and not name.startswith("residency.hold."):
            continue
        start, duration = event.get("ts"), event.get("dur")
        owner = event.get("args", {}).get("owner")
        if (event.get("ph") != "X" or owner in (None, "0")
                or event.get("pid") is None or event.get("tid") is None
                or any(type(v) not in (int, float) or not math.isfinite(v) or v < 0
                       for v in (start, duration)) or not math.isfinite(start + duration)):
            invalid += 1
            continue
        row = (start, start + duration, event)
        if name == "tile_submit.lock":
            waits.append(row)
        else:
            holders[(event["pid"], owner)].append(row)
    ambiguous, ends = set(), {}
    for key, rows in holders.items():
        rows.sort(key=lambda row: row[0])
        # Serialized microseconds can differ by floating-point roundoff.
        if any(a[1] > b[0] + 1e-6 for a, b in zip(rows, rows[1:])):
            ambiguous.add(key)
        ends[key] = [row[1] for row in rows]
    overlap = collections.Counter()
    matched_waits = excluded = 0
    total_wait_us = covered_us = 0
    for start, end, event in waits:
        key = (event["pid"], event["args"]["owner"])
        if key in ambiguous:
            excluded += 1
            continue
        total_wait_us += end - start
        found = False
        rows = holders.get(key, [])
        index = bisect.bisect_right(ends.get(key, []), start)
        while index < len(rows) and rows[index][0] < end:
            hstart, hend, holder = rows[index]
            index += 1
            if holder["tid"] == event["tid"]:
                continue
            duration = min(end, hend) - max(start, hstart)
            overlap[holder["name"]] += duration
            covered_us += duration
            found = True
        matched_waits += found
    return {"waits": len(waits), "matched_waits": matched_waits,
            "invalid_events": invalid, "ambiguous_owner_groups": len(ambiguous),
            "excluded_waits": excluded, "wait_ms": total_wait_us / 1000,
            "observed_hold_overlap_ms": covered_us / 1000,
            "overlap_ms_by_holder": {key: value / 1000 for key, value in sorted(overlap.items())},
            "limitations": "Phase 4.78 owner is the residency backend; old wait owners cannot match. Native holds below 10 microseconds are omitted. Same process/owner and different thread only. Reject overlapping hold intervals beyond 1e-6 microsecond rounding tolerance. Overlap is observational, not proof of the blocking holder or scheduler cause. Sum is across waits, not elapsed wall time; concurrent waits can count a hold more than once. Recording and unlock gaps are not covered. Includes warm-ups unless events are explicitly filtered."}
