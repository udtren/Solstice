# SPDX-FileCopyrightText: 2026 Krita contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import unittest
from paths import summarize_paths


class PathsTest(unittest.TestCase):
    def event(self, name, identity, parent="0"):
        return {"name": name, "args": {"id": identity, "parent": parent}}

    def pipeline(self):
        return {"input_audit": [{"input": "1", "linked_batches": ["b"],
                                "downstream_walkers": ["w"], "downstream_uploads": ["u"]}]}

    def test_links_select_only_actual_descendants_and_keep_mixed_paths(self):
        events = [self.event("batch.paint_job", "j", "b"),
                  self.event("path.brush.submitted", "j"), self.event("path.brush.cpu_fallback", "j"),
                  self.event("path.compositor.submitted", "w"), self.event("path.projection.cpu", "w"),
                  self.event("path.canvas.shared_buffer", "u"), self.event("path.canvas.cpu_pixels", "u"),
                  self.event("path.brush.cpu", "unrelated")]
        stages = summarize_paths(events, self.pipeline())["inputs"][0]["stages"]
        self.assertEqual(stages["brush"]["events"], {"path.brush.submitted": 1, "path.brush.cpu_fallback": 1})
        self.assertEqual(stages["projection"]["events"], {"path.compositor.submitted": 1, "path.projection.cpu": 1})
        self.assertEqual(len(stages["canvas"]["events"]), 2)
        self.assertEqual(stages["brush"]["identities_without_evidence"], 0)

    def test_missing_is_unknown_not_cpu_and_zero_ids_are_ignored(self):
        events = [self.event("batch.paint_job", "j", "b"), self.event("path.brush.submitted", "0")]
        stages = summarize_paths(events, self.pipeline())["inputs"][0]["stages"]
        for stage in stages.values():
            self.assertEqual(stage["events"], {})
            self.assertEqual(stage["identities_without_evidence"], 1)

    def test_shared_jobs_not_double_counted_within_input(self):
        p = self.pipeline(); p["input_audit"][0]["linked_batches"].append("c")
        events = [self.event("batch.paint_job", "j", "b"), self.event("batch.paint_job", "j", "c"),
                  self.event("path.brush.submitted", "j")]
        self.assertEqual(summarize_paths(events,p)["inputs"][0]["stages"]["brush"]["events"], {"path.brush.submitted": 1})

    def test_reuse_and_skip_are_not_gpu_or_cpu_and_do_not_hide_other_walkers(self):
        p = self.pipeline()
        p["input_audit"][0]["downstream_walkers"].append("unknown")
        events = [self.event("path.projection.child_reused", "w"),
                  self.event("path.projection.no_target", "w"),
                  self.event("path.projection.original_reused", "w"),
                  self.event("path.projection.root_recalculated", "w"),
                  self.event("path.compositor.submitted", "unrelated")]
        stage = summarize_paths(events, p)["inputs"][0]["stages"]["projection"]
        self.assertEqual(stage["identities_without_evidence"], 1)
        self.assertEqual(stage["identities"], 2)
        self.assertEqual(len(stage["events"]), 4)
        self.assertNotIn("path.projection.cpu", stage["events"])
        self.assertNotIn("path.compositor.submitted", stage["events"])

    def test_recalculation_and_mixed_execution_stay_separate(self):
        names = ["path.projection.empty_walk", "path.projection.invisible",
                 "path.projection.recalculate_skipped", "path.projection.extra_recalculated",
                 "path.projection.masks_applied", "path.projection.cpu", "path.compositor.submitted"]
        events = [self.event(name, "w") for name in names]
        stage = summarize_paths(events, self.pipeline())["inputs"][0]["stages"]["projection"]
        self.assertEqual(stage["events"], dict.fromkeys(names, 1))


if __name__ == "__main__":
    unittest.main()
