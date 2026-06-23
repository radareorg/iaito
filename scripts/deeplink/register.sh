#!/bin/sh
# Register the iaito:// URI scheme handler on Linux and macOS.
# Usage: register.sh [path-to-iaito.app]   (the argument is only used on macOS)

case "$(uname)" in
Darwin)
	APP="$1"
	[ -d "$APP" ] || APP="/Applications/iaito.app"
	[ -d "$APP" ] || APP="$HOME/Applications/iaito.app"
	if [ ! -d "$APP" ]; then
		echo "Cannot find iaito.app, pass its path as first argument" >&2
		exit 1
	fi
	LSREG="/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/LaunchServices.framework/Versions/A/Support/lsregister"
	"$LSREG" -f "$APP"
	echo "Registered iaito:// for $APP"
	;;
*)
	DESKTOP="org.radare.iaito.desktop"
	if command -v xdg-mime >/dev/null 2>&1; then
		xdg-mime default "$DESKTOP" x-scheme-handler/iaito
	fi
	if command -v update-desktop-database >/dev/null 2>&1; then
		update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
	fi
	echo "Registered iaito:// -> $DESKTOP"
	;;
esac
