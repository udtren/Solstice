# SPDX-FileCopyrightText: 2026 Krita contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import unittest
import random
from geometry import coverage, rectangle, summarize_geometry, summarize_transfers


class GeometryTest(unittest.TestCase):
    def test_holes_edges_and_negative_coordinates(self):
        expected = [rectangle([-10, -10, 20, 20])]
        self.assertEqual(coverage(expected, [rectangle([-10, -10, 10, 20]),
                                              rectangle([0, -10, 10, 20])]), "covered")
        # Same bounding box but a one-pixel hole must not pass.
        self.assertEqual(coverage(expected, [rectangle([-10, -10, 9, 20]),
                                              rectangle([0, -10, 10, 20])]), "gap")
        self.assertEqual(coverage(expected, [rectangle([10, -10, 20, 20])]), "gap")
        self.assertEqual(coverage([None], []), "covered")
        self.assertEqual(coverage(expected, expected, budget=0), "unverified_complexity")

    def test_union_and_overlapping_rectangles(self):
        expected = [rectangle([0, 0, 10, 10])]
        ring = [rectangle([0, 0, 10, 4]), rectangle([0, 6, 10, 4]),
                rectangle([0, 0, 4, 10]), rectangle([6, 0, 4, 10])]
        self.assertEqual(coverage(expected, ring), "gap")
        self.assertEqual(coverage(expected, ring + [rectangle([3, 3, 4, 4])]), "covered")

    def test_invalid_rectangle(self):
        for value in (None, [0, 0, 2], [0, 0, 1.5, 2], [False, 0, 1, 2]):
            with self.assertRaises(ValueError):
                rectangle(value)

    def test_coverage_matches_small_pixel_set_oracle(self):
        rng = random.Random(41)
        def pixels(rects):
            return {(x, y) for rect in rects if rect
                    for x in range(rect[0], rect[2]) for y in range(rect[1], rect[3])}
        for _ in range(100):
            expected = [rectangle([rng.randrange(-4, 4), rng.randrange(-4, 4),
                                   rng.randrange(6), rng.randrange(6)]) for _ in range(3)]
            actual = [rectangle([rng.randrange(-5, 5), rng.randrange(-5, 5),
                                 rng.randrange(9), rng.randrange(9)]) for _ in range(7)]
            status = "covered" if pixels(expected) <= pixels(actual) else "gap"
            self.assertEqual(coverage(expected, actual), status)

    def test_request_splits_and_view_isolation(self):
        def event(name, identity, parent="0", rect=None, owner="node", lod=0):
            args = {"id": identity, "parent": parent, "owner": owner}
            if rect is not None:
                args.update(rect=rect, lod=lod)
            return {"name": name, "args": args}
        events = [event("projection.request", "1"),
                  event("projection.request_rect", "1", rect=[0, 0, 10, 10]),
                  event("projection.request", "2", "1"),
                  event("projection.walker_request", "2", "3"),
                  event("projection.merge", "3"),
                  event("projection.executed_request_rect", "3", rect=[0, 0, 5, 10]),
                  event("projection.change_rect", "3", rect=[0, 0, 10, 10]),
                  event("update.ready", "4", "3", owner="view1"),
                  event("update.request_rect", "4", rect=[0, 0, 5, 10], owner="view1"),
                  event("update.ready", "5", "3", owner="view2"),
                  event("update.request_rect", "5", rect=[5, 0, 5, 10], owner="view2")]
        result = summarize_geometry(events)
        self.assertEqual(result["requests"][0]["status"], "gap")
        self.assertEqual(result["canvas_notification_counts"], {"gap": 2})
        # Missing old-trace coordinates must not silently pass.
        self.assertEqual(result["requests"][1]["status"], "unverified_missing_rectangles")
        events.extend([event("projection.walker_request", "1", "6"),
                       event("projection.walker_merged", "6", "7"), event("projection.merge", "7"),
                       event("projection.executed_request_rect", "7", rect=[5, 0, 5, 10])])
        self.assertEqual(summarize_geometry(events)["requests"][0]["status"], "covered")
        events[-1]["args"]["lod"] = 1
        self.assertEqual(summarize_geometry(events)["requests"][0]["status"], "unverified_lod")
        events[-1]["args"].update(lod=0, owner="other_node")
        self.assertEqual(summarize_geometry(events)["requests"][0]["status"], "unverified_owner")

    def transfer_events(self):
        def event(name, identity, parent="0", rect=None):
            args = {"id": identity, "parent": parent, "owner": "widget"}
            if rect is not None:
                args.update(rect=rect, lod=0)
            return {"name": name, "ts": 10, "args": args}
        return [event("update.upload_issued", "1", "2"),
                event("update.upload_expected_rect", "1", rect=[0, 0, 10, 10]),
                event("update.upload_patch_rect", "1", rect=[0, 0, 5, 10]),
                event("update.upload_patch_rect", "1", rect=[5, 0, 5, 10]),
                event("update.widget_expected_rect", "1", rect=[20, 20, 10, 10]),
                event("update.widget_patch_rect", "1", rect=[20, 20, 10, 10]),
                event("update.tracked_widget_rect", "1", rect=[18, 18, 14, 14]),
                event("frame.submitted", "3"), event("frame.covered_upload", "1", "3"),
                event("frame.submitted", "4"), event("frame.replaced", "3", "4"),
                {**event("frame.swapped", "4"), "ts": 20}]

    def test_transfer_geometry_and_acknowledgment(self):
        events = self.transfer_events()
        def status():
            return summarize_transfers(events)["uploads"][0]["status"]
        self.assertEqual(status(), "covered_to_swapped_commands")
        events[3]["args"]["rect"] = [6, 0, 4, 10]
        self.assertEqual(status(), "image_gap")
        events[3]["args"]["rect"] = [5, 0, 5, 10]
        events[5]["args"]["rect"] = [20, 20, 9, 10]
        self.assertEqual(status(), "widget_gap")
        events[5]["args"]["rect"] = [20, 20, 10, 10]
        events[6]["args"]["rect"] = [20, 20, 9, 10]
        self.assertEqual(status(), "tracking_gap")
        events[6]["args"]["rect"] = [18, 18, 14, 14]
        events[-1]["args"]["owner"] = "another_widget"
        self.assertEqual(status(), "unverified_no_swapped_coverage")
        events[-1]["args"]["owner"] = "widget"
        events[0]["ts"] = 30
        self.assertEqual(status(), "unverified_no_swapped_coverage")

    def test_transfer_view_change_offscreen_and_missing_metadata(self):
        events = self.transfer_events()
        events.append({"name": "frame.reset", "ts": 15, "args": {"owner": "widget"}})
        self.assertEqual(summarize_transfers(events)["uploads"][0]["status"], "unverified_view_change")
        events.pop()
        events[4]["args"]["rect"] = [0, 0, 0, 0]
        self.assertEqual(summarize_transfers(events)["uploads"][0]["status"], "outside_view")
        events = self.transfer_events()
        events[2]["args"]["lod"] = 1
        self.assertEqual(summarize_transfers(events)["uploads"][0]["status"], "unverified_lod")
        self.assertEqual(summarize_transfers([events[0]])["uploads"][0]["status"], "unverified_missing_rectangles")


if __name__ == "__main__":
    unittest.main()
