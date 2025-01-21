#!/bin/sh -e

R2PKGDIR="$1"
APPDIR="$2"

SCRIPTS="$(dirname "$0")"
R2DIR="${R2PKGDIR}/Payload/usr/local"
R2V=$(readlink "${R2DIR}/lib/radare2/last")

fix_binary() {
  echo "Change library paths for \"$1\"..."
  ARGS=$(otool -L "$1" | awk '/\/usr\/local\/lib\/libr_/{dst=$1; sub(/\/usr\/local\/lib/,"@executable_path/../Frameworks", dst); print "-change "$1" "dst}')
  [ -n "$ARGS" ] && install_name_tool $ARGS "$1"
}

mkdir -p \
  "${APPDIR}/Contents/Helpers" \
  "${APPDIR}/Contents/Frameworks" \
  "${APPDIR}/Contents/PlugIns/radare2" \
  "${APPDIR}/Contents/Resources/radare2/bin" \
  "${APPDIR}/Contents/Resources/radare2/lib/radare2/${R2V}/"

cp -a "${R2DIR}/bin/"*                      "${APPDIR}/Contents/Helpers/"
cp -a "${R2DIR}/lib/radare2/${R2V}/"*.dylib "${APPDIR}/Contents/PlugIns/radare2/"
cp -a "${R2DIR}/lib/"*.dylib                "${APPDIR}/Contents/Frameworks/"
cp -a "${R2DIR}/include"                    "${APPDIR}/Contents/Resources/radare2/"
cp -a "${R2DIR}/share"                      "${APPDIR}/Contents/Resources/radare2/"
#cp -a "${R2DIR}/lib/pkgconfig"              "${APPDIR}/Contents/Resources/radare2/lib/"
cp -p "${SCRIPTS}/command.sh"               "${APPDIR}/Contents/Resources/radare2/bin/radare2"
cp -a "${R2DIR}/lib/radare2/last"           "${APPDIR}/Contents/Resources/radare2/lib/radare2/"
#cp -a "${R2DIR}/lib/radare2/${R2V}/"*.js    "${APPDIR}/Contents/Resources/radare2/lib/radare2/${R2V}/"

(
  cd "${APPDIR}/Contents/MacOS"
  fix_binary "iaito"
)

(
  cd "${APPDIR}/Contents/Helpers"
  for c in *; do
    [ -L "$c" ] || fix_binary "$c"
    [ "$c" != "radare2" ] && ln -s radare2 "../Resources/radare2/bin/$c"
  done
)

(
  LIBS=$(cd "${R2DIR}/lib"; ls *.dylib)
  cd "${APPDIR}/Contents/Frameworks"
  for c in $LIBS; do
    [ -L "$c" ] || fix_binary "$c"
    c2=$c # Resolve upto 2 link levels
    [ -L "$c2" ] && c2=$(readlink "$c2")
    [ -L "$c2" ] && c2=$(readlink "$c2")
    ln -s "../../../Frameworks/$c2" "../Resources/radare2/lib/$c"
  done
)

(
  cd "${APPDIR}/Contents/PlugIns/radare2"
  for c in *.dylib; do
    [ -L "$c" ] || fix_binary "$c"
    ln -s "../../../../../PlugIns/radare2/$c" "../../Resources/radare2/lib/radare2/${R2V}/$c"
  done
)
