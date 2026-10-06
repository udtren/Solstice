# SPDX-FileCopyrightText: 2026 Krita contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import unittest
from overhead import summarize_overhead, summarize_ready_to_issue, summarize_projection_preparation


class OverheadTest(unittest.TestCase):
    def projection_fixture(self):
        events = [{"name": "projection.merge", "ph": "X", "ts": 1000, "dur": 500,
                   "args": {"id": "w"}},
                  {"name": "update.ready", "ts": 4000, "args": {"id": "u", "parent": "w"}}]
        pipeline = {"input_audit": [{"input": "i", "downstream_walkers": ["w"]}]}
        timing = {"inputs": [{"input": "i", "stroke": "s"}] * 2,
                  "strokes": [{"stroke": "s", "conditions": {}}, {"stroke": "empty", "conditions": {}}]}
        return events, pipeline, timing

    def test_projection_direct_join_deduplicates_and_separates_strokes(self):
        args = self.projection_fixture()
        args[0].append({"name": "update.ready", "ts": 1501, "args": {"id": "other", "parent": "other"}})
        result = summarize_projection_preparation(*args)
        self.assertEqual(result["samples"], 1)
        self.assertEqual(result["walkers"][0]["merge_cpu_ms"], .5)
        self.assertEqual(result["walkers"][0]["merge_end_to_ready_ms"], 2.5)
        self.assertEqual(result["strokes"][0]["samples"], 1)
        self.assertIsNone(result["strokes"][1]["merge_cpu_ms"]["median"])

    def test_projection_missing_and_duplicate_markers(self):
        for index in (0, 1):
            for duplicate in (False, True):
                args = self.projection_fixture()
                if duplicate:
                    args[0].append(args[0][index].copy())
                else:
                    args[0].pop(index)
                self.assertEqual(summarize_projection_preparation(*args)["samples"], 0)

    def test_projection_invalid_timing_and_reversed_interval(self):
        for index, field in ((0, "ts"), (0, "dur"), (1, "ts")):
            for value in (None, True, "1000", -1, float("nan"), float("inf")):
                args = self.projection_fixture(); args[0][index][field] = value
                self.assertEqual(summarize_projection_preparation(*args)["samples"], 0)
        args = self.projection_fixture(); args[0][1]["ts"] = 1499
        self.assertEqual(summarize_projection_preparation(*args)["exclusion_counts"], {"ready_before_merge_end": 1})
        args = self.projection_fixture(); args[2]["inputs"] = []
        self.assertEqual(summarize_projection_preparation(*args)["samples"], 0)

    def ready_fixture(self):
        events, pipeline, timing = self.fixture()
        for event in events:
            event["args"]["parent"] = "update"
        events.append({"name": "update.ready", "ts": 2000, "args": {"id": "update"}})
        return events, pipeline, timing

    def test_ready_intervals_use_direct_parent_and_deduplicate_inputs(self):
        args = self.ready_fixture()
        args[2]["inputs"].append(args[2]["inputs"][0].copy())
        args[0].append({"name": "update.ready", "ts": 0, "args": {"id": "older"}})
        args[0].append({"name": "update.merged", "args": {"id": "older", "parent": "update"}})
        result = summarize_ready_to_issue(*args)
        self.assertEqual(result["samples"], 2)
        self.assertEqual([row["ready_to_issue_ms"] for row in result["uploads"]], [1, 6])

    def test_ready_missing_duplicate_or_invalid_are_excluded(self):
        for value in (None, True, "2000", float("nan"), float("inf"), -1, 9000):
            args = self.ready_fixture(); args[0][-1]["ts"] = value
            self.assertEqual(summarize_ready_to_issue(*args)["samples"], 0)
        for duplicate in (False, True):
            args = self.ready_fixture()
            if duplicate:
                args[0].append(args[0][-1].copy())
            else:
                args[0].pop()
            self.assertEqual(summarize_ready_to_issue(*args)["exclusion_counts"],
                             {"missing_or_ambiguous_ready": 2})

    def test_ready_issue_ambiguity_and_empty_verified_set(self):
        args = self.ready_fixture(); args[0].append(args[0][0].copy())
        self.assertEqual(summarize_ready_to_issue(*args)["samples"], 1)
        args[2]["inputs"] = []
        result = summarize_ready_to_issue(*args)
        self.assertEqual(result["samples"], 0)
        self.assertIsNone(result["median_ms"])

    def fixture(self):
        events = [{"name": "update.upload_issued", "ts": ts, "args": {"id": identity}}
                  for identity, ts in (("a", 3000), ("b", 8000), ("unrelated", 50000))]
        pipeline = {"input_audit": [{"input": "i", "downstream_uploads": ["a", "b"]}]}
        timing = {"inputs": [{"input": "i", "stroke": "s", "received_at_us": 1000,
                              "last_required_swap_at_us": 10000}],
                  "strokes": [{"stroke": "s", "conditions": {"nominal_size_px": 64}}]}
        return events, pipeline, timing

    def test_last_required_upload_and_exact_partition(self):
        result = summarize_overhead(*self.fixture())
        row = result["inputs"][0]
        self.assertEqual((row["before_last_upload_ms"], row["after_last_upload_ms"]), (7, 2))
        self.assertEqual(row["before_last_upload_ms"] + row["after_last_upload_ms"], 9)

    def test_missing_duplicate_and_invalid_issue_are_excluded(self):
        for value in (None, True, "8000", float("nan"), float("inf"), -1):
            args = self.fixture(); args[0][1]["ts"] = value
            self.assertEqual(summarize_overhead(*args)["samples"], 0)
        args = self.fixture(); args[0].append(args[0][1].copy())
        self.assertEqual(summarize_overhead(*args)["excluded_timed_inputs"], 1)
        args = self.fixture(); args[0].pop(1)
        self.assertEqual(summarize_overhead(*args)["samples"], 0)

    def test_all_required_issues_must_be_inside_interval(self):
        for index, value in ((0, 999), (1, 10001)):
            args = self.fixture(); args[0][index]["ts"] = value
            self.assertEqual(summarize_overhead(*args)["exclusion_counts"], {"upload_outside_verified_interval": 1})

    def test_no_new_samples_from_unverified_inputs(self):
        args = self.fixture(); args[2]["inputs"] = []
        result = summarize_overhead(*args)
        self.assertEqual(result["samples"], 0)
        self.assertIsNone(result["strokes"][0]["before_last_upload_ms"]["median"])

    def test_medians_are_not_additive_and_strokes_not_pooled(self):
        args = self.fixture()
        args[0].extend({"name": "update.upload_issued", "ts": ts, "args": {"id": identity}}
                       for identity, ts in (("c", 2000), ("d", 3000)))
        for identity, upload, end in (("j", "c", 12000), ("k", "d", 4000)):
            args[1]["input_audit"].append({"input": identity, "downstream_uploads": [upload]})
            args[2]["inputs"].append({"input": identity, "stroke": "s", "received_at_us": 1000,
                                      "last_required_swap_at_us": end})
        args[2]["strokes"].append({"stroke": "other", "conditions": {}})
        result = summarize_overhead(*args)
        self.assertEqual(result["strokes"][0]["before_last_upload_ms"]["median"], 2)
        self.assertEqual(result["strokes"][0]["after_last_upload_ms"]["median"], 2)
        # Total median is 9, not the sum of the component medians (4).
        self.assertEqual(result["strokes"][1]["samples"], 0)


if __name__ == "__main__":
    unittest.main()
