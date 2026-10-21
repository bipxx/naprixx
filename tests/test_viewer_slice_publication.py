"""Finite slice-publication protocols against unchanged pinned method bodies.

These checks call current facades. They extract exact pinned method source,
retaining its AST body, and share installed module globals/dataclass metadata
and external provider adapters. Image's reference uses an actual Python class
with the pinned scalar parent so zero-argument super() remains real. No Qt
application, Viewer, worker, canvas or renderer is constructed here; original
async GUI fixtures provide that separate acceptance evidence.
"""

import ast
import gc
import hashlib
import importlib
import inspect
import weakref
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / ".upstream/napari/src/napari"
PINNED = {
    "_qt/qt_viewer.py":
        "ade606601e6a349d8903b132ba887a352663a2b0e4929dc7acfcc85615e27d5a",
    "layers/_scalar_field/_slice.py":
        "0e327552ef2d11eaa198f84e86cac2ddbdefd5fa2b10947c42c45956b1c61188",
    "layers/_scalar_field/scalar_field.py":
        "50d0f2c6adb4412589394dde4a55a8b156799b89ed55610a57496ef6d0d2a0b4",
    "layers/image/image.py":
        "73027b2f096445c6aea88689bbf29f8707a75847d1348fa3b80eaf867c728c56",
    "layers/base/base.py":
        "2c9e08f2cf0508fdf2399abf49eea42ecb545adb81ac4c8422b57103b57eefa4",
}


def _method_text(relative, owner, name):
    raw = (UPSTREAM / relative).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == PINNED[relative]
    source = raw.decode("utf-8")
    cls = next(
        node for node in ast.parse(source).body
        if isinstance(node, ast.ClassDef) and node.name == owner
    )
    original = next(
        node for node in cls.body
        if isinstance(node, ast.FunctionDef) and node.name == name
    )
    # The def line excludes decorators; its executable body is unchanged.
    text = "".join(source.splitlines(keepends=True)[
        original.lineno - 1:original.end_lineno
    ])
    # Keep class indentation as well: dedent would alter multiline docstrings.
    extracted = ast.parse("class PinnedBodyOwner:\n" + text).body[0].body[0]
    assert [ast.dump(node) for node in extracted.body] == [
        ast.dump(node) for node in original.body
    ]
    return text


def _method(relative, owner, name, namespace):
    text = _method_text(relative, owner, name)
    exec(compile("from __future__ import annotations\n"
                 "class PinnedMethodOwner:\n" + text,
                 str(UPSTREAM / relative), "exec"), namespace)
    return vars(namespace["PinnedMethodOwner"])[name]


@pytest.fixture(scope="module")
def implementations():
    qt = importlib.import_module("napari._qt.qt_viewer")
    slice_module = importlib.import_module("napari.layers._scalar_field._slice")
    scalar_module = importlib.import_module(
        "napari.layers._scalar_field.scalar_field"
    )
    image_module = importlib.import_module("napari.layers.image.image")
    base = importlib.import_module("napari.layers.base.base")
    namespaces = {
        "qt": dict(vars(qt)), "slice": dict(vars(slice_module)),
        "scalar": dict(vars(scalar_module)), "image": dict(vars(image_module)),
        "base": dict(vars(base)),
    }
    view = type("PinnedView", (slice_module._ScalarFieldView,), {
        "from_raw": classmethod(_method(
            "layers/_scalar_field/_slice.py", "_ScalarFieldView", "from_raw",
            namespaces["slice"],
        )),
    })
    response = type("PinnedResponse", (slice_module._ScalarFieldSliceResponse,), {
        "to_displayed": _method(
            "layers/_scalar_field/_slice.py", "_ScalarFieldSliceResponse",
            "to_displayed", namespaces["slice"],
        ),
    })
    namespaces["slice"]["_ScalarFieldView"] = view
    namespaces["slice"]["_ScalarFieldSliceResponse"] = response
    scalar = _method(
        "layers/_scalar_field/scalar_field.py", "ScalarFieldSlicingState",
        "_update_slice_response", namespaces["scalar"],
    )
    namespaces["image"]["_PinnedScalarState"] = type(
        "PinnedScalarState", (), {"_update_slice_response": scalar}
    )
    image_text = _method_text(
        "layers/image/image.py", "_ImageSlicingState", "_update_slice_response"
    )
    # Compile the unmodified method inside a real class to create __class__.
    exec(compile(
        "from __future__ import annotations\n"
        "class PinnedImageState(_PinnedScalarState):\n"
        + image_text,
        str(UPSTREAM / "layers/image/image.py"), "exec",
    ), namespaces["image"])
    pinned = SimpleNamespace(
        view=view, response=response, scalar=scalar,
        image=namespaces["image"]["PinnedImageState"],
        viewer=_method("_qt/qt_viewer.py", "QtViewer", "_on_slice_ready",
                       namespaces["qt"]),
        begin=_method("layers/base/base.py", "_LayerSlicingState",
                      "_set_unloaded_slice_id", namespaces["base"]),
        finish=_method("layers/base/base.py", "_LayerSlicingState",
                       "_update_loaded_slice_id", namespaces["base"]),
        refresh=_method("layers/base/base.py", "Layer", "_refresh_sync",
                        namespaces["base"]),
        image_globals=namespaces["image"],
    )
    current = SimpleNamespace(
        view=slice_module._ScalarFieldView,
        response=slice_module._ScalarFieldSliceResponse,
        scalar=scalar_module.ScalarFieldSlicingState._update_slice_response,
        image=image_module._ImageSlicingState,
        viewer=inspect.unwrap(qt.QtViewer._on_slice_ready),
        begin=base._LayerSlicingState._set_unloaded_slice_id,
        finish=base._LayerSlicingState._update_loaded_slice_id,
        refresh=base.Layer._refresh_sync,
        image_globals=vars(image_module),
    )
    return pinned, current


def _compare(implementations, scenario, contract):
    pinned, current = implementations
    expected = scenario(pinned)
    assert expected == contract
    assert scenario(current) == expected


class Marker:
    pass


def test_viewer_delivery_uses_live_layer_truth_and_replaced_slicing_state(
    implementations,
):
    def scenario(impl):
        trace = []

        class LayerProtocol:
            def __init__(self, name, truth):
                self.name, self.truth = name, truth
                self._slicing_state = SimpleNamespace(
                    _update_slice_response=self.apply,
                )
                self.events = SimpleNamespace(
                    set_data=lambda: trace.append((name, "set-data"))
                )

            def __bool__(self):
                trace.append((self.name, "bool"))
                return self.truth

            def apply(self, response):
                trace.append((self.name, "apply", response.request_id))
                self._slicing_state = SimpleNamespace(
                    _update_loaded_slice_id=lambda value:
                        trace.append((self.name, "loaded", value)),
                )

            def _refresh_sync(self, **kwargs):
                trace.append((self.name, "refresh", tuple(kwargs.items())))

        dead = LayerProtocol("dead", True)
        dead_ref = weakref.ref(dead)
        hash(dead_ref)
        del dead
        gc.collect()
        assert dead_ref() is None
        false, live = LayerProtocol("false", False), LayerProtocol("live", True)
        # A stale ID still reaches response application; loaded policy is separate.
        event = SimpleNamespace(value={
            dead_ref: SimpleNamespace(request_id=1),
            weakref.ref(false): SimpleNamespace(request_id=2),
            weakref.ref(live): SimpleNamespace(request_id=7),
        })
        assert impl.viewer(object(), event) is None
        return tuple(trace)

    _compare(implementations, scenario, (
        ("false", "bool"), ("live", "bool"), ("live", "apply", 7),
        ("live", "loaded", 7), ("live", "set-data"),
        ("live", "refresh", (("data_displayed", False), ("thumbnail", True),
                             ("highlight", True), ("extent", False))),
    ))


@pytest.mark.parametrize("failure", ["dict-mutation", "loaded-error"])
def test_viewer_live_iteration_and_callback_failure_leave_prior_order_visible(
    implementations, failure,
):
    def scenario(impl):
        trace, responses = [], {}

        class Response:
            @property
            def request_id(self):
                trace.append("request-id")
                return 9

        response = Response()
        response_ref = weakref.ref(response)

        def apply(value):
            trace.append("apply")
            assert value is response_ref()
            if failure == "dict-mutation":
                responses.clear()

        def loaded(value):
            trace.append(("loaded", value))
            if failure == "loaded-error":
                raise RuntimeError("loaded callback failed")

        layer = SimpleNamespace(
            _slicing_state=SimpleNamespace(
                _update_slice_response=apply, _update_loaded_slice_id=loaded,
            ),
            events=SimpleNamespace(set_data=lambda: trace.append("set-data")),
            _refresh_sync=lambda **kwargs: trace.append("refresh"),
        )
        responses[lambda: layer] = response
        del response
        try:
            impl.viewer(object(), SimpleNamespace(value=responses))
        except RuntimeError as error:
            message = str(error)
        else:
            raise AssertionError("Expected callback/iteration error")
        # The removed response survives through its request-id read, then drops.
        return tuple(trace), message, (
            response_ref() is None if failure == "dict-mutation" else True
        )

    trace = ("apply", "request-id", ("loaded", 9))
    if failure == "dict-mutation":
        contract = (
            trace + ("set-data", "refresh"),
            "dictionary changed size during iteration", True,
        )
    else:
        contract = trace, "loaded callback failed", True
    _compare(implementations, scenario, contract)


def test_viewer_temporary_layer_pin_ends_with_call_without_persistent_roots(
    implementations,
):
    def scenario(impl):
        trace = []

        class LayerProtocol:
            def __init__(self):
                self._slicing_state = SimpleNamespace(
                    _update_slice_response=lambda response: trace.append("apply"),
                    _update_loaded_slice_id=lambda value: trace.append("loaded"),
                )
                self.events = SimpleNamespace(
                    set_data=lambda: trace.append("set-data")
                )

            def _refresh_sync(self, **kwargs):
                trace.append("refresh")

            def __del__(self):
                trace.append("layer-released")

        holders = {"layer": LayerProtocol()}
        reference = weakref.ref(holders["layer"])

        class WeakProvider:
            def __call__(self):
                return holders.pop("layer")

        weak_provider = WeakProvider()
        provider_ref = weakref.ref(weak_provider)
        event = SimpleNamespace(value={weak_provider: SimpleNamespace(request_id=3)})
        impl.viewer(object(), event)
        assert reference() is None
        del event, weak_provider
        gc.collect()
        return tuple(trace), provider_ref() is None

    _compare(implementations, scenario, (
        ("apply", "loaded", "set-data", "refresh", "layer-released"), True,
    ))


@pytest.mark.parametrize("raises", [False, True])
def test_scalar_view_from_raw_converts_once_before_subclass_factory(
    implementations, raises,
):
    def scenario(impl):
        trace, raw, converted = [], Marker(), Marker()

        class View(impl.view):
            def __init__(self, *, raw, view):
                trace.append("factory")
                super().__init__(raw=raw, view=view)

        def converter(value):
            trace.append(("convert", value is raw))
            if raises:
                raise RuntimeError("conversion failed")
            return converted

        try:
            result = View.from_raw(raw=raw, converter=converter)
        except RuntimeError as error:
            return tuple(trace), str(error)
        return tuple(trace), (type(result) is View, result.raw is raw,
                              result.view is converted)

    contract = ((("convert", True),), "conversion failed") if raises else (
        (("convert", True), "factory"), (True, True, True),
    )
    _compare(implementations, scenario, contract)


def _response(impl, *, shared=False):
    raw, other, tile, plan = Marker(), Marker(), Marker(), Marker()
    image = impl.view(raw=raw, view=raw)
    thumbnail = image if shared else impl.view(raw=other, view=other)
    response = impl.response(image=image, thumbnail=thumbnail, tile_to_data=tile,
                             slice_input=plan, request_id=17, empty=False)
    return response, raw, other, tile, plan


@pytest.mark.parametrize("shared", [False, True])
def test_slice_to_displayed_preserves_aliases_and_exact_provider_payloads(
    implementations, shared,
):
    def scenario(impl):
        source, raw, other, tile, plan = _response(impl, shared=shared)
        trace, displays = [], []

        def convert(value):
            trace.append("image" if value is raw else "thumbnail")
            assert value is raw or value is other
            display = Marker()
            displays.append(display)
            return display

        result = source.to_displayed(convert)
        return (tuple(trace), result is not source, result.image.raw is raw,
                result.image.view is displays[0],
                result.thumbnail is result.image if shared else
                result.thumbnail.raw is other and result.thumbnail.view is displays[1],
                result.tile_to_data is tile, result.slice_input is plan,
                result.request_id, result.empty)

    _compare(implementations, scenario, (
        ("image",) if shared else ("image", "thumbnail"),
        True, True, True, True, True, True, 17, False,
    ))


def test_slice_conversion_reentry_reads_mutated_thumbnail_and_metadata_fresh(
    implementations,
):
    def scenario(impl):
        source, raw, _, _, _ = _response(impl, shared=True)
        other, tile, plan = Marker(), Marker(), Marker()
        trace = []

        def convert(value):
            trace.append("image" if value is raw else "new-thumbnail")
            if value is raw:
                object.__setattr__(
                    source, "thumbnail", impl.view(raw=other, view=other),
                )
                for name, replacement in (
                    ("tile_to_data", tile), ("slice_input", plan),
                    ("request_id", 99), ("empty", True),
                ):
                    object.__setattr__(source, name, replacement)
            return value

        result = source.to_displayed(convert)
        return (tuple(trace), result.thumbnail is not result.image,
                result.thumbnail.raw is other, result.tile_to_data is tile,
                result.slice_input is plan, result.request_id, result.empty)

    _compare(implementations, scenario, (
        ("image", "new-thumbnail"), True, True, True, True, 99, True,
    ))


def test_second_slice_conversion_failure_releases_partial_converted_payload(
    implementations,
):
    def scenario(impl):
        source, raw, _, _, _ = _response(impl)
        trace, references = [], []

        def convert(value):
            trace.append("image" if value is raw else "thumbnail")
            if value is not raw:
                raise RuntimeError("thumbnail conversion failed")
            display = Marker()
            references.append(weakref.ref(display))
            return display

        try:
            source.to_displayed(convert)
        except RuntimeError as error:
            message = str(error)
        else:
            raise AssertionError("Expected thumbnail conversion failure")
        return tuple(trace), message, references[0]() is None

    _compare(implementations, scenario, (
        ("image", "thumbnail"), "thumbnail conversion failed", True,
    ))


class RecordingState:
    def __setattr__(self, name, value):
        if getattr(self, "recording", False) and name in {
            "_slice_input", "transforms", "_slice",
        }:
            self.trace.append(name)
            if self.failure == name:
                raise RuntimeError(name + " failed")
        object.__setattr__(self, name, value)


class TransformProtocol(list):
    def __init__(self, trace, failure=None):
        super().__init__([Marker()])
        self.trace, self.failure = trace, failure

    def __setitem__(self, key, value):
        self.trace.append("transform")
        assert key == 0
        if self.failure == "transform":
            raise RuntimeError("transform failed")
        super().__setitem__(key, value)


def _scalar_scenario(impl, failure=None, reenter=False):
    state = RecordingState()
    state.trace, state.failure = [], failure
    old_layer = SimpleNamespace(_raw_to_displayed=Marker(),
                                _transforms=TransformProtocol(state.trace, failure))
    new_layer = SimpleNamespace(_transforms=TransformProtocol(state.trace, failure))
    state.layer = old_layer
    state._slice_input, state.transforms, state._slice = Marker(), Marker(), Marker()
    converted = SimpleNamespace(slice_input=Marker(), tile_to_data=Marker())
    target = weakref.ref(state)

    class OldSlice:
        def __del__(self):
            current = target()
            if current is not None:
                current.trace.append(("old-slice-released",
                                      current._slice is converted,
                                      current.transforms is converted.tile_to_data))

    if reenter:
        state._slice = OldSlice()

    class Response:
        def to_displayed(self, converter):
            state.trace.append("convert")
            assert converter is old_layer._raw_to_displayed
            if failure == "convert":
                raise RuntimeError("convert failed")
            if reenter:
                state.layer = new_layer
            return converted

    state.recording = True
    try:
        result = impl.scalar(state, Response())
    except RuntimeError as error:
        message = str(error)
    else:
        assert result is None
        message = None
    layer = new_layer if reenter else old_layer
    return (tuple(state.trace), message,
            state._slice_input is converted.slice_input,
            layer._transforms[0] is converted.tile_to_data,
            state.transforms is converted.tile_to_data, state._slice is converted,
            old_layer._transforms[0] is not converted.tile_to_data if reenter else True)


def test_scalar_publication_reentry_uses_current_layer_and_finalizer_sees_commit(
    implementations,
):
    _compare(implementations, lambda impl: _scalar_scenario(impl, reenter=True), (
        ("convert", "_slice_input", "transform", "transforms", "_slice",
         ("old-slice-released", True, True)),
        None, True, True, True, True, True,
    ))


@pytest.mark.parametrize("failure", ["convert", "transform", "transforms", "_slice"])
def test_scalar_publication_failure_preserves_exact_completed_prefix(
    implementations, failure,
):
    steps = ("convert", "_slice_input", "transform", "transforms", "_slice")
    index = steps.index(failure)
    _compare(implementations, lambda impl: _scalar_scenario(impl, failure), (
        steps[:index + 1], failure + " failed",
        index > 1, index > 2, index > 3, False, True,
    ))


def _image_scenario(impl, monkeypatch, auto, should, multiscale, failure=None):
    class State(RecordingState, impl.image):
        pass

    state = object.__new__(State)
    state.trace, state.failure = [], None
    state._slice_input, state.transforms, state._slice = Marker(), Marker(), Marker()
    original_raw, displayed_raw = Marker(), Marker()
    converted = SimpleNamespace(slice_input=Marker(), tile_to_data=Marker(),
                                image=SimpleNamespace(raw=displayed_raw))

    class RawProtocol:
        def __getitem__(self, index):
            state.trace.append(("raw-index", index))
            assert index == -1
            return original_raw

    class LayerProtocol:
        auto_contrast = auto
        rgb, dtype = False, "uint8"
        _raw_to_displayed = None

        def __init__(self):
            self.multiscale, self._should_calc_clims = multiscale, should
            self._transforms = TransformProtocol(state.trace)
            self._limits = None

        @property
        def contrast_limits(self):
            return self._limits

        @contrast_limits.setter
        def contrast_limits(self, value):
            state.trace.append(("clims", value))
            self._limits = value

        def reset_contrast_limits_range(self):
            state.trace.append("reset-range")

        def reset_contrast_limits(self):
            state.trace.append("reset")
            if failure == "reset":
                raise RuntimeError("reset failed")

    state.layer = LayerProtocol()

    class Response:
        image = SimpleNamespace(raw=RawProtocol() if multiscale else original_raw)

        def to_displayed(self, converter):
            state.trace.append("convert")
            return converted

    def data_range(data, *, rgb, dtype):
        state.trace.append(("range", data is original_raw, rgb, dtype))
        if failure == "range":
            raise RuntimeError("range failed")
        return (1, 9)

    monkeypatch.setitem(impl.image_globals, "calc_data_range", data_range)
    state.recording = True
    try:
        result = impl.image._update_slice_response(state, Response())
    except RuntimeError as error:
        message = str(error)
    else:
        assert result is None
        message = None
    return (tuple(state.trace), message, state._slice is converted,
            state.layer._should_calc_clims, state.layer.contrast_limits)


@pytest.mark.parametrize(("auto", "should", "multiscale"), [
    (False, False, False), (False, True, False),
    (True, False, False), (True, True, True),
])
def test_image_scalar_parent_original_raw_and_contrast_reset_order(
    implementations, monkeypatch, auto, should, multiscale,
):
    trace = ["convert", "_slice_input", "transform", "transforms", "_slice"]
    if auto:
        if multiscale:
            trace.append(("raw-index", -1))
        trace.extend([("range", True, False, "uint8"), ("clims", (1, 9))])
    if should:
        trace.extend(["reset-range", "reset"])
    elif auto:
        trace.append("reset")
    _compare(implementations, lambda impl: _image_scenario(
        impl, monkeypatch, auto, should, multiscale,
    ), (tuple(trace), None, True, False, (1, 9) if auto else None))


@pytest.mark.parametrize("failure", ["range", "reset"])
def test_image_contrast_failure_preserves_scalar_commit_and_pending_flag(
    implementations, monkeypatch, failure,
):
    trace = ["convert", "_slice_input", "transform", "transforms", "_slice",
             ("range", True, False, "uint8")]
    if failure == "reset":
        trace.extend([("clims", (1, 9)), "reset-range", "reset"])
    _compare(implementations, lambda impl: _image_scenario(
        impl, monkeypatch, True, True, False, failure,
    ), (tuple(trace), failure + " failed", True, True,
        (1, 9) if failure == "reset" else None))


@pytest.mark.parametrize("notifier", ["reenter", "raise"])
def test_loaded_notifier_sees_committed_ids_and_recovers_after_reentry_or_error(
    implementations, notifier,
):
    from napari.layers.base.base import _LayerSlicingState
    from naparixx import native

    def scenario(impl):
        class State(_LayerSlicingState):
            def __init__(self):
                self._load_state = native.LayerLoadState()

            def _set_view_slice(self):
                raise AssertionError("No slicing is requested in this protocol")

        state, trace = State(), []

        def emit():
            trace.append((state._loaded, state._last_slice_id))
            if not state._loaded:
                if notifier == "reenter":
                    impl.begin(state, 42)
                else:
                    raise RuntimeError("notifier failed")

        # Installed Signal descriptor remains an external adapter. Replace its
        # delivery endpoint through a subclass property, not its implementation.
        class NotifyingState(State):
            @property
            def loaded_data(self):
                return SimpleNamespace(emit=emit)

        state = NotifyingState()
        try:
            impl.begin(state, 41)
        except RuntimeError as error:
            message = str(error)
        else:
            message = None
        if notifier == "reenter":
            impl.finish(state, 41)
            assert state._loaded is False
            impl.finish(state, 42)
            impl.finish(state, 42)
        else:
            assert state._loaded is False
            impl.finish(state, 41)
        return tuple(trace), message, state._loaded, state._last_slice_id

    if notifier == "reenter":
        contract = ((False, 41), (True, 42)), None, True, 42
    else:
        contract = ((False, 41), (True, 41)), "notifier failed", True, 41
    _compare(implementations, scenario, contract)


def _refresh_scenario(impl, visible, force, failure=None):
    trace = []

    class Truth:
        def __init__(self, name, value):
            self.name, self.value = name, value

        def __bool__(self):
            trace.append("bool-" + self.name)
            if self.value == "raise":
                raise RuntimeError("force truth must remain untouched")
            return self.value

    def action(name, **kwargs):
        trace.append(name)
        if name == "highlight":
            assert kwargs == {"force": True}
        if failure == name:
            raise RuntimeError(name + " failed")

    layer = SimpleNamespace(
        visible=Truth("visible", visible),
        _clear_extent=lambda: action("extent"),
        events=SimpleNamespace(set_data=lambda: action("data")),
        _update_thumbnail=lambda: action("thumbnail"),
        _set_highlight=lambda **kwargs: action("highlight", **kwargs),
    )
    try:
        result = impl.refresh(
            layer, extent=Truth("extent", True), data_displayed=Truth("data", True),
            thumbnail=Truth("thumbnail", True), highlight=Truth("highlight", True),
            force=Truth("force", force),
        )
    except RuntimeError as error:
        message = str(error)
    else:
        assert result is None
        message = None
    return tuple(trace), message


@pytest.mark.parametrize(("visible", "force"), [
    (True, "raise"), (False, False), (False, True),
])
def test_refresh_preserves_visible_force_short_circuit_and_provider_truth_order(
    implementations, visible, force,
):
    trace = ["bool-visible"]
    if not visible:
        trace.append("bool-force")
    if visible or force:
        trace.extend(["bool-extent", "extent", "bool-data", "data",
                      "bool-thumbnail", "thumbnail", "bool-highlight", "highlight"])
    _compare(implementations, lambda impl: _refresh_scenario(impl, visible, force),
             (tuple(trace), None))


@pytest.mark.parametrize("failure", ["data", "thumbnail"])
def test_refresh_callback_failure_stops_remaining_provider_truth_and_callbacks(
    implementations, failure,
):
    trace = ["bool-visible", "bool-extent", "extent", "bool-data", "data"]
    if failure == "thumbnail":
        trace.extend(["bool-thumbnail", "thumbnail"])
    _compare(
        implementations,
        lambda impl: _refresh_scenario(impl, True, "raise", failure),
        (tuple(trace), failure + " failed"),
    )


def test_viewer_logging_uses_executing_globals(implementations, monkeypatch):
    """A saved callback uses its real globals after module registry replacement."""

    import sys
    from types import ModuleType

    def scenario(impl):
        trace, responses = [], {}

        def debug(message, value):
            trace.append(("debug", message, value is responses))

        def get_logger(name):
            trace.append(("get-logger", name))
            return SimpleNamespace(debug=debug)

        def wrong_logger(name):
            raise AssertionError("Decoy module logger must remain unused")

        decoy = ModuleType("napari._qt.qt_viewer")
        decoy.logging = SimpleNamespace(getLogger=wrong_logger)
        with monkeypatch.context() as patch:
            patch.setitem(impl.viewer.__globals__, "logging",
                          SimpleNamespace(getLogger=get_logger))
            patch.setitem(sys.modules, decoy.__name__, decoy)
            assert impl.viewer(object(), SimpleNamespace(value=responses)) is None
        return tuple(trace)

    _compare(implementations, scenario, (
        ("get-logger", "napari"),
        ("debug", "QtViewer._on_slice_ready: %s", True),
    ))


def test_refresh_logging_uses_executing_globals(implementations, monkeypatch):
    """A saved refresh method uses its real logger after module replacement."""

    import sys
    from types import ModuleType

    def scenario(impl):
        trace, layer = [], SimpleNamespace(visible=False)

        def debug(message, value):
            trace.append(("debug", message, value is layer))

        def wrong_debug(*args):
            raise AssertionError("Decoy module logger must remain unused")

        decoy = ModuleType("napari.layers.base.base")
        decoy.logger = SimpleNamespace(debug=wrong_debug)
        with monkeypatch.context() as patch:
            patch.setitem(impl.refresh.__globals__, "logger",
                          SimpleNamespace(debug=debug))
            patch.setitem(sys.modules, decoy.__name__, decoy)
            assert impl.refresh(layer, force=False) is None
        return tuple(trace)

    _compare(implementations, scenario, (
        ("debug", "Layer._refresh_sync: %s", True),
    ))


def test_scalar_transient_response_release_precedes_publication_callbacks(
    implementations,
):
    """Response reassignment releases its old provider before publishing fields."""

    def scenario(impl):
        state = RecordingState()
        state.trace, state.failure = [], None
        old_input = Marker()
        state._slice_input, state.transforms, state._slice = (
            old_input, Marker(), Marker(),
        )
        displayed = SimpleNamespace(slice_input=Marker(), tile_to_data=Marker())

        class Transforms(list):
            def __init__(self, name):
                super().__init__([Marker()])
                self.name = name

            def __setitem__(self, key, value):
                state.trace.append(self.name)
                assert key == 0
                super().__setitem__(key, value)

        old_transforms = Transforms("old-transform")
        finalizer_transforms = Transforms("finalizer-transform")
        weakref_transforms = Transforms("weakref-transform")
        converter = Marker()
        state.layer = SimpleNamespace(_raw_to_displayed=converter,
                                      _transforms=old_transforms)
        target, references = weakref.ref(state), []

        def response_released(reference):
            current = target()
            assert reference() is None
            current.trace.append(("original-weakref",
                                  current._slice_input is old_input))
            current.layer._transforms = weakref_transforms

        class Response:
            def __init__(self):
                references.append(weakref.ref(self, response_released))

            def to_displayed(self, passed_converter):
                state.trace.append("convert")
                assert passed_converter is converter
                return displayed

            def __del__(self):
                current = target()
                if current is not None:
                    current.trace.append(("original-finalizer",
                                          current._slice_input is old_input))
                    current.layer._transforms = finalizer_transforms

        state.recording = True
        # Keep the transient call outside assert, whose rewrite retains arguments.
        result = impl.scalar.__get__(state, type(state))(Response())
        assert references[0]() is None
        assert result is None
        return (tuple(state.trace), references[0]() is None,
                state._slice_input is displayed.slice_input,
                weakref_transforms[0] is displayed.tile_to_data,
                finalizer_transforms[0] is not displayed.tile_to_data,
                old_transforms[0] is not displayed.tile_to_data,
                state.transforms is displayed.tile_to_data, state._slice is displayed)

    _compare(implementations, scenario, (
        ("convert", ("original-finalizer", True), ("original-weakref", True),
         "_slice_input", "weakref-transform", "transforms", "_slice"),
        True, True, True, True, True, True, True,
    ))


def test_scalar_publication_error_traceback_retains_converted_response(
    implementations,
):
    """A kept publication traceback owns converted locals after original release."""

    def scenario(impl):
        state = RecordingState()
        state.trace, state.failure = [], "_slice_input"
        old_input = Marker()
        state._slice_input, state.transforms, state._slice = (
            old_input, Marker(), Marker(),
        )
        converter = Marker()
        state.layer = SimpleNamespace(_raw_to_displayed=converter,
                                      _transforms=TransformProtocol(state.trace))
        target = weakref.ref(state)
        original_refs, converted_refs, input_refs = [], [], []

        class ConvertedResponse:
            def __init__(self):
                self.slice_input, self.tile_to_data = Marker(), Marker()
                converted_refs.append(weakref.ref(self))
                input_refs.append(weakref.ref(self.slice_input))

        class Response:
            def __init__(self):
                original_refs.append(weakref.ref(self))

            def to_displayed(self, passed_converter):
                state.trace.append("convert")
                assert passed_converter is converter
                return ConvertedResponse()

            def __del__(self):
                current = target()
                if current is not None:
                    current.trace.append(("original-released",
                                          current._slice_input is old_input))

        def invoke():
            try:
                impl.scalar(state, Response())
            except RuntimeError as error:
                return str(error), error.__traceback__
            raise AssertionError("Expected first publication assignment to fail")

        state.recording = True
        message, saved_traceback = invoke()
        # Capture before collection. The response local in the interrupted
        # Python boundary must keep the converted provider alive deliberately.
        retained = (original_refs[0]() is None, converted_refs[0]() is not None,
                    input_refs[0]() is not None, state._slice_input is old_input)
        trace = tuple(state.trace)
        del saved_traceback
        gc.collect()
        released = converted_refs[0]() is None, input_refs[0]() is None
        return trace, message, retained, released

    _compare(implementations, scenario, (
        ("convert", ("original-released", True), "_slice_input"),
        "_slice_input failed", (True, True, True, True), (True, True),
    ))


def test_slice_identity_comparison_releases_proxies_before_fresh_thumbnail_raw(
    implementations,
):
    """Comparison temporary finalizers choose the next thumbnail raw provider."""

    def scenario(impl):
        trace = []
        image_raw, first_thumbnail_raw, chosen_thumbnail_raw = (
            Marker(), Marker(), Marker(),
        )

        class Proxy:
            def __init__(self, name, owner):
                self.name, self.owner = name, owner

            @property
            def raw(self):
                trace.append(("raw", self.name))
                if self.name == "image-raw":
                    return image_raw
                assert self.name == "thumbnail-raw"
                return self.owner.chosen_raw

            def __del__(self):
                trace.append(("release", self.name))
                if self.name == "thumbnail-comparison":
                    self.owner.chosen_raw = first_thumbnail_raw
                elif self.name == "image-comparison":
                    self.owner.chosen_raw = chosen_thumbnail_raw

        class Response:
            tile_to_data, slice_input = Marker(), Marker()
            request_id, empty = 23, False

            def __init__(self):
                self.image_reads = self.thumbnail_reads = 0
                self.chosen_raw = Marker()

            @property
            def image(self):
                self.image_reads += 1
                name = "image-raw" if self.image_reads == 1 else "image-comparison"
                trace.append(("get", name))
                return Proxy(name, self)

            @property
            def thumbnail(self):
                self.thumbnail_reads += 1
                name = (
                    "thumbnail-comparison" if self.thumbnail_reads == 1
                    else "thumbnail-raw"
                )
                trace.append(("get", name))
                return Proxy(name, self)

        def converter(raw):
            name = (
                "image" if raw is image_raw else
                "chosen-thumbnail" if raw is chosen_thumbnail_raw else "wrong"
            )
            trace.append(("convert", name))
            return raw

        # Invoke both unchanged/current method bodies unbound on the same
        # transient attribute protocol, rather than their dataclass storage.
        result = impl.response.to_displayed(Response(), converter)
        return (tuple(trace), result.image.raw is image_raw,
                result.thumbnail.raw is chosen_thumbnail_raw,
                result.thumbnail.view is chosen_thumbnail_raw)

    _compare(implementations, scenario, (
        (("get", "image-raw"), ("raw", "image-raw"),
         ("release", "image-raw"), ("convert", "image"),
         ("get", "thumbnail-comparison"), ("get", "image-comparison"),
         ("release", "thumbnail-comparison"), ("release", "image-comparison"),
         ("get", "thumbnail-raw"), ("raw", "thumbnail-raw"),
         ("release", "thumbnail-raw"), ("convert", "chosen-thumbnail")),
        True, True, True,
    ))
