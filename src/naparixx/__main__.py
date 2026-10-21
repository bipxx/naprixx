"""Launch the bundled napari application with its native core."""

import json
import sys

from . import __version__, backend_status, enable


def main():
    if sys.argv[1:] == ["--backend-info"]:
        print(json.dumps(backend_status(), indent=2))
        return
    if sys.argv[1:] == ["--version"]:
        print(f"naparixx {__version__} (napari {backend_status()['napari_version']})")
        return
    enable()
    from napari.__main__ import main as napari_main

    napari_main()


if __name__ == "__main__":
    main()
