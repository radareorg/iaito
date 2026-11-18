#!/usr/bin/env bash
# Build a standalone Qt 6 Android APK, including radare2.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

usage() {
    cat <<'EOF'
Usage: scripts/android.sh [--check | --r2-only | --help]

Build iaito-VERSION.apk in the repository root (debug signed, for sideloading).
Requires Qt 6 for Android with Qt SVG and matching host tools, Android SDK,
NDK, JDK, Meson >= 0.63, Ninja, make, Python 3, pkg-config and a host C compiler.
Meson downloads radare2's pinned subprojects; Gradle may download dependencies.

Required for an APK:
  QT_ANDROID          Qt Android kit prefix, e.g. ~/Qt/6.10.3/android_arm64_v8a
  ANDROID_SDK_ROOT     Android SDK directory (ANDROID_HOME also accepted)

Optional:
  ANDROID_NDK_ROOT     NDK directory (otherwise newest SDK/ndk installation)
  ANDROID_ABI         arm64-v8a (default), armeabi-v7a, x86_64 or x86
  ANDROID_MIN_SDK     Minimum Android API, default 28
  ANDROID_TARGET_SDK  Target/compile API, default newest installed SDK platform
  R2_REPOSITORY       Git URL/path, default ./radare2 if present, otherwise GitHub
  R2_REF              Git revision to fetch, default HEAD; cached after first use
  BUILD_DIR           Separate build directory, default build-android/ABI
  APK_OUTPUT          Output filename, default ./iaito-VERSION.apk
  JOBS                Parallel jobs, 1 to 8 (default 8)

--check validates tools without building or downloading anything.
--r2-only builds and stages Android radare2; does not require Qt or the Java SDK.
Use a fresh BUILD_DIR when changing the toolchain, ABI or radare2 revision.
EOF
}

die() { echo "android.sh: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null || die "Missing tool: $1"; }

MODE=apk
case "${1:-}" in
    --help|-h) usage; exit 0 ;;
    --check) MODE=check ;;
    --r2-only) MODE=r2 ;;
    '') ;;
    *) usage >&2; exit 1 ;;
esac
[[ $# -le 1 ]] || die "Expected at most one argument"

for tool in python3 git make meson ninja pkg-config; do need "$tool"; done
ANDROID_ABI=${ANDROID_ABI:-arm64-v8a}
ANDROID_MIN_SDK=${ANDROID_MIN_SDK:-28}
JOBS=${JOBS:-8}
[[ "$JOBS" =~ ^[1-8]$ ]] || die "JOBS must be between 1 and 8"
[[ "$ANDROID_MIN_SDK" =~ ^[1-9][0-9]*$ ]] || die "ANDROID_MIN_SDK must be a positive integer"
(( ANDROID_MIN_SDK >= 28 )) || die "ANDROID_MIN_SDK must be at least 28"
case "$ANDROID_ABI" in
    arm64-v8a) TRIPLE=aarch64-linux-android; CPU_FAMILY=aarch64; CPU=aarch64 ;;
    armeabi-v7a) TRIPLE=armv7a-linux-androideabi; CPU_FAMILY=arm; CPU=armv7 ;;
    x86_64) TRIPLE=x86_64-linux-android; CPU_FAMILY=x86_64; CPU=x86_64 ;;
    x86) TRIPLE=i686-linux-android; CPU_FAMILY=x86; CPU=i686 ;;
    *) die "Unsupported ANDROID_ABI: $ANDROID_ABI" ;;
esac
case "$(uname -s)" in
    Linux) NDK_HOST=linux-x86_64 ;;
    Darwin) NDK_HOST=darwin-x86_64 ;;
    *) die "Build on Linux or macOS" ;;
esac

ANDROID_SDK_ROOT=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}
ANDROID_NDK_ROOT=${ANDROID_NDK_ROOT:-}
if [[ -z "$ANDROID_NDK_ROOT" && -n "$ANDROID_SDK_ROOT" ]]; then
    ANDROID_NDK_ROOT=$(python3 - "$ANDROID_SDK_ROOT/ndk" <<'PY'
import pathlib, sys
root = pathlib.Path(sys.argv[1])
versions = [p for p in root.glob('*') if (p / 'source.properties').is_file()]
versions = [p for p in versions if all(x.isdigit() for x in p.name.split('.'))]
print(max(versions, key=lambda p: tuple(map(int, p.name.split('.')))) if versions else '')
PY
    )
fi
[[ -d "$ANDROID_NDK_ROOT" ]] || die "Set ANDROID_NDK_ROOT to the NDK matching your Qt kit"
ANDROID_NDK_ROOT=$(cd -- "$ANDROID_NDK_ROOT" && pwd)
TOOLCHAIN="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/$NDK_HOST/bin"
[[ -x "$TOOLCHAIN/${TRIPLE}${ANDROID_MIN_SDK}-clang" ]] \
    || die "Missing NDK compiler for $ANDROID_ABI, API $ANDROID_MIN_SDK"

QT_ANDROID=${QT_ANDROID:-}
if [[ "$MODE" != r2 ]]; then
    for tool in java javac keytool; do need "$tool"; done
    [[ -n "$QT_ANDROID" && -x "$QT_ANDROID/bin/qmake" ]] \
        || die "Set QT_ANDROID to a Qt 6 Android kit prefix"
    QT_ANDROID=$(cd -- "$QT_ANDROID" && pwd)
    [[ -f "$QT_ANDROID/mkspecs/android-clang/qmake.conf" ]] \
        || die "QT_ANDROID does not contain the Android mkspec"
    [[ -f "$QT_ANDROID/lib/libQt6Core_${ANDROID_ABI}.so" ]] \
        || die "QT_ANDROID does not contain Qt 6 libraries for $ANDROID_ABI"
    [[ -f "$QT_ANDROID/lib/libQt6SvgWidgets_${ANDROID_ABI}.so" ]] \
        || die "Install Qt SVG for this Android kit"
    [[ -f "$QT_ANDROID/src/android/templates/AndroidManifest.xml" ]] \
        || die "Qt Android deployment templates are missing from this kit"
    [[ -d "$ANDROID_SDK_ROOT/platforms" ]] || die "Set ANDROID_SDK_ROOT and install an SDK platform"
    ANDROID_SDK_ROOT=$(cd -- "$ANDROID_SDK_ROOT" && pwd)
    ANDROID_TARGET_SDK=${ANDROID_TARGET_SDK:-$(python3 - "$ANDROID_SDK_ROOT" <<'PY'
import pathlib, sys
platforms = pathlib.Path(sys.argv[1], 'platforms').glob('android-*')
apis = [int(p.name[8:]) for p in platforms if p.name[8:].isdigit() and (p / 'android.jar').is_file()]
print(max(apis) if apis else '')
PY
    )}
    [[ "$ANDROID_TARGET_SDK" =~ ^[1-9][0-9]*$ ]] || die "No Android SDK platform found"
    (( ANDROID_TARGET_SDK >= ANDROID_MIN_SDK )) || die "Target API is below minimum API"
    [[ -f "$ANDROID_SDK_ROOT/platforms/android-$ANDROID_TARGET_SDK/android.jar" ]] \
        || die "Install SDK platform android-$ANDROID_TARGET_SDK"
    [[ -d "$ANDROID_SDK_ROOT/build-tools" ]] || die "Install Android SDK build-tools"
fi
if [[ "$MODE" == check ]]; then
    echo "Android prerequisites found for $ANDROID_ABI (NDK: $ANDROID_NDK_ROOT)"
    exit 0
fi

BUILD_DIR=${BUILD_DIR:-$ROOT/build-android/$ANDROID_ABI}
mkdir -p -- "$BUILD_DIR"
BUILD_DIR=$(cd -- "$BUILD_DIR" && pwd)
VERSION=$(python3 "$ROOT/scripts/get_version.py")
VERSION_CODE=$(python3 -c 'import sys; a, b, c = map(int, sys.argv[1].split(".")); print(a * 10000 + b * 100 + c)' "$VERSION")
APK_OUTPUT=${APK_OUTPUT:-$ROOT/iaito-$VERSION.apk}
R2_REPOSITORY=${R2_REPOSITORY:-$ROOT/radare2}
if [[ "$R2_REPOSITORY" == "$ROOT/radare2" && ! -e "$R2_REPOSITORY/.git" ]]; then
    R2_REPOSITORY=https://github.com/radareorg/radare2.git
fi
R2_REF=${R2_REF:-HEAD}
export ANDROID_SDK_ROOT ANDROID_NDK_ROOT
export ANDROID_NDK_HOST=$NDK_HOST ANDROID_API_VERSION=android-${ANDROID_TARGET_SDK:-$ANDROID_MIN_SDK}
export ANDROID_NDK_PLATFORM=android-$ANDROID_MIN_SDK
export ANDROID_TARGET_ARCH=$ANDROID_ABI ALL_ANDROID_ABIS=$ANDROID_ABI

# Never reconfigure an existing desktop checkout or mix Android toolchains.
python3 "$ROOT/scripts/android/prepare.py" "$BUILD_DIR" "$TOOLCHAIN" \
    "$TRIPLE" "$ANDROID_MIN_SDK" "$CPU_FAMILY" "$CPU" "$R2_REPOSITORY" "$R2_REF"
if [[ ! -f "$BUILD_DIR/radare2/.iaito-android-source" ]]; then
    if [[ ! -d "$BUILD_DIR/radare2/.git" ]]; then
        git init -q "$BUILD_DIR/radare2"
        git -C "$BUILD_DIR/radare2" remote add origin "$R2_REPOSITORY"
    fi
    git -C "$BUILD_DIR/radare2" fetch --depth 1 origin "$R2_REF"
    git -C "$BUILD_DIR/radare2" checkout --detach FETCH_HEAD
    touch "$BUILD_DIR/radare2/.iaito-android-source"
fi

echo "Building radare2 for $ANDROID_ABI (logs: $BUILD_DIR)"
make -f "$ROOT/scripts/android/Makefile" -j"$JOBS" radare2 \
    "ROOT=$ROOT" "BUILD_DIR=$BUILD_DIR" "JOBS=$JOBS" > /dev/null
python3 "$ROOT/scripts/android/prepare.py" --assets "$BUILD_DIR"
if [[ "$MODE" == r2 ]]; then
    echo "Android radare2 staged in $BUILD_DIR/prefix"
    exit 0
fi

echo "Building iaito $VERSION for $ANDROID_ABI"
python3 "$ROOT/scripts/android/prepare.py" --package "$BUILD_DIR" "$QT_ANDROID"
make -f "$ROOT/scripts/android/Makefile" -j"$JOBS" apk \
    "ROOT=$ROOT" "BUILD_DIR=$BUILD_DIR" "QT_ANDROID=$QT_ANDROID" \
    "ANDROID_ABI=$ANDROID_ABI" "ANDROID_MIN_SDK=$ANDROID_MIN_SDK" \
    "ANDROID_VERSION_CODE=$VERSION_CODE" \
    "ANDROID_TARGET_SDK=$ANDROID_TARGET_SDK" > /dev/null
APK="$BUILD_DIR/iaito/android-build/iaito.apk"
[[ -f "$APK" ]] || die "Qt did not produce $APK"
python3 "$ROOT/scripts/android/prepare.py" --verify-apk "$BUILD_DIR" "$APK" "$ANDROID_ABI"
cp -- "$APK" "$APK_OUTPUT"
echo "Created $APK_OUTPUT (debug signed; $ANDROID_ABI)"
