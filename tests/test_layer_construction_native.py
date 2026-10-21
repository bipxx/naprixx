"""Finite, independent twelve-body constructor and lexical-super baseline.

Complete pinned classes and separately extracted current classes are rebuilt in
test-only namespaces. Before the port, current constructor calls use a genuine
receiver-first lexical super provider at the original call point; this proves
binding glue and the accepted LayerNamespaceBase allocation, not native policy.
After the port, all twelve current bodies must have changed and are executed
without that test transformation. No reconstructed class is mounted in napari.

Real NumPy, transforms, events, overlay models, load-state and first refresh are
dependencies. Nonowned metadata inference, multiscale/materializer helpers,
descriptors, callbacks, slicing/refresh algorithms and accepted native engines
remain dependencies. Controlled failures expose their named constructor boundary
only. These comparisons do not close full Viewer workflows, cold circular-import
startup, arbitrary plugin MI, private frame reflection or platform/GC gates.
"""

from __future__ import annotations

import ast
import builtins
import copy
import gc
import hashlib
import importlib
import inspect
import re
import weakref
from pathlib import Path
from types import SimpleNamespace

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
PINNED = ROOT / ".upstream/napari/src/napari"
CURRENT = ROOT / "src/napari"
BEFORE = ROOT / ".artifacts/layer-construction-before/files/src/napari"
HASHES = {
    "layers/base/base.py": (
        "2c9e08f2cf0508fdf2399abf49eea42ecb545adb81ac4c8422b57103b57eefa4"
    ),
    "layers/_scalar_field/scalar_field.py": (
        "50d0f2c6adb4412589394dde4a55a8b156799b89ed55610a57496ef6d0d2a0b4"
    ),
    "layers/image/image.py": (
        "73027b2f096445c6aea88689bbf29f8707a75847d1348fa3b80eaf867c728c56"
    ),
    "layers/intensity_mixin.py": (
        "9d3a6aa09b010a133ccd63f237692f79e4013bf7b69f2a10b5d590237240e034"
    ),
    "utils/key_bindings.py": (
        "3814c21fc36758400a74234bfc4540667837cb25b519b3d1c9ac8d8ceac7a356"
    ),
    "utils/mouse_bindings.py": (
        "47ad9398fed6ef75556d189bb5174cf3b11ed48514b7bb59efd162aa573e5cbb"
    ),
}
BEFORE_HASHES = {
    "layers/base/base.py": (
        "8e03256757f2a2de25c0594d6a87d69458108d8707d7447eb5010b428d16da9f"
    ),
    "layers/_scalar_field/scalar_field.py": (
        "a02dbbaa2875cd1b25cc92256693856cea40b6705b097ca81cf3084b2017253a"
    ),
    "layers/image/image.py": (
        "d0193fb4e0dd5e941d2765a28acc26acc779aa716c7ce350a6eb6828c5b08a0d"
    ),
    "layers/intensity_mixin.py": (
        "ddaa050b441a6df571bb5674e0fa96738705297406dd43a80293149cfb300a0e"
    ),
    "utils/key_bindings.py": HASHES["utils/key_bindings.py"],
    "utils/mouse_bindings.py": HASHES["utils/mouse_bindings.py"],
}
OWNED = {
    ("utils/key_bindings.py", "KeymapProvider"): ("__init__",),
    ("utils/mouse_bindings.py", "MousemapProvider"): ("__init__",),
    ("layers/base/base.py", "PostInit"): ("__init__", "__call__"),
    ("layers/base/base.py", "_LayerSlicingState"): ("__init__",),
    ("layers/base/base.py", "Layer"): ("__init__",),
    ("layers/intensity_mixin.py", "IntensityVisualizationMixin"): ("__init__",),
    ("layers/_scalar_field/scalar_field.py", "ScalarFieldBase"): (
        "__init__",
        "_post_init",
    ),
    ("layers/_scalar_field/scalar_field.py", "ScalarFieldSlicingState"): ("__init__",),
    ("layers/image/image.py", "Image"): ("__init__", "_get_layer_slicing_state"),
}
HIERARCHY = (
    ("utils/key_bindings.py", ("KeymapProvider",)),
    ("utils/mouse_bindings.py", ("MousemapProvider",)),
    ("layers/base/base.py", ("PostInit", "_LayerSlicingState", "Layer")),
    ("layers/intensity_mixin.py", ("IntensityVisualizationMixin",)),
    (
        "layers/_scalar_field/scalar_field.py",
        ("ScalarFieldBase", "ScalarFieldSlicingState"),
    ),
    ("layers/image/image.py", ("Image", "_ImageSlicingState")),
)
ORIGINAL_SELECTORS = (
    ".upstream/napari/src/napari/layers/image/_tests/test_image.py::test_random_image",
    ".upstream/napari/src/napari/layers/image/_tests/test_image.py::test_rgb_image",
    ".upstream/napari/src/napari/layers/image/_tests/test_image.py::test_error_non_rgb_image",
    ".upstream/napari/src/napari/layers/image/_tests/test_image.py::test_rendering_init",
    ".upstream/napari/src/napari/layers/image/_tests/test_image.py::test_instantiate_with_plane_parameter_dict",
    ".upstream/napari/src/napari/layers/image/_tests/test_image.py::test_2d_image_with_channels_and_2d_scale_translate_then_scale_translate_padded",
    ".upstream/napari/src/napari/layers/base/_tests/test_base.py::test_units_constructor",
    ".upstream/napari/src/napari/layers/base/_tests/test_base.py::test_axis_labels_constructor",
    ".upstream/napari/src/napari/layers/base/_tests/test_base.py::test_layer_locked_constructor",
    ".upstream/napari/src/napari/layers/image/_tests/test_multiscale.py::test_infer_multiscale",
    ".upstream/napari/src/napari/layers/image/_tests/test_multiscale.py::test_infer_tuple_multiscale",
    ".upstream/napari/src/napari/components/_tests/test_layer_slicer.py::test_submit_with_one_3d_image",
    ".upstream/napari/src/napari/_qt/_tests/test_async_slicing.py::test_async_slice_image_on_current_step_change",
    ".upstream/napari/src/napari/_qt/_tests/test_async_slicing.py::test_async_slice_image_loaded",
    ".upstream/napari/src/napari/_qt/_tests/test_async_slicing.py::test_async_slice_multiscale_image_on_zoom",
)


def _source(root, relative):
    raw = (root / relative).read_bytes()
    hashes = HASHES if root == PINNED else BEFORE_HASHES if root == BEFORE else None
    if hashes is not None:
        assert hashlib.sha256(raw).hexdigest() == hashes[relative]
    return raw.decode("utf-8")


def _class(source, name):
    return next(
        n
        for n in ast.parse(source).body
        if isinstance(n, ast.ClassDef) and n.name == name
    )


def _method(source, owner, name):
    return next(
        n
        for n in _class(source, owner).body
        if isinstance(n, ast.FunctionDef) and n.name == name
    )


def _execute(source, node, namespace, filename):
    future = (
        [
            ast.ImportFrom(
                module="__future__", names=[ast.alias(name="annotations")], level=0
            )
        ]
        if "from __future__ import annotations" in source
        else []
    )
    tree = ast.fix_missing_locations(ast.Module(body=future + [node], type_ignores=[]))
    exec(compile(tree, filename, "exec", dont_inherit=True), namespace)


def _deferred_super_candidate(method):
    """Test-only reversible change of the original direct super call primitive."""
    original = copy.deepcopy(method)
    providers = []
    replacements = []
    for call in ast.walk(method):
        if (
            isinstance(call, ast.Call)
            and isinstance(call.func, ast.Attribute)
            and isinstance(call.func.value, ast.Call)
            and isinstance(call.func.value.func, ast.Name)
            and call.func.value.func.id == "super"
            and not call.func.value.args
            and not call.func.value.keywords
        ):
            name = f"_constructor_super_provider_{len(providers)}"
            attribute = call.func.attr
            provider = ast.parse(f"{name} = lambda self: super().{attribute}").body[0]
            providers.append(provider)
            replacements.append((call, copy.deepcopy(call.func)))
            call.func = ast.Call(
                func=ast.Name(id=name, ctx=ast.Load()),
                args=[ast.Name(id="self", ctx=ast.Load())],
                keywords=[],
            )
    insertion = int(
        bool(
            method.body
            and isinstance(method.body[0], ast.Expr)
            and isinstance(method.body[0].value, ast.Constant)
            and isinstance(method.body[0].value.value, str)
        )
    )
    method.body[insertion:insertion] = providers
    reverse = copy.deepcopy(method)
    del reverse.body[insertion : insertion + len(providers)]
    reverse_calls = [
        c
        for c in ast.walk(reverse)
        if isinstance(c, ast.Call)
        and isinstance(c.func, ast.Call)
        and isinstance(c.func.func, ast.Name)
        and c.func.func.id.startswith("_constructor_super_provider_")
    ]
    assert len(reverse_calls) == len(replacements)
    for call, (_, previous) in zip(reverse_calls, replacements, strict=True):
        call.func = previous
    assert ast.dump(reverse) == ast.dump(original)
    return len(providers)


@pytest.fixture(scope="module")
def hierarchies():
    from naparixx import _native

    changes = sum(
        ast.dump(_method(_source(CURRENT, relative), owner, name))
        != ast.dump(_method(_source(BEFORE, relative), owner, name))
        for (relative, owner), names in OWNED.items()
        for name in names
    )
    assert changes in (0, 12), (
        "The cohort must be wholly before or wholly after the port"
    )

    def build(root):
        classes, namespaces, provider_count = {}, {}, 0
        for relative, owners in HIERARCHY:
            module = importlib.import_module(
                "napari." + relative[:-3].replace("/", ".")
            )
            namespace = dict(vars(module))
            namespace.update(classes)
            namespace["_native"] = _native
            source = _source(root, relative)
            for owner in owners:
                node = copy.deepcopy(_class(source, owner))
                if root == CURRENT and not changes:
                    for method in node.body:
                        if isinstance(
                            method, ast.FunctionDef
                        ) and method.name in OWNED.get((relative, owner), ()):
                            provider_count += _deferred_super_candidate(method)
                _execute(source, node, namespace, str(root / relative))
                classes[owner] = namespace[owner]
                for previous in namespaces.values():
                    previous[owner] = namespace[owner]
            namespaces[relative] = namespace
        if root == CURRENT:
            assert issubclass(classes["Layer"], _native.LayerNamespaceBase)
            assert issubclass(classes["_LayerSlicingState"], _native.LayerNamespaceBase)
            assert provider_count == (9 if not changes else 0)
        return SimpleNamespace(
            classes=classes,
            namespaces=namespaces,
            root=root,
            native=_native,
            changes=changes,
        )

    return build


def _value(value):
    if isinstance(value, np.ndarray):
        return value.shape, str(value.dtype), _value(value.tolist())
    if isinstance(value, np.generic):
        return _value(value.item())
    if isinstance(value, (tuple, list)):
        return tuple(_value(v) for v in value)
    if isinstance(value, dict):
        return tuple((k, _value(v)) for k, v in value.items())
    return value


def _error(callback):
    try:
        callback()
    except Exception as error:
        return type(error).__name__, re.sub(r"0x[0-9a-fA-F]+", "0xADDRESS", str(error))
    return None


def _compare(hierarchies, scenario):
    expected = scenario(hierarchies(PINNED))
    observed = scenario(hierarchies(CURRENT))
    assert observed == expected
    return observed


def _image_snapshot(image):
    state = image._slicing_state
    return (
        image.name,
        image.ndim,
        image.rgb,
        image.multiscale,
        _value(image.level_shapes),
        _value(image.scale),
        _value(image.translate),
        tuple(str(unit) for unit in image.units),
        tuple(image.axis_labels),
        _value(image.contrast_limits),
        _value(image.contrast_limits_range),
        image.gamma,
        image.opacity,
        str(image.rendering),
        str(image.depiction),
        str(image.interpolation2d),
        str(image.interpolation3d),
        image.iso_threshold,
        image.locked_data_level,
        image.auto_contrast,
        _value(image.corner_pixels),
        type(state).__name__,
        state.loaded,
        state._last_slice_id,
        state._slice_input.ndisplay,
        tuple(state._slice_input.order),
        _value(state.transforms.affine_matrix),
        image.thumbnail.shape,
        tuple(image._overlays),
        len(image.events.emitters),
        bool(image.keymap),
        tuple(
            len(getattr(image, name))
            for name in (
                "mouse_move_callbacks",
                "mouse_drag_callbacks",
                "mouse_wheel_callbacks",
                "mouse_double_click_callbacks",
            )
        ),
    )


def _image_input(case):
    data = np.arange(48, dtype=np.float32).reshape(6, 8)
    kwargs = {"name": "constructor"}
    if case == "rgb_explicit":
        data = np.arange(144, dtype=np.uint8).reshape(6, 8, 3)
        kwargs["rgb"] = True
    elif case == "rgb_inferred":
        data = np.zeros((8, 9, 4), dtype=np.float32)
    elif case == "transforms_units":
        kwargs.update(
            scale=(2, 3),
            translate=(4, 5),
            units=("nm", "um"),
            axis_labels=("vertical", "horizontal"),
        )
    elif case == "plane":
        data = np.zeros((4, 6, 8), dtype=np.float32)
        kwargs.update(rgb=False, depiction="plane", plane={"thickness": 2})
    elif case == "explicit_contrast":
        kwargs.update(
            contrast_limits=(2, 40),
            gamma=1.7,
            iso_threshold=7,
            opacity=0.4,
            auto_contrast=True,
        )
    elif case in ("multiscale_list", "multiscale_tuple", "multiscale_explicit"):
        data = [
            np.arange(64, dtype=np.uint8).reshape(8, 8),
            np.arange(16, dtype=np.uint8).reshape(4, 4),
        ]
        if case == "multiscale_tuple":
            data = tuple(data)
        if case == "multiscale_explicit":
            kwargs.update(multiscale=True, locked_data_level=1)
    elif case == "interpolation_rendering":
        kwargs.update(
            interpolation2d="linear",
            interpolation3d="nearest",
            rendering="iso",
            attenuation=0.2,
            cache=False,
        )
    return data, kwargs


@pytest.mark.parametrize(
    "case",
    (
        "default",
        "rgb_explicit",
        "rgb_inferred",
        "transforms_units",
        "plane",
        "explicit_contrast",
        "multiscale_list",
        "multiscale_tuple",
        "multiscale_explicit",
        "interpolation_rendering",
    ),
)
def test_actual_image_constructor_and_first_refresh(hierarchies, case):
    def scenario(impl):
        data, kwargs = _image_input(case)
        image = impl.classes["Image"](data, **kwargs)
        result = _image_snapshot(image)
        assert image._slicing_state.layer is image
        assert vars(image) is image.__dict__
        assert vars(image._slicing_state) is image._slicing_state.__dict__
        return result

    result = _compare(hierarchies, scenario)
    assert result[0] == "constructor" and result[21] == "_ImageSlicingState"


def _partial(instance):
    names = (
        "rgb",
        "_data_raw",
        "_data",
        "_highlight_visible",
        "_ndim",
        "_transforms",
        "_thumbnail",
        "events",
        "_slicing_state",
        "_plane",
        "_gamma",
        "_colormap",
        "_interpolation2d",
        "_interpolation3d",
    )
    return tuple((name, name in vars(instance)) for name in names)


@pytest.mark.parametrize(
    "case",
    (
        "zero_scale",
        "non_rgb",
        "one_dimension",
        "interpolation",
        "rendering",
        "units",
    ),
)
def test_constructor_errors_leave_source_ordered_partial_state(hierarchies, case):
    def scenario(impl):
        retained = []

        class Observed(impl.classes["Image"]):
            def __new__(cls, *args, **kwargs):
                instance = super().__new__(cls)
                retained.append(instance)
                return instance

        data = np.arange(48, dtype=np.float32).reshape(6, 8)
        kwargs = {"name": "invalid"}
        if case == "zero_scale":
            kwargs["scale"] = (0, 1)
        elif case == "non_rgb":
            kwargs["rgb"] = True
        elif case == "one_dimension":
            data = np.arange(5)
        elif case == "interpolation":
            kwargs["interpolation2d"] = "invalid-interpolation"
        elif case == "rendering":
            kwargs["rendering"] = "invalid-rendering"
        else:
            kwargs["units"] = ("not_a_constructor_unit", "nm")
        error = _error(lambda: Observed(data, **kwargs))
        return error, _partial(retained[0])

    result = _compare(hierarchies, scenario)
    assert result[0] is not None


@pytest.mark.parametrize("stage", ("success", "state_failure", "post_failure"))
def test_dynamic_subclass_state_postinit_and_refresh_order(hierarchies, stage):
    def scenario(impl):
        trace, retained = [], []

        class Observed(impl.classes["Image"]):
            def __init__(self, data, *, name="subclass"):
                retained.append(self)
                trace.append(("init-enter", "_slicing_state" in vars(self)))
                super().__init__(data, name=name)
                trace.append(("init-return", self.gamma, self._slicing_state.loaded))

            def _get_layer_slicing_state(self, data, cache):
                trace.append(("state", self.ndim, "events" in vars(self), cache))
                if stage == "state_failure":
                    raise RuntimeError("state constructor failure")
                return super()._get_layer_slicing_state(data, cache)

            def _post_init(self):
                trace.append(("post", self.gamma, "_slicing_state" in vars(self)))
                if stage == "post_failure":
                    raise RuntimeError("post constructor failure")
                return super()._post_init()

            def refresh(self, *args, **kwargs):
                trace.append(("refresh", self.gamma))
                return super().refresh(*args, **kwargs)

        error = _error(lambda: Observed(np.ones((6, 8), dtype=np.uint8)))
        return tuple(trace), error, _partial(retained[0])

    trace, error, _ = _compare(hierarchies, scenario)
    assert trace[0] == ("init-enter", False)
    assert sum(entry[0] == "post" for entry in trace) == (stage != "state_failure")
    assert (error is None) == (stage == "success")


@pytest.mark.parametrize("fail", (False, True))
def test_key_mouse_provider_cooperative_tail_and_publication(hierarchies, fail):
    def scenario(impl):
        trace, retained = [], []

        class Tail:
            def __init__(self, *args, **kwargs):
                retained.append(self)
                trace.append((args, tuple(kwargs.items()), tuple(vars(self))))
                if fail:
                    raise RuntimeError("cooperative tail")

        bases = (impl.classes["KeymapProvider"], impl.classes["MousemapProvider"], Tail)
        if impl.root == CURRENT:
            bases = (impl.native.LayerNamespaceBase, *bases)
        Provider = type("Provider", bases, {})
        error = _error(lambda: Provider(3, tag="value"))
        instance = retained[0]
        state = tuple(
            (name, type(value).__name__, len(value))
            for name, value in vars(instance).items()
        )
        if not fail:
            callbacks = [
                getattr(instance, name)
                for name in (
                    "mouse_move_callbacks",
                    "mouse_drag_callbacks",
                    "mouse_double_click_callbacks",
                    "mouse_wheel_callbacks",
                )
            ]
            assert len({id(value) for value in callbacks}) == 4
            assert instance.keymap is instance._keymap
        return tuple(trace), error, state

    trace, error, state = _compare(hierarchies, scenario)
    assert trace == (((3,), (("tag", "value"),), ()),)
    assert (error is not None) == fail and bool(state) != fail


LOCAL_IMPORTS = {
    "napari.layers._source": ("current_source",),
    "napari.components.overlays.bounding_box": ("BoundingBoxOverlay",),
    "napari.components.overlays.interaction_box": (
        "SelectionBoxOverlay",
        "TransformBoxOverlay",
    ),
    "napari.components.overlays.text": ("LayerNameOverlay",),
    "napari.components.overlays": ("ColorBarOverlay",),
}


@pytest.mark.parametrize("failure", ("current_source", "ColorBarOverlay"))
def test_local_circular_imports_are_deferred_after_prior_publication(
    hierarchies,
    monkeypatch,
    failure,
):
    def scenario(impl):
        retained, trace = [], []

        class Observed(impl.classes["Image"]):
            def __new__(cls, *args, **kwargs):
                instance = super().__new__(cls)
                retained.append(instance)
                return instance

        original_import = builtins.__import__

        def importing(name, globals=None, locals=None, fromlist=(), level=0):
            if name in LOCAL_IMPORTS and tuple(fromlist) == LOCAL_IMPORTS[name]:
                instance = retained[0]
                trace.append((name, tuple(fromlist), _partial(instance)))
                if failure in fromlist:
                    raise ImportError(f"constructor import {failure}")
            return original_import(name, globals, locals, fromlist, level)

        with monkeypatch.context() as context:
            context.setattr(builtins, "__import__", importing)
            error = _error(lambda: Observed(np.zeros((6, 8)), name="imports"))
        return tuple(trace), error, _partial(retained[0])

    trace, error, _ = _compare(hierarchies, scenario)
    assert trace[-1][1][-1] == failure and error[0] == "ImportError"
    assert len(trace) == (1 if failure == "current_source" else 5)


@pytest.mark.parametrize("fail", (False, True))
def test_xarray_metadata_local_lifetime_releases_on_return_and_failure(
    hierarchies, fail
):
    def scenario(impl):
        trace = []
        scalar_ns = impl.namespaces["layers/_scalar_field/scalar_field.py"]
        image_ns = impl.namespaces["layers/image/image.py"]
        metadata_helper, map_helper = (
            scalar_ns["_get_xr_metadata"],
            image_ns["ensure_colormap"],
        )

        class Metadata:
            def __init__(self, payload):
                self.payload = payload

            def __getattr__(self, name):
                trace.append(("metadata", name))
                return getattr(self.payload, name)

            def __del__(self):
                trace.append(("metadata-released",))

        def metadata(*args, **kwargs):
            return Metadata(metadata_helper(*args, **kwargs))

        def colormap(value):
            trace.append(("after-super-colormap",))
            return map_helper(value)

        scalar_ns["_get_xr_metadata"] = metadata
        image_ns["ensure_colormap"] = colormap
        error = _error(
            lambda: impl.classes["Image"](
                np.ones((6, 8)), name="locals", scale=(0, 1) if fail else None
            )
        )
        return tuple(trace), error

    trace, error = _compare(hierarchies, scenario)
    assert sum(entry[0] == "metadata-released" for entry in trace) == 1
    if not fail:
        assert trace.index(("metadata-released",)) < trace.index(
            ("after-super-colormap",)
        )
    assert (error is not None) == fail


def test_scalar_slice_dtype_callback_failure_precedes_state_publication(hierarchies):
    def scenario(impl):
        retained, trace = [], []

        class Observed(impl.classes["Image"]):
            def __new__(cls, *args, **kwargs):
                instance = super().__new__(cls)
                retained.append(instance)
                return instance

            def _slice_dtype(self):
                trace.append(
                    (self.ndim, "_slicing_state" in vars(self), "events" in vars(self))
                )
                raise RuntimeError("slice dtype construction")

        error = _error(lambda: Observed(np.ones((6, 8)), name="dtype"))
        return tuple(trace), error, _partial(retained[0])

    trace, error, partial = _compare(hierarchies, scenario)
    assert trace == ((2, False, True),)
    assert error[0] == "RuntimeError" and ("_slicing_state", False) in partial


def test_image_slicing_factory_uses_fresh_global_and_original_arguments(hierarchies):
    def scenario(impl):
        namespace = impl.namespaces["layers/image/image.py"]
        receiver, payload, result = object(), object(), object()
        trace = []

        def factory(layer, data, cache):
            trace.append((layer is receiver, data is payload, cache))
            return result

        namespace["_ImageSlicingState"] = factory
        returned = impl.classes["Image"]._get_layer_slicing_state(
            receiver, payload, False
        )
        return tuple(trace), returned is result

    assert _compare(hierarchies, scenario) == (((True, True, False),), True)


def test_actual_constructor_namespace_alias_replacement_and_descriptor(hierarchies):
    def scenario(impl):
        image = impl.classes["Image"](np.arange(48).reshape(6, 8), name="aliases")
        state = image._slicing_state
        saved, saved_state = vars(image), vars(state)
        replacement = dict(saved)
        replacement["constructor_extra"] = "live"
        image.__dict__ = replacement
        replacement["_gamma"] = 2.5
        state.__dict__ = dict(saved_state)
        vars(state)["constructor_extra"] = "state-live"
        saved["constructor_extra"] = "detached"
        image.name = "descriptor"
        result = (
            vars(image) is replacement,
            image.gamma,
            image.name,
            image.constructor_extra,
            saved["constructor_extra"],
            state.constructor_extra,
            vars(state) is saved_state,
            state.layer is image,
            weakref.ref(image)() is image,
        )
        if impl.root == CURRENT:
            assert isinstance(image, impl.native.LayerNamespaceBase)
            assert isinstance(state, impl.native.LayerNamespaceBase)
        return result

    assert _compare(hierarchies, scenario) == (
        True,
        2.5,
        "descriptor",
        "live",
        "detached",
        "state-live",
        False,
        True,
        True,
    )


def test_subclass_descriptor_observes_constructor_assignment_order(hierarchies):
    def scenario(impl):
        trace = []

        class Observed(impl.classes["Image"]):
            def __setattr__(self, name, value):
                if name in (
                    "rgb",
                    "_data_raw",
                    "_data",
                    "_ndim",
                    "_slicing_state",
                    "_gamma",
                    "_colormap",
                    "_iso_threshold",
                ):
                    trace.append((name, "events" in vars(self)))
                return super().__setattr__(name, value)

        image = Observed(np.ones((6, 8)), name="assignment")
        return tuple(trace), image.gamma

    trace, gamma = _compare(hierarchies, scenario)
    assert trace[0] == ("rgb", False) and gamma == 1.0
    assert trace.index(("_slicing_state", True)) < trace.index(("_colormap", True))


def test_constructor_and_bound_callback_cycle_can_be_collected(hierarchies):
    def scenario(impl):
        image = impl.classes["Image"](np.ones((6, 8)), name="gc")
        state = image._slicing_state
        image.constructor_callback = image.refresh
        image.constructor_cycle = image
        layer_ref, state_ref = weakref.ref(image), weakref.ref(state)
        del image, state
        gc.collect()
        return layer_ref() is None, state_ref() is None

    assert _compare(hierarchies, scenario) == (True, True)


def test_postinit_signature_abstractness_and_cooperative_class_cell(hierarchies):
    def scenario(impl):
        image_class = impl.classes["Image"]

        class Observed(image_class):
            def __init__(self, data, *, constructor_flag=7):
                self.constructor_flag = constructor_flag
                super().__init__(data, name="signature")

        image = Observed(np.ones((6, 8)), constructor_flag=9)
        return (
            str(inspect.signature(Observed)),
            image.constructor_flag,
            bool(impl.classes["Layer"].__abstractmethods__),
            _error(lambda: impl.classes["Layer"](np.ones((6, 8)), 2)),
            tuple(
                c.__name__
                for c in Observed.__mro__
                if c.__name__ != "LayerNamespaceBase"
            ),
        )

    signature, flag, abstract, error, _ = _compare(hierarchies, scenario)
    assert signature == "(data, *, constructor_flag=7)" and flag == 9
    assert abstract and error[0] == "TypeError"


def test_twelve_owned_methods_keep_independent_source_metadata(hierarchies):
    old, new = hierarchies(PINNED), hierarchies(CURRENT)
    count = 0
    for (_, owner), names in OWNED.items():
        for name in names:
            before = old.classes[owner].__dict__[name]
            after = new.classes[owner].__dict__[name]
            assert before is not after
            assert (
                before.__name__,
                before.__qualname__,
                before.__doc__,
                str(inspect.signature(before)),
            ) == (
                after.__name__,
                after.__qualname__,
                after.__doc__,
                str(inspect.signature(after)),
            )
            count += 1
    assert count == 12 and old.classes["Image"] is not new.classes["Image"]


@pytest.mark.parametrize("fail", (False, True))
def test_scalar_constructor_materializes_actual_generator_in_source_order(
    hierarchies, fail
):
    def scenario(impl):
        trace, retained = [], []

        def levels():
            trace.append("first")
            yield np.zeros((8, 8), dtype=np.uint8)
            trace.append("second")
            if fail:
                raise RuntimeError("construction levels")
            yield np.zeros((4, 4), dtype=np.uint8)
            trace.append("done")

        class ScalarImage(impl.classes["Image"]):
            def __init__(self, data):
                retained.append(self)
                self.rgb = False
                impl.classes["ScalarFieldBase"].__init__(
                    self, data, name="generator", ndim=2
                )

            def _post_init(self):
                pass

        error = _error(lambda: ScalarImage(levels()))
        image = retained[0]
        state = (
            "_data_raw" in vars(image),
            _value(image.level_shapes) if error is None else None,
        )
        return tuple(trace), error, state

    trace, error, state = _compare(hierarchies, scenario)
    assert trace == (("first", "second") if fail else ("first", "second", "done"))
    assert (error is not None) == fail and state[0] != fail


def test_intensity_empty_contrast_constant_aliases_across_constructions(hierarchies):
    def scenario(impl):
        namespace = impl.namespaces["layers/intensity_mixin.py"]

        class Tail:
            def __init__(self):
                self.events = namespace["EmitterGroup"](source=self)
                self._overlays = {}

        # EmitterGroup is imported by base, rather than the defining mixin module.
        namespace["EmitterGroup"] = impl.namespaces["layers/base/base.py"][
            "EmitterGroup"
        ]
        Mixed = type("Mixed", (impl.classes["IntensityVisualizationMixin"], Tail), {})
        first, second = Mixed(), Mixed()
        return (
            first._contrast_limits,
            first._contrast_limits_range,
            first._contrast_limits is first._contrast_limits_range,
            first._contrast_limits is second._contrast_limits,
            first._auto_contrast_source,
            tuple(first._overlays),
        )

    assert _compare(hierarchies, scenario) == (
        (None, None),
        (None, None),
        True,
        True,
        "slice",
        ("colorbar",),
    )
