import json
from _cutter import *

try:
    from IaitoBindings import *

    def core():
        return IaitoCore.instance()
except ImportError:
    pass


def cmdj(command):
    """Execute a JSON command and return the result as a dictionary"""
    return json.loads(cmd(command))


