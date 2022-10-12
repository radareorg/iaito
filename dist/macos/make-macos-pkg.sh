#!/bin/sh

# based on
# http://blog.coolaj86.com/articles/how-to-unpackage-and-repackage-pkg-macos.html

# to uninstall:
# sudo pkgutil --forget org.radare.iaito
SKIPBUILD=0

SRC=/tmp/iaito-macos
PREFIX=/usr/local
DST="$(pwd)/tmp/iaito.unpkg"
if [ -n "$1" ]; then
	VERSION="$1"
else
	VERSION="`../../configure -qV`"
	[ -z "${VERSION}" ] && VERSION=5.3.l
fi

[ -z "${MAKE}" ] && MAKE=make

while : ; do
	[ -x "$PWD/configure" ] && break
	[ "$PWD" = / ] && break
	cd ..
done

[ ! -x "$PWD/configure" ] && exit 1

pwd
if [ ! -d build/iaito.app ]; then
	rm -rf "${SRC}"
	${MAKE} mrproper 2>/dev/null
	# ${MAKE} -j4 || exit 1
fi
export CFLAGS=-O2
if [ "$SKIPBUILD" = 0 ]; then
	./configure --prefix="${PREFIX}" || exit 1
fi
${MAKE} install install-translations PREFIX="${PREFIX}" DESTDIR=${SRC} || exit 1
mkdir -p "${DST}"
echo "DST=$DST"
if [ -d "${SRC}" ]; then
	(
		cd ${SRC} && \
		find . | cpio -o --format odc | gzip -c > "${DST}/Payload"
	)
	echo mkbom "${SRC}" "${DST}/Bom"
	mkbom "${SRC}" "${DST}/Bom"
	cp -rf dist/macos/Metadata/* "${DST}"
	pwd
	# Repackage
	pkgutil --flatten "${DST}" "${DST}/../../iaito-${VERSION}.pkg"
else
	echo "Failed install. DESTDIR is empty"
	exit 1
fi
