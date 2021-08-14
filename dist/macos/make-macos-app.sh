#!/bin/sh

SKIPBUILD=1

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
	make -C .. translations
fi
${MAKE} install PREFIX="${PREFIX}" DESTDIR=${SRC} || exit 1
mkdir -p "${DST}"
echo "DST=$DST"
if [ -d "${SRC}" ]; then
	(
		cd ${SRC} && find .
		mv usr/local/share/iaito/translations Applications/iaito.app/Contents/Resources || exit 1
		cd Applications || exit 1
		rm -f /tmp/iaito-${VERSION}.zip
		zip -r /tmp/iaito-${VERSION}.zip iaito.app
	)
	mv /tmp/iaito-${VERSION}.zip dist/macos
else
	echo "Failed install. DESTDIR is empty"
	exit 1
fi
