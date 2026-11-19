# Regression tests

The Qt test suites use the same radare2 and Qt installation as iaito, plus Qt's
Test module. Configure the project from the repository root first:

```sh
./configure
```

Build and run a suite with its handwritten Makefile, for example:

```sh
make -C tests/namelist -j8 > /dev/null
QT_QPA_PLATFORM=offscreen tests/namelist/build/test_namelist
```

Each suite also provides a `check` target that builds and runs its tests:

```sh
make -C tests/namelist -j8 check
```

The following suites cover additional core and widget behavior:

| Directory | Coverage |
| --- | --- |
| `android` | Runtime installation, encoded document URIs, APK assets, ABI and r2 requirements |
| `binlisting` | Symbol names, type filters, classes, and hierarchy updates |
| `dataanalysis` | Byte distributions, sparse reads, sampling, and plot navigation |
| `decompileroptions` | Listing, editing, and selecting decompiler options |
| `disassembly` | Token navigation, hit bounds, and scroll repainting |
| `exceptioninfo` | Exception ranges, metadata, navigation, and compact bars |
| `hexpat` | Pattern availability, displayed values, and address navigation |
| `ipad` | Offline static SDB preparation and universal iPhone/iPad bundle/resource verification (`python3 tests/ipad/test_package.py`) |
| `memoryshortcuts` | Switching among disassembly, graph, and decompiler views |
| `mobilemainwindow` | Mobile menus, address entry, panel grids, sidebar selection, rotation, edge swipes, opening files and layout recovery |
| `namelist` | Name hierarchies, sorting, filtering, selection, and shared controls |
| `package_manager` | Package filters, selection, and commands using a fake r2pm |
| `responsivedialogs` | Dialog sizing, action visibility, scrolling, reflow, and generated version labels |
| `switchtables` | Switch discovery, case values and ASCII, filtering, navigation, editing, and refresh |

`binlisting` skips its optional real-binary test unless `IAITO_TEST_BINARY` points
to a C++ fixture with demangled symbols, classes, and the expected `moneypunct`
and `pos_format` names. `hexpat` skips its real-plugin test unless `HEXPAT_PLUGIN`
points to `hexpat.r2.js`; see [its README](hexpat/README.md).

To save the optional name-list rendering screenshots, set
`IAITO_NAMELIST_SCREENSHOTS` to an existing output directory.

The `responsivedialogs` suite exercises 27 dialog types with normal and larger
text, including expanded analysis options, all file-opening tabs, and Settings
categories. Set `IAITO_DIALOG_SCREENSHOTS` to an existing directory to save their
renderings. The `catalog` test also runs on a small virtual screen using a Qt 6
offscreen platform with `configfile` support, for example:

```sh
printf '%s\n' '{"screens":[{"name":"phone","width":360,"height":640,"logicalDpi":96,"logicalBaseDpi":96}]}' > /tmp/iaito-phone-screen.json
QT_QPA_PLATFORM=offscreen:configfile=/tmp/iaito-phone-screen.json tests/responsivedialogs/build/test_responsivedialogs catalog
```

Track test sources, `.pro` files, resources, documentation, and the handwritten
Makefiles. Generated Makefiles, executables, objects, and moc output belong in
each suite's ignored `build/` directory.
