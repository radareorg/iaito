# Structures in the hexdump

Open the hexdump sidebar's **Structures** tab, choose an r2 type, enter an address
or expression, and press **Inspect**. **At cursor** uses the first selected byte
or the current cursor. You can also right-click the hexdump and choose **Inspect
structure here**; a type already linked to that address is selected automatically.

The tree shows each field's declared type, value, address, and size. Expand nested
structures, unions, arrays, and pointers to explore them. Selecting a field selects
its bytes in the hexdump. Field colors match the byte highlights. The inspected
structure stays anchored while you navigate, and **Refresh** preserves expanded
fields and the selected row while rereading memory and type definitions.

Types come from r2's type database, including imported C definitions and typedefs.
The tree uses stored member offsets and the current `asm.bits` and `cfg.bigendian`
settings. Large arrays are divided into expandable groups. Unmapped pointers and
recursive references stop expansion; invalid or overflowing layouts are reported.
Array expressions such as `uint16_t[16]` can be inspected directly.

**Pin type** creates an r2 type link (`tl`) at the inspected address. The pinned
address list also includes existing type links and command annotations (`Cr`) from
the current r2 session. Define a named typedef before pinning an array expression.

**Pin command…** stores a command at the address using the selected byte range or
the chosen type's size. Double-click a pin to navigate to its address. Select a
command pin and press **Run command** to display its output. **Unpin** removes only
the selected type link or command annotation. Pins use r2's existing project data
and are saved with the project. iaito also writes `iaito-memory-pins.json` inside the
project directory to preserve types and pins omitted by some r2 binary project versions;
keep that companion file when copying a project.
