"""Build the original top-view koi textures used by Pixel Koi.

Run with Python and Pillow: python make_textures.py
The generated header is checked in so rebuilding the app needs no art tools.
"""
from math import cos, exp, pi, sin, sqrt
from pathlib import Path

from PIL import Image, ImageDraw

WIDTH, HEIGHT = 104, 44
CENTER_Y = HEIGHT / 2
SUPERSAMPLE = 4

# Radius of the trunk from the tail root to the rounded head.
PROFILE = (
    (23, 0), (27, 1.9), (34, 2.8), (44, 4.8), (54, 7.3),
    (65, 9.8), (74, 10.9), (82, 10.6), (88, 9.3),
    (94, 7.9), (97, 6.5), (99, 4.9), (101, 0),
)

# Each fin starts with its root and follows the outer edge back to the body.
FINS = (
    (((29, 0), (19, -3), (12, -8), (2, -11), (6, -5), (12, -0.2),
      (7, 4), (3, 11), (12, 8), (20, 3)), (28, 0)),
    (((65, -6), (59, -11), (48, -13.2), (49, -10), (55, -5)), (64, -6)),
    (((63, 6), (55, 11), (45, 12), (47, 9), (54, 5)), (63, 6)),
    (((83, -7.1), (78, -12.8), (68, -17.3), (61, -17.9),
      (61.2, -15), (68, -10), (73, -6)), (82, -7)),
    (((80.8, 7.2), (76, 12.9), (68, 17.7), (61, 18.5),
      (61.7, 15.3), (68, 10.9), (72, 6)), (80, 7)),
)

# Kohaku and Sanke markings follow the body surface. In Sanke, the charcoal
# marks cross some of the red islands and leave the face mostly white.
RED_PATCHES = (
    (
        ((97, -3), (94, -6), (88, -5.2), (83, -1), (85, 2),
         (89, 2.8), (92, 1.4), (97, 2)),
        ((81, -9), (74, -12), (68, -9), (66, -4), (67, 0), (63, 4),
         (63, 8), (70, 10), (75, 7), (75.5, 3), (79, 1), (81, -3)),
        ((55, -7), (51, -5.5), (48, -2), (42, -3), (39, -0.9),
         (41, 2.7), (47, 4.2), (52, 2.8), (54, -0.6)),
    ),
    (
        ((96, -5), (89, -6), (83, -2), (81, 4), (86, 7),
         (89, 3.1), (92, 3), (93, -0.5)),
        ((75, -8), (72, -9), (67, -6), (65, -2), (58, -1),
         (54, 4), (60, 7), (64, 3), (69, 4), (74, 0)),
        ((47, -4), (43, -3), (38, -0.9), (38, 2.5), (43, 3.5), (49, 2)),
    ),
    (),
)

DARK_PATCHES = (
    ((83, -9), (79, -10), (76, -8), (75, -5), (79, -4), (82, -6)),
    ((77, 7), (73, 5), (69, 5.8), (65, 9), (71, 12), (77, 10)),
    ((58, -7), (53, -5), (51, -3), (52, 0), (55, 1.5),
     (58, -0.5), (61, -3), (61, -6)),
    ((35, -4), (30, -2.4), (31, 1.8), (36, 2.5), (39, 0)),
)


def soft_mask(points):
    """Rasterize a closed spline through the shape's control points."""
    curve = []
    for i, (x, y) in enumerate(points):
        a, b = points[i - 1], (x, y)
        c, d = points[(i + 1) % len(points)], points[(i + 2) % len(points)]
        for step in range(8):
            t = step / 8
            curve.append(tuple(
                (2*v + (-u + w)*t + (2*u - 5*v + 4*w - z)*t*t
                 + (-u + 3*v - 3*w + z)*t*t*t) / 2
                for u, v, w, z in zip(a, b, c, d)
            ))
    mask = Image.new("L", (WIDTH * SUPERSAMPLE, HEIGHT * SUPERSAMPLE))
    ImageDraw.Draw(mask).polygon(
        [(x * SUPERSAMPLE, (y + CENTER_Y) * SUPERSAMPLE) for x, y in curve], fill=255
    )
    return mask.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS).load()


def body_radius(x):
    for (a, low), (b, high) in zip(PROFILE, PROFILE[1:]):
        if a <= x < b:
            t = (x - a) / (b - a)
            if b == 101:
                return low * sqrt(1 - t*t)
            return low + (high - low) * t
    return 0.0


def rgb_mix(a, b, amount):
    return tuple(x + (y - x) * amount for x, y in zip(a, b))


def clipped(value, low=0.0, high=1.0):
    return min(high, max(low, value))


def render(species):
    fin_masks = [(soft_mask(points), root) for points, root in FINS]
    red_masks = [soft_mask(points) for points in RED_PATCHES[species]]
    dark_masks = [soft_mask(points) for points in DARK_PATCHES] if species == 1 else []
    fin_light = ((210, 202, 178), (202, 206, 201), (237, 177, 77))[species]
    white = ((236, 230, 210), (235, 240, 237), (234, 170, 51))[species]
    red = ((235, 68, 37), (233, 83, 47), (0, 0, 0))[species]
    image = Image.new("RGB", (WIDTH, HEIGHT))
    pixels = image.load()

    for ix in range(WIDTH):
        x = ix + 0.5
        radius = body_radius(x)
        for iy in range(HEIGHT):
            y = iy + 0.5 - CENTER_Y
            rgb = (0, 0, 0)

            for fin, (rx, ry) in fin_masks:
                coverage = fin[ix, iy] / 255
                if coverage < 0.02:
                    continue
                angle = (y - ry) / max(4, rx - x)
                rays = 0.83 + 0.17 * sin(angle * 48 + x * 0.21)
                fade = clipped(0.82 - (rx - x) * 0.015, 0.32, 0.86)
                rgb = rgb_mix(rgb, fin_light, coverage * fade * rays)

            center = 0.35 * sin(x * 0.095 + species * 0.7)
            dy = y - center
            body_coverage = clipped(radius - abs(dy) + 0.46)
            if radius > 0 and body_coverage:
                across = clipped(dy / radius, -1, 1)
                surface = sqrt(1 - across * across)
                color = rgb_mix(white, red, max((m[ix, iy] / 255 for m in red_masks), default=0))
                charcoal = max((m[ix, iy] / 255 for m in dark_masks), default=0)
                color = rgb_mix(color, (60, 66, 74), charcoal * 0.96)

                scale = 0.0
                if 33 < x < 83:
                    row = int((dy + 18) / 2.5)
                    scale = (0.5 + 0.5 * sin(x * 1.38 + row * 2.7)) ** 5
                    scale *= (0.63 + 0.37 * sin(dy * 2.51))
                ridge = exp(-((dy + 2.0) / 3.8) ** 2)
                light = 0.52 + 0.45 * surface + 0.08 * ridge - 0.11 * scale
                speckle = ((ix * 13 + iy * 23 + species * 19) % 17 - 8) * 0.27
                skin = tuple(c * light + 10 * ridge + speckle for c in color)
                rgb = rgb_mix(rgb, skin, body_coverage)

                # Paired eyes, gill folds and a small highlight on the rounded snout.
                eye_x = 92.3 - (0.7 if dy > 0 else 0.0)
                eye_y = 6.5 if dy > 0 else -6.35
                eye = clipped(1.75 - sqrt((x-eye_x)**2 + ((dy-eye_y)*1.13)**2))
                rgb = rgb_mix(rgb, (21, 23, 23), eye * 0.9)
                glint = clipped(0.86 - sqrt((x-eye_x-0.47)**2 + (dy-eye_y+0.38)**2))
                rgb = rgb_mix(rgb, (243, 235, 213), glint * eye)
                gill = exp(-((x - 85.5 + 0.10 * (abs(dy)-4)**2) / 0.8)**2)
                gill *= clipped((abs(dy) - 5) / 4) * 0.24
                rgb = rgb_mix(rgb, (62, 48, 38), gill)
                snout = exp(-((x-98.4)/1.15)**2 - ((dy+0.9)/2.2)**2)
                rgb = rgb_mix(rgb, (255, 234, 193), snout * 0.19 * body_coverage)

            pixels[ix, iy] = tuple(round(clipped(c, 0, 255)) for c in rgb)
    return image


def rgb565(rgb):
    r, g, b = rgb
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


def array_lines(values, count, number):
    return "\n".join("    " + ", ".join(number(v) for v in values[i:i+count]) + ","
                     for i in range(0, len(values), count))


DIRECTIONS, STROKES = 24, 5
ANCHOR_X = 55
SPRITE_PIVOT = 64
PALETTES = (
    (0x000000, 0x232322, 0x393631, 0x514941, 0x706257, 0x8a7d6e,
     0xa69a87, 0xc2b8a6, 0xd5cbb8, 0xe8e0ce, 0xf9eed6,
     0x55231a, 0x882e20, 0xb83b25, 0xdc4d31, 0xf26947),
    (0x000000, 0x27292b, 0x383c42, 0x4c5359, 0x626a6a, 0x808786,
     0x9ba3a1, 0xb1b8b4, 0xccd3ce, 0xe3e9e3, 0xf5f6ec,
     0x55251d, 0x8c3728, 0xb8442f, 0xd95639, 0xf27651),
    (0x000000, 0x211710, 0x3c2b17, 0x583d1b, 0x775122, 0x8b5c24,
     0xa76f29, 0xbc7e2c, 0xd08b2e, 0xdb9b33, 0xe9aa36,
     0xf0b747, 0xf7c252, 0xfbd16a, 0xfbd983, 0xffe395),
)


def bent_sprite(art, phase):
    bent = Image.new("RGB", (128, 128))
    for x in range(WIDTH):
        taper = clipped((76 - x) / 76) ** 2
        shift = 10 * taper * sin(phase + x * 0.065)
        fin = exp(-((x-68) / 9)**2) * cos(phase) * 0.16
        column = art.transform(
            (1, 68), Image.Transform.AFFINE,
            (1, 0, x, 0, 1 / (1 + fin), CENTER_Y - (34 + shift) / (1 + fin)),
            Image.Resampling.BICUBIC,
        )
        bent.paste(column, (SPRITE_PIVOT - ANCHOR_X + x, SPRITE_PIVOT - 34))
    return bent


def pack_pose(art, palette, angle, scale):
    c, s = cos(angle) / scale, sin(angle) / scale
    # Pixel centers at offset zero stay at the fish's motion anchor.
    pivot = SPRITE_PIVOT + 0.5
    rgb = art.transform(
        (128, 128), Image.Transform.AFFINE,
        (c, s, pivot * (1 - c - s), -s, c, pivot * (1 + s - c)),
        Image.Resampling.BICUBIC,
    )
    indexed = rgb.quantize(palette=palette, dither=Image.Dither.NONE)
    assert max(indexed.getdata()) < 16
    x0, y0, x1, y1 = indexed.getbbox()
    pixels = indexed.load()
    packed = bytearray(((x0 - SPRITE_PIVOT) & 255, (y0 - SPRITE_PIVOT) & 255, x1-x0, y1-y0))
    span_total = 0
    for y in range(y0, y1):
        spans = []
        x = x0
        while x < x1:
            if not pixels[x, y]:
                x += 1
                continue
            start = x
            while x < x1 and pixels[x, y]:
                x += 1
            colors = [pixels[i, y] for i in range(start, x)]
            spans.append((start - x0, colors))
        span_total += len(spans)
        packed.append(len(spans))
        for start, colors in spans:
            packed.extend((start, len(colors)))
            if len(colors) & 1:
                colors.append(0)
            packed.extend(colors[i] | colors[i+1] << 4 for i in range(0, len(colors), 2))
    return packed, span_total


def build_atlas():
    root = Path(__file__).resolve().parent / "pixel-fish"
    atlas = bytearray()
    offsets, tables = [], []
    largest_image = most_spans = 0
    for species, palette in enumerate(PALETTES):
        table = Image.new("P", (1, 1))
        rgb = [(n >> 16, n >> 8 & 255, n & 255) for n in palette]
        table.putpalette([c for color in rgb for c in color] + [0] * (256-16)*3)
        colors = [rgb565(color) for color in rgb]
        tables.append([colors[n & 15] | colors[n >> 4] << 16 for n in range(256)])
        art = render(species)
        for pose in range(STROKES):
            bent = bent_sprite(art, pose * 2*pi / STROKES)
            for direction in range(DIRECTIONS // 2):
                offsets.append(len(atlas))
                pose_data, spans = pack_pose(bent, table, direction * 2*pi / DIRECTIONS,
                                             (0.93, 0.89, 0.83)[species])
                atlas.extend(pose_data)
                largest_image = max(largest_image, pose_data[2] * pose_data[3])
                most_spans = max(most_spans, spans)
    sections = [
        "/* Generated by make_textures.py. */\n#pragma once\n#include <stdint.h>\n",
        f"enum {{ KOI_DIRECTIONS = {DIRECTIONS}, KOI_STROKES = {STROKES}, KOI_MAX_PIXELS = {largest_image}, KOI_MAX_SPANS = {most_spans} }};\n",
        "#if defined(KOI_NATIVE_FIRMWARE)\n"
        "extern const uint8_t koi_atlas[] __asm__(\"_binary_koi_poses_bin_start\");\n"
        "#else\nstatic const uint8_t koi_atlas[] = {\n#embed \"koi_poses.bin\"\n};\n#endif\n",
        "static const uint32_t koi_offsets[] = {\n"
        + array_lines(offsets, 12, str) + "\n};\n",
        "static const uint32_t koi_words[3][256] = {",
    ]
    for words in tables:
        sections.append("{\n" + array_lines(words, 8, lambda n: f"0x{n:08x}") + "\n},")
    sections.append("};\n")
    sections.append("static const uint32_t koi_reversed_words[3][256] = {")
    for words in tables:
        sections.append("{\n" + array_lines(words, 8, lambda n: f"0x{(n>>16 | (n&65535)<<16):08x}") + "\n},")
    sections.append("};\n")
    (root / "koi_textures.h").write_text("\n".join(sections), encoding="ascii", newline="\n")
    (root / "koi_poses.bin").write_bytes(atlas)
    print(f"Baked {len(offsets)} top-view poses ({len(atlas)} bytes)")


if __name__ == "__main__":
    build_atlas()
