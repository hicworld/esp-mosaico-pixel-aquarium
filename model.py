"""Parametric FDM enclosure for a stock, fully assembled ESP-Mosaico.

Run with CadQuery 2.6: python model.py
Assembly axes: X left to right, Y front to rear, Z up. Dimensions in mm.
"""

from math import sqrt
from pathlib import Path

import cadquery as cq


# Envelope and optical dimensions
WIDTH, DEPTH = 76.0, 74.0
DECK, WALL_SEAT, BOX_TOP = 3.2, 24.0, 79.0
FRONT_LIP = 29.4
SIDE_OUT, SIDE_T = 3.0, 2.8
SIDE_IN = SIDE_OUT + SIDE_T
BACK_Y, BACK_T = 70.5, 2.4
# Espressif display datasheet H0216F002AMT004-1, active area on pp. 4–5.
ACTIVE_SCREEN = 38.39
REFLECTOR_W, REFLECTOR_L = 54.0, 68.0
REFLECTOR_OPEN_W, REFLECTOR_OPEN_L = 46.0, 60.0
FRAME_T = 1.6
SHEET_W, SHEET_L, SHEET_MAX_T = 51.0, 65.0, 1.0
MIRROR_Z = 51.0
RAIL_GAP = 4.65
RAIL_SIDE_CLEARANCE = 0.40
RAIL_LIP = 3.2

# The stock enclosure is placed with USB-C pointing right. The factory LCD
# drawing describes only the screen, so this is an adjustable device pocket.
FRONT_GUIDE_Y, REAR_GUIDE_Y = 6.6, 63.2
DRAWER_CLEARANCE = 0.4
DRAWER_X = 7.4
DRAWER_Y = FRONT_GUIDE_Y + DRAWER_CLEARANCE
DRAWER_D = REAR_GUIDE_Y - DRAWER_CLEARANCE - DRAWER_Y
DRAWER_END = 75.2
DRAWER_T = 2.4
POCKET_W, POCKET_D = 54.0, DRAWER_D - 3.2
MAX_BODY = (53.0, 51.5, 17.0)
MIRROR_Y = DRAWER_Y + DRAWER_D / 2
TAB_DEPTH, TAB_CLEARANCE = 5.6, 0.45
SIDE_KEYS = ((3.3, 3.6), (66.5, 5.0))
BACK_KEYS = (22.0, 54.0)
BACK_KEY_W = 7.0
LID_T = 2.4
ROOT = Path(__file__).resolve().parent


def box(x0, x1, y0, y1, z0, z1):
    return cq.Solid.makeBox(x1 - x0, y1 - y0, z1 - z0, cq.Vector(x0, y0, z0))


def prism(points, z0, z1):
    return cq.Workplane("XY").polyline(points).close().extrude(z1 - z0).val().translate((0, 0, z0))


def optical_rib(s0, s1, n0, n1, height0, height1):
    # The ribs print on the side panel's outside face. The local XY plane is
    # the enclosure's side view; s runs down the 45 degree slope.
    points = [
        (MIRROR_Y + (s + n) / sqrt(2), MIRROR_Z + (n - s) / sqrt(2))
        for s, n in ((s0, n0), (s1, n0), (s1, n1), (s0, n1))
    ]
    rib = prism(points, height0, height1)
    return rib.intersect(box(0.8, DEPTH - 0.8, WALL_SEAT, BOX_TOP - 0.6, height0 - 1, height1 + 1))


def build_base():
    floor = box(0, WIDTH, 0, DEPTH, 0, DECK)
    front = box(0, WIDTH, 0, FRONT_GUIDE_Y, DECK - 0.2, WALL_SEAT)
    rear = box(0, WIDTH, REAR_GUIDE_Y, DEPTH, DECK - 0.2, WALL_SEAT)
    end_stop = box(0, DRAWER_X, FRONT_GUIDE_Y - 0.2, REAR_GUIDE_Y + 0.2, DECK - 0.2, WALL_SEAT)
    shade = box(SIDE_IN + 0.40, WIDTH - SIDE_IN - 0.40, 0, 3.7, WALL_SEAT - 0.2, FRONT_LIP)
    base = floor.fuse(front, rear, end_stop, shade).clean()

    pockets = []
    pocket_bottom = WALL_SEAT - TAB_DEPTH - 0.4
    for x in (SIDE_OUT + SIDE_T / 2, WIDTH - SIDE_OUT - SIDE_T / 2):
        for y, length in SIDE_KEYS:
            sx = (SIDE_T + TAB_CLEARANCE) / 2
            sy = (length + TAB_CLEARANCE) / 2
            pockets.append(box(x - sx, x + sx, y - sy, y + sy, pocket_bottom, WALL_SEAT + 1))
    for x in BACK_KEYS:
        sx = (BACK_KEY_W + TAB_CLEARANCE) / 2
        sy = (BACK_T + TAB_CLEARANCE) / 2
        pockets.append(box(x - sx, x + sx, BACK_Y - sy, BACK_Y + sy, pocket_bottom, WALL_SEAT + 1))
    return base.cut(*pockets).clean()


def build_drawer():
    length = DRAWER_END - DRAWER_X
    cx = WIDTH / 2 - DRAWER_X
    cy = DRAWER_D / 2
    x0, x1 = cx - POCKET_W / 2, cx + POCKET_W / 2
    rib_t = (DRAWER_D - POCKET_D) / 2
    wall_top = DRAWER_T + 2.2
    tray = box(0, length, 0, DRAWER_D, 0, DRAWER_T)
    handle = cq.Workplane("XY").box(10.5, 14, DRAWER_T, centered=(False, True, False)).edges("|Z").fillet(2).val()
    handle = handle.translate((length - 2.3, 12.0, 0))
    rails = [
        box(x0 - rib_t, x1 + rib_t, 0, rib_t, DRAWER_T - 0.2, wall_top),
        box(x0 - rib_t, x1 + rib_t, DRAWER_D - rib_t, DRAWER_D, DRAWER_T - 0.2, wall_top),
        box(x0 - rib_t, x0, 0, DRAWER_D, DRAWER_T - 0.2, wall_top),
        box(x1, x1 + rib_t, 0, cy - 12, DRAWER_T - 0.2, wall_top),
        box(x1, x1 + rib_t, cy + 12, DRAWER_D, DRAWER_T - 0.2, wall_top),
    ]
    tray = tray.fuse(handle, *rails).clean()
    # Recessed alignment marks leave the landing flat.
    marks = [
        box(cx - 0.35, cx + 0.35, cy - 8, cy + 8, DRAWER_T - 0.28, DRAWER_T + 0.2),
        box(cx - 8, cx + 8, cy - 0.35, cy + 0.35, DRAWER_T - 0.28, DRAWER_T + 0.2),
    ]
    return tray.cut(*marks).clean()


def build_side():
    side = box(0.8, DEPTH - 0.8, WALL_SEAT, BOX_TOP, 0, SIDE_T)
    keys = [
        box(y - length / 2, y + length / 2, WALL_SEAT - TAB_DEPTH, WALL_SEAT + 0.25, 0, SIDE_T)
        for y, length in SIDE_KEYS
    ]
    cheek = (WIDTH - REFLECTOR_W) / 2 - SIDE_IN - RAIL_SIDE_CLEARANCE
    n = RAIL_GAP / 2
    thickness = 2.0
    tongue_end = SIDE_T + cheek + RAIL_LIP
    ribs = [
        optical_rib(-31, REFLECTOR_L / 2 + 2.4, -n - thickness, n + thickness, SIDE_T - 0.15, SIDE_T + cheek),
        optical_rib(-31, REFLECTOR_L / 2 + 2.4, -n - thickness, -n, SIDE_T + cheek - 0.15, tongue_end),
        optical_rib(-31, REFLECTOR_L / 2 + 2.4, n, n + thickness, SIDE_T + cheek - 0.15, tongue_end),
        optical_rib(REFLECTOR_L / 2 + 0.25, REFLECTOR_L / 2 + 2.4, -n - thickness, n + thickness, SIDE_T + cheek - 0.15, tongue_end),
    ]
    return side.fuse(*keys, *ribs).clean()


def build_back():
    wall = box(SIDE_IN + 0.2, WIDTH - SIDE_IN - 0.2, WALL_SEAT, BOX_TOP, 0, BACK_T)
    tabs = [
        box(x - BACK_KEY_W / 2, x + BACK_KEY_W / 2, WALL_SEAT - TAB_DEPTH, WALL_SEAT + 0.25, 0, BACK_T)
        for x in BACK_KEYS
    ]
    return wall.fuse(*tabs).clean()


def build_lid():
    lid = box(0, WIDTH, 0, DEPTH, 0, LID_T)
    # Printed exterior down; locator pads face into the assembled enclosure.
    pads = []
    for x0, x1 in ((SIDE_IN + 0.35, SIDE_IN + 4.1), (WIDTH - SIDE_IN - 4.1, WIDTH - SIDE_IN - 0.35)):
        for y in (18, 46):
            pads.append(box(x0, x1, y, y + 8, LID_T - 0.15, LID_T + 2.2))
    for x0, x1 in ((14, 24), (52, 62)):
        back_front = DEPTH - (BACK_Y - BACK_T / 2)
        back_rear = DEPTH - (BACK_Y + BACK_T / 2)
        pads.append(box(x0, x1, back_front + 0.35, back_front + 3, LID_T - 0.15, LID_T + 2.2))
        pads.append(box(x0, x1, 0, back_rear - 0.35, LID_T - 0.15, LID_T + 2.2))
    return lid.fuse(*pads).clean()


def build_reflector_frame():
    frame = box(-REFLECTOR_W / 2, REFLECTOR_W / 2, -REFLECTOR_L / 2, REFLECTOR_L / 2, 0, FRAME_T)
    opening = box(-REFLECTOR_OPEN_W / 2, REFLECTOR_OPEN_W / 2, -REFLECTOR_OPEN_L / 2, REFLECTOR_OPEN_L / 2, -1, FRAME_T + 1)
    return frame.cut(opening).clean()


def build_positioner():
    # Four low, movable corners fixed with tape after centering the host.
    return box(0, 7, 0, 1.6, 0, 2.4).fuse(box(0, 1.6, 0, 7, 0, 2.4)).clean()


def parts():
    side = build_side()
    right = side.mirror("YZ", (DEPTH / 2, 0, 0))
    return {
        "01_base": build_base(),
        "02_drawer": build_drawer(),
        "03_left_wall": side,
        "04_right_wall": right,
        "05_back_wall": build_back(),
        "06_lid": build_lid(),
        "07_reflector_frame_print_twice": build_reflector_frame(),
        "08_positioner_print_four": build_positioner(),
    }


def assembly(models, drawer_extension=0, sheet_t=0.4):
    left = models["03_left_wall"].rotate((0, 0, 0), (1, 1, 1), 120).translate((SIDE_OUT, 0, 0))
    right_local = models["04_right_wall"]
    right = right_local.rotate((0, 0, 0), (1, 1, 1), 120).translate((SIDE_OUT, 0, 0))
    right = right.rotate((WIDTH / 2, DEPTH / 2, 0), (WIDTH / 2, DEPTH / 2, 1), 180)
    back = models["05_back_wall"].rotate((0, 0, 0), (1, 0, 0), 90).translate((0, BACK_Y + BACK_T / 2, 0))
    lid = models["06_lid"].rotate((0, 0, 0), (1, 0, 0), 180).translate((0, DEPTH, BOX_TOP + LID_T))
    drawer = models["02_drawer"].translate((DRAWER_X + drawer_extension, DRAWER_Y, DECK))

    def at_angle(shape, normal_offset):
        return shape.translate((0, 0, normal_offset)).rotate((0, 0, 0), (1, 0, 0), -45).translate((WIDTH / 2, MIRROR_Y, MIRROR_Z))

    ring = models["07_reflector_frame_print_twice"]
    sheet = box(-SHEET_W / 2, SHEET_W / 2, -SHEET_L / 2, SHEET_L / 2, 0, sheet_t)
    return {
        "base": models["01_base"],
        "drawer": drawer,
        "left": left,
        "right": right,
        "back": back,
        "lid": lid,
        "frame_lower": at_angle(ring, -FRAME_T - sheet_t / 2),
        "film": at_angle(sheet, -sheet_t / 2),
        "frame_upper": at_angle(ring, sheet_t / 2),
    }


def validate(models):
    assert 2 * FRAME_T + SHEET_MAX_T < RAIL_GAP
    assert REFLECTOR_OPEN_L / sqrt(2) > ACTIVE_SCREEN
    assert REFLECTOR_OPEN_W > ACTIVE_SCREEN
    assert MAX_BODY[0] < POCKET_W and MAX_BODY[1] < POCKET_D
    assert DECK + DRAWER_T + MAX_BODY[2] < WALL_SEAT
    for name, solid in models.items():
        if not solid.isValid() or len(solid.Solids()) != 1 or abs(solid.BoundingBox().zmin) > 1e-6:
            raise ValueError(f"Invalid or incorrectly oriented printable solid: {name}")

    assembled = assembly(models, sheet_t=SHEET_MAX_T)
    for i, (first, shape) in enumerate(assembled.items()):
        for second, other in list(assembled.items())[i + 1:]:
            if shape.intersect(other).Volume() > 0.03:
                raise ValueError(f"Interference between {first} and {second}")
    drawer_top = DECK + models["02_drawer"].BoundingBox().zmax
    drawer_sweep = box(DRAWER_X, WIDTH * 2, DRAWER_Y, DRAWER_Y + DRAWER_D, DECK, drawer_top)
    body_x, body_y, body_z = MAX_BODY
    body_sweep = box(
        (WIDTH - body_x) / 2, WIDTH * 2,
        MIRROR_Y - body_y / 2, MIRROR_Y + body_y / 2,
        DECK + DRAWER_T, DECK + DRAWER_T + body_z,
    )
    for name in ("base", "right", "frame_lower"):
        for part, swept in (("drawer", drawer_sweep), ("device", body_sweep)):
            if swept.intersect(assembled[name]).Volume() > 0.03:
                raise ValueError(f"{part} sweep obstructed by {name}")


def export():
    models = parts()
    validate(models)
    output = ROOT / "STL"
    output.mkdir(exist_ok=True)
    for name, solid in models.items():
        bounds = solid.BoundingBox()
        printable = solid.translate((-bounds.xmin, -bounds.ymin, -bounds.zmin))
        cq.exporters.export(printable, str(output / f"{name}.stl"), tolerance=0.02, angularTolerance=0.08)
        print(f"{name}   {bounds.xlen:.2f} x {bounds.ylen:.2f} x {bounds.zlen:.2f} mm")


if __name__ == "__main__":
    export()
