#!/bin/sh

CMD=$(basename "$0")
APPDIR=$(cd "$(dirname "$0")/../../../.."; pwd)

R2_BINDIR="${APPDIR}/Contents/Helpers"
R2_LIBDIR="${APPDIR}/Contents/Frameworks"
R2_PREFIX="${APPDIR}/Contents/Resources/radare2"

XDG_CACHE_HOME="${HOME}/Library/Caches/radareorg/iaito"
XDG_CONFIG_HOME="${HOME}/Library/Preferences/radareorg/iaito"
XDG_DATA_HOME="${HOME}/Library/Application Support/radareorg/iaito"

export R2_BINDIR
export R2_LIBDIR
export R2_PREFIX
export XDG_CACHE_HOME
export XDG_CONFIG_HOME
export XDG_DATA_HOME

exec "${R2_BINDIR}/${CMD}" "$@"
