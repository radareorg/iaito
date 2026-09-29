# Data Analysis

Open **View → Data → Analysis**. The dock is hidden in the default layout and when
first added to an older saved layout. Hidden docks do not sample data. **Zoom** is
also available under **View → Data**.

- **Algorithms** enables multiple byte metrics. Hover over a named legend entry
  for its definition; click it to edit its color or visibility. Clicking the legend
  does not remove the series.
- **Options → Resolution (buckets)** selects 32, 64, 128, 256, 512, 1024, 2048 or
  4096 buckets. Lower values give coarser graphs and less work. The default is 1024.
- **Options** also controls lines, filled lines or vertical bars, separate tracks,
  greyscale/rainbow/theme colors, custom colors, and structural markers.
- Click the graph to seek. Drag to select bytes. Wheel or `+`/`-` zooms;
  Shift+wheel or the arrow keys pans. `Home` fits all data, and `Esc` clears selection.
- The bottom entropy overview shows the complete address span. Drag its window
  to pan, drag its edges to resize the range, or double-click to fit all data.
- **Range** fits the current section, segment, map, block or selection.
  **Edit range** accepts radare2 expressions and inclusive endpoints.
- **Follow** keeps the current seek address visible. Offset synchronization and
  the global address-selection synchronization setting control integration with
  disassembly and hexdump widgets.
- **Find marker** searches sections, segments, maps, flags (including search hits)
  and comments. The graph context menu provides comments, flags and “Show in”.
  Hover details appear after the pointer rests for 650 ms.
- Hexdump selections, sections and segments have direct analysis actions.
  Other addressable widgets can use **Show in → Data Analysis**.

The graph uses the current radare2 address space: virtual addresses with `io.va`
enabled, otherwise physical offsets in the active descriptor. Gaps and failed reads
do not contribute invented bytes. Each bucket reads at most 4096 bytes from centered
windows in readable spans. Tooltips, the status line and exported CSV report how
many bytes were actually read. Zoom in for complete coverage of a region. Entropy
is measured per bucket, so very small buckets have a lower attainable entropy.

Multiple overlaid series use a normalized percentage scale. Entropy's native scale
is 0–8 bits/byte, unique bytes 0–256, and mean byte 0–255. A single series or separate
tracks uses its native scale. CSV exports normalized values plus exact bucket bounds
and coverage. Display settings are saved with the layout.

Other widgets can call `MainWindow::showDataAnalysis(start, end)` or
`DataAnalysisWidget::showRange(start, end)` with **inclusive** endpoints. Internally,
`DataAnalysis::Range` and graph signals use half-open ranges.

Run the focused tests with:

```sh
make -C tests/dataanalysis -j8 > /dev/null
QT_QPA_PLATFORM=offscreen tests/dataanalysis/build/test_dataanalysis
```
