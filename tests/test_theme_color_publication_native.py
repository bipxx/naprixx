"""Frozen original theme/color closure versus current canonical publication."""

# Each source-family callback completes before the loop advances.
# ruff: noqa: B023
from __future__ import annotations

import ast
import builtins
import hashlib
import json
import sys
import warnings
from pathlib import Path
from types import ModuleType, SimpleNamespace

import numpy as np
import pytest
from pydantic import TypeAdapter

import napari.settings as settings_module
from napari._vispy import canvas as vispy_module
from napari.components import canvas as canvas_module
from napari.utils import color as color_module
from napari.utils import theme as theme_module
from napari.utils.colormaps import standardize_color as conversion_module

ROOT = Path(__file__).resolve().parents[1]
BEFORE = ROOT / ".artifacts/theme-color-publication-before"
MANIFEST = ROOT / ".artifacts/theme-color-publication-before-snapshots.json"
SELECTION = ROOT / ".artifacts/settings-next-owner-after-publication-selection.json"
SELECTION_SHA = "f66afdf3708dfb800bd1f5fecd5eac228c510e96c89838412f03230b95e3505a"
HANDLERS = (
    "transform_color",
    "_handle_str",
    "_handle_list_like",
    "_handle_generator",
    "_handle_array",
    "_convert_array_to_correct_format",
    "_handle_str_list_like",
    "_handle_none",
    "_normalize_color_array",
)


def _function(tree, qualified):
    owner, _, name = qualified.rpartition(".")
    body = tree.body
    if owner:
        body = next(
            n.body for n in body if isinstance(n, ast.ClassDef) and n.name == owner
        )
    return next(n for n in body if isinstance(n, ast.FunctionDef) and n.name == name)


def _original_module(name, relative, namespace, names, overrides=None, registry=False):
    path = BEFORE / relative
    tree = ast.parse(path.read_bytes())
    nodes = [
        n
        for n in tree.body
        if isinstance(n, ast.ImportFrom) and n.module == "__future__"
    ] + [
        n
        for n in tree.body
        if isinstance(n, (ast.ClassDef, ast.FunctionDef)) and n.name in names
    ]
    if registry:
        nodes += [
            n
            for n in tree.body
            if isinstance(n, ast.AnnAssign)
            and isinstance(n.target, ast.Name)
            and n.target.id == "_color_switch"
        ]
    module = ModuleType(name)
    module.__file__ = str(path)
    module.__dict__.update(
        {
            key: value
            for key, value in namespace.items()
            if key
            not in ("__name__", "__file__", "__spec__", "__loader__", "__annotations__")
        }
    )
    module.__dict__.update(overrides or {})
    assert name not in sys.modules
    sys.modules[name] = module
    try:
        exec(
            compile(ast.Module(nodes, []), str(path), "exec", dont_inherit=True),
            module.__dict__,
        )
    except BaseException:
        sys.modules.pop(name, None)
        raise
    return module


class Family:
    def __init__(self, name, conversion, color, theme, canvas, vispy):
        self.name = name
        self.conversion = conversion
        self.color = color
        self.theme = theme
        self.Canvas = canvas.Canvas
        self.ColorValue = color.ColorValue
        self.Theme = theme.Theme
        self.canvas_namespace = canvas.__dict__
        self.background = canvas.Canvas.__dict__["background_color"].fget
        self.viewer_update = canvas.Canvas._update_bgcolor_from_viewer
        self.vispy_update = vispy.VispyCanvas._on_bgcolor_change


@pytest.fixture(scope="module")
def families():
    assert hashlib.sha256(SELECTION.read_bytes()).hexdigest() == SELECTION_SHA
    for row in json.loads(MANIFEST.read_bytes())["files"]:
        source = row["snapshot"]
        raw = (ROOT / source["path"]).read_bytes()
        assert len(raw) == source["bytes"]
        assert hashlib.sha256(raw).hexdigest() == source["sha256"]
    for row in json.loads(SELECTION.read_bytes())["selected_bodies"]:
        current = _function(
            ast.parse((BEFORE / row["source"]).read_bytes()), row["qualified_name"]
        )
        pinned = _function(
            ast.parse((BEFORE / row["pinned_source"]).read_bytes()),
            row["qualified_name"],
        )
        assert ast.dump(current, include_attributes=False) == ast.dump(
            pinned, include_attributes=False
        )
    modules = []
    try:
        conversion = _original_module(
            "_naparixx_theme_color_conversion_original",
            "src/napari/utils/colormaps/standardize_color.py",
            conversion_module.__dict__,
            HANDLERS,
            registry=True,
        )
        modules.append(conversion)
        color = _original_module(
            "_naparixx_theme_color_value_original",
            "src/napari/utils/color.py",
            color_module.__dict__,
            ("ColorValue",),
            {"transform_color": conversion.transform_color},
        )
        modules.append(color)
        theme = _original_module(
            "_naparixx_theme_color_theme_original",
            "src/napari/utils/theme.py",
            theme_module.__dict__,
            ("Theme", "get_system_theme", "get_theme", "available_themes"),
        )
        modules.append(theme)
        canvas = _original_module(
            "_naparixx_theme_color_canvas_original",
            "src/napari/components/canvas.py",
            canvas_module.__dict__,
            ("Canvas",),
            {"ColorValue": color.ColorValue, "get_theme": theme.get_theme},
        )
        modules.append(canvas)
        tree = ast.parse((BEFORE / "src/napari/_vispy/canvas.py").read_bytes())
        method = _function(tree, "VispyCanvas._on_bgcolor_change")
        declaration = ast.ClassDef(
            name="VispyCanvas",
            bases=[],
            keywords=[],
            body=[method],
            decorator_list=[],
            type_params=[],
        )
        ast.copy_location(declaration, method)
        vispy = ModuleType("_naparixx_theme_color_vispy_original")
        assert vispy.__name__ not in sys.modules
        sys.modules[vispy.__name__] = vispy
        modules.append(vispy)
        exec(
            compile(
                ast.fix_missing_locations(ast.Module([declaration], [])),
                str(BEFORE / "src/napari/_vispy/canvas.py"),
                "exec",
            ),
            vispy.__dict__,
        )
        assert theme._themes is theme_module._themes
        yield (
            Family("original", conversion, color, theme, canvas, vispy),
            Family(
                "current",
                conversion_module,
                color_module,
                theme_module,
                canvas_module,
                vispy_module,
            ),
        )
    finally:
        for module in modules:
            sys.modules.pop(module.__name__, None)


class Probe:
    def __init__(self, tag, values, trace, read=None, write=None):
        object.__setattr__(self, "_tag", tag)
        object.__setattr__(self, "_values", values)
        object.__setattr__(self, "_trace", trace)
        object.__setattr__(self, "_read", read)
        object.__setattr__(self, "_write", write)

    def __getattr__(self, name):
        self._trace.append(("read", self._tag, name))
        if self._read is not None:
            return self._read(name)
        return self._values[name]

    def __setattr__(self, name, value):
        self._trace.append(("write", self._tag, name))
        if self._write is not None:
            self._write(name, value)
        self._values[name] = value


def _disconnect_canvas(canvas):
    settings = settings_module.get_settings()
    for field in ("grid_stride", "grid_width", "grid_height", "grid_spacing"):
        getattr(settings.application.events, field).disconnect(
            canvas._update_viewer_grid
        )
    settings.appearance.events.theme.disconnect(canvas.events.background_color)


@pytest.mark.parametrize("mode", ["readonly_override", "viewer_vs_global"])
def test_original_real_canvas_override_copy_and_viewer_theme(families, mode):
    observations = []
    for family in families:
        canvas = family.Canvas()
        try:
            if mode == "readonly_override":
                storage = np.array([[0.1, 0.2, 0.3, 1]], dtype=np.float32)
                storage.flags.writeable = False
                override = family.ColorValue(storage)
                canvas.background_color_override = override
                result = canvas.background_color
                assert isinstance(result, family.ColorValue)
                assert result is not canvas.background_color_override
                assert not np.shares_memory(result, canvas.background_color_override)
                np.testing.assert_array_equal(result, override)
                assert result.flags.writeable
            else:
                global_name = settings_module.get_settings().appearance.theme
                global_result = canvas.background_color
                effective = family.theme.get_theme(global_name).type
                other = "light" if effective == "dark" else "dark"
                family.viewer_update(canvas, SimpleNamespace(value=other))
                result = canvas.background_color
                assert canvas._viewer_theme == other
                assert settings_module.get_settings().appearance.theme == global_name
                assert not np.array_equal(result, global_result)
            observations.append((result.dtype.str, result.shape, result.tobytes()))
        finally:
            _disconnect_canvas(canvas)
    assert observations[1] == observations[0]


@pytest.mark.parametrize("mode", ["override", "viewer", "global"])
def test_original_background_repeated_reads_and_callee_order(
    families, monkeypatch, mode
):
    for family in families:
        trace = []
        output = object()

        class Override:
            def copy(self):
                trace.append("copy")
                return output

        override_reads = 0

        def read(name):
            nonlocal override_reads
            if name == "background_color_override":
                override_reads += 1
                return Override() if mode == "override" else None
            if name == "_viewer_theme":
                return "dark" if mode == "viewer" else None
            raise AssertionError(name)

        owner = Probe("canvas", {}, trace, read=read)

        def settings():
            trace.append("settings")
            return SimpleNamespace(appearance=SimpleNamespace(theme="light"))

        def theme(name):
            trace.append(("theme", name))

            def rgb():
                trace.append("rgb")
                return (10, 20, 30)

            return SimpleNamespace(canvas=SimpleNamespace(as_rgb_tuple=rgb))

        class Numpy:
            @property
            def array(self):
                trace.append("array_lookup")

                def array(values):
                    trace.append(("array", values))
                    return np.array(values)

                return array

        def construct(value):
            trace.append("construct")
            return family.ColorValue(value)

        with monkeypatch.context() as patch:
            patch.setitem(family.canvas_namespace, "get_settings", settings)
            patch.setitem(family.canvas_namespace, "get_theme", theme)
            patch.setitem(family.canvas_namespace, "np", Numpy())
            patch.setitem(family.canvas_namespace, "ColorValue", construct)
            result = family.background(owner)
        expected = [("read", "canvas", "background_color_override")]
        if mode == "override":
            expected += [("read", "canvas", "background_color_override"), "copy"]
            assert result is output and override_reads == 2
        else:
            expected += [("read", "canvas", "_viewer_theme")]
            expected += (
                [("read", "canvas", "_viewer_theme")]
                if mode == "viewer"
                else ["settings"]
            )
            expected += [
                "array_lookup",
                ("theme", "dark" if mode == "viewer" else "light"),
                "rgb",
                ("array", (10, 20, 30)),
                "construct",
            ]
            assert isinstance(result, family.ColorValue)
            assert override_reads == 1
        assert trace == expected


@pytest.mark.parametrize("mode", ["unchanged", "override", "truth_reentry"])
def test_original_viewer_theme_comparison_store_truth_and_override(families, mode):
    for family in families:
        trace = []
        old, stored = object(), object()
        override = object()

        class Changed:
            def __bool__(self):
                trace.append("truth")
                assert owner._values["_viewer_theme"] is stored
                if mode == "truth_reentry":
                    owner._values["background_color_override"] = None
                return mode != "unchanged"

        class First:
            def __ne__(self, other):
                trace.append(("compare", other is old))
                return Changed()

        event_reads = 0

        def event_read(name):
            nonlocal event_reads
            assert name == "value"
            event_reads += 1
            return First() if event_reads == 1 else stored

        event = Probe("event", {}, trace, read=event_read)
        owner = Probe(
            "canvas",
            {
                "_viewer_theme": old,
                "background_color_override": override,
                "events": SimpleNamespace(
                    background_color=lambda: trace.append("emit")
                ),
            },
            trace,
        )
        assert family.viewer_update(owner, event) is None
        expected = [
            ("read", "event", "value"),
            ("read", "canvas", "_viewer_theme"),
            ("compare", True),
            ("read", "event", "value"),
            ("write", "canvas", "_viewer_theme"),
            "truth",
        ]
        if mode != "unchanged":
            expected += [("read", "canvas", "background_color_override")]
        if mode == "truth_reentry":
            expected += [("read", "canvas", "events"), "emit"]
        assert owner._values["_viewer_theme"] is stored
        assert event_reads == 2
        assert trace == expected


def test_original_vispy_background_rhs_before_live_scene_store(families):
    for family in families:
        trace = []
        value = object()
        old_scene = Probe("old_scene", {}, trace)
        scene = Probe("scene", {}, trace)

        def background(name):
            assert name == "background_color"
            owner._values["_scene_canvas"] = scene
            return value

        owner = Probe(
            "vispy",
            {
                "_scene_canvas": old_scene,
                "viewer": SimpleNamespace(
                    canvas=Probe("canvas", {}, trace, read=background)
                ),
            },
            trace,
        )
        assert family.vispy_update(owner) is None
        assert scene._values["bgcolor"] is value and old_scene._values == {}
        assert trace == [
            ("read", "vispy", "viewer"),
            ("read", "canvas", "background_color"),
            ("read", "vispy", "_scene_canvas"),
            ("write", "scene", "bgcolor"),
        ]


@pytest.mark.parametrize("mode", ["system_live_registry", "missing"])
def test_original_theme_registry_identity_copy_and_error_list(
    families, monkeypatch, mode
):
    registry = theme_module._themes
    for family in families:
        assert family.theme._themes is registry
        assert family.theme.available_themes() == [*registry, "system"]
        if mode == "missing":
            missing = "_naparixx_source_missing_theme_"
            assert missing not in registry
            with pytest.raises(ValueError) as caught:
                family.theme.get_theme(missing)
            assert str(caught.value) == (
                f"Unrecognized theme {missing}. "
                f"Available themes are {[*registry, 'system']}"
            )
        else:
            original = registry["dark"]
            replacement = original.model_copy(update={"label": "Source changed label"})

            def system():
                registry["dark"] = replacement
                return "dark"

            with monkeypatch.context() as patch:
                patch.setitem(registry, "dark", original)
                patch.setattr(family.theme, "get_system_theme", system)
                copied = family.theme.get_theme("system")
                assert copied is not replacement
                assert copied.model_dump() == replacement.model_dump()
                copied.label = "Caller-owned copy"
                assert registry["dark"].label == "Source changed label"
            assert registry["dark"] is original
        validated = family.Theme.model_validate(registry["dark"].model_dump())
        assert isinstance(validated, family.Theme)


@pytest.mark.parametrize("mode", ["import_error", "qt5_warning", "style_error"])
def test_original_system_theme_import_prefix_warning_and_uncaught_call(
    families, monkeypatch, mode
):
    for family in families:
        trace = []
        failure = RuntimeError("Qt primitive unavailable")
        actual_import = builtins.__import__
        schemes = SimpleNamespace(Dark=object(), Light=object())

        def style_hints():
            trace.append("style_hints")
            raise failure

        providers = {
            "qtpy": SimpleNamespace(QT6=mode != "qt5_warning"),
            "qtpy.QtCore": SimpleNamespace(Qt=SimpleNamespace(ColorScheme=schemes)),
            "qtpy.QtGui": SimpleNamespace(
                QGuiApplication=SimpleNamespace(styleHints=style_hints)
            ),
        }

        def importer(name, globals=None, locals=None, fromlist=(), level=0):
            if name in providers:
                trace.append(
                    (
                        "import",
                        name,
                        fromlist,
                        globals is family.theme.__dict__,
                        locals is None,
                        level,
                    )
                )
                if mode == "import_error" and name == "qtpy.QtCore":
                    raise failure
                return providers[name]
            return actual_import(name, globals, locals, fromlist, level)

        with monkeypatch.context() as patch:
            patch.setattr(builtins, "__import__", importer)
            with warnings.catch_warnings(record=True) as captured:
                warnings.simplefilter("always")
                if mode == "style_error":
                    with pytest.raises(RuntimeError) as caught:
                        family.theme.get_system_theme()
                    assert caught.value is failure
                else:
                    assert family.theme.get_system_theme() == "dark"
        expected = [
            ("import", "qtpy", ("QT6",), True, True, 0),
            ("import", "qtpy.QtCore", ("Qt",), True, True, 0),
        ]
        if mode != "import_error":
            expected += [("import", "qtpy.QtGui", ("QGuiApplication",), True, True, 0)]
        if mode == "style_error":
            expected += ["style_hints"]
        assert trace == expected
        assert [str(w.message) for w in captured] == (
            [
                "System theme detection requires a Qt6 backend. "
                "Please switch to PyQt6 or PySide6 to use it."
            ]
            if mode == "qt5_warning"
            else []
        )
        assert builtins.__import__ is actual_import


@pytest.mark.parametrize(
    "mode",
    [
        "readonly_f32_rgba_stride",
        "f64_rgb_fortran_normalize",
        "u8_rgba_negative_stride",
    ],
)
def test_original_numeric_physical_arrays_warnings_and_input_storage(families, mode):
    observations = []
    for family in families:
        if mode == "readonly_f32_rgba_stride":
            root = np.arange(24, dtype=np.float32).reshape(6, 4) / 24
            value = root[::-2, ::-1]
        elif mode == "f64_rgb_fortran_normalize":
            root = np.array(
                [[0.125, 4, 0.7], [0.7, 0.2, 0.1]], dtype=np.float64, order="F"
            )
            value = root
        else:
            root = (np.arange(24).reshape(6, 4) * 10).astype(np.uint8)
            value = root[::-2]
        value.flags.writeable = False
        initial = root.tobytes()
        with warnings.catch_warnings(record=True) as captured:
            warnings.simplefilter("always")
            result = family.conversion.transform_color(value)
        assert isinstance(result, np.ndarray) and result.dtype == np.float32
        assert result.ndim == 2 and result.shape[1] == 4
        assert root.tobytes() == initial and not value.flags.writeable
        first = family.ColorValue(result[0])
        assert isinstance(first, family.ColorValue) and first.shape == (4,)
        observations.append(
            (
                result.shape,
                result.strides,
                result.flags.writeable,
                result.tobytes(),
                np.shares_memory(result, value),
                [(type(w.message).__name__, str(w.message)) for w in captured],
                first.tobytes(),
            )
        )
    assert observations[1] == observations[0]


@pytest.mark.parametrize("mode", ["generator_numeric", "ragged_list_warning"])
def test_original_generator_consumption_and_list_valueerror_fallback(families, mode):
    observations = []
    for family in families:
        trace = []

        def generate():
            for value in (0.125, 0.25, 0.5):
                trace.append(("yield", value))
                yield value

        class Ragged(list):
            def __len__(self):
                trace.append("len")
                return super().__len__()

            def __iter__(self):
                trace.append("iter")
                return super().__iter__()

        value = (
            generate() if mode == "generator_numeric" else Ragged([[0, 1, 2], [0, 1]])
        )
        with warnings.catch_warnings(record=True) as captured:
            warnings.simplefilter("always")
            result = family.conversion.transform_color(value)
        assert result.dtype == np.float32 and result.shape[1] == 4
        if mode == "generator_numeric":
            assert trace == [("yield", 0.125), ("yield", 0.25), ("yield", 0.5)]
            assert list(value) == []
        else:
            assert len(captured) == 1
        observations.append(
            (trace, result.shape, result.tobytes(), [str(w.message) for w in captured])
        )
    assert observations[1] == observations[0]


def test_original_lru_wrapper_registry_alias_and_mutable_cached_array(families):
    for family in families:
        wrapper = family.conversion._handle_str
        registry = family.conversion._color_switch
        assert registry[str] is wrapper and registry[np.str_] is wrapper
        assert wrapper.cache_parameters() == {"maxsize": 1024, "typed": False}
        assert callable(wrapper.cache_clear) and callable(wrapper.__wrapped__)
        first = wrapper("#13579b")
        saved = first.copy()
        before = wrapper.cache_info()
        try:
            second = family.conversion.transform_color("#13579b")
            assert second is first
            first[0, 0] = np.float32(0.375)
            third = wrapper("#13579b")
            assert third is first and third[0, 0] == np.float32(0.375)
            assert wrapper.cache_info().hits == before.hits + 2
        finally:
            first[...] = saved
        np.testing.assert_array_equal(wrapper("#13579b"), saved)
        uncached = wrapper.__wrapped__("#13579b")
        assert uncached is not first
        np.testing.assert_array_equal(uncached, saved)
        wrapper.cache_clear()
        assert wrapper.cache_info().currsize == 0
        assert wrapper.cache_info().hits == 0 and wrapper.cache_info().misses == 0
        assert registry[str] is wrapper and registry[np.str_] is wrapper
        replacement = wrapper("#13579b")
        assert replacement is not first
        np.testing.assert_array_equal(replacement, saved)
        np.testing.assert_array_equal(first, saved)


@pytest.mark.parametrize("mode", ["return_identity", "raise_identity"])
def test_original_dispatch_uses_live_registry_order_and_handler(
    families, monkeypatch, mode
):
    for family in families:
        registry = family.conversion._color_switch
        keys = list(registry)
        calls = []
        output = object()
        failure = RuntimeError("source handler failed")

        class Input(str):
            pass

        value = Input("source subclass")

        def handler(received):
            calls.append(received is value)
            if mode == "raise_identity":
                raise failure
            return output

        with monkeypatch.context() as patch:
            patch.setitem(registry, str, handler)
            patch.setitem(
                registry,
                Input,
                lambda received: pytest.fail(
                    "later exact class must not outrank first str"
                ),
            )
            if mode == "return_identity":
                assert family.conversion.transform_color(value) is output
            else:
                with pytest.raises(RuntimeError) as caught:
                    family.conversion.transform_color(value)
                assert caught.value is failure
            assert calls == [True]
            assert family.conversion._color_switch is registry
        assert list(registry) == keys


@pytest.mark.parametrize("kind", [ValueError, KeyError], ids=["ValueError", "KeyError"])
def test_original_string_list_error_message_and_exact_cause(
    families, monkeypatch, kind
):
    for family in families:
        error = kind("source parse failure")
        calls = []

        def handler(value):
            calls.append(value)
            raise error

        with monkeypatch.context() as patch:
            patch.setitem(family.conversion._color_switch, str, handler)
            with pytest.raises(ValueError) as caught:
                family.conversion._handle_str_list_like(["bad"])
        assert calls == ["bad"]
        assert str(caught.value) == "Invalid color found: bad at index 0."
        assert caught.value.__cause__ is error


def test_original_colorvalue_none_and_schema_existing_instance_alias(families):
    for family in families:
        value = family.ColorValue(None)
        assert isinstance(value, family.ColorValue)
        np.testing.assert_array_equal(value, np.zeros(4, dtype=np.float32))
        adapter = TypeAdapter(family.ColorValue)
        assert adapter.validate_python(value) is value
        assert adapter.dump_python(value) is value
        assert json.loads(adapter.dump_json(value)) == value.tolist()
        from_json = adapter.validate_json('"#123456"')
        assert isinstance(from_json, family.ColorValue) and from_json.shape == (4,)


def test_original_colorvalue_new_uses_live_subclass_validate(families):
    for family in families:
        input_value, output = object(), object()
        calls = []

        class Override(family.ColorValue):
            @classmethod
            def validate(cls, value):
                calls.append((cls is Override, value is input_value))
                return output

        assert Override(input_value) is output
        assert calls == [(True, True)]
