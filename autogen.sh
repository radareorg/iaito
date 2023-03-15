#!/bin/sh
#
# Look for the 'acr' tool here: https://github.com/radare/acr
# Clone last version of ACR from here:
#  git clone https://github.com/radare/acr
#
# -- pancake

[ -z "$EDITOR" ] && EDITOR=vim

r2pm -h >/dev/null 2>&1
if [ $? = 0 ]; then
	echo "Installing the last version of 'acr'..."
	r2pm -i acr > /dev/null
	r2pm -r acr -h > /dev/null 2>&1
	if [ $? = 0 ]; then
		echo "Running 'acr -p'..."
		r2pm -r acr -p
	else
		echo "Cannot find 'acr' in PATH"
	fi
else
	echo "Running acr..."
	acr -p
fi

. ./configure

sed \
	-e "s,^IAITO_VERSION_MAJOR.*,IAITO_VERSION_MAJOR = ${VERSION_MAJOR}," \
	-e "s,^IAITO_VERSION_MINOR.*,IAITO_VERSION_MINOR = ${VERSION_MINOR}," \
	-e "s,^IAITO_VERSION_PATCH.*,IAITO_VERSION_PATCH = ${VERSION_PATCH}," \
< src/Iaito.pro > src/Iaito.pro.sed

mv src/Iaito.pro.sed src/Iaito.pro
${EDITOR} .github/workflows/ci.yml
echo
echo "Press enter and add a new changelog entry for the new Flatpak version"
read A
${EDITOR} src/org.radare.iaito.appdata.xml
#V=`./configure -qV | cut -d - -f -1`
# meson rewrite kwargs set project / version "$V"
# if [ -n "$1" ]; then
#	echo "./configure $*"
#	./configure $*
#fi
