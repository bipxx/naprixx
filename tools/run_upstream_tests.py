"""Run selected, unmodified 0.9.2 tests with the native backend active."""

import ast
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TESTS = (
    "components/_tests/test_dims.py",
    "components/_tests/test_camera.py",
    "components/_tests/test_grid.py",
    "utils/transforms/_tests/test_transforms.py",
    "utils/transforms/_tests/test_transform_utils.py",
    "utils/transforms/_tests/test_transform_chain.py",
    "components/_tests/test_layers_list.py",
    "components/_tests/test_layers_base.py",
    "utils/_tests/test_naming.py",
    "utils/events/_tests/test_evented_model.py",
    "utils/events/_tests/test_event_emitter.py",
    "utils/events/_tests/test_typed_list.py",
    "utils/events/_tests/test_evented_list.py",
    "utils/events/_tests/test_evented_set.py",
    "utils/events/_tests/test_selection.py",
    "utils/events/_tests/test_selectable_list.py",
    "utils/events/_tests/test_typed_dict.py",
    "utils/events/_tests/test_evented_dict.py",
    "layers/base/_tests/test_base.py",
    "layers/image/_tests/test_image.py",
    "layers/image/_tests/test_image_utils.py",
    "layers/image/_tests/test_multiscale.py",
    "layers/image/_tests/test_volume.py",
    "layers/vectors/_tests/test_vectors.py",
    "layers/surface/_tests/test_surface.py",
    "layers/points/_tests/test_points.py",
    "components/_tests/test_layer_slicer.py",
    "utils/_tests/test_geometry.py",
    "layers/points/_tests/test_points_utils.py",
    "layers/utils/_tests/test_layer_utils.py",
    "layers/labels/_tests/test_labels_utils.py",
)
VIEWER_TESTS = {
    "test_view_centering_with_points_add",
    "test_view_centering_with_scale",
    "test_reset_view_with_input_layers",
    "test_camera",
    "test_reset_view",
    "test_fit_to_view_margin",
    "test_fit_to_view_center_calculation",
    "test_fit_to_view_2d_data_in_3d_view",
    "test_synced_camera",
    "test_separate_camera_cache_round_trip",
    "test_separate_camera_toggle_off_after_synced_navigation",
    "test_fit_to_view_handles_no_layers",
}


def copy_viewer_tests(source, destination):
    """Copy exact navigation test definitions with their small import set."""
    text = (source / "components/_tests/test_viewer_model.py").read_text(
        encoding="utf-8"
    )
    lines = text.splitlines(keepends=True)
    definitions = []
    for node in ast.parse(text).body:
        if isinstance(node, ast.FunctionDef) and node.name in VIEWER_TESTS:
            start = min([node.lineno] + [d.lineno for d in node.decorator_list])
            definitions.append("".join(lines[start - 1 : node.end_lineno]))
    if len(definitions) != len(VIEWER_TESTS):
        raise RuntimeError("The upstream viewer navigation test selection changed")
    (destination / "test_viewer_navigation.py").write_text(
        "# Exact test definitions from the verified napari baseline.\n"
        "import numpy as np\nimport pytest\n"
        "from napari.components import ViewerModel\nfrom napari.layers import Image\n\n"
        + "\n\n".join(definitions)
        + "\n",
        encoding="utf-8",
    )


def fixture_definitions(source):
    text = (source / "conftest.py").read_text(encoding="utf-8")
    lines = text.splitlines(keepends=True)
    names = {
        "single_threaded_executor",
        "_fix_magic_name",
        "viewer_model",
        "layer_data_and_types",
        "layers",
        "napari_svg_name",
        "npe2pm_",
        "builtins",
    }
    definitions = []
    for node in ast.parse(text).body:
        if isinstance(node, ast.FunctionDef) and node.name in names:
            start = min([node.lineno] + [d.lineno for d in node.decorator_list])
            definitions.append("".join(lines[start - 1 : node.end_lineno]))
    if len(definitions) != len(names):
        raise RuntimeError("The upstream fixture selection changed")
    return "\n\n".join(definitions)


def main():
    subprocess.run([sys.executable, str(ROOT / "tools/fetch_upstream.py")], check=True)
    source = ROOT / ".upstream/napari/src/napari"
    destination = ROOT / ".artifacts/upstream-tests"
    destination.mkdir(parents=True, exist_ok=True)
    for relative in TESTS:
        shutil.copyfile(source / relative, destination / Path(relative).name)
    copy_viewer_tests(source, destination)
    helpers = destination / "support"
    helpers.mkdir(exist_ok=True)
    shutil.copyfile(source / "_tests/utils.py", helpers / "utils.py")
    shutil.copyfile(ROOT / "licenses/napari-BSD-3-Clause.txt", destination / "LICENSE")
    (destination / "conftest.py").write_text(
        "from __future__ import annotations\n"
        "import sys\nfrom pathlib import Path\nfrom types import ModuleType\n"
        "from concurrent.futures import ThreadPoolExecutor\nimport pytest\n"
        "from napari.components import ViewerModel\n"
        "import numpy as np\nfrom packaging.version import parse as parse_version\n"
        "from napari.components import LayerList\n"
        "from napari.layers import Image, Labels, Points, Shapes, Vectors\n"
        "import naparixx\nnaparixx.enable()\n"
        "support = ModuleType('napari._tests')\n"
        "support.__path__ = [str(Path(__file__).parent / 'support')]\n"
        "sys.modules['napari._tests'] = support\n"
        "def pytest_unconfigure(config):\n    naparixx.disable()\n\n"
        + fixture_definitions(source)
        + "\n",
        encoding="utf-8",
    )
    # MouseEvent fixtures live in upstream's full GUI conftest; those integration
    # flows have separate tests here. Other reference tests remain unchanged.
    result = subprocess.run(
        [
            sys.executable,
            "-m",
            "pytest",
            "-c",
            str(ROOT / "pyproject.toml"),
            "--confcutdir",
            str(destination),
            str(destination),
            "-q",
            "-k",
            "not mouse_event_to_labels_coordinate",
            "--junitxml",
            str(ROOT / ".artifacts/upstream-tests.xml"),
        ],
        cwd=ROOT,
    )
    raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
