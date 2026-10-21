"""Run selected, unchanged GUI contracts from the pinned napari source."""

import ast
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TESTS = ("test_qt_dims.py", "test_qt_dims_slider.py", "test_qt_color_swatch.py")


def definitions(path, names):
    source = path.read_text(encoding="utf-8")
    lines = source.splitlines(keepends=True)
    result = []
    for node in ast.parse(source).body:
        if isinstance(node, ast.FunctionDef) and node.name in names:
            start = min([node.lineno] + [part.lineno for part in node.decorator_list])
            result.append("".join(lines[start - 1 : node.end_lineno]))
    if len(result) != len(names):
        raise RuntimeError(f"The upstream fixture selection changed: {path}")
    return "\n\n".join(result)


def main():
    subprocess.run([sys.executable, str(ROOT / "tools/fetch_upstream.py")], check=True)
    source = ROOT / ".upstream/napari/src/napari"
    widgets = source / "_qt/widgets/_tests"
    destination = ROOT / ".artifacts/upstream-qt-tests"
    destination.mkdir(parents=True, exist_ok=True)
    for filename in TESTS:
        shutil.copyfile(widgets / filename, destination / filename)
    shutil.copyfile(ROOT / "licenses/napari-BSD-3-Clause.txt", destination / "LICENSE")
    (destination / "conftest.py").write_text(
        "from __future__ import annotations\n"
        "from contextlib import contextmanager\nfrom unittest.mock import MagicMock\n"
        "import pytest\nfrom napari.components import Dims\n"
        "from napari._qt.widgets.qt_dims import QtDims\n"
        "import naparixx\nnaparixx.enable()\n"
        "def pytest_unconfigure(config):\n    naparixx.disable()\n\n"
        + definitions(widgets / "conftest.py", {"qt_dims"})
        + "\n\n"
        + definitions(source / "conftest.py", {"mock_qt_method_ctx"})
        + "\n",
        encoding="utf-8",
    )
    result = subprocess.run(
        [
            sys.executable,
            "-m",
            "pytest",
            "-c",
            str(ROOT / "pyproject.toml"),
            "--confcutdir",
            str(destination),
            *[str(destination / filename) for filename in TESTS],
            "-q",
            "--junitxml",
            str(ROOT / ".artifacts/upstream-qt-tests.xml"),
        ],
        cwd=ROOT,
    )
    raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
