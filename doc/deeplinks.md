Iaito Deep Links
================

Iaito registers the `iaito://` URI scheme so binaries, projects, samples and raw
bytes can be referenced with stable, shareable links that open a specific
location and view. Clicking such a link (from a browser, file manager, chat
client, terminal, etc.) launches Iaito and navigates straight to the target.

The implementation lives in a single, self-contained pair of files,
`src/common/DeepLink.{h,cpp}`, so it is easy to find, maintain and remove.


URI format
----------

```
iaito://<handler>/<payload>?<query>
```

There are exactly four top-level handlers:

| Handler        | Payload          | Opens                                    |
|----------------|------------------|------------------------------------------|
| `file`         | absolute path    | a file from disk                         |
| `project`      | project name     | a saved radare2/iaito project            |
| `sha256`       | 64 hex chars     | a locally registered sample by its hash  |
| `bytes`        | hex byte string  | an in-memory buffer built from raw bytes |

Only **absolute paths** are accepted for `file`.

As a convenience, if the host part of the URI is empty the absolute path is
treated as a `file` reference:

```
iaito:///bin/ls        is the same as        iaito://file/bin/ls
```

Examples:

```
iaito://file/tmp/a.out
iaito://project/r2
iaito://sha256/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
iaito://bytes/554889e54883ec10
```


Query parameters
----------------

Parameters are passed as standard URL query parameters (`?a=1&b=2`), not as
`#fragments`. Unknown parameters are ignored, so the format can be extended in
the future without breaking existing links.

### Navigation

| Param   | Meaning                                                                |
|---------|------------------------------------------------------------------------|
| `addr`  | Any expression radare2 can resolve via `s <expr>`                      |
| `view`  | The panel to raise after opening                                       |
| `size`  | Region length / block size (integer, decimal or `0x` hex)              |

`addr` accepts anything radare2 understands:

```
addr=0x401000
addr=sym.main
addr=entry0
addr=str.Hello
```

`view` may be one of:

```
disasm      graph       hex         decompiler
strings     imports     exports     functions
```

`disasm`, `graph`, `hex` and `decompiler` open/raise the corresponding memory
widget; the other names raise the matching tool panel by name.

`size` sets the current block size (radare2 `b`), e.g. `size=64` or `size=0x100`.

### Analysis (mostly for `bytes` and raw/unknown binaries)

| Param    | Examples                                            |
|----------|-----------------------------------------------------|
| `arch`   | `x86`, `arm`, `arm64`, `mips`, `riscv`, `ppc`       |
| `bits`   | `16`, `32`, `64`                                    |
| `endian` | `little`, `big`                                     |
| `cpu`    | `generic`, `cortex`, `avr`, `68000`, `v850`         |
| `os`     | `linux`, `darwin`, `windows`, `ios`, `android`      |
| `base`   | base/map address, e.g. `0`, `0x1000`, `0x401000`    |

For `bytes` links Iaito builds an in-memory `malloc://` buffer, writes the
provided bytes into it and configures the session accordingly:

| Query param | radare2 config |
|-------------|----------------|
| `arch`      | `asm.arch`     |
| `bits`      | `asm.bits`     |
| `endian`    | `cfg.bigendian`|
| `cpu`       | `asm.cpu`      |
| `os`        | `asm.os`       |
| `base`      | `bin.baddr`    |


Examples
--------

Open a file and jump to an address:

```
iaito://file/tmp/a.out?addr=0x401000
```

Open a project and show the graph view at `main`:

```
iaito://project/r2?addr=sym.main&view=graph
```

Open a sample by SHA256 and show the decompiler:

```
iaito://sha256/<hash>?addr=sym.main&view=decompiler
```

Open a sample by SHA256 and select a region in the hex view:

```
iaito://sha256/<hash>?addr=0x401000&size=0x100&view=hex
```

Disassemble raw x86-64 shellcode:

```
iaito://bytes/554889e54883ec10?arch=x86&bits=64&view=disasm
```

Disassemble little-endian ARM code:

```
iaito://bytes/e92d4800e8bd8800?arch=arm&bits=32&endian=little
```

Analyze a raw big-endian MIPS firmware blob mapped at a base address:

```
iaito://bytes/<hex>?arch=mips&bits=32&endian=big&base=0x80000000
```

Open bytes and jump to an offset in the hex view:

```
iaito://bytes/<hex>?arch=x86&bits=64&addr=0x10&view=hex
```


SHA256 samples index
---------------------

`sha256` links resolve a hash to a local file through a small JSON index named
`samples.json`, stored in Iaito's application data directory
(`QStandardPaths::AppDataLocation`, e.g. `~/.local/share/radareorg/iaito/` on
Linux). The file maps lowercase SHA256 strings to absolute paths:

```json
{
  "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef": "/samples/a.out",
  "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210": "/samples/b.bin"
}
```

If the hash is present and the file exists, it is opened (and any `addr`/`view`
parameters are applied). If not, Iaito shows a dialog pointing at the index
file. Automatically downloading and registering unknown samples is out of scope
for now.


Registering the URI handler
---------------------------

The `iaito://` scheme must be associated with the Iaito executable at the OS
level. This happens automatically with the standard install targets, and can
also be run on its own.

### Make targets

```
make install-deeplink      # register iaito:// for the current user
make uninstall-deeplink     # remove the association
```

`make user-install` registers the handler as part of installation, and
`make uninstall` / `make user-uninstall` removes it. A system-wide
`make install` refreshes the desktop database so the `.desktop` MimeType
association is picked up.

### Linux

Registration relies on the desktop entry `src/org.radare.iaito.desktop`, which
declares:

```
Exec=iaito %u
MimeType=x-scheme-handler/iaito;
```

`scripts/deeplink/register.sh` runs `xdg-mime default` and
`update-desktop-database`; `scripts/deeplink/unregister.sh` removes the
association from `~/.config/mimeapps.list`.

### macOS

The app bundle declares the scheme in `Info.plist` via `CFBundleURLTypes`
(`CFBundleURLSchemes = iaito`). LaunchServices registers it automatically when
the bundle is installed; `scripts/deeplink/register.sh` forces a refresh with
`lsregister -f <iaito.app>`. The handler is dropped when the bundle is removed.

### Windows

`scripts/deeplink/register.bat` writes the `HKCU\Software\Classes\iaito` keys
(including `URL Protocol` and `shell\open\command`) and
`scripts/deeplink/unregister.bat` deletes them. Pass the path to `iaito.exe` as
the first argument:

```
scripts\deeplink\register.bat "C:\Program Files\iaito\iaito.exe"
scripts\deeplink\unregister.bat
```


Notes
-----

- `addr` is resolved through radare2's `s` command, so it accepts the full range
  of radare2 address expressions (flags, symbols, math, etc.).
- `size` is always resolved to a number before use, so it cannot inject
  additional radare2 commands.
- Query parameters are preferred over `#fragments`; future parameters can be
  added without changing the URI format.
- The four handlers (`file`, `project`, `sha256`, `bytes`) are intentionally the
  only top-level entry points.
