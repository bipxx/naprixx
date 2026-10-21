"""Probe full original models without the editable fork import finder."""

import json
import pickle
import sys
import types
from pathlib import Path


def main():
    root = Path(__file__).resolve().parents[2]
    source = (root / ".upstream/napari/src").resolve()
    sys.path.insert(0, str(source))
    sys.path.append(sys.argv[1])
    # The pinned Git tree omits its generated version module; supply metadata only.
    metadata = types.ModuleType("napari._version")
    metadata.__version__ = "0.9.2"
    sys.modules["napari._version"] = metadata

    from napari.components import Camera
    from napari.components.grid import GridCanvas
    from napari.utils.events import event

    assert Path(event.__file__).resolve().is_relative_to(source)
    assert "naparixx._native" not in sys.modules
    errors = {}
    for name, cls in (("camera", Camera), ("grid", GridCanvas)):
        try:
            pickle.dumps(cls())
        except Exception as error:
            errors[name] = [type(error).__name__, str(error)]
        else:
            errors[name] = None
    print(json.dumps(errors))


if __name__ == "__main__":
    main()
