#!/usr/bin/env python3
"""Check version queries and argument errors without starting the GUI event loop."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1] if len(sys.argv) > 1 else "build/iaito").resolve())
with tempfile.TemporaryDirectory() as directory:
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", XDG_CONFIG_HOME=directory,
               XDG_CACHE_HOME=directory, XDG_DATA_HOME=directory, R2_NOPLUGINS="1")

    def run(args, status, message=None):
        result = subprocess.run([binary, *args], env=env, capture_output=True,
                                text=True, timeout=15)
        assert result.returncode == status, (args, result)
        assert "QThreadStorage:" not in result.stderr, (args, result.stderr)
        if message:
            assert message in result.stderr, (args, result.stderr)
        else:
            assert re.fullmatch(r"iaito \S+\n", result.stdout), (args, result.stdout)

    for args in [("--version",), ("-v",), ("--no-plugins", "--version"),
                 ("--version", "--help"), ("-A", "99", "--version")]:
        run(args, 0)
    run(("-A", "99"), 1, "Invalid Analysis Level")
    run(("-A", "0"), 1, "Filename must be specified")
    # A value that looks like a flag must not trigger version handling.
    run(("-i", "--version", "-A", "99"), 1, "Invalid Analysis Level")

print("Command-line checks passed")
