"""Import the verified baseline once; never overwrite a modified fork."""

import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    baseline = ROOT / ".upstream/napari/src"
    if not baseline.is_dir():
        raise SystemExit("Run tools/fetch_upstream.py first.")
    expected = json.loads((ROOT / "upstream.json").read_text(encoding="utf-8"))
    actual = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=baseline, text=True
    ).strip()
    if actual != expected["commit"]:
        raise SystemExit("The reference checkout does not match upstream.json.")
    destinations = [ROOT / "src" / name for name in ("napari", "napari_builtins")]
    for destination in destinations:
        if destination.exists():
            raise SystemExit(f"Refusing to overwrite the existing fork: {destination}")
    for name in ("napari", "napari_builtins"):
        destination = ROOT / "src" / name
        shutil.copytree(
            baseline / name,
            destination,
            ignore=shutil.ignore_patterns("_tests", "__pycache__", "conftest.py"),
        )
    (ROOT / "src/napari/_version.py").write_text(
        'version = __version__ = "0.9.2+naparixx.2"\n'
        'version_tuple = __version_tuple__ = (0, 9, 2, "naparixx.2")\n',
        encoding="utf-8",
    )
    print("Imported the napari 0.9.2 application and builtin plugin sources.")


if __name__ == "__main__":
    main()
