#!/bin/sh
# Unregister the iaito:// URI scheme handler on Linux and macOS.

case "$(uname)" in
Darwin)
	echo "macOS drops the iaito:// handler when the iaito.app bundle is removed."
	;;
*)
	MIMEAPPS="$HOME/.config/mimeapps.list"
	if [ -f "$MIMEAPPS" ]; then
		sed -i.bak '/x-scheme-handler\/iaito=/d' "$MIMEAPPS"
		rm -f "$MIMEAPPS.bak"
	fi
	if command -v update-desktop-database >/dev/null 2>&1; then
		update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
	fi
	echo "Unregistered iaito:// handler"
	;;
esac
