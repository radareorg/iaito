#!/bin/sh

# based on
# http://blog.coolaj86.com/articles/how-to-unpackage-and-repackage-pkg-macos.html

# to uninstall:
# sudo pkgutil --forget org.radare.r2cutter

SRC=/tmp/r2cutter-macos
PREFIX=/usr/local
DST="$(pwd)/macos-pkg/r2cutter.unpkg"
if [ -n "$1" ]; then
	VERSION="$1"
else
	VERSION="`./configure -qv`"
	[ -z "${VERSION}" ] && VERSION=5.1.0
fi
[ -z "${MAKE}" ] && MAKE=make

while : ; do
	[ -x "$PWD/configure" ] && break
	[ "$PWD" = / ] && break
	cd ..
done

[ ! -x "$PWD/configure" ] && exit 1

rm -rf "${SRC}"
${MAKE} mrproper 2>/dev/null
export CFLAGS=-O2
./configure --prefix="${PREFIX}" || exit 1
# ${MAKE} -j4 || exit 1
${MAKE} install PREFIX="${PREFIX}" DESTDIR=${SRC} || exit 1
if [ -d "${SRC}" ]; then
	(
		cd ${SRC} && \
		find . | cpio -o --format odc | gzip -c > "${DST}/Payload"
	)
	mkbom ${SRC} "${DST}/Bom"
	# Repackage
	pkgutil --flatten "${DST}" "${DST}/../r2cutter-${VERSION}.pkg"
else
	echo "Failed install. DESTDIR is empty"
	exit 1
fi
