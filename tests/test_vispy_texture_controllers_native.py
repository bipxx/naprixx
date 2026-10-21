"""Thirty-four finite comparisons for 35 Vispy texture-controller bodies.

Pinned bodies are independently rebuilt from exact source segments, including
their decorators and lexical super cells. Current bodies are rebuilt separately.
NumPy, Vispy colormaps/nodes/textures, GL queries and event connection services
are controlled protocol boundaries. VispyBaseLayer callbacks, model dumping,
and already accepted cache/texture-dtype conversion callees are named owned
dependencies supplied as controlled services here, not live oracle helpers.
This packet does not establish rendering/GL-driver parity, BaseLayer or visual
implementation closure, optional-backend typing, Pydantic/events closure, full
generator/reflection contracts, or shutdown/platform/plugin acceptance.
"""

from __future__ import annotations

import ast
import copy
import gc
import hashlib
import importlib
import inspect
import math
import warnings
import weakref
from contextlib import contextmanager
from functools import lru_cache
from pathlib import Path
from types import SimpleNamespace

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
PINNED = ROOT / ".upstream/napari/src/napari"
CURRENT = ROOT / "src/napari"
PINNED_COMMIT = "595639295a153c84bcb127a9e07d801355440a35"
HASHES = {
    "_vispy/layers/labels.py": (
        "f959697110efcc48ef012e6adf2c44b0a8b649e4cb16b7ab48e0fd5a46baf29e"
    ),
    "_vispy/layers/scalar_field.py": (
        "64809674cbde0088c8b247694df976aba7b490e105e0625b69f26de3ceb4df01"
    ),
    "_vispy/utils/gl.py": (
        "76278bb2e622cc723ef8cbfc7302b9cba0869520e68475c126aa302c6641f65e"
    ),
    "utils/colormaps/colormap.py": (
        "af2eed8dfa33850a7e8000cd3ac36cffca00d2519ffdcbed024aeae27819ff80"
    ),
}
SCALAR = (
    "__init__",
    "_on_display_change",
    "_on_data_change",
    "_on_custom_interpolation_kernel_2d_change",
    "_on_rendering_change",
    "_on_depiction_change",
    "_on_blending_change",
    "_on_plane_thickness_change",
    "_on_plane_position_change",
    "_on_plane_normal_change",
    "_on_colormap_change",
    "reset",
    "downsample_texture",
)
LABELS = (
    "__init__",
    "_on_data_change",
    "_on_rendering_change",
    "_on_colormap_change",
    "_on_iso_gradient_mode_change",
    "_on_partial_labels_update",
    "reset",
)
LABEL_NODE = ("__init__", "_setup_nodes", "get_node")


def _segment(source, node, *, owner=None):
    first = min(
        [node.lineno, *(item.lineno for item in getattr(node, "decorator_list", ()))]
    )
    text = "".join(source.splitlines(keepends=True)[first - 1 : node.end_lineno])
    parsed = ast.parse((f"class {owner}:\n" if owner else "") + text).body[0]
    rebuilt = parsed.body[0] if owner else parsed
    assert ast.dump(rebuilt) == ast.dump(node)
    return text


def _methods(source, owner, names):
    body = next(
        node.body
        for node in ast.parse(source).body
        if isinstance(node, ast.ClassDef) and node.name == owner
    )
    return "\n".join(
        _segment(
            source,
            next(
                node
                for node in body
                if isinstance(node, ast.FunctionDef) and node.name == name
            ),
            owner=owner,
        )
        for name in names
    )


def _function(source, name):
    node = next(
        node
        for node in ast.parse(source).body
        if isinstance(node, ast.FunctionDef) and node.name == name
    )
    return _segment(source, node)


class _Emitter:
    def __init__(self, name, state):
        self.name, self.state = name, state

    def connect(self, callback):
        self.state.trace.append(("connect", self.name, callback.__name__))


class _Events:
    def __init__(self, state, prefix=""):
        self.state, self.prefix = state, prefix
        self.emitters = {}

    def __getattr__(self, name):
        if name not in self.emitters:
            self.emitters[name] = _Emitter(self.prefix + name, self.state)
        return self.emitters[name]


def _services():
    state = SimpleNamespace(
        trace=[],
        current_canvas=None,
        gl_values={3379: 256, 32883: 64},
        gl_failure=None,
        texture_failure=False,
        set_data_failure=False,
        mapping=np.arange(256 * 4, dtype=np.float32).reshape(256, 4),
        texture_dtype=np.dtype(np.uint8),
        textures=[],
        constructors=[],
        conversions={},
    )

    class Program(dict):
        def __setitem__(self, key, value):
            state.trace.append(("program", key))
            super().__setitem__(key, value)

    class Canvas:
        def __init__(self, *, show):
            state.trace.append(("canvas", show))

        def close(self):
            state.trace.append(("canvas.close",))

    class Node:
        def __init__(self, data=None, **kwargs):
            self.data = data
            self.texture_format = kwargs.get("texture_format", "auto")
            self.parent = None
            self.children = []
            self.order = 0
            self.visible = True
            self.shared_program = Program()
            state.constructors.append((type(self).__name__, data, kwargs))

        def __setattr__(self, name, value):
            object.__setattr__(self, name, value)
            if name == "cmap":
                state.trace.append(("cmap", type(value).__name__))

        def set_data(self, data):
            state.trace.append(("set_data", self, data))
            if state.set_data_failure:
                raise RuntimeError("node upload failed")
            self.data = data

        def update(self):
            state.trace.append(("node.update", self))

    class ImageVisual(Node):
        pass

    class LabelNode(ImageVisual):
        pass

    class VolumeNode(Node):
        pass

    class TiledImageNode(ImageVisual):
        pass

    class VispyColormap:
        def __init__(self, *args, **kwargs):
            state.trace.append(("vispy.colormap", args, kwargs))

    class Base:
        @classmethod
        def __class_getitem__(cls, item):
            return cls

        def __init__(self, layer, node, **kwargs):
            state.trace.append(("base.init", layer, node, kwargs))
            self.layer, self.node = layer, node
            self.MAX_TEXTURE_SIZE_2D = self.MAX_TEXTURE_SIZE_3D = None

        @property
        def order(self):
            return self.node.order

        def reset(self):
            state.trace.append(("base.reset",))

        def _on_matrix_change(self):
            state.trace.append(("matrix",))

        def _on_blending_change(self):
            state.trace.append(("base.blending",))

    def query(constant):
        state.trace.append(("gl", constant))
        if constant == state.gl_failure:
            raise RuntimeError("GL query failed")
        return state.gl_values[constant]

    def texture(data, **kwargs):
        state.trace.append(("texture", data, kwargs))
        if state.texture_failure:
            raise RuntimeError("texture creation failed")
        result = SimpleNamespace(data=data, kwargs=kwargs)
        state.textures.append(result)
        return result

    def texture_dtype(count, dtype):
        state.trace.append(("texture_dtype", count, str(dtype)))
        return state.texture_dtype

    namespace = {
        "np": np,
        "math": math,
        "warnings": warnings,
        "contextmanager": contextmanager,
        "lru_cache": lru_cache,
        "Canvas": Canvas,
        "get_current_canvas": lambda: state.current_canvas,
        "gl": SimpleNamespace(GL_MAX_TEXTURE_SIZE=3379, glGetParameter=query),
        "VispyColormap": VispyColormap,
        "Texture2D": texture,
        "VispyBaseLayer": Base,
        "ScalarFieldBase": object,
        "ScalarFieldLayerNode": object,
        "LabelNode": LabelNode,
        "VolumeNode": VolumeNode,
        "ImageVisual": ImageVisual,
        "TiledImageNode": TiledImageNode,
        "_texture_dtype": texture_dtype,
        "cast": lambda annotation, value: value,
        "_cast_labels_data_to_texture_dtype_direct": lambda data, cmap: data,
    }
    return state, namespace


def _compile(text, namespace, filename):
    exec(
        compile("from __future__ import annotations\n" + text, filename, "exec"),
        namespace,
    )


def _build(sources, *, current=False):
    state, namespace = _services()
    if current:
        # This fixture executes only in root's baseline/installed pytest jobs.
        # The original namespace never imports a live application helper.
        namespace["_native"] = importlib.import_module("naparixx._native")
    colormap = sources["utils/colormaps/colormap.py"]
    _compile(
        "class _RebuildableCache(dict):\n"
        + _methods(colormap, "_RebuildableCache", ("__deepcopy__",))
        + "\nclass LabelColormapBase:\n"
        + _methods(colormap, "LabelColormapBase", ("_selection_as_minimum_dtype",))
        + "\nclass CyclicLabelColormap(LabelColormapBase):\n"
        + _methods(colormap, "CyclicLabelColormap", ("_background_as_minimum_dtype",))
        + "\nclass DirectLabelColormap(LabelColormapBase):\n"
        + _methods(colormap, "DirectLabelColormap", ("_selection_as_minimum_dtype",)),
        namespace,
        "colormap-prerequisites.py",
    )
    base, cyclic, direct = (
        namespace[name]
        for name in ("LabelColormapBase", "CyclicLabelColormap", "DirectLabelColormap")
    )

    class Cyclic(cyclic):
        def __init__(self, **values):
            defaults = {
                "colors": np.ones((3, 4), dtype=np.float32),
                "selection": 1,
                "use_selection": False,
                "background_value": 0,
            }
            defaults.update(values)
            self.__dict__.update(defaults)

        def model_dump(self):
            state.trace.append(("model_dump", self))
            return {
                key: getattr(self, key)
                for key in ("colors", "selection", "use_selection", "background_value")
            }

        def _get_mapping_from_cache(self, dtype):
            state.trace.append(("cache", self, str(dtype)))
            return state.mapping

        def _data_to_texture(self, value):
            state.trace.append(("data_to_texture", self, value))
            return state.conversions.get(int(value), value)

    class Direct(direct):
        def __init__(self, **values):
            defaults = {
                "colors": np.array([[0, 0, 0, 1]], dtype=np.float32),
                "selection": 7,
                "use_selection": False,
                "_num_unique_colors": 3,
                "texture_colors": {0: (0, 0, 0, 0), 1: (1, 0, 0, 1)},
            }
            defaults.update(values)
            self.__dict__.update(defaults)

        def _get_mapping_from_cache(self, dtype):
            state.trace.append(("cache", self, str(dtype)))
            return state.mapping

        def _values_mapping_to_minimum_values_set(self):
            state.trace.append(("minimum_mapping", self))
            return {}, self.texture_colors

    namespace["CyclicLabelColormap"], namespace["DirectLabelColormap"] = Cyclic, Direct
    # These existing dictionaries/lists are pinned metadata boundaries. Rebuild
    # them independently rather than borrow the fork's global state.
    namespace["_VISPY_FORMAT_TO_DTYPE"] = {
        "r8": np.dtype(np.uint8),
        "r16": np.dtype(np.uint16),
        "r32f": np.dtype(np.float32),
        None: np.dtype(np.float32),
    }
    namespace["_DTYPE_TO_VISPY_FORMAT"] = {
        np.dtype(np.uint8): "r8",
        np.dtype(np.uint16): "r16",
        np.dtype(np.float32): "r32f",
    }
    namespace["texture_dtypes"] = [
        np.dtype(np.uint8),
        np.dtype(np.uint16),
        np.dtype(np.float32),
    ]
    gl_source = sources["_vispy/utils/gl.py"]
    _compile(
        "\n".join(
            _function(gl_source, name)
            for name in ("_opengl_context", "get_max_texture_sizes", "fix_data_dtype")
        ),
        namespace,
        "texture-gl-services.py",
    )
    scalar_source = sources["_vispy/layers/scalar_field.py"]
    _compile(
        _function(scalar_source, "get_dtype_from_vispy_texture_format")
        + "\nclass VispyScalarFieldBaseLayer(VispyBaseLayer[ScalarFieldBase]):\n"
        + _methods(scalar_source, "VispyScalarFieldBaseLayer", SCALAR),
        namespace,
        "scalar-texture-controller.py",
    )
    labels_source = sources["_vispy/layers/labels.py"]
    for node in ast.parse(labels_source).body:
        if isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name)
            and target.id.endswith("shader")
            or isinstance(target, ast.Name)
            and "lookup_shader" in target.id
            for target in node.targets
        ):
            _compile(
                _segment(labels_source, node), namespace, "label-shader-metadata.py"
            )
    text = (
        "class LabelVispyColormap(VispyColormap):\n"
        + _methods(labels_source, "LabelVispyColormap", ("__init__",))
        + "\nclass DirectLabelVispyColormap(VispyColormap):\n"
        + _methods(labels_source, "DirectLabelVispyColormap", ("__init__",))
        + "\n"
        + _function(labels_source, "build_textures_from_dict")
        + "\n"
        + _function(labels_source, "_select_colormap_texture")
        + "\nclass VispyLabelsLayer(VispyScalarFieldBaseLayer):\n"
        + _methods(labels_source, "VispyLabelsLayer", LABELS)
        + "\nclass LabelLayerNode(ScalarFieldLayerNode):\n"
        + _methods(labels_source, "LabelLayerNode", LABEL_NODE)
    )
    _compile(text, namespace, "labels-texture-controller.py")
    return SimpleNamespace(
        state=state,
        namespace=namespace,
        base=base,
        cyclic=Cyclic,
        direct=Direct,
        cache=namespace["_RebuildableCache"],
        scalar=namespace["VispyScalarFieldBaseLayer"],
        labels=namespace["VispyLabelsLayer"],
        node=namespace["LabelLayerNode"],
    )


@pytest.fixture
def implementations():
    original, current = {}, {}
    for relative, digest in HASHES.items():
        raw = (PINNED / relative).read_bytes()
        assert hashlib.sha256(raw).hexdigest() == digest
        original[relative] = raw.decode("utf-8")
        current[relative] = (CURRENT / relative).read_text(encoding="utf-8")
    return _build(original), _build(current, current=True)


def _array(value):
    return value.shape, str(value.dtype), value.tolist()


def _compare(implementations, scenario, contract=None):
    expected = scenario(implementations[0])
    if contract is not None:
        assert expected == contract
    assert scenario(implementations[1]) == expected


def _error(callback):
    try:
        callback()
    except Exception as error:
        return type(error).__name__, str(error)
    raise AssertionError("Expected controller/provider failure")


def _layer(impl, *, data=None, raw_dtype=np.uint8, view_dtype=np.uint8, ndisplay=2):
    data = np.ones((2, 3), dtype=np.uint8) if data is None else data
    image = SimpleNamespace(
        raw=np.zeros(data.shape, dtype=raw_dtype),
        view=np.zeros(data.shape, dtype=view_dtype),
    )
    return SimpleNamespace(
        events=_Events(impl.state),
        _slice=SimpleNamespace(image=image, empty=False),
        _slice_input=SimpleNamespace(
            ndisplay=ndisplay, displayed=tuple(range(data.ndim))
        ),
        _data_view=data,
        ndim=data.ndim,
        visible=True,
        multiscale=False,
        loaded=True,
        rendering="translucent",
        depiction="volume",
        custom_interpolation_kernel_2d=np.ones((1, 1)),
        iso_gradient_mode="fast",
        show_selected_label=False,
        plane=SimpleNamespace(
            events=_Events(impl.state, "plane."),
            position=(1, 2, 3),
            normal=(0, 0, 1),
            thickness=2,
        ),
        _transforms={"tile2data": SimpleNamespace(scale=np.ones(data.ndim))},
        refresh=lambda: impl.state.trace.append(("refresh",)),
    )


def _controller(impl, owner="scalar", *, layer=None, node=None):
    controller = object.__new__(getattr(impl, owner))
    controller.layer = _layer(impl) if layer is None else layer
    controller.node = impl.namespace["LabelNode"]() if node is None else node
    controller._layer_node = SimpleNamespace(get_node=lambda *args: controller.node)
    controller.MAX_TEXTURE_SIZE_2D = controller.MAX_TEXTURE_SIZE_3D = None
    controller._colormap_dtypes = None
    return controller


def _trace(impl, **identities):
    def normalize(value):
        for name, original in identities.items():
            if value is original:
                return "@" + name
        if isinstance(value, np.ndarray):
            return ("array", *_array(value))
        if isinstance(value, np.generic):
            return str(value.dtype), value.item()
        if isinstance(value, dict):
            return tuple((key, normalize(item)) for key, item in value.items())
        if isinstance(value, list | tuple):
            return tuple(normalize(item) for item in value)
        if value is None or isinstance(value, bool | int | float | str):
            return value
        if isinstance(value, impl.cyclic):
            return (
                "cyclic",
                value.use_selection,
                value.selection,
                value.background_value,
            )
        return type(value).__name__

    return [normalize(event) for event in impl.state.trace]


def test_all_35_bodies_have_independent_exact_metadata_and_lexical_hierarchies(
    implementations,
):
    first, second = implementations
    groups = (
        ("scalar", SCALAR),
        ("labels", LABELS),
        ("node", LABEL_NODE),
        ("cache", ("__deepcopy__",)),
        ("base", ("_selection_as_minimum_dtype",)),
    )
    functions = [
        "_opengl_context",
        "get_max_texture_sizes",
        "fix_data_dtype",
        "get_dtype_from_vispy_texture_format",
        "build_textures_from_dict",
        "_select_colormap_texture",
    ]
    pairs = [
        (vars(getattr(first, owner))[name], vars(getattr(second, owner))[name])
        for owner, names in groups
        for name in names
    ]
    pairs.extend((first.namespace[name], second.namespace[name]) for name in functions)
    pairs.extend(
        (
            first.namespace[owner].__mro__[1].__dict__[name],
            second.namespace[owner].__mro__[1].__dict__[name],
        )
        for owner, name in (
            ("CyclicLabelColormap", "_background_as_minimum_dtype"),
            ("DirectLabelColormap", "_selection_as_minimum_dtype"),
        )
    )
    pairs.extend(
        (
            first.namespace[owner].__dict__["__init__"],
            second.namespace[owner].__dict__["__init__"],
        )
        for owner in ("LabelVispyColormap", "DirectLabelVispyColormap")
    )
    assert len(pairs) == 35
    for pinned, current in pairs:
        assert pinned is not current
        assert inspect.signature(pinned) == inspect.signature(current)
        assert pinned.__doc__ == current.__doc__
        assert inspect.unwrap(pinned).__globals__ is first.namespace
        assert inspect.unwrap(current).__globals__ is second.namespace
    assert first.labels.__mro__[1] is first.scalar
    assert second.labels.__mro__[1] is second.scalar


def test_cache_deepcopy_constructs_an_empty_same_type_without_touching_values_or_memo(
    implementations,
):
    def scenario(impl):
        calls = []

        class Cache(impl.cache):
            def __init__(self):
                calls.append("construct")
                super().__init__()

        class Value:
            def __deepcopy__(self, memo):
                raise RuntimeError("value must not be copied")

        source = Cache()
        source["owned"] = Value()
        marker = object()
        memo = {123: marker}
        direct = source.__deepcopy__(memo)
        indirect = copy.deepcopy(source)
        return (
            type(direct) is type(source),
            direct is not source,
            dict(direct),
            type(indirect) is type(source),
            dict(indirect),
            calls,
            memo == {123: marker},
            len(source),
        )

    _compare(
        implementations,
        scenario,
        (True, True, {}, True, {}, ["construct", "construct", "construct"], True, 1),
    )


@pytest.mark.parametrize("owner", ["base", "direct", "cyclic"])
def test_selection_and_background_prerequisites_cast_before_dynamic_texture_callback(
    implementations,
    owner,
):
    def scenario(impl):
        parent = getattr(impl, owner)
        instance = object.__new__(parent)
        instance.selection, instance.background_value = 7, -2
        trace = []

        def convert(value, cmap=None):
            trace.append((str(value.dtype), int(value), cmap is instance))
            return np.uint16(19)

        instance._data_to_texture = convert
        impl.namespace["_cast_labels_data_to_texture_dtype_direct"] = convert
        name = (
            "_background_as_minimum_dtype"
            if owner == "cyclic"
            else ("_selection_as_minimum_dtype")
        )
        result = getattr(instance, name)(np.dtype(np.int16))
        return type(result).__name__, result, trace

    _compare(implementations, scenario)


def test_gl_context_keeps_existing_canvas_and_cleans_created_canvas_on_success_or_error(
    implementations,
):
    def scenario(impl):
        outcomes = []
        for existing, fail in ((True, False), (False, False), (False, True)):
            impl.state.current_canvas = object() if existing else None
            impl.state.trace.clear()
            try:
                with impl.namespace["_opengl_context"]():
                    impl.state.trace.append(("body",))
                    if fail:
                        raise RuntimeError("context body failed")
            except RuntimeError as error:
                assert str(error) == "context body failed"
            outcomes.append(list(impl.state.trace))
        return outcomes

    _compare(
        implementations,
        scenario,
        [
            [("body",)],
            [("canvas", False), ("body",), ("canvas.close",)],
            [("canvas", False), ("body",), ("canvas.close",)],
        ],
    )


def test_raw_gl_generator_is_lazy_and_explicit_close_runs_cleanup_exactly_once(
    implementations,
):
    def scenario(impl):
        factory = impl.namespace["_opengl_context"].__wrapped__
        generator = factory()
        assert impl.state.trace == []
        yielded = next(generator)
        first_close = generator.close()
        second_close = generator.close()
        stopped = _error(lambda: next(generator))
        return yielded, first_close, second_close, stopped, list(impl.state.trace)

    _compare(
        implementations,
        scenario,
        (
            None,
            None,
            None,
            ("StopIteration", ""),
            [("canvas", False), ("canvas.close",)],
        ),
    )


def test_raw_gl_generator_abandonment_and_canvas_cycle_finalize_without_fixture_roots(
    implementations,
):
    def scenario(impl):
        trace, canvas_refs = [], []

        class Canvas:
            def __init__(self, *, show):
                trace.append(("canvas", show))
                canvas_refs.append(weakref.ref(self))

            def close(self):
                trace.append(("canvas.close",))

        impl.namespace["Canvas"] = Canvas
        factory = impl.namespace["_opengl_context"].__wrapped__
        generator = factory()
        assert trace == []
        generator_ref = weakref.ref(generator)
        assert next(generator) is None
        assert canvas_refs[-1]() is not None
        del generator
        assert generator_ref() is None
        assert canvas_refs[-1]() is None
        abandoned_trace = list(trace)
        trace.clear()

        generator = factory()
        assert trace == []
        generator_ref = weakref.ref(generator)
        assert next(generator) is None
        canvas = canvas_refs[-1]()
        assert canvas is not None
        canvas.generator = generator
        del canvas
        del generator
        gc.collect()
        return (
            abandoned_trace,
            list(trace),
            generator_ref() is None,
            all(reference() is None for reference in canvas_refs),
        )

    _compare(
        implementations,
        scenario,
        (
            [("canvas", False), ("canvas.close",)],
            [("canvas", False), ("canvas.close",)],
            True,
            True,
        ),
    )


def test_gl_sizes_use_two_contexts_normalize_falsy_and_cache_exact_tuple(
    implementations,
):
    def scenario(impl):
        impl.state.gl_values = {3379: 0, 32883: 64}
        lookup = impl.namespace["get_max_texture_sizes"]
        first, second = lookup(), lookup()
        return first, second is first, list(impl.state.trace), lookup.cache_info().hits

    _compare(
        implementations,
        scenario,
        (
            (None, 64),
            True,
            [
                ("canvas", False),
                ("gl", 3379),
                ("canvas.close",),
                ("canvas", False),
                ("gl", 32883),
                ("canvas.close",),
            ],
            1,
        ),
    )


def test_gl_query_failure_closes_the_current_temporary_canvas_and_is_not_cached(
    implementations,
):
    def scenario(impl):
        impl.state.gl_failure = 32883
        lookup = impl.namespace["get_max_texture_sizes"]
        error = _error(lookup)
        failed_trace = list(impl.state.trace)
        impl.state.gl_failure = None
        impl.state.trace.clear()
        result = lookup()
        return error, failed_trace, result, lookup.cache_info().currsize

    _compare(implementations, scenario)


def test_fix_dtype_keeps_supported_aliases_casts_other_numeric_types_and_chains_errors(
    implementations,
):
    def scenario(impl):
        results = []
        for dtype in (
            np.uint8,
            np.uint16,
            np.float32,
            np.int8,
            np.int16,
            np.uint32,
            np.float64,
            np.bool_,
        ):
            data = np.array([0, 1, 2], dtype=dtype)
            result = impl.namespace["fix_data_dtype"](data)
            results.append((str(data.dtype), _array(result), result is data))
        try:
            impl.namespace["fix_data_dtype"](np.array([object()], dtype=object))
        except TypeError as error:
            failure = type(error).__name__, str(error), type(error.__cause__).__name__
        else:
            raise AssertionError("Object texture should fail")
        return results, failure

    _compare(implementations, scenario)


def test_texture_format_lookup_keeps_metadata_aliases_and_float32_fallback(
    implementations,
):
    def scenario(impl):
        function = impl.namespace["get_dtype_from_vispy_texture_format"]
        mapping = impl.namespace["_VISPY_FORMAT_TO_DTYPE"]
        return [
            (name, str(function(name)), function(name) is mapping.get(name))
            for name in ("r8", "r16", "r32f", None)
        ] + [("auto", str(function("auto")), function("auto") == np.dtype(np.float32))]

    _compare(implementations, scenario)


def test_constructors_connections_and_reset_hooks_keep_dynamic_dispatch_order(
    implementations,
):
    def scenario(impl):
        class Life(impl.labels):
            def _on_display_change(self, data=None):
                impl.state.trace.append(("display.callback",))

            def reset(self, event=None):
                impl.state.trace.append(("reset.callback",))

            def _on_data_change(self):
                impl.state.trace.append(("data.callback",))

            def _on_colormap_change(self, event=None):
                impl.state.trace.append(("colormap.callback",))

        layer, marker = _layer(impl), object()
        controller = Life(layer, font_info=marker)
        constructor_trace = _trace(impl, layer=layer, font=marker, node=controller.node)
        initial = (
            controller._array_like,
            _array(controller._data),
            controller._colormap_dtypes,
        )
        controller.node = impl.namespace["VolumeNode"]()
        impl.state.trace.clear()
        impl.labels.reset(controller)
        categorical = controller.node.method
        impl.scalar._on_rendering_change(controller)
        impl.scalar._on_blending_change(controller)
        unsupported = _error(lambda: impl.scalar._on_colormap_change(controller))
        node = controller.node
        return (
            constructor_trace,
            initial,
            _trace(impl),
            categorical,
            node.method,
            node.raycasting_mode,
            node.plane_position,
            node.plane_normal,
            node.plane_thickness,
            node.custom_kernel is layer.custom_interpolation_kernel_2d,
            node.iso_gradient_mode,
            unsupported,
        )

    _compare(implementations, scenario)


def test_scalar_upload_reads_view_twice_and_preserves_raw_and_uploaded_aliases(
    implementations,
):
    def scenario(impl):
        first = np.ones((2, 3), dtype=np.uint8)
        second = np.full((2, 3), 7, dtype=np.uint8)
        layer = _layer(impl)

        class Layer:
            reads = 0

            def __getattr__(self, name):
                return getattr(layer, name)

            @property
            def _data_view(self):
                self.reads += 1
                impl.state.trace.append(("data_view", self.reads))
                return first if self.reads == 1 else second

        proxy = Layer()
        controller = _controller(impl, layer=proxy)
        node = controller.node

        def get_node(ndisplay, dtype, shape):
            impl.state.trace.append(("get_node", ndisplay, str(dtype), shape))
            return node

        controller._layer_node.get_node = get_node
        controller._on_data_change()
        return (
            controller._data is first,
            node.data is second,
            proxy.reads,
            _trace(impl, node=node, uploaded=second),
            node.visible,
        )

    _compare(
        implementations,
        scenario,
        (
            True,
            True,
            2,
            [
                ("data_view", 1),
                ("data_view", 2),
                ("get_node", 2, "uint8", (2, 3)),
                ("set_data", "@node", "@uploaded"),
                ("matrix",),
                ("node.update", "@node"),
            ],
            True,
        ),
    )


def test_scalar_node_switch_pads_upload_shape_before_display_and_keeps_new_node_order(
    implementations,
):
    def scenario(impl):
        data = np.arange(6, dtype=np.uint8).reshape(2, 3)
        layer = _layer(impl, data=data, ndisplay=3)
        controller = _controller(impl, layer=layer)
        old = controller.node
        old.parent = parent = object()
        old.order = 99
        volume = impl.namespace["VolumeNode"](texture_format="r8")
        volume.order = 7

        def get_node(ndisplay, dtype, shape):
            impl.state.trace.append(("get_node", ndisplay, str(dtype), shape))
            return volume

        controller._layer_node.get_node = get_node
        controller._on_data_change()
        return (
            controller._data is data,
            controller.node is volume,
            np.shares_memory(volume.data, data),
            _array(volume.data),
            old.parent is None,
            volume.parent is parent,
            volume.order,
            _trace(impl, node=volume, uploaded=volume.data),
        )

    _compare(implementations, scenario)


def test_display_replacement_keeps_children_tiled_parents_and_placeholder(
    implementations,
):
    def scenario(impl):
        layer = _layer(impl, data=np.zeros((2, 3, 4), dtype=np.uint8), ndisplay=3)
        layer._slice.empty = True
        controller = _controller(impl, layer=layer)
        old, parent = controller.node, object()
        old.parent, old.order = parent, 99
        tiled = impl.namespace["TiledImageNode"]()
        regular, tiled_child = (
            SimpleNamespace(parent=old),
            SimpleNamespace(parent=tiled),
        )
        old.children = [regular, tiled_child]
        volume = impl.namespace["VolumeNode"](texture_format="r16")
        volume.order = 7
        controller._layer_node.get_node = lambda *args: volume
        controller._on_display_change()
        return (
            old.parent is None,
            controller.node is volume,
            volume.parent is parent,
            volume.order,
            regular.parent is volume,
            tiled_child.parent is tiled,
            volume.visible,
            _array(volume.data),
            _trace(impl, node=volume, uploaded=volume.data),
        )

    _compare(implementations, scenario)


def test_display_upload_failure_keeps_detachment_and_old_children(
    implementations,
):
    def scenario(impl):
        controller = _controller(impl)
        old = controller.node
        old.parent = object()
        child = SimpleNamespace(parent=old)
        old.children = [child]
        new = impl.namespace["LabelNode"](texture_format="r8")
        controller._layer_node.get_node = lambda *args: new
        impl.state.set_data_failure = True
        data = np.ones((2, 3), dtype=np.uint8)
        error = _error(lambda: controller._on_display_change(data))
        return (
            error,
            controller.node is new,
            old.parent is None,
            new.parent is None,
            child.parent is old,
            _trace(impl, node=new, uploaded=data),
        )

    _compare(
        implementations,
        scenario,
        (
            ("RuntimeError", "node upload failed"),
            True,
            True,
            True,
            True,
            [("set_data", "@node", "@uploaded")],
        ),
    )


def test_downsample_warns_publishes_axis_scale_then_returns_a_strided_data_alias(
    implementations,
):
    def scenario(impl):
        data = np.arange(40, dtype=np.uint8).reshape(5, 8)
        layer = _layer(impl, data=data)
        layer._slice_input.displayed = (1, 0)
        controller = _controller(impl, layer=layer)
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            output = controller.downsample_texture(data, 3)
        return (
            _array(output),
            np.shares_memory(output, data),
            output is data,
            _array(layer._transforms["tile2data"].scale),
            _trace(impl),
            [(warning.category.__name__, str(warning.message)) for warning in caught],
        )

    _compare(implementations, scenario)


def test_downsample_noop_keeps_identity_and_multiscale_error_leaves_state_untouched(
    implementations,
):
    def scenario(impl):
        data = np.ones((4, 4), dtype=np.uint8)
        controller = _controller(impl, layer=_layer(impl, data=data))
        scale = controller.layer._transforms["tile2data"].scale
        unchanged = controller.downsample_texture(data, 8) is data
        controller.layer.multiscale = True
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            error = _error(lambda: controller.downsample_texture(data, 2))
        return (
            unchanged,
            error,
            controller.layer._transforms["tile2data"].scale is scale,
            (_array(scale)),
            len(caught),
            _trace(impl),
        )

    _compare(implementations, scenario)


@pytest.mark.parametrize("view_dtype", [np.uint8, np.uint16, np.float32])
def test_label_shader_constructor_initializes_vispy_before_dtype_branch_and_selection(
    implementations,
    view_dtype,
):
    def scenario(impl):
        colormap = impl.cyclic(use_selection=True, selection=7)
        constructor = impl.namespace["LabelVispyColormap"]
        if np.dtype(view_dtype).itemsize > 2:
            error = _error(
                lambda: constructor(colormap, np.dtype(view_dtype), np.dtype(np.int16))
            )
            return error, _trace(impl, colormap=colormap)
        result = constructor(colormap, np.dtype(view_dtype), np.dtype(np.int16))
        assert "$selection" not in result.glsl_map
        assert "$use_selection" not in result.glsl_map
        return result.glsl_map, _trace(impl, colormap=colormap)

    _compare(implementations, scenario)


def test_direct_shader_constructor_keeps_single_and_many_shader_substitution(
    implementations,
):
    def scenario(impl):
        outputs = []
        for multi in (False, True):
            impl.state.trace.clear()
            result = impl.namespace["DirectLabelVispyColormap"](
                use_selection=True,
                selection=7,
                scale=255,
                color_map_size=2,
                multi=multi,
            )
            assert "$scale" not in result.glsl_map
            assert "$color_map_size" not in result.glsl_map
            outputs.append((result.glsl_map, _trace(impl)))
        return outputs

    _compare(implementations, scenario)


@pytest.mark.parametrize("case", ["layout", "area_limit", "precision_limit"])
def test_texture_builder_keeps_float32_layout_and_ordered_capacity_failures(
    implementations,
    case,
):
    def scenario(impl):
        build = impl.namespace["build_textures_from_dict"]
        if case == "precision_limit":

            class Colors:
                def __len__(self):
                    return 2**23 + 1

            return _error(lambda: build(Colors(), 1))
        colors = {2: (0, 0, 1, 1), 0: (0, 0, 0, 0), 1: (1, 0, 0, 1), 3: (0, 1, 0, 1)}
        if case == "area_limit":
            return _error(lambda: build(colors, 1))
        output = build(colors, 2)
        assert output.dtype == np.float32
        assert output.shape == (2, 2, 4)
        np.testing.assert_array_equal(output[0, 1], colors[2])
        np.testing.assert_array_equal(output[1, 0], colors[1])
        return _array(output), output.flags.c_contiguous

    _compare(implementations, scenario)


def test_texture_selector_chooses_raw_or_view_cache_and_returns_a_reshape_alias(
    implementations,
):
    def scenario(impl):
        outputs = []
        select = impl.namespace["_select_colormap_texture"]
        for raw_dtype, view_dtype in (
            (np.int8, np.uint8),
            (np.int16, np.uint16),
            (np.uint32, np.uint8),
        ):
            raw, view = np.dtype(raw_dtype), np.dtype(view_dtype)
            colormap = impl.cyclic()
            size = 65536 if raw.itemsize == 2 else 256
            impl.state.mapping = np.ones((size, 4), dtype=np.float32)
            impl.state.trace.clear()
            output = select(colormap, view, raw)
            output[0, 0, 0] = 19
            outputs.append(
                (
                    output.shape,
                    str(output.dtype),
                    np.shares_memory(output, impl.state.mapping),
                    float(impl.state.mapping[0, 0]),
                    _trace(impl, colormap=colormap),
                )
            )
        impl.state.mapping = None
        failure = _error(
            lambda: select(impl.cyclic(), np.dtype(np.uint8), np.dtype(np.uint32))
        )
        return outputs, failure

    _compare(implementations, scenario)


@pytest.mark.parametrize("mode", ["direct8", "cyclic32", "direct32"])
def test_label_rebuild_keeps_shader_texture_alias_and_final_dtype_commit(
    implementations,
    mode,
):
    def scenario(impl):
        wide = mode.endswith("32")
        raw_dtype = np.uint32 if wide else np.uint8
        layer = _layer(impl, raw_dtype=raw_dtype, view_dtype=np.uint8)
        colormap = (
            impl.cyclic(background_value=9)
            if mode == "cyclic32"
            else impl.direct(
                texture_colors={0: (0, 0, 0, 0), 1: (1, 0, 0, 1), 2: (0, 1, 0, 1)}
            )
        )
        layer.colormap = colormap
        layer._direct_colormap = impl.direct(_num_unique_colors=65536)
        controller = _controller(impl, "labels", layer=layer)
        old_texture = object()
        controller.texture_data = old_texture
        old_dtypes = (np.dtype(np.int16), np.dtype(np.uint16))
        controller._colormap_dtypes = old_dtypes
        impl.state.conversions = {9: np.uint8(0)}
        impl.state.gl_values[3379] = 2
        impl.state.texture_dtype = np.dtype(np.float32)
        controller._on_colormap_change()
        texture = controller.node.shared_program["texture2D_values"]
        if mode == "direct32":
            assert controller.texture_data is old_texture
            assert controller.node.shared_program["LUT_shape"] == (2, 2)
            assert texture.data.shape == (2, 2, 4)
            assert "t = t * 1.0" in controller.node.cmap.glsl_map
            alias = False
        else:
            assert controller.texture_data is texture.data
            assert np.shares_memory(texture.data, impl.state.mapping)
            assert "LUT_shape" not in controller.node.shared_program
            alias = True
        assert colormap.background_value == 9 if mode == "cyclic32" else True
        return (
            type(controller.node.cmap).__name__,
            controller.node.cmap.glsl_map,
            _array(texture.data),
            texture.kwargs,
            alias,
            tuple(str(dtype) for dtype in controller._colormap_dtypes),
            _trace(impl, colormap=colormap, texture=texture.data),
        )

    _compare(implementations, scenario)


def test_label_selected_event_short_circuits_and_texture_error_does_not_commit_dtypes(
    implementations,
):
    def scenario(impl):
        layer = _layer(impl)
        layer.colormap = impl.direct()
        controller = _controller(impl, "labels", layer=layer)
        old = object()
        controller.node.cmap, controller.texture_data = old, old
        dtypes = (np.dtype(np.int16), np.dtype(np.uint16))
        controller._colormap_dtypes = dtypes
        impl.state.trace.clear()
        controller._on_colormap_change(SimpleNamespace(type="selected_label"))
        skipped = (
            controller.node.cmap is old,
            controller.texture_data is old,
            controller._colormap_dtypes is dtypes,
            _trace(impl),
        )
        layer.show_selected_label = True
        impl.state.texture_failure = True
        error = _error(
            lambda: controller._on_colormap_change(
                SimpleNamespace(type="selected_label")
            )
        )
        return (
            skipped,
            error,
            type(controller.node.cmap).__name__,
            controller.texture_data is old,
            controller._colormap_dtypes is dtypes,
            "texture2D_values" not in controller.node.shared_program,
            _trace(impl, colormap=layer.colormap),
        )

    _compare(implementations, scenario)


def test_label_data_change_uploads_before_dtype_reads_and_rebuild(
    implementations,
):
    def scenario(impl):
        layer = _layer(impl)

        class DtypeHolder:
            def __init__(self, name, value):
                self.name, self.value = name, np.dtype(value)

            @property
            def dtype(self):
                impl.state.trace.append(("dtype", self.name, str(self.value)))
                return self.value

        raw, view = DtypeHolder("raw", np.uint8), DtypeHolder("view", np.uint8)
        layer._slice.image = SimpleNamespace(raw=raw, view=view)
        controller = _controller(impl, "labels", layer=layer)
        controller._colormap_dtypes = (np.dtype(np.uint8),) * 2

        def rebuild():
            impl.state.trace.append(("rebuild",))
            controller._colormap_dtypes = (raw.value, view.value)

        controller._on_colormap_change = rebuild
        controller._on_data_change()
        same_trace = _trace(impl, node=controller.node, uploaded=layer._data_view)
        impl.state.trace.clear()
        raw.value = np.dtype(np.uint32)
        controller._on_data_change()
        return (
            same_trace,
            _trace(impl, node=controller.node, uploaded=layer._data_view),
            tuple(str(dtype) for dtype in controller._colormap_dtypes),
        )

    _compare(implementations, scenario)


def test_partial_upload_checks_loaded_and_shape_before_event_alias_upload(
    implementations,
):
    def scenario(impl):
        controller = _controller(impl, "labels")
        data, offset = np.full((1, 1), 7, dtype=np.uint8), (0, 1)
        event = SimpleNamespace(data=data, offset=offset)

        class Texture:
            shape = (2, 3)

            def scale_and_set_data(self, value, *, copy, offset):
                impl.state.trace.append(
                    ("partial", value is data, copy, offset is event.offset)
                )

        texture = Texture()
        controller.node._texture = texture
        controller.layer.loaded = False
        controller._on_partial_labels_update(event)
        unloaded = _trace(impl)
        controller.layer.loaded = True
        texture.shape = (3, 3)
        controller._on_partial_labels_update(event)
        mismatch = _trace(impl)
        impl.state.trace.clear()
        texture.shape = (2, 3)
        controller._on_partial_labels_update(event)
        return unloaded, mismatch, _trace(impl, node=controller.node)

    _compare(
        implementations,
        scenario,
        (
            [],
            [("refresh",)],
            [("partial", True, False, True), ("node.update", "@node")],
        ),
    )


def test_label_node_builds_both_nodes_and_rebuild_recursion_drops_shape_argument(
    implementations,
):
    def scenario(impl):
        calls = []

        class Nodes(impl.node):
            def get_node(self, ndisplay, dtype=None, shape=None):
                calls.append((ndisplay, str(dtype), shape))
                return super().get_node(ndisplay, dtype, shape)

        custom = object()
        nodes = Nodes(custom_node=custom, texture_format="r8")
        old_image, old_volume = nodes._image_node, nodes._volume_node
        image = nodes.get_node(2, np.dtype(np.uint16), (10, 20))
        volume = nodes.get_node(3)
        constructor_summary = [
            (name, None if data is None else _array(data), kwargs)
            for name, data, kwargs in impl.state.constructors
        ]
        automatic = impl.node(custom_node=custom, texture_format="auto")
        before = len(impl.state.constructors)
        automatic_image = automatic.get_node(2, np.dtype(np.uint16))
        return (
            nodes._custom_node is custom,
            image is nodes._image_node,
            image is not old_image,
            volume is not old_volume,
            image.texture_format,
            volume.texture_format,
            calls,
            constructor_summary,
            automatic._image_node.data is None,
            automatic_image is automatic._image_node,
            len(impl.state.constructors) == before,
            automatic_image is not custom,
        )

    _compare(implementations, scenario)
