#!/usr/bin/env python3
"""Rebuild Iaito W95FA from the original W95FA outlines; requires fonttools."""

import argparse
import hashlib
import io
from collections import defaultdict
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.recordingPen import RecordingPen
from fontTools.pens.pointInsidePen import PointInsidePen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont, newTable


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "scripts/fonts/w95fa/W95FA.otf"
OUTPUT = ROOT / "src/fonts/IaitoW95FA.ttf"
LICENSE = OUTPUT.with_name("IaitoW95FA-LICENSE.txt")
SOURCE_SHA256 = "9e1ad53708307b2b68e06d43799b2267f6aec620dda972bc62753ad16ba50f2b"
GRID = 50
FAMILY = "Iaito W95FA"


def contours_for(glyph, glyph_set):
    bounds = BoundsPen(glyph_set)
    glyph.draw(bounds)
    if bounds.bounds is None:
        return []
    left = bounds.bounds[0]
    recording = RecordingPen()
    glyph.draw(recording)
    contours = []
    contour = []
    for operation, points in recording.value:
        if operation in ("moveTo", "lineTo", "curveTo"):
            # The sole cubic in the source (section) is a sub-unit join artifact.
            # Snapping its endpoint removes it without changing a design pixel.
            if operation == "curveTo":
                assert all(abs(p[i] - points[-1][i]) < 1 for p in points for i in (0, 1))
            x, y = points[-1]
            point = (round((x - left) / 40) + 1, round(y / 40))
            if not contour or point != contour[-1]:
                contour.append(point)
        elif operation == "closePath":
            if len(contour) > 2:
                contours.append(contour)
            contour = []
        else:
            raise ValueError(f"Unsupported source operation: {operation}")
    assert not contour
    return contours


def filled_cells(contours):
    if not contours:
        return set()
    points = [p for contour in contours for p in contour]
    edges = [(a, b) for c in contours for a, b in zip(c, c[1:] + c[:1])]
    cells = set()
    for y in range(min(p[1] for p in points), max(p[1] for p in points)):
        for x in range(min(p[0] for p in points), max(p[0] for p in points)):
            px, py = x + 0.5, y + 0.5
            winding = 0
            for (ax, ay), (bx, by) in edges:
                cross = (bx - ax) * (py - ay) - (by - ay) * (px - ax)
                if ay <= py < by and cross > 0:
                    winding += 1
                elif by <= py < ay and cross < 0:
                    winding -= 1
            if winding:
                cells.add((x, y))
    return cells


def boundaries(cells):
    edges = set()
    for x, y in sorted(cells):
        corners = [(x, y), (x + 1, y), (x + 1, y + 1), (x, y + 1)]
        for a, b in zip(corners, corners[1:] + corners[:1]):
            if (b, a) in edges:
                edges.remove((b, a))
            else:
                edges.add((a, b))
    outgoing = defaultdict(set)
    for a, b in edges:
        outgoing[a].add(b)
    directions = {(1, 0): 0, (0, 1): 1, (-1, 0): 2, (0, -1): 3}
    turns = {1: 0, 0: 1, 3: 2, 2: 3}
    while edges:
        start, current = min(edges)
        previous = start
        contour = [start]
        while True:
            edges.remove((previous, current))
            outgoing[previous].remove(current)
            if current == start:
                break
            contour.append(current)
            direction = directions[(current[0] - previous[0], current[1] - previous[1])]

            def turn(point):
                following = directions[(point[0] - current[0], point[1] - current[1])]
                return turns[(following - direction) % 4]

            # Keep corner-touching islands separate instead of making bow-ties.
            following = min(outgoing[current], key=turn)
            previous, current = current, following
        simplified = []
        for i, point in enumerate(contour):
            a, b = contour[i - 1], contour[(i + 1) % len(contour)]
            if (point[0] - a[0]) * (b[1] - point[1]) != (point[1] - a[1]) * (b[0] - point[0]):
                simplified.append(point)
        # TrueType uses clockwise exterior contours and counterclockwise holes.
        yield list(reversed(simplified))


def build():
    assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == SOURCE_SHA256
    source = TTFont(SOURCE)
    glyph_set = source.getGlyphSet()
    glyph_order = source.getGlyphOrder() + ["multiply"]
    cmap = dict(source.getBestCmap())
    cmap[0x2026] = cmap[0xE900]  # The source stores its ellipsis in the private-use area.
    cmap[0xD7] = "multiply"
    glyphs, metrics, expected_cells = {}, {}, {}
    bottom, top = 0, 0
    for name in glyph_order:
        if name == "multiply":
            # A compact pixel cross for dock close buttons; no system-font fallback.
            cells = {(1 + 2 * x + dx, 2 + 2 * (6 - y) + dy)
                     for y in range(7) for x in range(7) if x == y or x == 6 - y
                     for dx in range(2) for dy in range(2)}
            contours = []
        else:
            contours = contours_for(glyph_set[name], glyph_set)
            cells = filled_cells(contours)
        expected_cells[name] = cells
        pen = TTGlyphPen(None)
        for contour in boundaries(cells):
            assert len(contour) >= 4
            pen.moveTo(tuple(v * GRID for v in contour[0]))
            for point in contour[1:]:
                pen.lineTo(tuple(v * GRID for v in point))
            pen.closePath()
        glyphs[name] = pen.glyph()
        if cells:
            left = min(x for x, y in cells) * GRID
            advance = (max(x for x, y in cells) + 2) * GRID
            bottom = min(bottom, min(y for x, y in cells) * GRID)
            top = max(top, (max(y for x, y in cells) + 1) * GRID)
        else:
            assert not contours, f"Lost all ink in {name}"
            left = 0
            advance = round(glyph_set[name].width / 40) * GRID
        metrics[name] = (advance, left)
    ascent, descent = top + GRID, bottom - GRID
    builder = FontBuilder(1000, isTTF=True)
    builder.setupGlyphOrder(glyph_order)
    builder.setupCharacterMap(cmap)
    builder.setupGlyf(glyphs)
    builder.setupHorizontalMetrics(metrics)
    builder.setupHorizontalHeader(ascent=ascent, descent=descent, lineGap=0)
    builder.setupNameTable({
        "familyName": FAMILY,
        "styleName": "Regular",
        "uniqueFontIdentifier": "IaitoW95FA-Regular-1.000",
        "fullName": FAMILY + " Regular",
        "psName": "IaitoW95FA-Regular",
        "version": "Version 1.000",
        "copyright": ("Based on W95FA by Alina Sava (FontsArena.com). "
                      "Modifications for iaito, 2026."),
        "manufacturer": "iaito",
        "designer": "Alina Sava; iaito contributors",
        "licenseDescription": "SIL Open Font License, Version 1.1. See IaitoW95FA-LICENSE.txt.",
        "licenseInfoURL": "https://openfontlicense.org/",
    })
    builder.setupOS2(version=4, sTypoAscender=ascent, sTypoDescender=descent, sTypoLineGap=0,
                     usWinAscent=ascent, usWinDescent=-descent, sxHeight=600, sCapHeight=900,
                     usWeightClass=400, fsSelection=0xC0, fsType=0)
    builder.setupPost(underlinePosition=-150, underlineThickness=100)
    builder.font["gasp"] = newTable("gasp")
    builder.font["gasp"].gaspRange = {65535: 0}
    builder.font["head"].created = builder.font["head"].modified = 3877977600
    builder.font.recalcTimestamp = False
    output = io.BytesIO()
    builder.save(output)
    data = output.getvalue()
    # Check compiled contours, coverage and vertical bounds, not only inputs.
    compiled = TTFont(io.BytesIO(data))
    compiled_glyphs = compiled.getGlyphSet()
    assert compiled.getBestCmap() == cmap
    assert all(cmap[c] == glyph for c, glyph in source.getBestCmap().items())
    for name in compiled.getGlyphOrder():
        glyph = compiled["glyf"][name]
        if glyph.numberOfContours:
            assert glyph.yMin >= descent and glyph.yMax <= ascent
            assert all(x % GRID == 0 and y % GRID == 0 for x, y in glyph.coordinates)
            assert all(flag & 1 for flag in glyph.flags)
        assert compiled["hmtx"][name][0] % GRID == 0
        cells = expected_cells[name]
        if cells:
            for y in range(min(y for x, y in cells) - 1, max(y for x, y in cells) + 2):
                for x in range(min(x for x, y in cells) - 1, max(x for x, y in cells) + 2):
                    point = ((x + 0.5) * GRID, (y + 0.5) * GRID)
                    inside = PointInsidePen(compiled_glyphs, point)
                    compiled_glyphs[name].draw(inside)
                    assert inside.getResult() == ((x, y) in cells), (name, x, y)
    notice = ("Iaito W95FA: a modified version of W95FA by Alina Sava (FontsArena.com).\n"
              "Outline and metric repairs for the iaito project, 2026.\n"
              "Source: https://fontsarena.com/w95fa-by-alina-sava/\n"
              "Rebuild: scripts/fonts/build_w95fa.py\n\n")
    license_text = notice + SOURCE.with_name("OFL.txt").read_text()
    return data, license_text.encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="Verify the committed font is reproducible")
    args = parser.parse_args()
    data, license_text = build()
    if args.check:
        if OUTPUT.read_bytes() != data or LICENSE.read_bytes() != license_text:
            parser.exit(1, "The generated font or license differs; rebuild it.\n")
    else:
        OUTPUT.write_bytes(data)
        LICENSE.write_bytes(license_text)
    action = "Verified" if args.check else "Built"
    print(f"{action} {OUTPUT.name}: 161 glyphs, 169 mappings, {len(data)} bytes")


if __name__ == "__main__":
    main()
