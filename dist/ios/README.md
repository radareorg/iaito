# iaito for iOS: iPhone and iPad

`scripts/ios.sh` downloads Qt 6 for iOS and matching Mac tools, fetches radare2,
builds static device and simulator libraries, and generates the native Xcode
project at `dist/ios/iaito.xcodeproj`. It uses the source list in
`src/Iaito.pro`, so new widgets do not need a second project-file entry.
The app is universal: one build runs on both iPhone and iPad. The existing
`scripts/ipad.sh` entry point remains an alias. Build products and dependency
caches use `build-ipad` by default.

## First build

Install full Xcode, select it with `xcode-select`, launch it once to finish SDK
setup, and install Python 3, Git, Meson >= 0.63 and Ninja. For example:

```sh
brew install python meson ninja
./scripts/ios.sh
```

The default build is an unsigned arm64 device app in
`build-ipad/products/build/iaito.app`. It checks compilation and packaging;
running on a device requires signing. Compilation uses at most eight jobs.
Nothing is installed on a device or uploaded to Apple automatically.

Qt 6.10.3, Qt SVG, and matching Mac tools are downloaded through
[aqtinstall](https://aqtinstall.readthedocs.io/en/latest/cli.html#install-qt-command)
to `build-ipad/Qt`. The installer runs in `build-ipad/tools`, without changing
the system Python. Set `QT_VERSION` to select another binary release, or
`QT_IOS` to use an existing iOS kit with its matching host tools. The script
does not install into an explicitly supplied `QT_IOS` directory.

```sh
./scripts/ios.sh --install-qt  # Just download Qt and its host tools
./scripts/ios.sh --check       # Read-only check; no downloads
./scripts/ios.sh --project-only
./scripts/ios.sh 6.2.6         # Pin radare2 to a tag (or pass a commit/ref)
```

With no argument, every build fetches the latest radare2 `HEAD` from
`https://github.com/radareorg/radare2.git`. `R2_REF` and `R2_REPOSITORY` can
override this. Sources and builds are keyed by the resolved commit, so updating
HEAD cannot silently reuse an older library. The repository's desktop radare2
checkout and installation are not used or reconfigured. The source copy gets
small Meson adjustments for a Mac-native SDB database generator and static SDB.
Radare2's pinned dependencies are downloaded by Meson.

## Run on an iPhone or iPad

Open `dist/ios/iaito.xcodeproj` in Xcode, select the **iaito** scheme, and choose
your team under **Signing & Capabilities**. Use a unique bundle identifier owned
by that team, choose an attached iPhone or iPad, and Run. Enable Developer Mode on
the device when requested by Xcode. Alternatively, prepare a signed build:

```sh
BUNDLE_ID=org.example.iaito DEVELOPMENT_TEAM=ABCDE12345 ./scripts/ios.sh
```

The app targets iPhone and iPad, with iOS/iPadOS 17.0 as the default minimum.
iPhone supports portrait and both landscape orientations; iPad also supports
upside-down portrait and resizable tablet windows. `IOS_MIN_VERSION` overrides
the deployment target; it must meet the selected Qt kit's minimum.
The mobile UI and Tablet theme provide the touch layout. Portrait phones show
one panel at a time, with **Panels** to switch views and **Browse** to toggle
the sidebar panel. Landscape phones and tablets can show the sidebar alongside
the current panel. Swipe the tab strip to scroll it and tap a tab to select it.
The Files app and Finder file sharing expose iaito's Documents directory for
importing binaries and saving projects.
Radare2's runtime databases live in the app bundle, while settings, cache, and
user data use writable directories inside the application sandbox.

**Browse Files…** uses the system document picker, including iCloud Drive and
installed Files providers. Selected binaries are imported into
`Documents/Imports` so analysis and recent files remain available after a restart.
Edits affect the imported copy. The keyboard stays dismissed while browsing;
Return in the manual path field dismisses it before tapping Open.

## Simulator

```sh
./scripts/ios.sh --simulator
```

This produces `build-ipad/products/simulator/iaito.app` for both iPhone and iPad
simulators. Choose either device type in Xcode and Run. Qt's downloaded iOS
kit supplies an arm64 device slice and an **x86_64 simulator** slice. On Apple
Silicon, install Rosetta and use a simulator runtime that supports Rosetta;
select its Rosetta destination in Xcode. `arch -x86_64 /usr/bin/true` checks
whether Rosetta is available; a `Bad CPU type in executable` error means it
needs to be installed before running this simulator build. See
[Qt's iOS notes](https://doc.qt.io/qt-6/ios.html). An arm64 simulator requires a
Qt kit built for that target; pass `SIMULATOR_ARCHS=arm64` with that custom kit.
`SIMULATOR_ARCHS="arm64 x86_64"` also works with a matching universal custom kit.

## Archive and export

```sh
BUNDLE_ID=org.example.iaito DEVELOPMENT_TEAM=ABCDE12345 BUILD_NUMBER=1 \
    ./scripts/ios.sh --archive

BUNDLE_ID=org.example.iaito DEVELOPMENT_TEAM=ABCDE12345 BUILD_NUMBER=2 \
    ./scripts/ios.sh --export
```

The archive is `build-ipad/products/iaito.xcarchive`. Export writes the IPA under
`build-ipad/products/export/` using `app-store-connect` and automatic signing.
Xcode needs the team's Apple account and appropriate signing assets. Override
`EXPORT_OPTIONS_PLIST` with an Xcode export-options plist for ad hoc, development,
or manual signing. You can also use **Product → Archive** and the Organizer in
Xcode. Re-running the script regenerates the project; use environment settings
to preserve signing choices across regeneration.

The build bundles icons, a launch screen and a privacy manifest. Radare2 uses
static plugins with dynamic loading, fork, subprocess execution and native
debugging disabled for iOS. Features requiring external commands or separately
installed native plugins are consequently unavailable. Archive/export prepares
the artifact; App Store Connect metadata and Apple's review are separate steps.

The iOS build uses the SDK's zlib for both radare2 and otezip. Otezip's bundled
partial zlib implementation otherwise overrides Qt's zlib calls in the static
executable and crashes shader decompression during the first window repaint.
The library and app verifiers reject definitions of these system zlib symbols.
The isolated sources also receive guards for subprocess paths that ignore
radare2's build options; verification rejects subprocess and ptrace imports.
Developer Mode is required for development-signed installations; App Store and
TestFlight installations do not need it. This does not bypass the iOS sandbox.

## Dependency artifacts and logs

`--r2-only` builds without Qt. Its output under
`build-ipad/r2/COMMIT/stage/` contains:

- `iphoneos/lib/libradare2.a` and headers (arm64 device).
- `iphonesimulator/lib/libradare2.a` and headers (selected simulator architectures).
- `Radare2.xcframework`, including radare2's private static dependencies.
- `radare2/share/radare2/`, the runtime databases to bundle alongside the library.
- `source.json`, recording the source commit and deployment target.

The Xcode project selects the matching archive for its SDK. Its generated files
contain local dependency paths and are ignored by Git. `ipad.pri`, `Makefile`,
`prepare.py`, the plist and asset catalog are the maintained project sources.

Use `BUILD_DIR` for another dependency/build cache, `QT_ROOT` for a managed Qt
download directory, and `JOBS` (1–8) to limit compilation. Use a fresh `BUILD_DIR`
when changing Xcode's SDK or the deployment target. Logs live in
`build-ipad/r2/COMMIT/build.log`, `qt-install.log`, `iaito-configure.log` and
`iaito-build.log`. A failed command prints the tail of its log.
