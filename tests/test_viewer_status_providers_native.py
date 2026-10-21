"""Finite missing cursor/provider contracts; root owns all application execution."""

# Per-family callbacks/generators are drained or released inside the same loop;
# B023 deliberately keeps live source cells rather than replacing them by copies.
# ruff: noqa: B023
from __future__ import annotations

import ast
import builtins
import gc
import hashlib
import importlib
import inspect
import json
import types
import weakref
from contextlib import contextmanager
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
SCOPE = ROOT / ".artifacts/viewer-status-providers-scope-next.json"
SCOPE_SHA = "d91925b44fe2fd25778eaee153272ef887163e714f8411ffdf85195d369fb4bc"
BEFORE = ROOT / ".artifacts/viewer-status-providers-before/index.json"
BEFORE_SHA = "d244ee943da6d11ec030523c7de07bb116501dc68303c3630953c3255d280b72"


def _hash(raw):
    return hashlib.sha256(raw).hexdigest()


def _member(owner, name):
    value = inspect.getattr_static(owner, name)
    if isinstance(value, property):
        return value.fget
    if isinstance(value, (classmethod, staticmethod)):
        return value.__func__
    return getattr(value, "func", value)


def _nodes(scope, qualified):
    parts = qualified.split(".")
    for name in parts:
        if name == "<locals>":
            continue
        options = [
            node
            for node in scope.body
            if isinstance(node, (ast.ClassDef, ast.FunctionDef))
            and node.name == name
            and not any(
                isinstance(decorator, ast.Name) and decorator.id == "overload"
                for decorator in getattr(node, "decorator_list", ())
            )
        ]
        assert len(options) == 1, qualified
        scope = options[0]
    return scope


class Family:
    def __init__(self, mode, rows, source_files, modules):
        self.mode, self.rows, self.modules = mode, rows, modules
        self.namespaces, self.descriptors, self.functions = {}, {}, {}
        grouped = {}
        for row in rows:
            if ".<locals>." in row["qualified_name"]:
                continue  # The exact outer factory owns its nested declaration.
            grouped.setdefault(row["path"], []).append(row)
        for path, selected in grouped.items():
            module = modules[path]
            if mode == "actual":
                namespace = vars(module)
            else:
                original = source_files[(mode, path)]
                tree = ast.parse(original)
                namespace = dict(vars(module))
                namespace["__builtins__"] = dict(vars(builtins))
                futures = [
                    n
                    for n in tree.body
                    if isinstance(n, ast.ImportFrom) and n.module == "__future__"
                ]
                nodes, classes = [], {}
                for row in selected:
                    q = row["qualified_name"]
                    node = _nodes(tree, q)
                    if "." in q:
                        classes.setdefault(q.split(".")[0], []).append(node)
                    else:
                        nodes.append(node)
                for name, methods in classes.items():
                    # Test-only signature/class-cell declaration. Real objects,
                    # descriptors and solid layouts are never reconstructed.
                    nodes.append(
                        ast.ClassDef(
                            name=name,
                            bases=[],
                            keywords=[],
                            body=methods,
                            decorator_list=[],
                            type_params=[],
                        )
                    )
                declaration = ast.fix_missing_locations(
                    ast.Module(body=[*futures, *nodes], type_ignores=[])
                )
                exec(
                    compile(declaration, str(ROOT / path), "exec", dont_inherit=True),
                    namespace,
                )
            self.namespaces[path] = namespace
            for row in selected:
                q = row["qualified_name"]
                if "." not in q:
                    function = namespace[q]
                    # Source-body oracle bypasses only the established native
                    # dispatch binding. The fork adapter is compared separately.
                    function = inspect.unwrap(function)
                    self.functions[(path, q)] = function
                    continue
                cls_name, method = q.split(".")
                real_class = getattr(module, cls_name)
                descriptor = inspect.getattr_static(namespace[cls_name], method)
                if mode != "actual":
                    function = _member(namespace[cls_name], method)
                    for name, cell in zip(
                        function.__code__.co_freevars,
                        function.__closure__ or (),
                        strict=True,
                    ):
                        if name == "__class__":
                            cell.cell_contents = real_class
                self.descriptors[(real_class, method)] = descriptor
                self.functions[(path, q)] = _member(namespace[cls_name], method)
        if mode != "actual":
            # Imported owned helper aliases point to this source family, while
            # accepted unowned native callees remain the genuine dependencies.
            by_name = {
                q: fn for (_path, q), fn in self.functions.items() if "." not in q
            }
            for namespace in self.namespaces.values():
                for name, fn in by_name.items():
                    if name in namespace:
                        namespace[name] = fn

    def function(self, qualified):
        return next(
            fn for (_path, name), fn in self.functions.items() if name == qualified
        )

    def namespace(self, qualified):
        path = next(path for path, name in self.functions if name == qualified)
        return self.namespaces[path]

    @contextmanager
    def bound(self, monkeypatch):
        with monkeypatch.context() as patch:
            if self.mode != "actual":
                for (owner, name), descriptor in self.descriptors.items():
                    patch.setattr(owner, name, descriptor)
                    if hasattr(descriptor, "__set_name__"):
                        descriptor.__set_name__(owner, name)
            yield patch


@pytest.fixture(scope="module")
def families():
    assert _hash(SCOPE.read_bytes()) == SCOPE_SHA
    assert _hash(BEFORE.read_bytes()) == BEFORE_SHA
    scope, before = json.loads(SCOPE.read_bytes()), json.loads(BEFORE.read_bytes())
    rows = scope["owned_bodies"]
    assert len(rows) == 74
    modules = {
        path: importlib.import_module(
            path.removeprefix("src/").removesuffix(".py").replace("/", ".")
        )
        for path in {row["path"] for row in rows}
    }
    source_files = {}
    for path in modules:
        for mode in ("pinned", "current_fork"):
            if path.startswith("src/naparixx/"):
                original = path
            else:
                original = ".upstream/napari/" + path if mode == "pinned" else path
            record = next(item for item in before["files"] if item["path"] == original)
            raw = (ROOT / record["before"]).read_bytes()
            assert _hash(raw) == record["before_sha256"]
            source_files[(mode, path)] = raw
    return [
        Family(mode, rows, source_files, modules)
        for mode in ("pinned", "current_fork", "actual")
    ]


def test_exact74_public_metadata_and_owned_helper_aliases(families):
    rows = json.loads(SCOPE.read_bytes())["owned_bodies"]
    for family in families:
        for row in rows:
            q = row["qualified_name"]
            if ".<locals>." in q:
                continue
            fn, reference = family.function(q), families[1].function(q)
            assert str(inspect.signature(fn)) == str(inspect.signature(reference)), q
            assert fn.__name__ == reference.__name__, q
            assert fn.__qualname__ == reference.__qualname__, q
            assert fn.__doc__ == reference.__doc__, q
            assert fn.__annotations__ == reference.__annotations__, q


@pytest.mark.parametrize(
    "kind", ["image", "labels", "points", "shapes", "surface", "vectors", "tracks"]
)
def test_actual_seven_layer_instances_status_tooltips_and_picker_values(
    families, monkeypatch, kind
):
    import numpy as np

    from napari.layers import Image, Labels, Points, Shapes, Surface, Tracks, Vectors

    constructors = {
        "image": lambda: Image(np.array([[1, 2], [3, 4]], dtype=np.uint8)),
        "labels": lambda: Labels(np.array([[1, 2], [3, 4]], dtype=np.uint8)),
        "points": lambda: Points(np.array([[0, 0], [1, 1]], dtype=float), size=2),
        "shapes": lambda: Shapes(
            [np.array([[0, 0], [0, 2], [2, 2], [2, 0]])], shape_type="polygon"
        ),
        "surface": lambda: Surface(
            (
                np.array([[0, 0], [0, 2], [2, 0]], dtype=float),
                np.array([[0, 1, 2]]),
                np.array([1, 2, 3]),
            )
        ),
        "vectors": lambda: Vectors(np.array([[[0, 0], [1, 1]]], dtype=float)),
        "tracks": lambda: Tracks(np.array([[1, 0, 0, 0], [1, 1, 1, 1]], dtype=float)),
    }
    expected_classes = {
        "image": Image,
        "labels": Labels,
        "points": Points,
        "shapes": Shapes,
        "surface": Surface,
        "vectors": Vectors,
        "tracks": Tracks,
    }
    outcomes = []
    for family in families:
        layer = constructors[kind]()
        with family.bound(monkeypatch):
            position = np.zeros(layer.ndim)
            value = layer.get_value(
                position, dims_displayed=list(layer._slice_input.displayed), world=True
            )
            status = layer.get_status(
                position, dims_displayed=list(layer._slice_input.displayed), world=True
            )
            tooltip = layer._get_tooltip_text(
                position, dims_displayed=list(layer._slice_input.displayed), world=True
            )
            assert type(layer) is expected_classes[kind]
            assert isinstance(status, dict) and isinstance(tooltip, str)
            outcomes.append((repr(value), status, tooltip))
    assert outcomes[0] == outcomes[1] == outcomes[2]


@pytest.mark.parametrize("case", ["outside", "active", "multiple", "grid"])
def test_viewer_tooltip_first_sampling_selection_loading_and_grid(
    families, monkeypatch, case
):
    import numpy as np

    outcomes = []
    for family in families:
        trace = []

        class Layer:
            def __init__(self, name, value, *, loaded=True, visible=True, opacity=1):
                self.name, self.value = name, value
                self.visible, self.opacity = visible, opacity
                self._slicing_state = SimpleNamespace(_loaded=loaded)

            def get_status(self, position, **kwargs):
                trace.append(("status", self.name, tuple(position), kwargs))
                return {"coords": "[0, 0]", "value": self.value}

            def _get_tooltip_text(self, position, **kwargs):
                assert isinstance(position, np.ndarray)
                trace.append(("tooltip", self.name, tuple(position), kwargs))
                return "active tooltip"

        active, other = Layer("active", "1"), Layer("other", "2")
        hidden, unloaded = (
            Layer("hidden", "3", visible=False),
            Layer("unloaded", "4", loaded=False),
        )

        class Selection(set):
            pass

        selection = Selection([active] if case == "active" else [active, other])
        selection.active = active

        class Layers(list):
            pass

        layers = Layers([hidden, active, unloaded, other])
        layers.selection = selection
        subject = SimpleNamespace(
            mouse_over_canvas=case != "outside",
            layers=layers,
            tooltip=SimpleNamespace(visible=True),
            cursor=SimpleNamespace(position=(0, 0), _view_direction=(0, 0)),
            dims=SimpleNamespace(displayed=(0, 1)),
            canvas=SimpleNamespace(grid=SimpleNamespace(enabled=case == "grid")),
        )
        result = family.function("ViewerModel._calc_status_from_cursor")(subject)
        if case == "outside":
            assert result is None and trace == []
        else:
            assert trace[0][0] == "tooltip"
            assert [row[1] for row in trace if row[0] == "status"] == (
                ["active"] if case == "active" else ["other", "active"]
            )
            assert result[1] == "active tooltip"
        outcomes.append((result, trace))
    assert outcomes[0] == outcomes[1] == outcomes[2]


@pytest.mark.parametrize("case", ["invisible", "pan_zoom", "edit", "ray"])
def test_layer_cast_override_order_world_conversion_and_value_publication(
    families, monkeypatch, case
):
    import numpy as np

    outcomes = []
    for family in families:
        trace, result = [], object()

        class ArrayProvider:
            def __array__(self, dtype=None, copy=None):
                trace.append("array")
                return np.array([1, 2, 3])

        old = object()
        subject = SimpleNamespace(
            visible=case != "invisible",
            mode="pan_zoom" if case == "pan_zoom" else "edit",
            ndim=3,
            _value=old,
        )
        subject.world_to_data = lambda position: (
            trace.append(("world", tuple(position))) or position
        )
        subject._world_to_data_ray = lambda direction: (
            trace.append(("direction", tuple(direction))) or direction
        )
        subject.get_ray_intersections = lambda **kwargs: (
            trace.append(("ray", kwargs["world"])) or (np.zeros(3), np.ones(3))
        )
        subject._get_value = lambda *args, **kwargs: trace.append("point") or result
        subject._get_value_3d = lambda **kwargs: trace.append("pick3d") or result
        namespace = family.namespace("Layer.get_value")
        mapping = namespace["dims_displayed_world_to_layer"]
        with monkeypatch.context() as patch:
            patch.setitem(
                namespace,
                "dims_displayed_world_to_layer",
                lambda displayed, **kwargs: (
                    trace.append(("dims", kwargs)) or mapping(displayed, **kwargs)
                ),
            )
            actual = family.function("Layer.get_value")(
                subject,
                ArrayProvider(),
                view_direction=np.ones(3) if case == "ray" else None,
                dims_displayed=[0, 1, 2],
                world=True,
            )
        assert trace[0] == "array"
        if case == "invisible":
            assert actual is None and trace == ["array"] and subject._value is None
        else:
            assert actual is result
            assert subject._value is (old if case == "pan_zoom" else result)
            assert trace[-1] == ("pick3d" if case == "ray" else "point")
        outcomes.append(trace)
    assert outcomes[0] == outcomes[1] == outcomes[2]


@pytest.mark.parametrize("case", ["scalar", "rgb", "rgba", "empty", "nan"])
def test_image_ray_empty_nan_luminance_and_rendering_modes(families, monkeypatch, case):
    import numpy as np

    from napari.layers.image._image_constants import ImageRendering

    values = {
        "scalar": np.array([1.0, 2.0, 5.0]),
        "rgb": np.array([[0.1, 0.2, 0.3], [0.8, 0.5, 0.2], [0.3, 0.7, 0.9]]),
        "rgba": np.array(
            [[0.1, 0.2, 0.3, 1], [0.8, 0.5, 0.2, 0.5], [0.3, 0.7, 0.9, 1]]
        ),
        "empty": np.array([], dtype=float),
        "nan": np.array([np.nan, np.nan]),
    }[case]
    outcomes = []
    for family in families:
        results = []
        for mode in ImageRendering:
            subject = SimpleNamespace(
                rgb=case in {"rgb", "rgba"},
                rendering=mode,
                iso_threshold=0.5,
                contrast_limits=(0, 5),
                attenuation=0.5,
            )
            result = family.function("Image._calculate_value_from_ray")(subject, values)
            if case == "empty" or (
                case == "nan" and mode is not ImageRendering.TRANSLUCENT
            ):
                assert result is None
            results.append(result)
        outcomes.append(results)
    for expected, fork, actual in zip(*outcomes, strict=True):
        if expected is None:
            assert fork is None and actual is None
        else:
            np.testing.assert_allclose(fork, expected, equal_nan=True)
            np.testing.assert_allclose(actual, expected, equal_nan=True)


@pytest.mark.parametrize("multiscale", [False, True])
def test_label_features_huge_integer_mapping_and_repeated_column_reads(
    families, monkeypatch, multiscale
):
    import numpy as np

    huge = 2**100
    outcomes = []
    for family in families:
        trace = []

        class Column:
            def __init__(self, name, value):
                self.name, self.value = name, value

            def __len__(self):
                trace.append(("len", self.name))
                return 1

            def __getitem__(self, index):
                trace.append(("item", self.name, index))
                return self.value

        class Features:
            shape = (1, 4)

            def items(self):
                trace.append("items")
                return [
                    ("index", Column("index", huge)),
                    ("good", Column("good", 1.5)),
                    ("none", Column("none", None)),
                    ("nan", Column("nan", float("nan"))),
                ]

        subject = SimpleNamespace(
            _label_index={huge: 0}, features=Features(), multiscale=multiscale
        )
        subject.get_value = lambda *args, **kwargs: (3, huge) if multiscale else huge
        result = family.function("Labels._get_properties")(subject, np.zeros(2))
        assert result == ["good: 1.5"]
        assert sum(row == ("item", "good", 0) for row in trace) == 4
        assert ("item", "index", 0) not in trace
        outcomes.append((result, trace))
    assert outcomes[0] == outcomes[1] == outcomes[2]


def test_points_tracks_moving_shapes_keep_live_python_integer_and_payload_domains(
    families, monkeypatch
):
    import numpy as np

    huge = 2**100
    outcomes = []
    for family in families:
        trace = []
        point = SimpleNamespace(
            _view_data=np.array([[0, 0], [0, 0]]),
            scale=np.ones(2),
            _view_size=np.array([2, 2]),
            _view_indices=np.array([0, huge], dtype=object),
            _slice_input=SimpleNamespace(displayed=[0, 1]),
        )
        value = family.function("Points._get_value")(point, (0, 0))
        assert value == huge and isinstance(value, int)
        payload = (huge, object())
        shape = SimpleNamespace(
            _slice_input=SimpleNamespace(ndisplay=2),
            _is_moving=True,
            _moving_value=payload,
        )
        assert family.function("Shapes._get_value")(shape, (0, 0)) is payload

        class Tree:
            def query(self, coords, **kwargs):
                trace.append((tuple(coords), kwargs))
                return np.zeros(4), np.array([-1, 100, 1, 0])

        track = SimpleNamespace(
            _kdtree=Tree(),
            _points=np.array([[0, 1, 1], [2, 1, 1]]),
            _points_id=np.array([huge, 3], dtype=object),
        )
        chosen = family.function("TrackManager.get_value")(track, np.array([0, 1, 1]))
        assert chosen == huge and isinstance(chosen, int)
        assert trace == [((0, 1, 1), {"k": 10})]
        outcomes.append((value, chosen, trace))
    assert outcomes[0] == outcomes[1] == outcomes[2]


@pytest.mark.parametrize("world", [False, "numpy_true"])
def test_ray_copy_scatter_readonly_input_and_literal_true_domain(
    families, monkeypatch, world
):
    import numpy as np

    outcomes = []
    for family in families:
        trace = []
        position, direction = np.array([1.0, 2.0, 3.0]), np.array([1.0, 0.0, 0.0])
        position.flags.writeable = False
        subject = SimpleNamespace(
            _get_offset_data_position=lambda point: trace.append("offset") or point
        )
        subject._world_to_displayed_data_ray = lambda *args: pytest.fail(
            "source world is True must not accept numpy.bool_"
        )
        subject._world_to_displayed_data = lambda *args: pytest.fail(
            "source world is True must not accept numpy.bool_"
        )
        namespace = family.namespace("Layer._get_ray_intersections")
        front, back = np.array([-1, 0, 0]), np.array([1, 0, 0])

        def intersect(point, view, box, normal):
            trace.append(("face", tuple(normal)))
            return (
                np.array([0.0, 0.0, 0.0])
                if normal is front
                else np.array([4.0, 4.0, 4.0])
            )

        with monkeypatch.context() as patch:
            patch.setitem(
                namespace, "find_front_back_face", lambda *args: (front, back)
            )
            patch.setitem(
                namespace, "intersect_line_with_axis_aligned_bounding_box_3d", intersect
            )
            result = family.function("Layer._get_ray_intersections")(
                subject,
                position,
                direction,
                [0, 1, 2],
                np.array([[0, 4]] * 3),
                world=np.bool_(True) if world == "numpy_true" else False,
            )
        np.testing.assert_array_equal(position, [1, 2, 3])
        np.testing.assert_array_equal(result[0], [0, 0, 0])
        np.testing.assert_array_equal(result[1], [4, 4, 4])
        assert result[0].flags.writeable and result[1].flags.writeable
        assert not np.shares_memory(result[0], position) and not np.shares_memory(
            result[1], position
        )
        outcomes.append(trace)
    assert outcomes[0] == outcomes[1] == outcomes[2]


@pytest.mark.parametrize("dtype", ["float64", "complex128"])
def test_projection_generic_namespace_operation_order_and_existing_typed_split(
    families, monkeypatch, dtype
):
    import numpy as np

    outcomes = []
    for family in families:
        trace = []

        class Numpy:
            def __getattr__(self, name):
                trace.append(("lookup", name))
                fn = getattr(np, name)
                if not callable(fn):
                    return fn

                def call(*args, **kwargs):
                    trace.append(("call", name))
                    return fn(*args, **kwargs)

                return call

        namespace = family.namespace("project_points_onto_plane")
        with monkeypatch.context() as patch:
            patch.setitem(namespace, "np", Numpy())
            result = family.function("project_points_onto_plane")(
                np.array([[1, 2, 3], [2, 3, 4]], dtype=dtype),
                np.array([0, 0, 0], dtype=dtype),
                np.array([0, 0, 1], dtype=dtype),
            )
        assert trace[:6] == [
            ("lookup", "atleast_2d"),
            ("call", "atleast_2d"),
            ("lookup", "asarray"),
            ("call", "asarray"),
            ("lookup", "tile"),
            ("call", "tile"),
        ]
        outcomes.append((result, trace))
    for actual in outcomes[1:]:
        np.testing.assert_allclose(actual[0][0], outcomes[0][0][0])
        np.testing.assert_allclose(actual[0][1], outcomes[0][0][1])
        assert actual[1] == outcomes[0][1]


def test_shape_cached_values_and_lazy_override_reads_remain_authoritative(
    families, monkeypatch
):
    import numpy as np

    outcomes = []
    for family in families:
        trace, captured = [], []

        class Shape:
            z_index = 0

            def _all_triangles(self):
                trace.append("triangles")
                return np.zeros((1, 3, 2))

        subject = SimpleNamespace(
            shapes=[Shape()],
            _bounding_boxes=np.array([[[0, 0]], [[3, 3]]]),
            _visible_shapes=[(2**100, Shape())],
        )
        namespace = family.namespace("ShapeList.inside")

        def next_consumer(generator):
            assert isinstance(generator, types.GeneratorType)
            assert (
                generator.__name__ == "<genexpr>"
                and generator.__qualname__ == "ShapeList.inside.<locals>.<genexpr>"
            )
            captured.append(generator)
            subject._visible_shapes = [(7, Shape())]
            return next(generator)

        with monkeypatch.context() as patch:
            patch.setitem(namespace, "next", next_consumer)
            patch.setitem(
                namespace,
                "inside_triangles",
                lambda triangles: trace.append("inside") or np.array([True]),
            )
            result = family.function("ShapeList.inside")(subject, np.array([1, 1]))
        assert result == 2**100 and trace == ["triangles", "inside"]
        captured.pop().close()
        outcomes.append((result, trace))
    assert outcomes[0] == outcomes[1] == outcomes[2]


def test_units_generator_lazy_fresh_registry_and_throw_close_protocol(
    families, monkeypatch
):
    import numpy as np

    outcomes = []
    for family in families:
        captured, trace = [], []

        class Pint:
            def get_application_registry(self):
                trace.append("registry")
                return SimpleNamespace(pixel="pixel")

        def all_consumer(generator):
            assert isinstance(generator, types.GeneratorType)
            captured.append(generator)
            assert (
                generator.__name__ == "<genexpr>"
                and generator.__qualname__
                == "Layer._use_integer_coords_in_status.<locals>.<genexpr>"
            )
            assert next(generator) is True
            return True

        subject = SimpleNamespace(
            scale=np.ones(2), translate=np.zeros(2), units=["pixel", "pixel"]
        )
        namespace = family.namespace("Layer._use_integer_coords_in_status")
        with monkeypatch.context() as patch:
            patch.setitem(namespace, "all", all_consumer)
            patch.setitem(namespace, "pint", Pint())
            result = family.function("Layer._use_integer_coords_in_status")(subject)
            assert result is True and trace == ["registry"]
            generator = captured.pop()
            with pytest.raises(LookupError, match="unit suspended"):
                generator.throw(LookupError("unit suspended"))
            generator.close()
        outcomes.append((result, trace))
    assert outcomes[0] == outcomes[1] == outcomes[2]


def test_scalar_bounds_error_retains_only_original_raw_local_until_error_release(
    families, monkeypatch
):
    import numpy as np

    for family in families:

        class Raw(np.ndarray):
            pass

        raw = np.zeros((2, 2)).view(Raw)
        reference = weakref.ref(raw)
        subject = SimpleNamespace(
            multiscale=False,
            ndim=2,
            _data=np.zeros((2, 2)),
            _slice=SimpleNamespace(image=SimpleNamespace(raw=raw)),
            _slice_input=SimpleNamespace(displayed=[0, 1]),
        )
        namespace = family.namespace("ScalarFieldBase._get_value")

        def fail_bounds(_generator):
            raise LookupError("bounds consumer")

        error = None
        with monkeypatch.context() as patch:
            patch.setitem(namespace, "all", fail_bounds)
            try:
                family.function("ScalarFieldBase._get_value")(subject, np.array([0, 0]))
            except LookupError as caught:
                error = caught
        assert error is not None and str(error) == "bounds consumer"
        subject._slice.image.raw = None
        del raw
        gc.collect()
        assert reference() is not None
        error = None
        gc.collect()
        assert reference() is None


@pytest.mark.parametrize("case", ["reader", "sample", "widget", "none"])
def test_source_manifest_and_widget_namespace_fallback_order(
    families, monkeypatch, case
):
    outcomes = []
    for family in families:
        trace = []

        class Manager:
            def get_manifest(self, name):
                trace.append(name)
                raise KeyError(name)

        def widget_fn():
            return None

        widget_fn.__name__ = "widget provider"
        source = SimpleNamespace(
            reader_plugin="reader" if case == "reader" else None,
            path="some/path/data.tif",
            sample=("sample", "data") if case == "sample" else None,
            widget=SimpleNamespace(_function=widget_fn) if case == "widget" else None,
        )
        subject = SimpleNamespace(source=source, name="layer")
        namespace = family.namespace("Layer._get_source_info")
        with monkeypatch.context() as patch:
            patch.setitem(namespace, "pm", Manager())
            result = family.function("Layer._get_source_info")(subject)
        assert (
            result["plugin"]
            == {
                "reader": "reader",
                "sample": "sample",
                "widget": "widget provider",
                "none": "",
            }[case]
        )
        assert trace == ([case] if case in {"reader", "sample"} else [])
        outcomes.append((result, trace))
    assert outcomes[0] == outcomes[1] == outcomes[2]


def test_fork_projection_adapter_reads_live_original_cell_after_array_conversion(
    families, monkeypatch
):
    import numpy as np

    for family in families:
        factory = family.function("projection_adapter")
        calls = []

        def first(*args):
            calls.append("first")
            return "first"

        def replacement(*args):
            calls.append("replacement")
            return "replacement"

        adapted = factory(first)
        assert inspect.signature(adapted) == inspect.signature(first)
        assert adapted.__wrapped__ is first
        original_cell = next(
            cell
            for name, cell in zip(
                adapted.__code__.co_freevars, adapted.__closure__, strict=True
            )
            if name == "original"
        )

        class Array:
            def __array__(self, dtype=None, copy=None):
                original_cell.cell_contents = replacement
                return np.array([1, 2], dtype=np.float64)

        assert adapted(Array(), np.zeros(3), np.ones(3)) == "replacement"
        assert calls == ["replacement"]
