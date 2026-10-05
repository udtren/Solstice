# SPDX-FileCopyrightText: 2026 Krita contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import unittest
from timing import summarize_timing


class TimingTest(unittest.TestCase):
    def fixture(self):
        return ([{"name": "input.tablet_move", "ts": 1000, "args": {"id": "1"}}],
                {"input_audit": [{"input": "1", "downstream_uploads": ["a", "b"]}]},
                {"records": [{"input": "s", "conditions": {"nominal_size_px": 64}}]},
                {"transfers": {"uploads": [
                    {"upload": "a", "status": "covered_to_swapped_commands", "swapped_at_us": 3000},
                    {"upload": "b", "status": "covered_to_swapped_commands", "swapped_at_us": 9000}]}},
                {"inputs": [{"input": "1", "stroke": "s", "exclusions": [], "recorded_checks_passed": True}]})

    def test_last_required_upload_not_first_or_unrelated_swap(self):
        args = self.fixture()
        args[0].append({"name": "frame.swapped", "ts": 999999, "args": {"id": "unrelated"}})
        result = summarize_timing(*args)
        self.assertEqual(result["samples"], 1)
        self.assertEqual(result["inputs"][0]["elapsed_ms"], 8)
        self.assertEqual(result["strokes"][0]["p95_ms"], 8)

    def test_missing_ambiguous_or_invalid_receipt(self):
        for timestamps in ([], [1000, 1000], [-1], [float("nan")], [float("inf")], [True], ["1000"]):
            args = self.fixture()
            args[0][:] = [{"name": "input.mouse_move", "ts": ts, "args": {"id": "1"}} for ts in timestamps]
            result = summarize_timing(*args)
            self.assertEqual(result["samples"], 0)
            self.assertIn("missing_or_ambiguous_input_timestamp", result["exclusion_counts"])

    def test_geometry_exclusion_cannot_be_rescued_by_timestamps(self):
        args = self.fixture()
        args[4]["inputs"][0].update(recorded_checks_passed=False, exclusions=["dirty_group_geometry"])
        result = summarize_timing(*args)
        self.assertEqual(result["samples"], 0)
        self.assertIsNone(result["strokes"][0]["median_ms"])

    def test_all_uploads_need_valid_ordered_acknowledgments(self):
        for end in (None, float("nan"), -1, 999):
            args = self.fixture()
            args[3]["transfers"]["uploads"][1]["swapped_at_us"] = end
            self.assertEqual(summarize_timing(*args)["samples"], 0)
        args = self.fixture()
        args[3]["transfers"]["uploads"].append(args[3]["transfers"]["uploads"][0].copy())
        self.assertEqual(summarize_timing(*args)["samples"], 0)

    def test_strokes_are_not_pooled(self):
        args = self.fixture()
        args[2]["records"].append({"input": "other", "conditions": {"nominal_size_px": 256}})
        result = summarize_timing(*args)
        self.assertEqual([row["samples"] for row in result["strokes"]], [1, 0])


if __name__ == "__main__":
    unittest.main()
