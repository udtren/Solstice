# SPDX-FileCopyrightText: 2026 Krita contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import unittest
from residency import summarize_residency


def event(name, start, duration, tid=1, owner="backend", pid=1):
    return dict(name=name, ts=start, dur=duration, tid=tid, pid=pid, ph="X", args=dict(owner=owner))


class ResidencyTest(unittest.TestCase):
    def test_clipped_overlap_and_noncausal_gaps(self):
        result = summarize_residency([event("tile_submit.lock", 1000, 5000),
                                     event("residency.hold.pin", 0, 2000, 2),
                                     event("residency.hold.submit", 4000, 4000, 3)])
        self.assertEqual(result["wait_ms"], 5)
        self.assertEqual(result["observed_hold_overlap_ms"], 3)
        self.assertEqual(result["overlap_ms_by_holder"], {"residency.hold.pin": 1, "residency.hold.submit": 2})

    def test_no_nearest_or_other_thread_process_owner_inference(self):
        for kwargs in ({"tid": 1}, {"tid": 2, "pid": 2}, {"tid": 2, "owner": "other"}):
            result = summarize_residency([event("tile_submit.lock", 1000, 1000),
                                         event("residency.hold.pin", 1000, 1000, **kwargs)])
            self.assertEqual(result["matched_waits"], 0)
        result = summarize_residency([event("tile_submit.lock", 1000, 1000), event("residency.hold.pin", 0, 1000, 2)])
        self.assertEqual(result["matched_waits"], 0)

    def test_overlapping_holders_rejected(self):
        result = summarize_residency([event("tile_submit.lock", 0, 5000),
                                     event("residency.hold.pin", 100, 2000, 2),
                                     event("residency.hold.submit", 200, 2000, 3)])
        self.assertEqual(result["excluded_waits"], 1)
        self.assertEqual(result["observed_hold_overlap_ms"], 0)

    def test_serialization_roundoff_is_not_overlap(self):
        result = summarize_residency([event("tile_submit.lock", 0, 100000),
                                     event("residency.hold.pin", 74705.1, .1, 2),
                                     event("residency.hold.submit", 74705.2, 1.7, 3)])
        self.assertEqual(result["ambiguous_owner_groups"], 0)
        self.assertAlmostEqual(result["observed_hold_overlap_ms"], .0018)

    def test_invalid_and_old_owner_are_not_attributed(self):
        for value in (None, True, -1, float("nan"), float("inf")):
            result = summarize_residency([event("tile_submit.lock", 0, value)])
            self.assertEqual(result["invalid_events"], 1)
        result = summarize_residency([event("tile_submit.lock", 0, 1000, owner="old-command"),
                                     event("residency.hold.pin", 0, 1000, 2)])
        self.assertEqual(result["matched_waits"], 0)


if __name__ == "__main__":
    unittest.main()
