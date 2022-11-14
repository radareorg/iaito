#!/bin/sh

SKIPBUILD=0
BUNDLE=1

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
	( # bundle
		LIBS=`rabin2 -ql ${SRC}/usr/local/bin/iaito | grep -v System`
		mkdir -p bundle
		for a in $LIBS ; do
			echo "LIB $a"
			D="`basename '$a'`"
			cp -f $a bundle
			LIBS2=`rabin2 -ql ${a} | grep -v System`
			for b in $LIBS2 ; do
				D2="`basename '$a'`"
				echo "$a $b"
				cp $b bundle
			done
		done
		# binary patch the binary
		echo cp -f ${SRC}/usr/local/bin/iaito bundle/
		cp -f ${SRC}/usr/local/bin/iaito bundle/
		for a in bundle/* ; do
			libs=`rabin2 -ql $a | grep -v System`
			for b in $libs ; do
				bn=`basename "$b"`
				if ls bundle | grep -q "$bn" ; then
					# its bundled
					from="$b"
					to="@executable_path/lib/$bn"
					echo install_name_tool "$from" "$to" "$a"
					install_name_tool -change "$from" "$to" "$a"
				fi
			done
		done
		rm -rf bundle/lib
		mkdir -p bundle/lib
		mv bundle/* bundle/lib
		mv bundle/lib/iaito bundle/
		# cp -f ${SRC}/usr/local/bin/iaito bundle/
		exit 1
	)
	( # massage
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
