# Sharing iaito95 with the W95FA author

The source bundled here is W95FA 1.002, credited to Alina Sava / FontsArena by
[the original publication](https://fontsarena.com/w95fa-by-alina-sava/).
Its embedded manufacturer is Alina Sava and its designer/vendor URL is FontsArena.

The publication links to [FontsArena's contact page](https://fontsarena.com/contact/).
That page currently lists a general publisher contact, rather than a verified
personal address for Alina. Ask the publisher to forward the note if a direct
author contact is unavailable. No message has been sent.

The files to share are listed in [README.md](README.md). Include the original
OTF and OFL, the Python generator, the generated TTF and attribution/license,
and the strict FreeType test. The source font and upstream license are unchanged.
Publish the commit before sending a repository link; committing locally does not
make these changes available on GitHub.

## Technical findings

- The source outlines contain sub-unit joins and adjacent filled regions. The
  generator snaps the design to a 50/1000-em grid and rebuilds region boundaries,
  preserving the original character coverage and pixel style.
- Design-grid alignment alone left strokes uneven at fractional sizes. At 16px
  on a 2x display, `o` could have a 3-device-pixel stem beside a 4-pixel stem.
  The repaired outlines previously had no TrueType fitting instructions.
- iaito95 fits individual strokes, preserves mirrored edges and joins, and lets
  counters and step lengths adjust within the existing advance and ascent bounds.
  Scaling the entire pixel grid instead caused overlap and clipping, so that
  approach was discarded.
- The font adds U+2026 for the source's private-use ellipsis and U+00D7 for a close
  mark. All original glyphs and Unicode mappings remain present.
- Qt needs full hinting and monochrome rendering for this pixel style. Its
  default 64-device-pixel glyph-cache cutoff also switches larger text to paths;
  iaito raises that limit to retain the hinted rendering at larger UI sizes.
  This application setting is separate from the font repairs.
- Verification covers `o` and `A` at sizes 10–32 in pixels and points, four device
  ratios and fractional drawing origins. The appearance suite passed 423 checks.
  FreeType's strict interpreter checked 32,085 nonempty glyph/size combinations
  at 10–216 physical pixels. Font and license generation is reproducible.
- The output remains a vector TrueType font without embedded bitmap strikes.
  The dedicated fitting starts at 10 physical pixels per em; extremely small
  sizes and renderers that ignore TrueType instructions still have limitations.

## Draft message

Subject: W95FA scaling repairs and a reproducible iaito95 fork

Hello Alina,

Thank you for creating W95FA. We use its Windows 95 pixel style in iaito, the
Qt interface for radare2, and worked on some rendering problems while integrating
it into our Classic theme.

We found uneven stroke widths at some sizes, including `o` and `A` around 11px
and 16px. We now have a Python generator that repairs outline joins and spacing,
retains the original design, and emits TrueType instructions to keep strokes,
symmetry and joins consistent as the font scales. We also addressed a separate
Qt rendering cutoff for larger sizes.

We named the modified family iaito95, retained the W95FA source and original OFL,
and kept your attribution in the font metadata and documentation. The repository
contains the original font, license, generator, generated TTF and regression tests,
so the changes can be reviewed and reproduced. The work remains under the OFL.

Would you be interested in reviewing or incorporating any of these repairs?
We would be happy to contribute the generator and findings upstream so other
W95FA users can benefit. Please let us know your preferred place for sharing them.

Thanks!
