"""Measure complete adapter calls against their upstream implementations."""

import argparse
import importlib.util
import json
import platform
import statistics
import subprocess
import sys
import timeit
from pathlib import Path

import numpy as np

import naparixx
from napari.layers.labels._labels_utils import first_nonzero_coordinate
from napari.layers.points._points_constants import PointsProjectionMode
from napari.layers.points._slice import _PointSliceRequest
from napari.layers.utils._slice_input import _SliceInput, _ThickNDSlice
from napari.layers.utils.layer_utils import calc_data_range
from napari.utils.transforms import Affine

ROOT = Path(__file__).resolve().parents[1]


def load_reference():
    """Load pinned Python implementations, independent of native dispatch."""
    checkout = ROOT / ".upstream/napari"
    baseline = json.loads((ROOT / "upstream.json").read_text(encoding="utf-8"))
    if not checkout.is_dir():
        raise SystemExit("Run tools/fetch_upstream.py before benchmarking.")
    commit = subprocess.check_output(
        ["git", "-C", str(checkout), "rev-parse", "HEAD"], text=True
    ).strip()
    if commit != baseline["commit"]:
        raise SystemExit("The reference checkout does not match upstream.json.")
    paths = {
        "helpers": "utils/transforms/transform_utils.py",
        "transforms": "utils/transforms/transforms.py",
        "input": "layers/utils/_slice_input.py",
        "points": "layers/points/_slice.py",
        "range": "layers/utils/layer_utils.py",
        "labels": "layers/labels/_labels_utils.py",
    }
    modules = {}
    for key, relative in paths.items():
        name = f"_naparixx_benchmark_reference_{key}"
        spec = importlib.util.spec_from_file_location(
            name, checkout / "src/napari" / relative
        )
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
        modules[key] = module
    for name, function in vars(modules["helpers"]).items():
        if callable(function) and hasattr(modules["transforms"], name):
            setattr(modules["transforms"], name, function)
    modules["input"].Affine = modules["transforms"].Affine
    modules["points"]._SliceInput = modules["input"]._SliceInput
    modules["points"]._ThickNDSlice = modules["input"]._ThickNDSlice
    return modules, baseline


def measure(function, iterations):
    function()
    return (
        statistics.median(timeit.repeat(function, number=iterations, repeat=5))
        / iterations
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--points", type=int, default=100_000)
    parser.add_argument("--iterations", type=int, default=10)
    args = parser.parse_args()
    if args.points < 1 or args.iterations < 1:
        parser.error("--points and --iterations must be positive")
    reference_modules, baseline = load_reference()
    naparixx.enable()
    rng = np.random.default_rng(42)
    data = rng.normal(size=(args.points, 5))
    thick = _ThickNDSlice((0.0,) * 5, (1.0,) * 5, (1.0,) * 5)
    request = _PointSliceRequest(
        slice_input=_SliceInput(ndisplay=2, world_slice=thick, order=tuple(range(5))),
        data=data,
        data_slice=thick,
        projection_mode=PointsProjectionMode.RESCALE_LINEAR,
        size=np.full(args.points, 5.0),
        shown=np.ones(args.points, dtype=bool),
    )
    transform = Affine(scale=(1.3, 0.7, 2.0, 1.1, 0.8), translate=(0.1,) * 5)
    old_transform = reference_modules["transforms"].Affine(
        scale=(1.3, 0.7, 2.0, 1.1, 0.8), translate=(0.1,) * 5
    )
    old_thick = reference_modules["input"]._ThickNDSlice(
        thick.point, thick.margin_left, thick.margin_right
    )
    old_request = reference_modules["points"]._PointSliceRequest(
        slice_input=reference_modules["input"]._SliceInput(
            ndisplay=2, world_slice=old_thick, order=tuple(range(5))
        ),
        data=data,
        data_slice=old_thick,
        projection_mode=request.projection_mode,
        size=request.size,
        shown=request.shown,
    )
    image = rng.normal(size=(1024, 1024)).astype(np.float32)
    labels = np.zeros((128, 128, 128), dtype=np.int32)
    labels[60:70, 60:70, 60:70] = 1
    start, end = np.zeros(3), np.full(3, 127.0)
    cases = [
        ("affine", lambda: old_transform(data), lambda: transform(data)),
        (
            "point_slice_linear",
            lambda: old_request._get_slice_data([0, 1, 2]),
            lambda: request._get_slice_data([0, 1, 2]),
        ),
        (
            "image_contrast_range",
            lambda: reference_modules["range"].calc_data_range(image),
            lambda: calc_data_range(image),
        ),
        (
            "label_ray_picking",
            lambda: reference_modules["labels"].first_nonzero_coordinate(
                labels, start, end
            ),
            lambda: first_nonzero_coordinate(labels, start, end),
        ),
    ]
    results = []
    for name, reference, candidate in cases:
        expected, actual = reference(), candidate()
        if isinstance(expected, tuple):
            for a, b in zip(actual, expected, strict=True):
                np.testing.assert_allclose(a, b, atol=1e-12, rtol=1e-12)
        else:
            np.testing.assert_allclose(actual, expected, atol=1e-12, rtol=1e-12)
        original_seconds = measure(reference, args.iterations)
        native_seconds = measure(candidate, args.iterations)
        row = {
            "operation": name,
            "upstream_ms": original_seconds * 1000,
            "native_adapter_ms": native_seconds * 1000,
            "speedup": original_seconds / native_seconds,
        }
        results.append(row)
        print(
            f"{name}: {row['speedup']:.2f}x "
            f"({row['upstream_ms']:.3f} ms -> {row['native_adapter_ms']:.3f} ms)"
        )
    report = {
        "napari_version": naparixx.NAPARI_VERSION,
        "naparixx_version": naparixx.__version__,
        "reference_commit": baseline["commit"],
        "reference_source": "Pinned upstream Python implementations",
        "python": platform.python_version(),
        "numpy": np.__version__,
        "platform": platform.platform(),
        "processor": platform.processor(),
        "points": args.points,
        "iterations": args.iterations,
        "repetitions": 5,
        "statistic": "median per-call wall time",
        "seed": 42,
        "results": results,
        "limitations": "These workloads exclude whole-viewer and GPU performance.",
    }
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
