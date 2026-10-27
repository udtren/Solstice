#!/usr/bin/env python3
#
#  SPDX-License-Identifier: GPL-3.0-or-later
#
"""Tests for the parts of paint_trace_series.py that need no Solstice.

Run: python dev-tools/test-control/test_paint_trace_series.py (registered with
ctest as plugins-extensions-testcontrol-PaintTraceSeriesTest).
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import paint_trace_series as series  # noqa: E402


def view(width=1906, height=948, x=-287, y=-1280):
    return {"imageRectInWidget": {"x": x, "y": y, "width": 2480, "height": 3508},
            "canvasWidth": width, "canvasHeight": height}


def summary(strokes, conditions=None, dropped=0):
    records = []
    for index, stroke in enumerate(strokes):
        record_conditions = {"nominal_size_px": stroke["size"], "incremental": stroke["paintMode"] == "buildup",
                             "preset_name": series.PRESET}
        if conditions and index in conditions:
            record_conditions.update(conditions[index])
        records.append({"stroke": str(100 + index), "conditions": record_conditions, "samples": 20,
                        "median_ms": 10.0 + index, "p95_ms": 20.0 + index})
    return {"metadata": {"dropped_events": dropped}, "command_presentation_timing": {"strokes": records}}


class LayoutTest(unittest.TestCase):
    def test_strokes_stay_visible_and_apart(self):
        strokes = series.layout(view())
        self.assertEqual(len(strokes), 16)
        self.assertEqual([s["warmUp"] for s in strokes[:4]], [True, False, False, False])
        self.assertEqual([(s["size"], s["paintMode"]) for s in strokes[::4]], series.CONDITIONS)
        for stroke in strokes:
            for point in stroke["points"]:
                # Inside the visible image area: x 287-2193, y 1280-2228.
                self.assertGreaterEqual(point["x"] - stroke["size"] / 2, 287)
                self.assertLessEqual(point["x"] + stroke["size"] / 2, 2193)
                self.assertGreaterEqual(point["y"] - stroke["size"] / 2, 1280)
                self.assertLessEqual(point["y"] + stroke["size"] / 2, 2228)
            self.assertEqual(len(stroke["points"]), series.STROKE_POINTS)
        # Neighbouring strokes of the 256 px rows do not touch.
        row = strokes[8:12]
        for left, right in zip(row, row[1:]):
            self.assertGreater(right["points"][0]["x"] - left["points"][-1]["x"], 256)

    def test_too_small_view_is_refused(self):
        with self.assertRaises(series.SeriesError):
            series.layout(view(width=900, height=600))


class SelectionTest(unittest.TestCase):
    def test_warm_ups_are_dropped_by_order(self):
        strokes = series.layout(view())
        selected, problems = series.check_and_select(summary(strokes), strokes)
        self.assertEqual(problems, [])
        self.assertEqual(list(selected), ["64px Buildup", "64px Wash", "256px Buildup", "256px Wash"])
        self.assertEqual([r["stroke"] for r in selected["64px Wash"]], ["105", "106", "107"])

    def test_mismatches_are_problems(self):
        strokes = series.layout(view())
        _, problems = series.check_and_select(summary(strokes, {5: {"incremental": True}}, dropped=3), strokes)
        self.assertEqual(len(problems), 2)
        _, problems = series.check_and_select(summary(strokes[:15]), strokes)
        self.assertIn("15 strokes in the trace, 16 played", problems)


class AggregateTest(unittest.TestCase):
    def test_median_of_process_medians(self):
        runs = []
        for mode, medians in (("cpu", [1, 2, 3]), ("cpu", [7, 8, 9]), ("cpu", [4, 5, 6]), ("brush", [1, 1, 1])):
            runs.append({"mode": mode, "conditions": {"64px Buildup": {
                "inputs": [20, 20, 20], "processMedianMs": sorted(medians)[1], "processP95Ms": 10}}})
        table = series.aggregate(runs)
        cpu = table["64px Buildup"]["cpu"]
        self.assertEqual((cpu["processes"], cpu["medianMs"], cpu["minMs"], cpu["maxMs"]), (3, 5, 2, 8))
        self.assertEqual(cpu["inputs"], 180)
        self.assertEqual(table["64px Buildup"]["brush"]["processes"], 1)
        self.assertNotIn("projection", table["64px Buildup"])


if __name__ == "__main__":
    unittest.main()
