# SPDX-FileCopyrightText: 2026 Krita contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import unittest
from overhead import summarize_overhead


class OverheadTest(unittest.TestCase):
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
