# SPDX-FileCopyrightText: 2026 Krita contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import unittest
from geometry import summarize_bridges


def event(name, identity, parent="0", owner="node", rect=None):
    args = {"id": identity, "parent": parent, "owner": owner}
    if rect is not None:
        args.update(rect=rect, lod=0)
    return {"name": name, "args": args}


class BridgeTest(unittest.TestCase):
    def test_dirty_regions_do_not_lose_holes_or_masked_patches(self):
        events = [event("dirty.dispatch", "1", owner="strategy"),
                  event("dirty.source_rect", "1", rect=[0, 0, 10, 10]),
                  event("dirty.submitted_rect", "1", rect=[0, 0, 5, 10]),
                  event("dirty.submitted_rect", "1", rect=[5, 0, 5, 10]),
                  event("projection.request", "2", "1"),
                  event("projection.request_rect", "2", rect=[0, 0, 10, 10])]
        row = summarize_bridges(events)["dirty_groups"][0]
        self.assertEqual(row["source_to_submission"], "covered")
        self.assertEqual(row["submission_to_projection"], "covered")
        events[3]["args"]["rect"] = [6, 0, 4, 10]
        self.assertEqual(summarize_bridges(events)["dirty_groups"][0]["source_to_submission"], "gap")
        events[3]["args"]["rect"] = [5, 0, 5, 10]
        events[-1]["args"]["rect"] = [0, 0, 9, 10]
        self.assertEqual(summarize_bridges(events)["dirty_groups"][0]["submission_to_projection"], "gap")
        events[-1]["args"]["owner"] = "other_node"
        self.assertEqual(summarize_bridges(events)["dirty_groups"][0]["submission_to_projection"], "unverified_owner")

    def updates(self):
        return [{"name": "canvas.created", "args": {"owner": "canvas", "related": "widget"}},
                event("update.ready", "1", owner="canvas"),
                event("update.request_rect", "1", owner="canvas", rect=[-5, -5, 15, 15]),
                event("update.ready", "2", owner="canvas"),
                event("update.request_rect", "2", owner="canvas", rect=[-5, -5, 20, 20]),
                event("update.superseded", "1", "2", owner="0"),
                event("update.ready", "3", owner="canvas"),
                event("update.request_rect", "3", owner="canvas", rect=[0, 0, 10, 10]),
                event("update.merged", "2", "3", owner="0"),
                event("update.upload_issued", "4", "3", owner="widget"),
                event("update.upload_bounds_rect", "4", owner="widget", rect=[0, 0, 10, 10]),
                event("update.upload_expected_rect", "4", owner="widget", rect=[0, 0, 10, 10])]

    def test_compressed_updates_are_clipped_then_covered_by_real_descendants(self):
        events = self.updates()
        self.assertEqual(summarize_bridges(events)["compressed_update_counts"], {"covered": 3})
        events[-1]["args"]["rect"] = [0, 0, 9, 10]
        self.assertEqual(summarize_bridges(events)["compressed_update_counts"], {"gap": 3})
        events = self.updates()
        events[3]["args"]["owner"] = "other_canvas"
        self.assertEqual(summarize_bridges(events)["compressed_updates"][0]["status"], "unverified_no_upload")
        events = self.updates()
        events[0]["args"]["owner"] = "other_canvas"
        self.assertEqual(summarize_bridges(events)["compressed_update_counts"], {"unverified_owner": 3})

    def test_missing_metadata_and_resized_image_do_not_pass(self):
        events = self.updates()
        events.pop(-2)
        self.assertEqual(summarize_bridges(events)["compressed_update_counts"], {"unverified_missing_rectangles": 3})
        events = self.updates()
        events.extend([event("update.upload_issued", "5", "3", owner="widget"),
                       event("update.upload_bounds_rect", "5", owner="widget", rect=[0, 0, 20, 20]),
                       event("update.upload_expected_rect", "5", owner="widget", rect=[0, 0, 10, 10])])
        self.assertEqual(summarize_bridges(events)["compressed_update_counts"], {"unverified_image_bounds_change": 3})


if __name__ == "__main__":
    unittest.main()
