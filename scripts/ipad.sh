#!/usr/bin/env bash
# Compatibility entry point for the universal iPhone/iPad build.
exec "$(dirname -- "${BASH_SOURCE[0]}")/ios.sh" "$@"
