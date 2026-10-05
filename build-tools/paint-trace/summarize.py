#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Krita contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Summarize buffered CPU trace spans. Does not infer input-to-pixel latency."""
import argparse
import collections
import json
import math
import pathlib
import statistics
from geometry import summarize_geometry
from timing import summarize_timing
from paths import summarize_paths
from overhead import summarize_overhead


def lineage_counts(events):
    """Count explicit links only. Never associate a nearest input or swap."""
    inputs, accepted, uploads = set(), set(), set()
    submitted, swapped, covered, replaced = set(), set(), {}, {}
    for event in events:
        name = event["name"]
        args = event.get("args", {})
        identity = args.get("id")
        if not identity:
            continue
        key = (args.get("owner"), identity)
        parent = (args.get("owner"), args.get("parent"))
        if name.startswith(("input.mouse_", "input.tablet_")):
            inputs.add(identity)
        elif name.startswith("input.accepted_"):
            accepted.add(identity)
        elif name == "update.upload_issued":
            uploads.add(key)
        elif name == "frame.submitted":
            submitted.add(key)
        elif name == "frame.swapped":
            swapped.add(key)
        elif name == "frame.covered_upload":
            covered[key] = parent
        elif name == "frame.replaced":
            replaced[key] = parent

    def reached_swap(frame):
        visited = set()
        while frame in submitted:
            if frame in visited:
                raise ValueError("Cyclic frame lineage")
            visited.add(frame)
            if frame in swapped:
                return True
            if frame not in replaced:
                break
            frame = replaced[frame]
        return False

    linked = uploads & covered.keys()
    return {
        "received_pointer_inputs": len(inputs),
        "inputs_linked_to_freehand_dispatch": len(inputs & accepted),
        "upload_occurrences": len(uploads),
        "uploads_with_render_and_blit_coverage": len(linked),
        "covered_uploads_in_swapped_frame_chain": sum(reached_swap(covered[key]) for key in linked),
        "limitations": "Command coverage only; no guarantee of pixel survival or input-to-pixel causality.",
    }


def summarize(document):
    metadata = document["metadata"]
    if metadata.get("schema") not in (1, 2) or metadata.get("time_unit") != "microseconds":
        raise ValueError("Unsupported trace schema or time unit")
    if metadata.get("dropped_events", 0):
        raise ValueError("Trace overflowed or tracking was incomplete; repeat a shorter capture")
    events = document["traceEvents"]
    counts = collections.Counter()
    durations = collections.defaultdict(list)
    for event in events:
        counts[event["name"]] += 1
        if event["ph"] == "X":
            duration = event["dur"] / 1000.0
            if not math.isfinite(duration) or duration < 0:
                raise ValueError("Invalid duration")
            durations[event["name"]].append(duration)
    stages = {}
    for name in sorted(counts):
        row = {"count": counts[name]}
        if name in durations:
            values = sorted(durations[name])
            row.update(cpu_span_median_ms=statistics.median(values),
                       cpu_span_p95_ms=values[math.ceil(len(values) * .95) - 1])
        stages[name] = row
    geometry = summarize_geometry(events)
    pipeline = pipeline_summary(events, geometry)
    strokes = stroke_summary(events, metadata)
    readiness = sample_readiness(pipeline, strokes)
    timing = summarize_timing(events, pipeline, strokes, geometry, readiness)
    return {
        "metadata": metadata,
        "stages": stages,
        "lineage": lineage_counts(events) if metadata["schema"] == 2 else None,
        "jobs": job_summary(events),
        "batches": batch_summary(events),
        "pipeline": pipeline,
        "stroke_conditions": strokes,
        "geometry": geometry,
        "sample_readiness": readiness,
        "command_presentation_timing": timing,
        "upload_boundary_timing": summarize_overhead(events, pipeline, timing),
        "execution_path_evidence": summarize_paths(events, pipeline),
        "limitations": [
            "Stage durations and job waits are process-wide and can include background preview work.",
            "CPU spans can overlap/nest; do not sum them as wall-clock latency.",
            "GPU execution is asynchronous and is not timed separately here.",
            "frameSwapped is a Qt boundary, not physical display scanout.",
            "No event-to-pixel causality is established; no end-to-end latency is reported.",
            "Environment flags are requests, not evidence of successful GPU execution.",
        ],
    }


def job_summary(events):
    """Scheduling ancestry, not proof of which inputs a batched job paints."""
    created, started, parents = {}, {}, {}
    destroyed, accepted = set(), set()
    tagged_spans = 0
    for event in events:
        name, args = event["name"], event.get("args", {})
        identity = args.get("id")
        if event["ph"] == "X" and args.get("job"):
            tagged_spans += 1
        if not identity:
            continue
        if name.startswith("input.accepted_"):
            accepted.add(identity)
        if name == "job.created":
            if identity in created:
                raise ValueError("Duplicate job identity")
            created[identity] = event["ts"]
            parents[identity] = args.get("parent")
        elif name == "job.started":
            if identity in started:
                raise ValueError("Job executed more than once")
            started[identity] = event["ts"]
        elif name == "job.destroyed":
            destroyed.add(identity)

    def has_input_ancestor(identity):
        visited = set()
        while identity in parents:
            if identity in visited:
                raise ValueError("Cyclic job lineage")
            visited.add(identity)
            identity = parents[identity]
        return identity in accepted

    waits = sorted((started[key] - created[key]) / 1000.0 for key in started.keys() & created.keys())
    if any(not math.isfinite(value) or value < 0 for value in waits):
        raise ValueError("Invalid job scheduling timestamps")
    result = {
        "created": len(created), "started": len(started),
        "destroyed_without_execution": len((destroyed & created.keys()) - started.keys()),
        "created_with_freehand_input_ancestor": sum(has_input_ancestor(key) for key in created),
        "cpu_spans_with_job_identity": tagged_spans,
        "limitations": "Creation-to-start includes queue insertion/scheduling. Ancestry identifies the creating context, not all inputs consumed by a batch. No end-to-end latency.",
    }
    if waits:
        result.update(creation_to_start_samples=len(waits),
                      creation_to_start_median_ms=statistics.median(waits),
                      creation_to_start_p95_ms=waits[math.ceil(len(waits) * .95) - 1])
    return result


def batch_summary(events):
    jobs, requests, memberships = {}, {}, {}
    accepted, ready, dirty, generated, paint_jobs = set(), set(), set(), set(), set()
    kinds = collections.Counter()
    for event in events:
        name, args = event["name"], event.get("args", {})
        identity, parent = args.get("id"), args.get("parent")
        if not identity:
            continue
        if name == "job.created":
            jobs[identity] = parent
        elif name.startswith("input.accepted_"):
            accepted.add(identity)
        elif name in ("dab.request", "dab.cache_request", "dab.postprocess_request"):
            if identity in requests:
                raise ValueError("Duplicate dab request identity")
            requests[identity] = parent
            kinds[name] += 1
        elif name == "dab.in_batch":
            if identity in memberships:
                raise ValueError("Dab consumed by more than one batch record")
            memberships[identity] = parent
        elif name == "dab.generate_and_postprocess":
            generated.add(identity)
        elif name == "batch.ready":
            ready.add(identity)
        elif name == "batch.dirty_recorded":
            dirty.add(identity)
        elif name == "batch.paint_job":
            paint_jobs.add((identity, parent))

    def source_input(identity):
        visited = set()
        while identity in jobs:
            if identity in visited:
                raise ValueError("Cyclic job lineage")
            visited.add(identity)
            identity = jobs[identity]
        return identity if identity in accepted else None

    inputs = collections.defaultdict(set)
    for dab, batch in memberships.items():
        source = source_input(requests.get(dab))
        if source:
            inputs[batch].add(source)
    return {
        "requests": len(requests), "request_kinds": dict(kinds),
        "requests_with_generation_span": len(requests.keys() & generated),
        "requests_in_batches": len(requests.keys() & memberships.keys()),
        "ready_batches": len(ready),
        "batches_with_dirty_recorded": len(ready & dirty),
        "batches_with_input_sources": len(ready & inputs.keys()),
        "batches_with_multiple_input_sources": sum(len(inputs[key]) > 1 for key in ready),
        "paint_job_batch_links": len(paint_jobs),
        "limitations": "Request creation sources and batch membership only. A cache hit remains a separate request. Dirty recording is not projection or presentation completion.",
    }


def pipeline_summary(events, geometry=None):
    """Explicit command descendants, never pixel visibility or nearest timestamps."""
    groups = collections.defaultdict(set)
    records = []
    owners = {}
    for event in events:
        name, args = event["name"], event.get("args", {})
        identity = args.get("id")
        if not identity or identity == "0":
            continue
        groups[name].add(identity)
        records.append((name, identity, args.get("parent"), args.get("owner")))
        if name in ("update.ready", "update.upload_issued", "frame.submitted"):
            owners[(name, identity)] = args.get("owner")

    jobs = groups["job.created"]
    inputs = set().union(*(ids for name, ids in groups.items() if name.startswith("input.accepted_")))
    dabs = groups["dab.request"] | groups["dab.cache_request"] | groups["dab.postprocess_request"]
    batches, dirty, requests = groups["batch.ready"], groups["dirty.dispatch"], groups["projection.request"]
    walkers = groups["projection.merge"] | groups["projection.walker_merged"]
    walkers |= {parent for name, _, parent, _ in records if name in ("projection.walker_request", "projection.walker_merged")}
    updates, uploads, frames = groups["update.ready"], groups["update.upload_issued"], groups["frame.submitted"]
    causes, request_sources = jobs | inputs, dirty | requests
    reverse = collections.defaultdict(set)
    forward = collections.defaultdict(set)
    swaps = set()

    def connect(source, target):
        if source and target and source != "0" and target != "0":
            reverse[target].add(source)
            forward[source].add(target)

    for name, identity, parent, owner in records:
        if name == "job.created" and parent in causes:
            connect(parent, identity)
        elif name in ("dab.request", "dab.cache_request", "dab.postprocess_request") and parent in causes:
            connect(parent, identity)
        elif name == "dab.in_batch" and identity in dabs and parent in batches:
            connect(identity, parent)
        elif name == "batch.to_dirty" and identity in batches and parent in dirty:
            connect(identity, parent)
        elif name == "projection.request" and parent in request_sources:
            connect(parent, identity)
        elif name == "projection.walker_request" and identity in requests and parent in walkers:
            connect(identity, parent)
        elif name == "projection.walker_merged" and parent in walkers:
            connect(identity, parent)
        elif name == "update.ready" and parent in groups["projection.merge"]:
            connect(parent, identity)
        elif name in ("update.merged", "update.superseded") and identity in updates and parent in updates:
            update_owner = owners.get(("update.ready", identity))
            if update_owner not in (None, "0") and update_owner == owners.get(("update.ready", parent)):
                connect(identity, parent)
        elif name == "update.upload_issued" and parent in updates:
            connect(parent, identity)
        elif name == "frame.covered_upload" and identity in uploads and parent in frames:
            if owner == owners.get(("update.upload_issued", identity)) == owners.get(("frame.submitted", parent)):
                connect(identity, parent)
        elif name == "frame.replaced" and identity in frames and parent in frames:
            if owner == owners.get(("frame.submitted", identity)) == owners.get(("frame.submitted", parent)):
                connect(identity, parent)
        elif name == "frame.swapped" and identity in frames and owner == owners.get(("frame.submitted", identity)):
            swaps.add(identity)

    def reachable(seeds, edges):
        pending, seen = list(seeds), set()
        while pending:
            current = pending.pop()
            if current in seen:
                continue
            seen.add(current)
            pending.extend(edges.get(current, ()))
        return seen

    def ancestors(seeds):
        return reachable(seeds, reverse)

    presented = ancestors(swaps)
    input_batches = batches & reachable(inputs, forward)
    input_presented = input_batches & presented
    branch_nodes = batches | dirty | requests | walkers | updates | uploads | frames
    branch_edges = {source: targets & branch_nodes for source, targets in forward.items()
                    if source in branch_nodes}
    complete_branches = all_recorded_branches_swapped(branch_nodes, branch_edges, swaps)
    relevant = reachable(input_batches, branch_edges)
    terminal_groups = {
        "batch_without_dirty_link": batches,
        "dirty_without_projection_request": dirty,
        "projection_request_without_walker_or_child": requests,
        "walker_without_canvas_update_or_merge_target": walkers,
        "canvas_update_without_upload_or_replacement": updates,
        "upload_without_frame_coverage": uploads,
        "frame_without_swap_or_replacement": frames,
    }
    unresolved_terminals = relevant - complete_branches
    unresolved_terminals = {node for node in unresolved_terminals if not branch_edges.get(node)}
    # Stop at batches: walking through merged projection work would attribute
    # other inputs' requests to this input. Presentation is checked separately.
    request_nodes = jobs | dabs | batches
    dab_sources = jobs | inputs | dabs
    request_edges = {source: targets & request_nodes for source, targets in forward.items()
                     if source in dab_sources}
    input_rows = []
    request_geometry = {row["request"]: row["status"] for row in (geometry or {}).get("requests", [])}
    walker_geometry = collections.defaultdict(list)
    for row in (geometry or {}).get("canvas_notifications", []):
        walker_geometry[row["walker"]].append(row["status"])
    upload_geometry = collections.defaultdict(list)
    for row in (geometry or {}).get("transfers", {}).get("uploads", []):
        upload_geometry[row["upload"]].append(row["status"])
    dirty_geometry = {row["dirty"]: row for row in (geometry or {}).get("bridges", {}).get("dirty_groups", [])}
    compressed_geometry = {row["update"]: row["status"] for row in
                           (geometry or {}).get("bridges", {}).get("compressed_updates", [])}
    input_kinds = {identity: name.removeprefix("input.accepted_")
                   for name, ids in groups.items() if name.startswith("input.accepted_")
                   for identity in ids}
    for identity in sorted(inputs):
        descendants = reachable([identity], request_edges)
        input_dabs = dabs & descendants
        linked_batches = batches & descendants
        unbatched = sum(not (forward.get(dab, set()) & batches) for dab in input_dabs)
        unpresented = len(linked_batches - presented)
        downstream = reachable(linked_batches, branch_edges)
        downstream_requests = requests & downstream
        downstream_walkers = groups["projection.merge"] & downstream
        downstream_uploads = uploads & downstream
        failures = []
        downstream_dirty = dirty & downstream
        if not downstream_dirty or any(
                dirty_geometry.get(node, {}).get("source_to_submission") != "covered"
                or dirty_geometry.get(node, {}).get("submission_to_projection") != "covered" for node in downstream_dirty):
            failures.append("dirty_group_geometry")
        if not downstream_requests or any(request_geometry.get(node) != "covered" for node in downstream_requests):
            failures.append("projection_request_geometry")
        if not downstream_walkers or any(not walker_geometry[node] or any(status != "covered" for status in walker_geometry[node])
                                         for node in downstream_walkers):
            failures.append("canvas_notification_geometry")
        if not downstream_uploads or any(upload_geometry[node] != ["covered_to_swapped_commands"] for node in downstream_uploads):
            failures.append("upload_display_geometry")
        downstream_updates = updates & downstream
        if not downstream_updates or any(compressed_geometry.get(node) != "covered" for node in downstream_updates):
            failures.append("compressed_update_geometry")
        input_rows.append({
            "input": identity, "kind": input_kinds[identity],
            "dab_requests": len(input_dabs), "unbatched_dab_requests": unbatched,
            "batches": len(linked_batches), "batches_without_swapped_command_descendant": unpresented,
            "all_requested_dabs_have_swapped_batch_command": bool(input_dabs) and not unbatched and not unpresented,
            "all_requested_dabs_have_all_recorded_branches_swapped": bool(input_dabs) and not unbatched
                and bool(linked_batches) and linked_batches <= complete_branches,
            "unverified_geometry_stages": failures,
            "downstream_uploads": sorted(downstream_uploads),
            "linked_batches": sorted(linked_batches),
            "downstream_walkers": sorted(downstream_walkers),
        })
    return {
        "input_linked_batches": len(input_batches),
        "batches_without_input_link": len(batches - input_batches),
        "input_linked_batches_with_projection_request_descendant": len(input_batches & ancestors(requests)),
        "input_linked_batches_with_executed_projection_descendant": len(input_batches & ancestors(groups["projection.merge"])),
        "input_linked_batches_with_canvas_update_descendant": len(input_batches & ancestors(updates)),
        "input_linked_batches_with_some_swapped_command_descendant": len(input_presented),
        "input_linked_batches_without_swapped_command_descendant": len(input_batches - input_presented),
        "batches_with_projection_request_descendant": len(batches & ancestors(requests)),
        "batches_with_executed_projection_descendant": len(batches & ancestors(groups["projection.merge"])),
        "batches_with_canvas_update_descendant": len(batches & ancestors(updates)),
        "batches_with_some_swapped_command_descendant": len(batches & presented),
        "inputs_with_some_swapped_command_descendant": len(inputs & presented),
        "inputs_without_dab_requests_by_kind": dict(collections.Counter(
            row["kind"] for row in input_rows if not row["dab_requests"])),
        "inputs_with_unbatched_dab_requests": sum(row["unbatched_dab_requests"] > 0 for row in input_rows),
        "inputs_with_all_requested_dabs_in_swapped_batch_commands": sum(
            row["all_requested_dabs_have_swapped_batch_command"] for row in input_rows),
        "input_audit": input_rows,
        "recorded_branch_audit": {
            "input_linked_batches_with_all_recorded_branches_swapped": len(input_batches & complete_branches),
            "input_linked_batches_with_unresolved_recorded_branches": len(input_batches - complete_branches),
            "inputs_with_all_requested_dabs_and_recorded_branches_swapped": sum(
                row["all_requested_dabs_have_all_recorded_branches_swapped"] for row in input_rows),
            "unresolved_terminal_counts": {name: len(nodes & unresolved_terminals)
                                           for name, nodes in terminal_groups.items()},
            "unresolved_terminal_ids_sample": sorted(unresolved_terminals)[:16],
            "limitations": "Every recorded branch must reach a swap. Missing instrumentation, offscreen regions, suppressed updates and resets remain unresolved, not proof of lost pixels. Shared downstream work can conservatively block several inputs. This branch audit does not evaluate rectangle coordinates; see the separate geometry report. This is not full-region or input-to-pixel validation.",
        },
        "limitations": "At least one command descendant, not coverage of all split regions or proof of input pixel survival. Batches without input links may be background work or uninstrumented input paths; they are not automatically lost painting updates. Suppressed/deferred paths without explicit links remain unattributed. No end-to-end latency.",
    }


def all_recorded_branches_swapped(nodes, edges, swaps):
    """Linear-time DAG audit. A successful sibling never hides a pending one."""
    parents = collections.defaultdict(set)
    remaining = {}
    for node in nodes:
        children = edges.get(node, set())
        remaining[node] = len(children)
        for child in children:
            parents[child].add(node)
    pending = [node for node, count in remaining.items() if count == 0]
    complete, processed = set(), 0
    while pending:
        node = pending.pop()
        processed += 1
        children = edges.get(node, set())
        if node in swaps or (children and children <= complete):
            complete.add(node)
        for parent in parents[node]:
            remaining[parent] -= 1
            if not remaining[parent]:
                pending.append(parent)
    if processed != len(nodes):
        raise ValueError("Cyclic projection/presentation lineage")
    return complete


def stroke_summary(events, metadata):
    """GUI snapshots, never inferred from event order or a requested run label."""
    beginnings = {event.get("args", {}).get("id"): event.get("args", {}).get("owner")
                  for event in events if event["name"] == "input.accepted_begin"}
    records = metadata.get("stroke_conditions", [])
    seen = set()
    for row in records:
        identity = row["input"]
        if identity in seen or identity not in beginnings or row["canvas"] != beginnings[identity]:
            raise ValueError("Invalid stroke condition input/canvas association")
        seen.add(identity)
    accepted = {event.get("args", {}).get("id"): event.get("args", {}).get("owner")
                for event in events if event["name"].startswith("input.accepted_")}
    membership = {identity: identity for identity in beginnings}
    ended = set()
    for event in events:
        name, args = event["name"], event.get("args", {})
        identity, parent, canvas = args.get("id"), args.get("parent"), args.get("owner")
        if name == "stroke.input" and parent and parent != "0":
            if (identity not in accepted or parent not in beginnings or canvas in (None, "0")
                    or canvas != accepted[identity] or canvas != beginnings[parent]):
                raise ValueError("Invalid stroke membership input/canvas association")
            if identity in membership and membership[identity] != parent:
                raise ValueError("Input belongs to multiple strokes")
            membership[identity] = parent
        elif name == "stroke.ended":
            if identity not in beginnings or canvas != beginnings[identity]:
                raise ValueError("Invalid stroke end association")
            ended.add(identity)
    return {
        "recorded": len(records), "beginnings_without_conditions": len(beginnings.keys() - seen),
        "records": records,
        "input_membership": membership,
        "ended_strokes": sorted(ended),
        "accepted_inputs_without_stroke_membership": len(accepted.keys() - membership.keys()),
        "limitations": "GUI stroke-start settings only. Size is nominal, not pressure-adjusted dab size; stored preset hash does not describe unsaved edits. No proof of effective GPU use or unchanged settings throughout the stroke.",
    }


def sample_readiness(pipeline, strokes):
    """Join independent checks; never substitute nearby timestamps for provenance."""
    conditions = {row["input"]: row["conditions"] for row in strokes["records"]}
    membership = strokes["input_membership"]
    rows = []
    for source in pipeline["input_audit"]:
        identity = source["input"]
        stroke = membership.get(identity)
        reasons = []
        if not source["dab_requests"]:
            reasons.append("no_recorded_dab_requests")
        else:
            if not source["all_requested_dabs_have_all_recorded_branches_swapped"]:
                reasons.append("incomplete_recorded_branches")
            reasons.extend(source["unverified_geometry_stages"])
        if stroke is None:
            reasons.append("missing_stroke_membership")
        elif stroke not in conditions:
            reasons.append("missing_stroke_conditions")
        if stroke is not None and stroke not in strokes["ended_strokes"]:
            reasons.append("stroke_end_not_recorded")
        rows.append({"input": identity, "stroke": stroke,
                     "recorded_checks_passed": not reasons, "exclusions": reasons})
    return {"inputs_passing_recorded_checks": sum(row["recorded_checks_passed"] for row in rows),
            "exclusion_counts": dict(collections.Counter(reason for row in rows for reason in row["exclusions"])),
            "inputs": rows,
            "limitations": "Passing joins recorded dirty/projection/update/upload/frame checks and explicit stroke conditions only. Request creation does not capture all interpolation dependencies; pixel survival and physical scanout are not established. No timing is reported; offscreen or unsupported paths are excluded conservatively."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("traces", nargs="+", type=pathlib.Path)
    args = parser.parse_args()
    results = []
    for path in args.traces:
        try:
            report = summarize(json.loads(path.read_text(encoding="utf-8")))
        except (ValueError, KeyError, TypeError) as error:
            parser.error(f"{path}: {error}")
        results.append({"file": str(path), **report})
    print(json.dumps(results, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
