"""Generate Pixel Koi's indexed 96 x 96 pond floor deterministically."""
import math
import random
from pathlib import Path

SIZE = 96
PALETTE = (
    (8, 25, 29), (9, 36, 38), (12, 47, 45), (16, 58, 51),
    (19, 69, 59), (24, 80, 68), (31, 91, 76), (38, 102, 84),
    (48, 113, 95), (62, 131, 109), (18, 42, 32), (29, 55, 38),
    (40, 67, 44), (58, 84, 51), (44, 66, 63), (67, 91, 84),
)


def clamp(value, lower=0, upper=1):
    return max(lower, min(upper, value))


def mix(a, b, weight):
    return tuple(a[i] + (b[i] - a[i]) * weight for i in range(3))


def smooth(t):
    t = clamp(t)
    return t * t * (3 - 2 * t)


def rgb565(color):
    r, g, b = color
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


class Noise:
    def __init__(self, rng, spacing):
        self.spacing = spacing
        size = math.ceil(SIZE / spacing) + 2
        self.grid = [[rng.uniform(-1, 1) for _ in range(size)] for _ in range(size)]

    def __call__(self, x, y):
        sx, sy = x / self.spacing, y / self.spacing
        ix, iy = int(sx), int(sy)
        fx, fy = smooth(sx - ix), smooth(sy - iy)
        top = self.grid[iy][ix] * (1 - fx) + self.grid[iy][ix + 1] * fx
        bottom = self.grid[iy + 1][ix] * (1 - fx) + self.grid[iy + 1][ix + 1] * fx
        return top * (1 - fy) + bottom * fy


def sites_for_caustics(rng):
    spacing = 15
    sites = []
    for row in range(-1, 8):
        for col in range(-1, 8):
            sites.append((spacing * (col + rng.uniform(0.16, 0.85)),
                          spacing * (row + rng.uniform(0.16, 0.85))))
    return sites


def caustic(x, y, sites):
    first, second = math.inf, math.inf
    for sx, sy in sites:
        distance = (x - sx) ** 2 + (y - sy) ** 2
        if distance < first:
            first, second = distance, first
        elif distance < second:
            second = distance
    gap = math.sqrt(second) - math.sqrt(first)
    return 1 - smooth(gap / 2.7)


ROCKS = (
    (-2, 10, 17, 13, -0.4), (9, -4, 17, 10, 0.48),
    (4, 39, 12, 18, 0.47), (-3, 56, 11, 14, -0.28),
    (91, 11, 17, 12, 0.5), (98, 29, 13, 14, -0.45),
    (96, 73, 15, 18, 0.33), (86, 93, 18, 12, -0.3),
    (12, 97, 20, 14, 0.23), (44, 98, 17, 10, -0.32),
)

LEAF_GROUPS = (
    (7, 8, 14), (18, 2, 10), (1, 24, 7), (6, 80, 14),
    (19, 93, 11), (89, 88, 18), (95, 63, 9), (89, 5, 11),
)


def prepare_rocks():
    return [(cx, cy, rx, ry, math.cos(angle), math.sin(angle))
            for cx, cy, rx, ry, angle in ROCKS]


def prepare_leaves(rng):
    leaves = []
    for cx, cy, count in LEAF_GROUPS:
        for _ in range(count):
            leaves.append((cx + rng.uniform(-7, 7), cy + rng.uniform(-5, 5),
                           rng.uniform(1.15, 3.4), rng.uniform(0, math.tau),
                           rng.uniform(0.2, 0.95)))
    return leaves


def floor_color(x, y, broad, grain, grit, sites):
    radial = math.hypot((x - 49) / 62, (y - 47) / 61)
    open_water = clamp(0.95 - 0.57 * radial + broad * 0.25 + grain * 0.095)
    color = mix((7, 25, 30), (42, 108, 89), open_water)
    light = caustic(x + broad * 4 + 2 * math.sin(y * .11),
                    y + grain * 2 + 2 * math.cos(x * .12), sites)
    light *= (0.16 + 0.30 * smooth((broad + 0.66) * 0.85)) * (0.38 + open_water * 0.62)
    color = mix(color, (71, 137, 113), light)
    return tuple(channel + grit * 2 for channel in color)


def paint_rocks(color, x, y, broad, grain, grit, rocks):
    for cx, cy, rx, ry, ca, sa in rocks:
        dx, dy = x - cx, y - cy
        if abs(dx) > 27 or abs(dy) > 27:
            continue
        u, v = (dx * ca + dy * sa) / rx, (dy * ca - dx * sa) / ry
        radius = math.hypot(u, v) + broad * .14 + grain * .13 + grit * .044
        if radius >= 1.18:
            continue
        color = mix(color, (9, 29, 29), 0.5 * smooth((1.18 - radius) / .21))
        if radius >= 1.05:
            continue
        facet = clamp(.57 - .25 * u - .36 * v + .19 * grain + .14 * grit)
        rock = mix((16, 34, 34), (71, 96, 87), facet)
        moss = smooth((broad + grain * .65 + grit * .25 + .26) * .8)
        rock = mix(rock, (47, 72, 44), moss * .68)
        color = mix(color, rock, smooth((1.05 - radius) / .18) * .88)
    return color


def paint_leaves(color, x, y, grain, leaves):
    for cx, cy, radius, angle, shade in leaves:
        dx, dy = x - cx, (y - cy) * 1.22
        if abs(dx) > radius + 1 or abs(dy) > radius + 1:
            continue
        distance = math.hypot(dx, dy) / radius
        if distance >= 1.12:
            continue
        if distance > .3 and abs(math.remainder(math.atan2(dy, dx) - angle, math.tau)) < .26:
            continue
        pad = mix((21, 45, 31), (70, 91, 52),
                  clamp(shade - (dx + dy) * .055 + grain * .12))
        color = mix(color, pad, smooth((1.12 - distance) / .33))
    return color


def generate():
    rng = random.Random(27117)
    broad = Noise(rng, 16)
    grain = Noise(rng, 5)
    grit = Noise(rng, 1)
    sites = sites_for_caustics(rng)
    rocks, leaves = prepare_rocks(), prepare_leaves(rng)
    packed = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            px, py = x + .5, y + .5
            low, mid, high = broad(px, py), grain(px, py), grit(px, py)
            color = floor_color(px, py, low, mid, high, sites)
            color = paint_rocks(color, px, py, low, mid, high, rocks)
            color = paint_leaves(color, px, py, mid, leaves)
            row.append(min(range(len(PALETTE)),
                           key=lambda i: sum((color[c] - PALETTE[i][c]) ** 2 for c in range(3))))
        packed.extend(row[x] | (row[x + 1] << 4) for x in range(0, SIZE, 2))
    return packed



# The nearer bough enters at the upper left. The other barely reaches the
# water from the right. The clusters list center, horizontal and vertical spread.
REFLECTION_BRANCHES = (
    ((-5, 29, 2.4), (9, 29, 1.9), (18, 25, 1.4), (27, 22, 0.9),
     (33, 17, 0.6), (42, 15, 0.3)),
    ((11, 28, 1.2), (15, 20, 0.9), (20, 15, 0.6), (21, 9, 0.3)),
    ((18, 25, 0.9), (25, 29, 0.7), (33, 31, 0.5), (40, 29, 0.3)),
    ((26, 22, 0.7), (29, 13, 0.5), (35, 9, 0.3)),
    ((32, 17, 0.5), (40, 20, 0.4), (45, 19, 0.25)),
    ((9, 29, 1.2), (13, 35, 0.8), (17, 40, 0.5), (23, 42, 0.3)),
    ((98, 68, 1.6), (90, 65, 1.3), (85, 60, 0.9), (78, 58, 0.6), (72, 55, 0.3)),
    ((89, 65, 0.9), (83, 68, 0.6), (76, 69, 0.3)),
    ((85, 60, 0.7), (88, 54, 0.5), (86, 51, 0.25)),
)

REFLECTION_CLUSTERS = (
    (13, 16, 4.8, 3.4, 12), (21, 11, 4.2, 4.6, 15), (24, 17, 4.0, 3.6, 11),
    (31, 11, 4.4, 3.2, 12), (36, 9, 3.4, 3.6, 10), (40, 15, 5.3, 3.0, 16),
    (43, 21, 4.0, 3.1, 12), (33, 25, 3.3, 3.1, 10), (38, 30, 5.4, 3.8, 15),
    (29, 33, 3.1, 2.8, 8), (18, 38, 3.0, 3.0, 9), (23, 43, 3.3, 3.2, 10),
    (73, 54, 4.7, 3.0, 13), (81, 56, 3.6, 3.3, 10), (87, 51, 3.8, 3.3, 11),
    (78, 69, 4.3, 3.1, 12), (88, 70, 3.9, 3.2, 11),
)


def blend_reflection(mask, left, top, right, bottom, coverage):
    for y in range(max(0, math.floor(top)), min(SIZE, math.ceil(bottom))):
        for x in range(max(0, math.floor(left)), min(SIZE, math.ceil(right))):
            # A quiet surface still breaks the straight edges of the canopy.
            wx = x + 0.5 + 0.78 * math.sin(y * .74) + .42 * math.sin(y * 1.7)
            wy = y + 0.5 + 0.48 * math.sin(x * .51)
            strength = coverage(wx, wy)
            if strength > 0:
                previous = mask[y][x]
                mask[y][x] = previous + (1 - previous) * strength


def reflection_mask():
    mask = [[0.0] * SIZE for _ in range(SIZE)]
    for branch in REFLECTION_BRANCHES:
        for (ax, ay, start), (bx, by, end) in zip(branch, branch[1:]):
            dx, dy = bx - ax, by - ay
            length2 = dx * dx + dy * dy

            def branch_coverage(x, y):
                t = clamp(((x - ax) * dx + (y - ay) * dy) / length2)
                radius = start + (end - start) * t
                distance = math.hypot(x - (ax + dx * t), y - (ay + dy * t))
                return .82 * smooth((radius + .64 - distance) / 1.05)

            margin = max(start, end) + 2
            blend_reflection(mask, min(ax, bx) - margin, min(ay, by) - margin,
                             max(ax, bx) + margin, max(ay, by) + margin, branch_coverage)

    rng = random.Random(78215)
    for cx, cy, spread_x, spread_y, count in REFLECTION_CLUSTERS:
        for _ in range(count):
            direction = rng.uniform(0, math.tau)
            distance = math.sqrt(rng.random())
            leaf_x = cx + math.cos(direction) * spread_x * distance
            leaf_y = cy + math.sin(direction) * spread_y * distance
            angle = direction + rng.uniform(-.9, .9)
            ca, sa = math.cos(angle), math.sin(angle)
            length = rng.uniform(.85, 2.5)
            width = length * rng.uniform(.36, .62)
            strength = rng.uniform(.34, .68)

            def leaf_coverage(x, y):
                dx, dy = x - leaf_x, y - leaf_y
                u = (dx * ca + dy * sa) / length
                v = (dy * ca - dx * sa) / width
                return strength * smooth((1.38 - math.hypot(u, v)) / .72)

            margin = length * 1.4 + 2
            blend_reflection(mask, leaf_x - margin, leaf_y - margin,
                             leaf_x + margin, leaf_y + margin, leaf_coverage)
    return mask


def reflection_texture(packed):
    shade_palette = [rgb565(tuple(map(round, mix(color, (5, 19, 24), amount))))
                     for amount in (0, .14, .25, .38) for color in PALETTE]
    mask = reflection_mask()
    cells = []
    for y in range(SIZE):
        for x in range(SIZE):
            index = (packed[y * (SIZE // 2) + x // 2] >> (4 * (x & 1))) & 15
            weight = mask[y][x]
            # Overhangs reflect in the water and fade where rocks emerge.
            if index >= 10:
                weight *= .25
            grade = sum(weight >= threshold for threshold in (.16, .36, .68))
            cells.append(grade * len(PALETTE) + index)
    return shade_palette, cells


def write_reflection_header(path, packed):
    colors, cells = reflection_texture(packed)
    lines = [
        "/* Generated by make_pond.py. Sunny native water with tree reflections. */",
        "#pragma once",
        "",
        "static const uint64_t water_reflection_colors[64] = {",
    ]
    palette = [color * 0x0001000100010001 for color in colors]
    for i in range(0, len(palette), 4):
        lines.append("    " + ", ".join(f"UINT64_C(0x{x:016x})" for x in palette[i:i + 4]) + ",")
    lines.extend(("};", "", "static const uint8_t water_reflection_map[WATER_MAP_SIZE * WATER_MAP_SIZE] = {"))
    for i in range(0, len(cells), 16):
        lines.append("    " + ", ".join(f"0x{x:02x}" for x in cells[i:i + 16]) + ",")
    lines.extend(("};", ""))
    path.write_text("\n".join(lines), encoding="utf-8")

def write_header(path, packed):
    palette = [rgb565(color) * 0x0001000100010001 for color in PALETTE]
    lines = [
        "/* Generated by make_pond.py. Each byte indexes two adjacent water cells. */",
        "#pragma once",
        "",
        f"enum {{ WATER_MAP_SIZE = {SIZE} }};",
        "static const uint64_t water_colors[16] = {",
    ]
    for i in range(0, len(palette), 4):
        lines.append("    " + ", ".join(f"UINT64_C(0x{x:016x})" for x in palette[i:i + 4]) + ",")
    lines.extend(("};", "", "static const uint8_t water_map[WATER_MAP_SIZE * WATER_MAP_SIZE / 2] = {"))
    for i in range(0, len(packed), 16):
        lines.append("    " + ", ".join(f"0x{x:02x}" for x in packed[i:i + 16]) + ",")
    lines.extend(("};", ""))
    path.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    root = Path(__file__).resolve().parent
    packed = generate()
    write_header(root / "pixel-fish" / "water_texture.h", packed)
    write_reflection_header(root / "pixel-fish" / "water_reflection.h", packed)
