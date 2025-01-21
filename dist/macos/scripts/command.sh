#!/bin/sh

CMD=$(basename "$0")
APPDIR=$(cd "$(dirname "$0")/../../../.."; pwd)

R2_BINDIR="${APPDIR}/Contents/Helpers"
R2_LIBDIR="${APPDIR}/Contents/Frameworks"
R2_PREFIX="${APPDIR}/Contents/Resources/radare2"

export R2_BINDIR
export R2_LIBDIR
export R2_PREFIX

exec "${R2_BINDIR}/${CMD}" "$@"
