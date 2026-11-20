#!/usr/bin/env python3
"""Check compiled iaito95 hints with FontTools and freetype-py (optional test dependencies)."""

import importlib.util
import unittest
from pathlib import Path

import freetype


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("build_iaito95", ROOT / "scripts/fonts/build_iaito95.py")
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)


class Iaito95Hints(unittest.TestCase):
    def test_compiled_hints(self):
        font = BUILDER.TTFont(BUILDER.OUTPUT)
        face = freetype.Face(str(BUILDER.OUTPUT))
        cache = {}
        for ppem in range(10, 217):
            face.set_pixel_sizes(0, ppem)
            base = 10 + (ppem - 10) % 20
            cycles = (ppem - 10) // 20
            for index, name in enumerate(font.getGlyphOrder()):
                with self.subTest(ppem=ppem, glyph=name):
                    # PEDANTIC catches malformed bytecode that permissive native
                    # renderers can silently ignore or execute only in part.
                    face.load_glyph(index, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO
                                    | freetype.FT_LOAD_NO_AUTOHINT | freetype.FT_LOAD_PEDANTIC)
                    glyph = font["glyf"][name]
                    if not glyph.numberOfContours:
                        continue
                    advance = font["hmtx"][name][0]
                    ascent, descent = font["hhea"].ascent, font["hhea"].descent
                    key = (name, base)
                    if key not in cache:
                        cache[key] = BUILDER.hint_positions(glyph, advance, ascent, descent, base)
                    # Execute the actual compiled program, including its 20-ppem
                    # period arithmetic, and compare it to the independently
                    # computed edge fit. Then check ink and layout bounds.
                    expected = [tuple((p[a] + c[a] // BUILDER.GRID * cycles) * 64
                                      for a in (0, 1))
                                for p, c in zip(cache[key], glyph.coordinates)]
                    actual = face.glyph.outline.points
                    self.assertEqual(actual, expected)
                    self.assertGreater(face.glyph.bitmap.width, 0)
                    self.assertGreater(face.glyph.bitmap.rows, 0)
                    self.assertGreaterEqual(min(x for x, y in actual), 0)
                    self.assertLessEqual(max(x for x, y in actual), advance * ppem // 1000 * 64)
                    self.assertGreaterEqual(min(y for x, y in actual),
                                            -(-descent * ppem // 1000) * 64)
                    self.assertLessEqual(max(y for x, y in actual), ascent * ppem // 1000 * 64)


if __name__ == "__main__":
    unittest.main()
