#!/usr/bin/env python3
# SPDX-License-Identifier: OFL-1.1
"""Rebuild iaito95 from the original W95FA outlines; requires fonttools."""

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
from fontTools.ttLib.tables.ttProgram import Program


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "scripts/fonts/w95fa/W95FA.otf"
SOURCE_LICENSE = SOURCE.with_name("OFL.txt")
OUTPUT = ROOT / "src/fonts/iaito95.ttf"
LICENSE = OUTPUT.with_name("iaito95-LICENSE.txt")
SOURCE_SHA256 = "9e1ad53708307b2b68e06d43799b2267f6aec620dda972bc62753ad16ba50f2b"
SOURCE_LICENSE_SHA256 = "ca122f3bda0154f692817450168cb650a7fc59ef96c93582acd2e7a744d464d3"
GRID = 50
FAMILY = "iaito95"


def rounded(value):
    # TrueType's round-to-grid rounds half pixels away from zero.
    return int(value + 0.5) if value >= 0 else -int(-value + 0.5)


def fit_edges(constraints, targets, lower, upper, mirrors=(), center=None):
    """Find nearby integer edge positions satisfying stroke and join constraints."""
    rules = list(constraints)
    if center is not None:
        rules.extend((a, b, 1, 1, center, True) for a, b in mirrors)

    def propagate(domains):
        changed = True
        while changed:
            changed = False
            for a, b, sa, sb, limit, equal in rules:
                def valid(x, y):
                    value = sa * x + sb * y
                    return value == limit if equal else value <= limit
                if a == b:
                    da = {x for x in domains[a] if valid(x, x)}
                    db = da
                else:
                    da = {x for x in domains[a] if any(valid(x, y) for y in domains[b])}
                    db = {y for y in domains[b] if any(valid(x, y) for x in da)}
                if not da or not db:
                    return False
                if da != domains[a] or db != domains[b]:
                    domains[a], domains[b] = da, db
                    changed = True
        return True

    def search(domains):
        if not propagate(domains):
            return None
        choices = [i for i, d in enumerate(domains) if len(d) > 1]
        if not choices:
            return [next(iter(d)) for d in domains]
        index = min(choices, key=lambda i: (len(domains[i]), i))
        for value in sorted(domains[index], key=lambda v: (abs(v - targets[index]), v)):
            trial = list(domains)
            trial[index] = {value}
            result = search(trial)
            if result is not None:
                return result
        return None

    for radius in range(5):
        domains = [set(range(max(lower, t - radius), min(upper, t + radius) + 1))
                   for t in targets]
        if all(domains):
            result = search(domains)
            if result is not None:
                return result
    raise ValueError("Unable to fit glyph strokes within its metrics")


def hint_positions(glyph, advance, ascent, descent, ppem):
    """Fit strokes, while allowing counters and the lengths of steps to adjust."""
    coordinates = list(glyph.coordinates)
    contours = []
    first = 0
    for end in glyph.endPtsOfContours:
        contours.append(list(range(first, end + 1)))
        first = end + 1
    contacts = defaultdict(list)
    for contour in contours:
        for i, point in enumerate(contour):
            contacts[coordinates[point]].append((point, contour[i - 1],
                                                 contour[(i + 1) % len(contour)]))
    result = [[0, 0] for _ in coordinates]
    for axis in (0, 1):
        edges, owners = [], {}
        for contour in contours:
            for a, b in zip(contour, contour[1:] + contour[:1]):
                if coordinates[a][axis] == coordinates[b][axis]:
                    value = coordinates[a][axis]
                    start, end = coordinates[a][1 - axis], coordinates[b][1 - axis]
                    # Clockwise outlines put ink east of upward vertical edges,
                    # and north of leftward horizontal edges (including holes).
                    positive = end > start if axis == 0 else end < start
                    owners[a] = owners[b] = len(edges)
                    edges.append((value, min(start, end), max(start, end), positive))
        constraints = []
        for a, edge in enumerate(edges):
            for b, other in enumerate(edges):
                width = other[0] - edge[0]
                if (width in (GRID, 2 * GRID) and edge[3] and not other[3]
                        and max(edge[1], other[1]) < min(edge[2], other[2])):
                    pixels = max(1, rounded(width * ppem / 1000))
                    constraints.append((a, b, -1, 1, pixels, True))
        # Preserve the direction of each intervening edge, so counters cannot invert.
        for contour in contours:
            for a, b in zip(contour, contour[1:] + contour[:1]):
                if coordinates[a][axis] < coordinates[b][axis]:
                    constraints.append((owners[a], owners[b], 1, -1, 0, False))
                elif coordinates[a][axis] > coordinates[b][axis]:
                    constraints.append((owners[b], owners[a], 1, -1, 0, False))
        # Point-touching design pixels may overlap after fitting, but must not gap.
        for point, touching in contacts.items():
            for a, before_a, after_a in touching:
                neighbor_a = next(coordinates[n][axis] for n in (before_a, after_a)
                                  if coordinates[n][axis] != point[axis])
                for b, before_b, after_b in touching:
                    neighbor_b = next(coordinates[n][axis] for n in (before_b, after_b)
                                      if coordinates[n][axis] != point[axis])
                    if neighbor_a < point[axis] < neighbor_b:
                        constraints.append((owners[b], owners[a], 1, -1, 0, axis == 1))
        mirrors, center = [], None
        if axis == 0:
            twice_center = min(p[0] for p in coordinates) + max(p[0] for p in coordinates)
            lookup = {(x, lo, hi, positive): i
                      for i, (x, lo, hi, positive) in enumerate(edges)}
            reflection = [lookup.get((twice_center - x, lo, hi, not positive))
                          for x, lo, hi, positive in edges]
            if all(i is not None for i in reflection):
                mirrors = [(a, b) for a, b in enumerate(reflection) if a < b]
                center = rounded(twice_center * ppem / 1000)
                # A central stem needs its two edges on opposite grid lines.
                for a, b, sa, sb, pixels, equal in constraints:
                    if equal and reflection[a] == b and (center - pixels) % 2:
                        center += 1 if center < twice_center * ppem / 1000 else -1
                        break
        if axis == 1:
            # Mirrored horizontal strokes must also stay on the same scanlines.
            twice_center_x = min(p[0] for p in coordinates) + max(p[0] for p in coordinates)
            lookup = {edge: i for i, edge in enumerate(edges)}
            reflection = [lookup.get((y, twice_center_x - hi, twice_center_x - lo, positive))
                          for y, lo, hi, positive in edges]
            if all(i is not None for i in reflection):
                constraints.extend((a, b, 1, -1, 0, True)
                                   for a, b in enumerate(reflection) if a < b)
        targets = [rounded(edge[0] * ppem / 1000) for edge in edges]
        lower = 0 if axis == 0 else int(-(-descent * ppem // 1000))
        upper = advance * ppem // 1000 if axis == 0 else ascent * ppem // 1000
        try:
            positions = fit_edges(constraints, targets, lower, upper, mirrors, center)
        except ValueError as error:
            raise ValueError(f"{ppem}px axis {axis}: {error}") from error
        for point, owner in owners.items():
            result[point][axis] = positions[owner]
    return result


def gridfit(font):
    """Generate portable TrueType instructions for equal stems and intact joins.

    Fit individual strokes, rather than rescaling the entire design pixel grid:
    rescaling everything also enlarges glyphs beyond their advances and ascents.
    The fitting pattern repeats every 20 ppem because all coordinates are on the
    50/1000-em grid. Keep the ordinary vector outlines and encode only corrections
    to their rounded positions; no bitmap strikes or alternate glyphs are needed.
    """
    font["prep"] = newTable("prep")
    program = Program()
    program.fromAssembly(["RTG[ ]"])
    font["prep"].program = program
    largest_program, largest_stack = 0, 4
    for name in font.getGlyphOrder():
        glyph = font["glyf"][name]
        if not glyph.numberOfContours:
            continue
        instructions = ["MPPEM[ ]", "PUSHB[ ]", "10", "GTEQ[ ]", "IF[ ]"]
        for axis in (0, 1):
            instructions.append(f"SVTCA[{1 - axis}]")
            for index, point in enumerate(glyph.coordinates):
                instructions.extend([
                    "PUSHW[ ]", str(index), str(point[axis] // GRID * 64), "MPPEM[ ]",
                    "MUL[ ]", "PUSHB[ ]", "20", "DIV[ ]", "ROUND[00]", "SCFS[ ]",
                ])
        for ppem in range(10, 30):
            try:
                positions = hint_positions(glyph, font["hmtx"][name][0],
                                           font["hhea"].ascent, font["hhea"].descent, ppem)
            except ValueError as error:
                raise ValueError(f"{name}: {error}") from error
            corrections = []
            for axis in (0, 1):
                groups = defaultdict(list)
                for index, point in enumerate(glyph.coordinates):
                    position = positions[index][axis]
                    if position != rounded(point[axis] * ppem / 1000):
                        groups[(point[axis] // GRID, position)].append(index)
                if groups:
                    corrections.append(f"SVTCA[{1 - axis}]")
                for (coordinate, position), points in sorted(groups.items()):
                    # Set absolute positions without relying on reference points.
                    corrections.extend([
                        "PUSHW[ ]", str(coordinate * 64), "MPPEM[ ]", "PUSHB[ ]", "10",
                        "SUB[ ]", "PUSHB[ ]", "20", "DIV[ ]", "FLOOR[ ]", "MUL[ ]",
                        "PUSHW[ ]", str(position * 64), "ADD[ ]",
                    ])
                    for index in points[:-1]:
                        corrections.extend(["DUP[ ]", "PUSHB[ ]", str(index),
                                            "SWAP[ ]", "SCFS[ ]"])
                    corrections.extend(["PUSHB[ ]", str(points[-1]), "SWAP[ ]", "SCFS[ ]"])
            if corrections:
                instructions.extend([
                    "MPPEM[ ]", "MPPEM[ ]", "PUSHB[ ]", "10", "SUB[ ]",
                    "PUSHB[ ]", "20", "DIV[ ]", "FLOOR[ ]", "PUSHB[ ]", "20", "MUL[ ]",
                    "SUB[ ]", "PUSHB[ ]", str(ppem), "EQ[ ]", "IF[ ]",
                    *corrections, "EIF[ ]",
                ])
        instructions.append("EIF[ ]")
        assert instructions.count("IF[ ]") == instructions.count("EIF[ ]")
        glyph.program = Program()
        glyph.program.fromAssembly(instructions)
        largest_program = max(largest_program, len(glyph.program.getBytecode()))
    font["maxp"].maxStorage = 0
    font["maxp"].maxStackElements = largest_stack
    font["maxp"].maxSizeOfInstructions = largest_program
    font["head"].flags |= 1 << 2


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
    assert hashlib.sha256(SOURCE_LICENSE.read_bytes()).hexdigest() == SOURCE_LICENSE_SHA256
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
        "uniqueFontIdentifier": "iaito95-Regular-1.001",
        "fullName": FAMILY + " Regular",
        "psName": "iaito95-Regular",
        "version": "Version 1.001",
        "copyright": ("Based on W95FA by Alina Sava (FontsArena.com). "
                      "Modifications for iaito, 2026."),
        "manufacturer": "iaito",
        "designer": "Alina Sava; iaito contributors",
        "licenseDescription": "SIL Open Font License, Version 1.1. See iaito95-LICENSE.txt.",
        "licenseInfoURL": "https://openfontlicense.org/",
    })
    builder.setupOS2(version=4, sTypoAscender=ascent, sTypoDescender=descent, sTypoLineGap=0,
                     usWinAscent=ascent, usWinDescent=-descent, sxHeight=600, sCapHeight=900,
                     usWeightClass=400, fsSelection=0xC0, fsType=0)
    builder.setupPost(underlinePosition=-150, underlineThickness=100)
    gridfit(builder.font)
    builder.font["gasp"] = newTable("gasp")
    builder.font["gasp"].gaspRange = {65535: 1}  # Grid fitting, without grayscale.
    builder.font["head"].fontRevision = 1.001
    builder.font["head"].created = builder.font["head"].modified = 3877977600
    builder.font.recalcTimestamp = False
    output = io.BytesIO()
    builder.save(output)
    data = output.getvalue()
    # Check compiled contours, coverage and vertical bounds, not only inputs.
    compiled = TTFont(io.BytesIO(data))
    compiled_glyphs = compiled.getGlyphSet()
    assert compiled["prep"].program.getBytecode()
    assert compiled["gasp"].gaspRange == {65535: 1}
    assert compiled.getBestCmap() == cmap
    assert all(cmap[c] == glyph for c, glyph in source.getBestCmap().items())
    for name in compiled.getGlyphOrder():
        glyph = compiled["glyf"][name]
        if glyph.numberOfContours:
            assert glyph.program.getBytecode(), f"Missing grid fitting in {name}"
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
    notice = ("iaito95: a modified version of W95FA by Alina Sava (FontsArena.com).\n"
              "Outline, metric and TrueType hinting repairs for the iaito project, 2026.\n"
              "Source: https://fontsarena.com/w95fa-by-alina-sava/\n"
              "Rebuild: scripts/fonts/build_iaito95.py\n\n")
    license_text = notice.encode() + SOURCE_LICENSE.read_bytes()
    return data, license_text


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
