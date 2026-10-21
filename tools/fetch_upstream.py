"""Fetch or verify the exact release used as the migration reference."""

import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    pin = json.loads((ROOT / "upstream.json").read_text(encoding="utf-8"))
    destination = ROOT / ".upstream" / "napari"
    if not destination.exists():
        subprocess.run(
            [
                "git",
                "clone",
                "--depth",
                "1",
                "--branch",
                pin["tag"],
                pin["repository"],
                str(destination),
            ],
            check=True,
        )
    commit = subprocess.check_output(
        ["git", "-C", str(destination), "rev-parse", "HEAD"], text=True
    ).strip()
    if commit != pin["commit"]:
        raise SystemExit(
            f"Expected {pin['commit']}; found {commit}. Checkout unchanged."
        )
    print(f"Verified napari {pin['version']} at {commit}")


if __name__ == "__main__":
    main()
