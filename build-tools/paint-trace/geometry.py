# SPDX-FileCopyrightText: 2026 Krita contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Conservative image-space rectangle checks, not pixel/presentation validation."""
import collections


def rectangle(value):
    if not isinstance(value, list) or len(value) != 4 or any(type(n) is not int for n in value):
        raise ValueError("Invalid trace rectangle")
    x, y, width, height = value
    return (x, y, x + width, y + height) if width > 0 and height > 0 else None


def coverage(expected, covering, budget=100000):
    """Subtract rectangles exactly, retaining holes; use half-open boundaries."""
    work = 0
    gaps = []
    if len(expected) > 1024 or len(covering) > 1024:
        return "unverified_complexity"
    for target in expected:
        pieces = [target] if target else []
        for cover in covering:
            if not cover or not pieces:
                continue
            remaining = []
            for x1, y1, x2, y2 in pieces:
                work += 1
                if work > budget:
                    return "unverified_complexity"
                left, top = max(x1, cover[0]), max(y1, cover[1])
                right, bottom = min(x2, cover[2]), min(y2, cover[3])
                if left >= right or top >= bottom:
                    remaining.append((x1, y1, x2, y2))
                    continue
                if x1 < left:
                    remaining.append((x1, y1, left, y2))
                if right < x2:
                    remaining.append((right, y1, x2, y2))
                if y1 < top:
                    remaining.append((left, y1, right, top))
                if bottom < y2:
                    remaining.append((left, bottom, right, y2))
            pieces = remaining
        gaps.extend(pieces)
        if len(gaps) > 1024:
            return "unverified_complexity"
    return "gap" if gaps else "covered"


def summarize_geometry(events):
    rects = collections.defaultdict(lambda: collections.defaultdict(list))
    requests, updates, executed = {}, {}, set()
    edges = collections.defaultdict(set)
    rect_names = {"projection.request_rect", "projection.executed_request_rect",
                  "projection.change_rect", "update.request_rect"}
    for event in events:
        name, args = event["name"], event.get("args", {})
        identity, parent = args.get("id"), args.get("parent")
        if not identity or identity == "0":
            continue
        if name in rect_names:
            lod = args.get("lod")
            if type(lod) is not int or lod < 0:
                raise ValueError("Invalid rectangle LOD")
            rects[name][identity].append((args.get("owner"), lod, rectangle(args.get("rect"))))
        elif name == "projection.request":
            requests[identity] = (args.get("owner"), parent)
        elif name == "update.ready":
            updates[identity] = (args.get("owner"), parent)
        elif name == "projection.merge":
            executed.add(identity)
        elif name in ("projection.walker_request", "projection.walker_merged"):
            if parent and parent != "0":
                edges[identity].add(parent)
    for identity, (owner, parent) in requests.items():
        if parent in requests and owner == requests[parent][0]:
            edges[parent].add(identity)

    def descendants(identity):
        seen, pending = set(), [identity]
        while pending:
            node = pending.pop()
            if node in seen:
                continue
            seen.add(node)
            pending.extend(edges.get(node, ()))
        return seen

    def compare(expected, actual, owner):
        if not expected:
            return "unverified_missing_rectangles"
        if owner in (None, "0") or any(row[0] != owner for row in expected + actual):
            return "unverified_owner"
        if any(row[1] != 0 for row in expected + actual):
            return "unverified_lod"
        if not any(row[2] for row in expected):
            return "empty"
        if not actual:
            return "unverified_missing_rectangles"
        return coverage([row[2] for row in expected], [row[2] for row in actual])

    request_rows = []
    for identity, (owner, _) in requests.items():
        actual = [row for node in descendants(identity) & executed
                  for row in rects["projection.executed_request_rect"].get(node, ())]
        status = compare(rects["projection.request_rect"].get(identity, []), actual, owner)
        request_rows.append({"request": identity, "status": status})

    by_walker = collections.defaultdict(lambda: collections.defaultdict(list))
    for identity, (canvas, walker) in updates.items():
        if walker in executed:
            by_walker[walker][canvas].append(identity)
    canvas_rows = []
    for walker in executed:
        expected = rects["projection.change_rect"].get(walker, [])
        if not by_walker[walker]:
            canvas_rows.append({"walker": walker, "canvas": None, "status": "unverified_no_canvas_link"})
        for canvas, identities in by_walker[walker].items():
            # Expected owner is a node, actual owner a canvas. Compare geometry
            # only after checking each update's recorded canvas separately.
            actual = [row for identity in identities for row in rects["update.request_rect"].get(identity, ())]
            if any(not rects["update.request_rect"].get(identity) for identity in identities):
                status = "unverified_missing_rectangles"
            else:
                normalized_expected = [(canvas, lod, rect) for _, lod, rect in expected]
                status = compare(normalized_expected, actual, canvas)
            canvas_rows.append({"walker": walker, "canvas": canvas, "status": status})
    return {
        "request_counts": dict(collections.Counter(row["status"] for row in request_rows)),
        "canvas_notification_counts": dict(collections.Counter(row["status"] for row in canvas_rows)),
        "requests": request_rows, "canvas_notifications": canvas_rows,
        "transfers": summarize_transfers(events),
        "bridges": summarize_bridges(events),
        "limitations": "LOD 0 declared image-space rectangles only. Requested walker area does not prove pixel writes; canvas notification does not prove texture upload or visible presentation. Views are checked separately. Missing data and nonzero LOD are unverified, not successful. No input-to-pixel latency.",
    }


def summarize_bridges(events):
    """Check dirty submission and update compression across their explicit edges."""
    rects = collections.defaultdict(lambda: collections.defaultdict(list))
    dirty, requests, updates, uploads = set(), {}, {}, {}
    widget_canvas, replacement_pairs = {}, []
    names = {"dirty.source_rect", "dirty.submitted_rect", "projection.request_rect",
             "update.request_rect", "update.upload_expected_rect", "update.upload_bounds_rect"}
    for event in events:
        name, args = event["name"], event.get("args", {})
        identity, parent, owner = args.get("id"), args.get("parent"), args.get("owner")
        if name == "canvas.created":
            widget_canvas[args.get("related")] = owner
        if not identity or identity == "0":
            continue
        if name in names:
            lod = args.get("lod")
            if type(lod) is not int or lod < 0:
                raise ValueError("Invalid bridge rectangle LOD")
            rects[name][identity].append((owner, lod, rectangle(args.get("rect"))))
        elif name == "dirty.dispatch":
            dirty.add(identity)
        elif name == "projection.request":
            requests[identity] = parent
        elif name == "update.ready":
            updates[identity] = owner
        elif name == "update.upload_issued":
            uploads[identity] = (owner, parent)
        elif name in ("update.merged", "update.superseded"):
            replacement_pairs.append((identity, parent))

    def compare(expected, actual, owner):
        if not expected or not actual:
            return "unverified_missing_rectangles"
        if owner in (None, "0") or any(row[0] != owner for row in expected + actual):
            return "unverified_owner"
        if any(row[1] != 0 for row in expected + actual):
            return "unverified_lod"
        if not any(row[2] for row in expected):
            return "empty"
        return coverage([row[2] for row in expected], [row[2] for row in actual])

    by_dirty = collections.defaultdict(list)
    for request, source in requests.items():
        by_dirty[source].extend(rects["projection.request_rect"].get(request, []))
    dirty_rows = []
    for identity in dirty:
        source = rects["dirty.source_rect"].get(identity, [])
        submitted = rects["dirty.submitted_rect"].get(identity, [])
        owner = source[0][0] if source else None
        dirty_rows.append({"dirty": identity,
                           "source_to_submission": compare(source, submitted, owner),
                           "submission_to_projection": compare(submitted, by_dirty[identity], owner)})

    edges = collections.defaultdict(set)
    for source, target in replacement_pairs:
        if source in updates and target in updates and updates[source] not in (None, "0") and updates[source] == updates[target]:
            edges[source].add(target)
    for upload, (_, parent) in uploads.items():
        if parent in updates:
            edges[parent].add(upload)

    def descendants(identity):
        seen, pending = set(), [identity]
        while pending:
            node = pending.pop()
            if node in seen:
                continue
            seen.add(node)
            pending.extend(edges.get(node, ()))
        return seen

    def update_status(identity, canvas):
        expected = rects["update.request_rect"].get(identity, [])
        targets = descendants(identity) & uploads.keys()
        if not targets:
            return "unverified_no_upload"
        actual, bounds = [], set()
        for upload in targets:
            widget = uploads[upload][0]
            if canvas in (None, "0") or widget_canvas.get(widget) != canvas:
                return "unverified_owner"
            image_bounds = rects["update.upload_bounds_rect"].get(upload, [])
            upload_rect = rects["update.upload_expected_rect"].get(upload, [])
            if len(image_bounds) != 1 or len(upload_rect) != 1 or not image_bounds[0][2]:
                return "unverified_missing_rectangles"
            if any(row[0] != widget for row in image_bounds + upload_rect):
                return "unverified_owner"
            if any(row[1] != 0 for row in image_bounds + upload_rect):
                return "unverified_lod"
            bounds.add(image_bounds[0][2])
            actual.append((canvas, 0, upload_rect[0][2]))
        if len(bounds) != 1:
            return "unverified_image_bounds_change"
        x1, y1, x2, y2 = bounds.pop()
        clipped = []
        for owner, lod, rect in expected:
            if rect:
                a, b, c, d = max(x1, rect[0]), max(y1, rect[1]), min(x2, rect[2]), min(y2, rect[3])
                rect = (a, b, c, d) if a < c and b < d else None
            clipped.append((owner, lod, rect))
        result = compare(clipped, actual, canvas)
        return "outside_image" if result == "empty" else result

    update_rows = [{"update": identity, "status": update_status(identity, canvas)} for identity, canvas in updates.items()]
    return {"dirty_groups": dirty_rows, "compressed_updates": update_rows,
            "dirty_source_counts": dict(collections.Counter(row["source_to_submission"] for row in dirty_rows)),
            "dirty_projection_counts": dict(collections.Counter(row["submission_to_projection"] for row in dirty_rows)),
            "compressed_update_counts": dict(collections.Counter(row["status"] for row in update_rows)),
            "limitations": "Recorded LOD-0 dirty groups and same-canvas update replacement chains only. Image bounds must agree across descendant uploads. Outside-image work is excluded; missing metadata is unverified. No pixel survival or physical presentation claim."}


def summarize_transfers(events):
    """Combine patch geometry with explicit same-widget frame acknowledgments."""
    names = {"update.upload_expected_rect", "update.widget_expected_rect", "update.tracked_widget_rect",
             "update.upload_patch_rect", "update.widget_patch_rect"}
    rectangles = collections.defaultdict(lambda: collections.defaultdict(list))
    uploads, frames, swaps, replacements, acknowledgments = {}, {}, {}, {}, {}
    unsupported, resets = set(), collections.defaultdict(list)
    for event in events:
        name, args = event["name"], event.get("args", {})
        owner, identity, parent = args.get("owner"), args.get("id"), args.get("parent")
        key = (owner, identity)
        if name == "frame.reset":
            resets[owner].append(event["ts"])
        if not identity or identity == "0":
            continue
        if name in names:
            lod = args.get("lod")
            if type(lod) is not int or lod < 0:
                raise ValueError("Invalid transfer rectangle LOD")
            rectangles[key][name].append((lod, rectangle(args.get("rect"))))
        elif name == "update.upload_issued":
            uploads[key] = event["ts"]
        elif name == "frame.submitted":
            frames[key] = event["ts"]
        elif name == "frame.swapped":
            swaps[key] = event["ts"]
        elif name == "frame.replaced":
            replacements[key] = (owner, parent)
        elif name == "frame.covered_upload":
            acknowledgments[key] = (owner, parent)
        elif name == "update.geometry_unsupported":
            unsupported.add(key)

    def swapped_at(key):
        frame = acknowledgments.get(key)
        seen = set()
        while frame in frames:
            if frame in seen:
                raise ValueError("Cyclic transfer frame lineage")
            seen.add(frame)
            if frames[frame] < uploads[key]:
                return None
            if frame in swaps:
                return swaps[frame] if swaps[frame] >= frames[frame] else None
            frame = replacements.get(frame)
        return None

    rows = []
    for key, timestamp in uploads.items():
        data = rectangles[key]
        status = "covered_to_swapped_commands"
        end = swapped_at(key)
        if key[0] in (None, "0"):
            status = "unverified_owner"
        elif key in unsupported:
            status = "unverified_mapping"
        elif any(timestamp < reset and (end is None or reset <= end) for reset in resets[key[0]]):
            status = "unverified_view_change"
        elif any(len(data[name]) != 1 for name in
                 ("update.upload_expected_rect", "update.widget_expected_rect", "update.tracked_widget_rect")):
            status = "unverified_missing_rectangles"
        elif any(lod != 0 for records in data.values() for lod, _ in records):
            status = "unverified_lod"
        else:
            expected = data["update.upload_expected_rect"][0][1]
            visible = data["update.widget_expected_rect"][0][1]
            tracked = data["update.tracked_widget_rect"][0][1]
            if not expected:
                status = "outside_image"
            elif not data["update.upload_patch_rect"]:
                status = "unverified_missing_patches"
            else:
                image_check = coverage([expected], [rect for _, rect in data["update.upload_patch_rect"]])
                widget_check = coverage([visible], [rect for _, rect in data["update.widget_patch_rect"]])
                tracking_check = coverage([visible], [tracked])
                if image_check != "covered":
                    status = "image_gap" if image_check == "gap" else image_check
                elif not visible:
                    status = "outside_view"
                elif not data["update.widget_patch_rect"]:
                    status = "unverified_missing_patches"
                elif widget_check != "covered":
                    status = "widget_gap" if widget_check == "gap" else widget_check
                elif tracking_check != "covered":
                    status = "tracking_gap" if tracking_check == "gap" else tracking_check
                elif end is None:
                    status = "unverified_no_swapped_coverage"
        rows.append({"upload": key[1], "widget": key[0], "status": status,
                     "swapped_at_us": end if status == "covered_to_swapped_commands" else None})
    return {"counts": dict(collections.Counter(row["status"] for row in rows)), "uploads": rows,
            "limitations": "Declared LOD-0 upload patches, axis-aligned widget mapping and Qt command coverage only. Offscreen work is excluded, not displayed. Does not prove GPU completion, pixel survival, physical scanout or full input coverage."}
