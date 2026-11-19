#!/usr/bin/env bash
# Build iaito for iPhone and iPad with isolated radare2 and Qt 6 installations.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

usage() {
    cat <<'EOF'
Usage: scripts/ios.sh [OPTION] [RADARE2_TAG_OR_REF]

Build a universal iPhone/iPad app and generate dist/ios/iaito.xcodeproj.
With no ref, fetch the latest radare2 HEAD on every invocation. A tag or commit
can pin the source.
Requires macOS, full Xcode (selected with xcode-select), Python 3, Git, make,
Meson >= 0.63 and Ninja. Missing Qt kits are downloaded automatically.

  --check          Check installed prerequisites without downloads or writes
  --install-qt     Install Qt for iOS and matching Mac tools, then exit
  --r2-only        Build device/simulator static libraries and an XCFramework
  --project-only   Prepare dependencies and Xcode project without building iaito
  --simulator      Build an unsigned app for iPhone and iPad simulators
  --archive        Create a signed device .xcarchive (requires DEVELOPMENT_TEAM)
  --export         Archive and export an App Store .ipa (requires DEVELOPMENT_TEAM)
  --help           Show this help

Environment:
  R2_REPOSITORY     Default https://github.com/radareorg/radare2.git
  R2_REF            Default HEAD; positional ref takes precedence
  BUILD_DIR         Default ./build-ipad (sources, dependencies, products, logs)
  QT_VERSION        Download version, default 6.10.3
  QT_ROOT           Managed Qt directory, default BUILD_DIR/Qt
  QT_IOS            Existing Qt 6 iOS prefix; skips automatic Qt installation
  IOS_MIN_VERSION   Deployment target, default 17.0 (must also satisfy the Qt kit)
  SIMULATOR_ARCHS   Default x86_64 (Qt's binary kit); override for a custom Qt kit
  BUNDLE_ID         Default org.radare.iaito; use your own ID for signing
  BUILD_NUMBER      CFBundleVersion, default 1; increment for store uploads
  DEVELOPMENT_TEAM Apple team ID; omit for an unsigned device test build
  EXPORT_OPTIONS_PLIST  Optional custom xcodebuild export options
  JOBS              1 to 8, default 8

Open dist/ios/iaito.xcodeproj, select your signing team and an attached iPhone
or iPad, and Run. Select Any iOS Device and Archive for distribution. No device install
or App Store upload is performed by this script. See dist/ios/README.md.
EOF
}

die() { echo "ios.sh: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null || die "Missing tool: $1"; }
MODE=build
REF_ARGUMENT=
for arg in "$@"; do
    case "$arg" in
        --help|-h) usage; exit 0 ;;
        --check|--install-qt|--r2-only|--project-only|--simulator|--archive|--export)
            [[ "$MODE" == build ]] || die "Choose only one build option"
            MODE=${arg#--} ;;
        -*) die "Unknown option: $arg" ;;
        *) [[ -z "$REF_ARGUMENT" ]] || die "Expected at most one radare2 ref"
            REF_ARGUMENT=$arg ;;
    esac
done

[[ "$(uname -s)" == Darwin ]] || die "Build on macOS with full Xcode installed"
for tool in python3 git make xcrun xcodebuild; do need "$tool"; done
JOBS=${JOBS:-8}
[[ "$JOBS" =~ ^[1-8]$ ]] || die "JOBS must be between 1 and 8"
IOS_MIN_VERSION=${IOS_MIN_VERSION:-17.0}
[[ "$IOS_MIN_VERSION" =~ ^[1-9][0-9]*\.[0-9]+$ ]] || die "Invalid IOS_MIN_VERSION"
SIMULATOR_ARCHS=${SIMULATOR_ARCHS:-x86_64}
export SIMULATOR_ARCHS
for arch in $SIMULATOR_ARCHS; do
    case "$arch" in arm64|x86_64) ;; *) die "Unsupported simulator architecture: $arch" ;; esac
done
[[ -n "$SIMULATOR_ARCHS" ]] || die "SIMULATOR_ARCHS cannot be empty"
DEVICE_SDK=$(xcrun --sdk iphoneos --show-sdk-path) || die "Install and select full Xcode"
SIMULATOR_SDK=$(xcrun --sdk iphonesimulator --show-sdk-path) || die "Install Xcode's iOS SDK"
BUILD_DIR=${BUILD_DIR:-$ROOT/build-ipad}
QT_ROOT=${QT_ROOT:-$BUILD_DIR/Qt}
QT_VERSION=${QT_VERSION:-6.10.3}
QT_EXPLICIT=${QT_IOS:+yes}
QT_IOS=${QT_IOS:-$QT_ROOT/$QT_VERSION/ios}
BUILD_DIR=$(python3 -c 'import os, sys; print(os.path.abspath(sys.argv[1]))' "$BUILD_DIR")
QT_ROOT=$(python3 -c 'import os, sys; print(os.path.abspath(sys.argv[1]))' "$QT_ROOT")
QT_IOS=$(python3 -c 'import os, sys; print(os.path.abspath(sys.argv[1]))' "$QT_IOS")
BUNDLE_ID=${BUNDLE_ID:-org.radare.iaito}
BUILD_NUMBER=${BUILD_NUMBER:-1}
[[ "$BUNDLE_ID" =~ ^[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+$ ]] || die "Invalid BUNDLE_ID"
[[ "$BUILD_NUMBER" =~ ^[1-9][0-9]*$ ]] || die "BUILD_NUMBER must be a positive integer"
if [[ "$MODE" == archive || "$MODE" == export ]]; then
    [[ -n "${DEVELOPMENT_TEAM:-}" ]] || die "Set DEVELOPMENT_TEAM for signed archives"
fi

check_qt() {
    [[ -x "$QT_IOS/bin/qmake" && -f "$QT_IOS/mkspecs/macx-ios-clang/qmake.conf" ]] \
        || die "Set QT_IOS to a Qt 6 iOS kit, or run scripts/ios.sh --install-qt"
    [[ -f "$QT_IOS/mkspecs/modules/qt_lib_svgwidgets.pri" ]] || die "The iOS Qt SVG module is missing"
    make -f "$ROOT/dist/ios/Makefile" -j"$JOBS" check-qt \
        "ROOT=$ROOT" "QT_IOS=$QT_IOS" "IOS_MIN_VERSION=$IOS_MIN_VERSION" > /dev/null
}

if [[ "$MODE" == check ]]; then
    for tool in meson ninja; do need "$tool"; done
    check_qt
    echo "iOS prerequisites found (Qt: $QT_IOS, SDK: $DEVICE_SDK)"
    exit 0
fi

mkdir -p -- "$BUILD_DIR"
BUILD_DIR=$(cd -- "$BUILD_DIR" && pwd)
if [[ "$MODE" != r2-only ]]; then
    if [[ ! -x "$QT_IOS/bin/qmake" || "$MODE" == install-qt ]]; then
        [[ -z "$QT_EXPLICIT" ]] || die "QT_IOS is explicit; install or repair that kit yourself, or unset QT_IOS"
        echo "Installing Qt $QT_VERSION for iOS and matching Mac tools (logs: $BUILD_DIR)"
        [[ -x "$BUILD_DIR/tools/bin/python" ]] || python3 -m venv "$BUILD_DIR/tools"
        "$BUILD_DIR/tools/bin/python" -m pip install 'aqtinstall==3.3.0' \
            > "$BUILD_DIR/qt-installer.log" 2>&1 \
            || { tail -60 "$BUILD_DIR/qt-installer.log" >&2; exit 1; }
        (cd -- "$BUILD_DIR" && "$BUILD_DIR/tools/bin/python" -m aqt \
            install-qt mac ios "$QT_VERSION" ios \
            --autodesktop --archives qtbase qtsvg -O "$QT_ROOT") \
            > "$BUILD_DIR/qt-install.log" 2>&1 \
            || { tail -60 "$BUILD_DIR/qt-install.log" >&2; exit 1; }
    fi
    check_qt
    QT_IOS=$(cd -- "$QT_IOS" && pwd)
    [[ "$MODE" != install-qt ]] || { echo "Qt ready in $QT_IOS"; exit 0; }
fi
for tool in meson ninja; do need "$tool"; done

R2_REPOSITORY=${R2_REPOSITORY:-https://github.com/radareorg/radare2.git}
R2_REF=${REF_ARGUMENT:-${R2_REF:-HEAD}}
CACHE="$BUILD_DIR/radare2.git"
if [[ ! -d "$CACHE" ]]; then
    git init --bare -q "$CACHE"
fi
echo "Fetching radare2 $R2_REF"
git --git-dir="$CACHE" fetch --depth 1 "$R2_REPOSITORY" "$R2_REF"
R2_COMMIT=$(git --git-dir="$CACHE" rev-parse 'FETCH_HEAD^{commit}')
R2_SOURCE="$BUILD_DIR/sources/$R2_COMMIT"
if [[ ! -f "$R2_SOURCE/.iaito-source" ]]; then
    mkdir -p -- "$R2_SOURCE"
    git --git-dir="$CACHE" archive "$R2_COMMIT" | tar -x -C "$R2_SOURCE"
    touch "$R2_SOURCE/.iaito-source"
fi
R2_BUILD="$BUILD_DIR/r2/$R2_COMMIT"
mkdir -p -- "$R2_BUILD"
echo "Building radare2 $R2_COMMIT for iOS devices and simulators (logs: $R2_BUILD)"
make -f "$ROOT/dist/ios/Makefile" -j"$JOBS" radare2 \
    "ROOT=$ROOT" "BUILD_DIR=$BUILD_DIR" "R2_SOURCE=$R2_SOURCE" \
    "R2_BUILD=$R2_BUILD" "R2_COMMIT=$R2_COMMIT" "JOBS=$JOBS" \
    "IOS_MIN_VERSION=$IOS_MIN_VERSION" "SIMULATOR_ARCHS=$SIMULATOR_ARCHS" > /dev/null
if [[ "$MODE" == r2-only ]]; then
    echo "Radare2 static libraries and XCFramework ready in $R2_BUILD/stage"
    exit 0
fi

echo "Generating dist/ios/iaito.xcodeproj"
python3 "$ROOT/dist/ios/prepare.py" project "$ROOT" "$BUILD_DIR" "$R2_BUILD" \
    "$QT_IOS" "$IOS_MIN_VERSION" "$BUNDLE_ID" "$BUILD_NUMBER" "${DEVELOPMENT_TEAM:-}"
make -f "$ROOT/dist/ios/Makefile" -j"$JOBS" project \
    "ROOT=$ROOT" "BUILD_DIR=$BUILD_DIR" "QT_IOS=$QT_IOS" > /dev/null
[[ "$MODE" != project-only ]] || { echo "Open $ROOT/dist/ios/iaito.xcodeproj"; exit 0; }

export BUILD_DIR JOBS DEVELOPMENT_TEAM=${DEVELOPMENT_TEAM:-}
export EXPORT_OPTIONS_PLIST=${EXPORT_OPTIONS_PLIST:-}
make -f "$ROOT/dist/ios/Makefile" -j"$JOBS" "$MODE" "ROOT=$ROOT" > /dev/null
echo "iOS products: $BUILD_DIR/products (Xcode project: $ROOT/dist/ios/iaito.xcodeproj)"
