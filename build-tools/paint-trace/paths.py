# SPDX-FileCopyrightText: 2026 Krita contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit execution evidence. Never infer effective paths from environment flags."""
import collections


def summarize_paths(events, pipeline):
    batch_jobs = collections.defaultdict(set)
    records = collections.defaultdict(collections.Counter)
    names = {"path.brush.submitted", "path.brush.cpu", "path.brush.cpu_fallback",
             "path.compositor.submitted", "path.projection.cpu", "path.projection.cpu_fallback",
             "path.canvas.shared_buffer", "path.canvas.cpu_pixels"}
    names.update({"path.projection.empty_walk", "path.projection.root_recalculated",
                  "path.projection.extra_recalculated", "path.projection.child_reused",
                  "path.projection.no_target", "path.projection.invisible",
                  "path.projection.recalculate_skipped", "path.projection.original_reused",
                  "path.projection.masks_applied"})
    for event in events:
        name, args = event["name"], event.get("args", {})
        identity = args.get("id")
        if not identity or identity == "0":
            continue
        if name == "batch.paint_job":
            batch_jobs[args.get("parent")].add(identity)
        elif name in names:
            records[identity][name] += 1
    rows = []
    for source in pipeline["input_audit"]:
        jobs = set().union(*(batch_jobs[batch] for batch in source["linked_batches"]))
        evidence = {}
        for stage, identities, prefix in (
                ("brush", jobs, "path.brush."),
                ("projection", set(source["downstream_walkers"]), "path.projection."),
                ("canvas", set(source["downstream_uploads"]), "path.canvas.")):
            counts = collections.Counter()
            missing = 0
            for identity in identities:
                found = {name: count for name, count in records[identity].items()
                         if name.startswith(prefix) or (stage == "projection" and name == "path.compositor.submitted")}
                counts.update(found)
                missing += not bool(found)
            evidence[stage] = {"events": dict(counts), "identities": len(identities),
                               "identities_without_evidence": missing}
        rows.append({"input": source["input"], "stages": evidence})
    return {"inputs": rows,
            "limitations": "Evidence counts only, not a pure-path classification or latency gate. GPU events mean successful submission, not completion; CPU events mean a CPU branch was used, possibly with no pixel effect. Projection reuse/skip/recalculation events describe decisions, not CPU/GPU execution; one recorded decision does not certify all leaf work. A shared-buffer marker identifies the issued upload source, not physical display. Missing evidence remains unknown (including skipped mirror jobs and old captures). Jobs/batches/walkers shared by inputs are counted per input; do not sum these rows as independent work. Wash preview/final merge and all internal leaf work are not fully classified."}
