# SPDX-FileCopyrightText: 2026 Krita contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import unittest
from summarize import summarize, all_recorded_branches_swapped


class SummaryTest(unittest.TestCase):
    def document(self, events, dropped=0):
        return {"metadata": {"schema": 1, "time_unit": "microseconds", "dropped_events": dropped},
                "traceEvents": events}

    def test_overlapping_spans_are_not_latency(self):
        result = summarize(self.document([
            {"name": "worker", "ph": "X", "ts": 0, "dur": 2000},
            {"name": "worker", "ph": "X", "ts": 0, "dur": 4000},
            {"name": "canvas.frame_swapped", "ph": "i", "ts": 8000},
        ]))
        self.assertEqual(result["stages"]["worker"]["cpu_span_median_ms"], 3)
        self.assertEqual(result["stages"]["worker"]["cpu_span_p95_ms"], 4)
        self.assertEqual(result["stages"]["canvas.frame_swapped"], {"count": 1})
        self.assertNotIn("latency", result)

    def test_reject_incomplete_capture(self):
        with self.assertRaises(ValueError):
            summarize(self.document([], dropped=1))

    def test_reject_wrong_units(self):
        doc = self.document([])
        doc["metadata"]["time_unit"] = "milliseconds"
        with self.assertRaises(ValueError):
            summarize(doc)

    def test_explicit_frame_links_only(self):
        def event(name, identity, parent="0", owner="canvas1"):
            return {"name": name, "ph": "i", "ts": 0,
                    "args": {"owner": owner, "id": identity, "parent": parent}}
        doc = self.document([
            event("input.tablet_move", "1"), event("input.mouse_move", "2"),
            event("input.accepted_move", "1"), event("input.accepted_move", "1"),
            event("update.upload_issued", "3"), event("frame.swapped", "4"),
            # Swap alone has no relationship with the preceding upload.
            event("frame.submitted", "5"), event("frame.covered_upload", "3", "5"),
            event("frame.submitted", "6"), event("frame.replaced", "5", "6"),
            event("frame.swapped", "6", owner="canvas2"),
            event("update.upload_issued", "7"),
        ])
        doc["metadata"]["schema"] = 2
        counts = summarize(doc)["lineage"]
        self.assertEqual(counts["received_pointer_inputs"], 2)
        self.assertEqual(counts["inputs_linked_to_freehand_dispatch"], 1)
        self.assertEqual(counts["upload_occurrences"], 2)
        self.assertEqual(counts["covered_uploads_in_swapped_frame_chain"], 0)
        doc["traceEvents"].append(event("frame.swapped", "6"))
        self.assertEqual(summarize(doc)["lineage"]["covered_uploads_in_swapped_frame_chain"], 1)

    def test_job_ancestry_and_cancel(self):
        def event(name, identity, ts, parent="0"):
            return {"name": name, "ph": "i", "ts": ts,
                    "args": {"id": identity, "parent": parent}}
        doc = self.document([
            event("input.accepted_move", "1", 0),
            event("job.created", "2", 100, "1"),
            event("job.started", "2", 1100),
            event("job.created", "3", 1200, "2"),
            event("job.created", "4", 1300, "2"),
            event("job.destroyed", "4", 1400),
            event("job.started", "3", 4200),
        ])
        result = summarize(doc)["jobs"]
        self.assertEqual(result["created_with_freehand_input_ancestor"], 3)
        self.assertEqual(result["destroyed_without_execution"], 1)
        self.assertEqual(result["creation_to_start_samples"], 2)
        self.assertEqual(result["creation_to_start_median_ms"], 2)
        doc["traceEvents"].append(event("job.started", "3", 4500))
        with self.assertRaises(ValueError):
            summarize(doc)

    def test_job_graph_cycle_rejected(self):
        doc = self.document([
            {"name": "job.created", "ph": "i", "ts": 0, "args": {"id": "1", "parent": "2"}},
            {"name": "job.created", "ph": "i", "ts": 0, "args": {"id": "2", "parent": "1"}},
        ])
        with self.assertRaises(ValueError):
            summarize(doc)

    def test_projection_pipeline_requires_explicit_edges(self):
        def event(name, identity, parent="0", owner="canvas"):
            return {"name": name, "ph": "i", "ts": 0,
                    "args": {"id": identity, "parent": parent, "owner": owner}}
        doc = self.document([
            event("input.accepted_move", "1"), event("job.created", "2", "1"),
            event("dab.request", "3", "2"), event("dab.in_batch", "3", "4"),
            event("batch.ready", "4"), event("batch.to_dirty", "4", "5"),
            event("dirty.dispatch", "5"), event("projection.request", "6", "5"),
            event("projection.request", "7", "6"), event("projection.walker_request", "7", "8"),
            event("projection.walker_merged", "8", "9"), event("projection.merge", "9"),
            event("update.ready", "10", "9"), event("update.ready", "11"),
            event("update.superseded", "10", "11"), event("update.upload_issued", "12", "11"),
            event("frame.submitted", "13"), event("frame.covered_upload", "12", "13"),
            event("frame.swapped", "13", owner="other_canvas"),
            # Another batch at the same timestamp is not a dependency.
            event("batch.ready", "14"),
        ])
        result = summarize(doc)["pipeline"]
        self.assertEqual(result["input_linked_batches"], 1)
        self.assertEqual(result["batches_without_input_link"], 1)
        self.assertEqual(result["input_linked_batches_without_swapped_command_descendant"], 1)
        self.assertEqual(result["batches_with_canvas_update_descendant"], 1)
        self.assertEqual(result["batches_with_some_swapped_command_descendant"], 0)
        doc["traceEvents"].append(event("frame.swapped", "13"))
        result = summarize(doc)["pipeline"]
        self.assertEqual(result["batches_with_some_swapped_command_descendant"], 1)
        self.assertEqual(result["inputs_with_some_swapped_command_descendant"], 1)
        self.assertEqual(result["input_linked_batches_with_some_swapped_command_descendant"], 1)
        self.assertEqual(result["input_linked_batches_without_swapped_command_descendant"], 0)
        doc["traceEvents"] = [e for e in doc["traceEvents"] if e["name"] != "batch.to_dirty"]
        result = summarize(doc)["pipeline"]
        self.assertEqual(result["inputs_with_some_swapped_command_descendant"], 0)
        self.assertEqual(result["input_linked_batches"], 1)
        self.assertEqual(result["input_linked_batches_without_swapped_command_descendant"], 1)

    def test_batch_retains_distinct_inputs_and_cache_hits(self):
        def event(name, identity, parent="0"):
            return {"name": name, "ph": "i", "ts": 0,
                    "args": {"id": identity, "parent": parent}}
        doc = self.document([
            event("input.accepted_move", "1"), event("input.accepted_move", "2"),
            event("job.created", "3", "1"), event("job.created", "4", "2"),
            event("dab.request", "5", "3"), event("dab.cache_request", "6", "4"),
            event("dab.in_batch", "5", "7"), event("dab.in_batch", "6", "7"),
            # Timer job has no input ancestor, but its batch has two sources.
            event("job.created", "8"), event("batch.ready", "7", "8"),
            event("batch.paint_job", "9", "7"), event("batch.dirty_recorded", "7", "10"),
        ])
        result = summarize(doc)["batches"]
        self.assertEqual(result["requests_in_batches"], 2)
        self.assertEqual(result["requests_with_generation_span"], 0)
        self.assertEqual(result["batches_with_multiple_input_sources"], 1)
        self.assertEqual(result["batches_with_dirty_recorded"], 1)
        audit = summarize(doc)["pipeline"]["input_audit"]
        self.assertEqual([row["dab_requests"] for row in audit], [1, 1])
        doc["traceEvents"].append(event("dab.in_batch", "5", "11"))
        with self.assertRaises(ValueError):
            summarize(doc)

    def test_input_audit_does_not_hide_partial_batches_or_missing_requests(self):
        def event(name, identity, parent="0"):
            return {"name": name, "ph": "i", "ts": 0,
                    "args": {"id": identity, "parent": parent, "owner": "canvas"}}
        doc = self.document([
            event("input.accepted_move", "1"), event("input.accepted_end", "2"),
            event("job.created", "3", "1"), event("dab.request", "4", "3"),
            event("dab.cache_request", "5", "3"), event("dab.request", "6", "3"),
            event("dab.in_batch", "4", "7"), event("batch.ready", "7"),
            event("dab.in_batch", "5", "8"), event("batch.ready", "8"),
            event("batch.to_dirty", "7", "9"), event("dirty.dispatch", "9"),
            event("projection.request", "10", "9"), event("projection.walker_request", "10", "11"),
            event("projection.merge", "11"), event("update.ready", "12", "11"),
            event("update.upload_issued", "13", "12"), event("frame.submitted", "14"),
            event("frame.covered_upload", "13", "14"), event("frame.swapped", "14"),
        ])
        result = summarize(doc)["pipeline"]
        self.assertEqual(result["inputs_with_some_swapped_command_descendant"], 1)
        self.assertEqual(result["inputs_without_dab_requests_by_kind"], {"end": 1})
        self.assertEqual(result["inputs_with_unbatched_dab_requests"], 1)
        self.assertEqual(result["inputs_with_all_requested_dabs_in_swapped_batch_commands"], 0)
        row = result["input_audit"][0]
        self.assertEqual(row["dab_requests"], 3)
        self.assertEqual(row["unbatched_dab_requests"], 1)
        self.assertEqual(row["batches_without_swapped_command_descendant"], 1)
        doc["traceEvents"].extend([
            event("dab.in_batch", "6", "8"), event("batch.to_dirty", "8", "9")])
        result = summarize(doc)["pipeline"]
        self.assertEqual(result["inputs_with_all_requested_dabs_in_swapped_batch_commands"], 1)
        self.assertEqual(result["inputs_with_unbatched_dab_requests"], 0)

    def test_stroke_conditions_require_same_input_and_canvas(self):
        doc = self.document([{"name": "input.accepted_begin", "ph": "i", "ts": 0,
                              "args": {"id": "1", "owner": "canvas"}}])
        self.assertEqual(summarize(doc)["stroke_conditions"]["beginnings_without_conditions"], 1)
        row = {"input": "1", "canvas": "canvas", "conditions": {"nominal_size_px": 64}}
        doc["metadata"]["stroke_conditions"] = [row]
        self.assertEqual(summarize(doc)["stroke_conditions"]["recorded"], 1)
        for replacement in ({**row, "input": "2"}, {**row, "canvas": "other"}):
            doc["metadata"]["stroke_conditions"] = [replacement]
            with self.assertRaises(ValueError):
                summarize(doc)
        doc["metadata"]["stroke_conditions"] = [row, row]
        with self.assertRaises(ValueError):
            summarize(doc)

    def test_recorded_split_branch_requires_every_child(self):
        def event(name, identity, parent="0"):
            return {"name": name, "ph": "i", "ts": 0,
                    "args": {"id": identity, "parent": parent, "owner": "canvas"}}
        doc = self.document([
            event("input.accepted_move", "1"), event("dab.request", "2", "1"),
            event("dab.in_batch", "2", "3"), event("batch.ready", "3"),
            event("batch.to_dirty", "3", "4"), event("dirty.dispatch", "4"),
            event("projection.request", "5", "4"),
            event("projection.request", "6", "5"), event("projection.request", "7", "5"),
            event("projection.walker_request", "6", "8"), event("projection.merge", "8"),
            event("update.ready", "9", "8"), event("update.upload_issued", "10", "9"),
            event("frame.submitted", "11"), event("frame.covered_upload", "10", "11"),
            event("frame.swapped", "11"),
        ])
        result = summarize(doc)["pipeline"]
        self.assertEqual(result["inputs_with_all_requested_dabs_in_swapped_batch_commands"], 1)
        audit = result["recorded_branch_audit"]
        self.assertEqual(audit["input_linked_batches_with_all_recorded_branches_swapped"], 0)
        self.assertEqual(audit["unresolved_terminal_counts"]["projection_request_without_walker_or_child"], 1)
        doc["traceEvents"].extend([
            event("projection.walker_request", "7", "12"), event("projection.walker_merged", "12", "8")])
        self.assertEqual(summarize(doc)["pipeline"]["recorded_branch_audit"][
            "input_linked_batches_with_all_recorded_branches_swapped"], 1)
        # Multiple canvas updates from one walker are also independent obligations.
        doc["traceEvents"].append(event("update.ready", "13", "8"))
        self.assertEqual(summarize(doc)["pipeline"]["recorded_branch_audit"][
            "input_linked_batches_with_all_recorded_branches_swapped"], 0)
        other = event("update.ready", "14")
        other["args"]["owner"] = "other_canvas"
        doc["traceEvents"].extend([other, event("update.superseded", "13", "14"),
                                    event("update.superseded", "14", "9")])
        self.assertEqual(summarize(doc)["pipeline"]["recorded_branch_audit"][
            "input_linked_batches_with_all_recorded_branches_swapped"], 0)
        doc["traceEvents"].append(event("update.superseded", "13", "9"))
        self.assertEqual(summarize(doc)["pipeline"]["recorded_branch_audit"][
            "inputs_with_all_requested_dabs_and_recorded_branches_swapped"], 1)

    def test_branch_audit_cycle_and_deep_chain(self):
        with self.assertRaises(ValueError):
            all_recorded_branches_swapped({"1", "2"}, {"1": {"2"}, "2": {"1"}}, {"2"})
        # Iterative traversal must not overflow Python's recursion limit.
        nodes = {str(i) for i in range(3000)}
        edges = {str(i): {str(i + 1)} for i in range(2999)}
        self.assertEqual(all_recorded_branches_swapped(nodes, edges, {"2999"}), nodes)
        self.assertEqual(all_recorded_branches_swapped(nodes, edges, set()), set())

    def test_joined_input_checks_require_explicit_stroke_and_geometry(self):
        def event(name, identity, parent="0", rect=None):
            args = {"id": identity, "parent": parent, "owner": "canvas"}
            if rect is not None:
                args.update(rect=rect, lod=0)
            return {"name": name, "ph": "i", "ts": 0, "args": args}
        doc = self.document([
            event("input.accepted_begin", "20"), event("input.accepted_move", "1"),
            event("stroke.input", "1", "20"), event("input.accepted_end", "21"),
            event("stroke.input", "21", "20"), event("stroke.ended", "20", "21"),
            event("dab.request", "2", "1"), event("dab.in_batch", "2", "3"),
            event("batch.ready", "3"), event("batch.to_dirty", "3", "4"),
            event("dirty.dispatch", "4"), event("projection.request", "5", "4"),
            event("projection.walker_request", "5", "6"), event("projection.merge", "6"),
            event("update.ready", "7", "6"), event("update.upload_issued", "8", "7"),
            event("frame.submitted", "9"), event("frame.covered_upload", "8", "9"), event("frame.swapped", "9")])
        doc["traceEvents"].append({"name": "canvas.created", "ph": "i", "ts": 0,
                                   "args": {"owner": "canvas", "related": "canvas"}})
        for name, identity in (("dirty.source_rect", "4"), ("dirty.submitted_rect", "4"),
                               ("projection.request_rect", "5"), ("projection.executed_request_rect", "6"),
                               ("projection.change_rect", "6"), ("update.request_rect", "7"),
                               ("update.upload_expected_rect", "8"), ("update.upload_bounds_rect", "8"),
                               ("update.upload_patch_rect", "8"),
                               ("update.widget_expected_rect", "8"), ("update.widget_patch_rect", "8"),
                               ("update.tracked_widget_rect", "8")):
            doc["traceEvents"].append(event(name, identity, rect=[0, 0, 10, 10]))
        doc["metadata"]["stroke_conditions"] = [{"input": "20", "canvas": "canvas",
                                                 "conditions": {"nominal_size_px": 64, "incremental": True}}]
        result = summarize(doc)["sample_readiness"]
        self.assertEqual(result["inputs_passing_recorded_checks"], 1)
        self.assertEqual(result["exclusion_counts"], {"no_recorded_dab_requests": 2})
        receipt = event("input.tablet_move", "1")
        doc["traceEvents"].append(receipt)
        for entry in doc["traceEvents"]:
            if entry["name"] == "frame.swapped":
                entry["ts"] = 8000
        timed = summarize(doc)["command_presentation_timing"]
        self.assertEqual(timed["samples"], 1)
        self.assertEqual(timed["inputs"][0]["elapsed_ms"], 8)
        original = doc["traceEvents"]
        for missing, reason in (("dirty.submitted_rect", "dirty_group_geometry"),
                                ("update.upload_bounds_rect", "compressed_update_geometry")):
            doc["traceEvents"] = [e for e in original if e["name"] != missing]
            result = summarize(doc)["sample_readiness"]
            self.assertEqual(result["inputs_passing_recorded_checks"], 0)
            self.assertEqual(result["exclusion_counts"][reason], 1)
        doc["traceEvents"] = original
        doc["traceEvents"] = [e for e in doc["traceEvents"] if e["name"] != "projection.executed_request_rect"]
        result = summarize(doc)["sample_readiness"]
        self.assertEqual(result["inputs_passing_recorded_checks"], 0)
        self.assertEqual(result["exclusion_counts"]["projection_request_geometry"], 1)
        doc["traceEvents"] = [e for e in doc["traceEvents"] if e["name"] not in ("stroke.input", "stroke.ended")]
        result = summarize(doc)["sample_readiness"]
        self.assertEqual(result["exclusion_counts"]["missing_stroke_membership"], 2)
        self.assertEqual(result["inputs_passing_recorded_checks"], 0)

    def test_stroke_membership_rejects_cross_canvas_and_conflicting_strokes(self):
        def event(name, identity, parent="0", owner="canvas"):
            return {"name": name, "ph": "i", "ts": 0,
                    "args": {"id": identity, "parent": parent, "owner": owner}}
        doc = self.document([event("input.accepted_begin", "1"), event("input.accepted_begin", "2"),
                             event("input.accepted_move", "3"), event("stroke.input", "3", "1")])
        self.assertEqual(summarize(doc)["stroke_conditions"]["input_membership"]["3"], "1")
        doc["traceEvents"].append(event("stroke.input", "3", "2"))
        with self.assertRaises(ValueError):
            summarize(doc)
        doc["traceEvents"][-1] = event("stroke.input", "3", "1", "other_canvas")
        with self.assertRaises(ValueError):
            summarize(doc)


if __name__ == "__main__":
    unittest.main()
