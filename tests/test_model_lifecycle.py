"""Model lifecycle and tuple control against unchanged Python function bodies.

Snapshot bodies exercise the previous Python integration around the same
native numerical/event services. Original upstream model tests remain the
public behavior oracle. The snapshot oracle does not establish whole-app parity.
"""

import ast
import copy
import gc
import inspect
import subprocess
import sys
import types
import weakref
from pathlib import Path

import pytest

from napari.components import Camera, camera, grid
from napari.components.grid import GridCanvas
from napari.utils import misc
from napari.utils.events import EventedModel, evented_model
from naparixx import native

ROOT = Path(__file__).resolve().parents[1]


def prior_function(filename, owner, name, globals_, index=0, optimize=0):
    """Compile only a saved unchanged body; remove signature decorators."""
    source = (
        ROOT / "tests/_reference/model_lifecycle" / filename.replace(".py", ".txt")
    ).read_text(encoding="utf-8")
    tree = ast.parse(source)
    nodes = tree.body
    if owner:
        nodes = next(
            n.body for n in nodes if isinstance(n, ast.ClassDef) and n.name == owner
        )
    function = [n for n in nodes if isinstance(n, ast.FunctionDef) and n.name == name][
        index
    ]
    function.decorator_list = []
    module = ast.Module(
        body=[
            ast.ImportFrom(
                module="__future__", names=[ast.alias(name="annotations")], level=0
            ),
            function,
        ],
        type_ignores=[],
    )
    namespace = dict(globals_)
    exec(
        compile(ast.fix_missing_locations(module), filename, "exec", optimize=optimize),
        namespace,
    )
    return namespace[name]


def captured(call):
    try:
        return "value", call()
    except Exception as error:
        return "error", type(error).__name__, str(error)


@pytest.mark.parametrize("before", (False, True))
@pytest.mark.parametrize("n", (1, 3, 5, 0, -1, True, 2.5, None, "bad"))
@pytest.mark.parametrize("value", ((), (1,), (1, 2, 3, 4), "abc"))
def test_tuple_normalization_against_original_body(before, n, value):
    original = prior_function(
        "model-lifecycle-prior-misc.py", None, "ensure_n_tuple", {}
    )
    assert captured(
        lambda: misc.ensure_n_tuple(value, n, fill=9, before=before)
    ) == captured(lambda: original(value, n, fill=9, before=before))


@pytest.mark.parametrize("before", (False, True))
@pytest.mark.parametrize("failure", ("none", "iteration", "truth", "index"))
def test_tuple_provider_evaluation_and_errors(before, failure):
    def scenario(original):
        trace = []

        class Values:
            def __iter__(self):
                trace.append("iterate")
                if failure == "iteration":
                    raise LookupError("iteration failed")
                return iter((1, 2, 3, 4))

        class Before:
            def __bool__(self):
                trace.append("before")
                if failure == "truth":
                    raise RuntimeError("truth failed")
                return before

        class N(int):
            def __gt__(self, other):
                trace.append("greater")
                return super().__gt__(other)

            def __sub__(self, other):
                trace.append("subtract")
                return super().__sub__(other)

            def __neg__(self):
                trace.append("negative")
                if failure == "index":
                    raise RuntimeError("negative failed")
                return super().__neg__()

        function = (
            prior_function("model-lifecycle-prior-misc.py", None, "ensure_n_tuple", {})
            if original
            else misc.ensure_n_tuple
        )
        return captured(lambda: function(Values(), N(3), before=Before())), trace

    assert scenario(False) == scenario(True)


@pytest.mark.parametrize("optimized", (False, True))
def test_tuple_assertion_respects_python_optimization(optimized):
    original = prior_function(
        "model-lifecycle-prior-misc.py",
        None,
        "ensure_n_tuple",
        {},
        optimize=int(optimized),
    )
    expected = captured(lambda: original((1, 2), 0))
    observed = captured(
        lambda: native.misc_ensure_n_tuple((1, 2), 0, 0, True, not optimized)
    )
    assert observed == expected
    if optimized:
        result = subprocess.run(
            [
                sys.executable,
                "-O",
                "-c",
                "from napari.utils.misc import ensure_n_tuple; "
                "print(ensure_n_tuple((1,2),0))",
            ],
            cwd=ROOT,
            text=True,
            capture_output=True,
        )
        assert result.returncode == 0, result.stderr
        assert result.stdout.strip() == "(1, 2)"
        assert "nanobind: leaked" not in result.stderr


@pytest.mark.parametrize("component", ("model", "camera", "grid"))
@pytest.mark.parametrize("failure", ("none", "parent", "state", "copy", "assignment"))
def test_shallow_copy_order_and_live_factory_lookup(component, failure):
    module, owner, field = {
        "model": (evented_model, "EventedModel", "_change_state"),
        "camera": (camera, "Camera", "_native_state"),
        "grid": (grid, "GridCanvas", "_native_state"),
    }[component]

    def scenario(original):
        trace = []

        def factory(value):
            trace.append(("copy-original", value))
            if failure == "copy":
                raise RuntimeError("copy failed")
            return "copied"

        class Result:
            def __setattr__(self, name, value):
                trace.append(("assign", name, value))
                if failure == "assignment":
                    raise RuntimeError("assignment failed")
                object.__setattr__(self, name, value)

        result = Result()

        class Parent:
            def __copy__(self):
                trace.append("parent")
                module.copy = factory
                if failure == "parent":
                    raise RuntimeError("parent failed")
                return result

        class Source:
            def __getattribute__(self, name):
                if name == field:
                    trace.append("state")
                    module.copy = lambda value: "replacement"
                    if failure == "state":
                        raise RuntimeError("state failed")
                    return "state-value"
                return object.__getattribute__(self, name)

        parent, source = Parent(), Source()
        previous = module.copy
        try:
            if original:
                function = prior_function(
                    f"model-lifecycle-prior-{module.__name__.rsplit('.', 1)[1]}.py",
                    owner,
                    "__copy__",
                    {"super": lambda: parent},
                )
                # The unchanged body's namespace performs live global lookup.
                function.__globals__["copy"] = lambda value: "early"
                old_parent = parent.__copy__

                def parent_copy():
                    value = old_parent()
                    function.__globals__["copy"] = factory
                    return value

                parent.__copy__ = parent_copy

                def call():
                    return function(source)
            else:
                function = getattr(
                    native,
                    "model_shallow_copy"
                    if component == "model"
                    else component + "_shallow_copy",
                )

                def call():
                    return function(source, parent)

            status = captured(call)
            if status[0] == "value":
                status = ("value", status[1] is result)
            return status, trace
        finally:
            module.copy = previous

    assert scenario(False) == scenario(True)


@pytest.mark.parametrize("component", ("camera", "grid"))
@pytest.mark.parametrize("deep", (False, True, None))
def test_model_copy_dispatch_and_post_copy_hook(component, deep):
    module, owner = (
        (camera, "Camera") if component == "camera" else (grid, "GridCanvas")
    )

    def scenario(original):
        trace = []

        class Result:
            def _sync_native_state(self):
                trace.append("sync")

        result = Result()

        class Parent:
            def model_copy(self, **kwargs):
                trace.append(("copy", kwargs))
                return result

        parent = Parent()
        function = (
            prior_function(
                f"model-lifecycle-prior-{component}.py",
                owner,
                "model_copy",
                {"super": lambda: parent},
            )
            if original
            else getattr(native, component + "_copy_model")
        )
        value = (
            function(None, update={"value": 2}, deep=deep)
            if original
            else function(None, parent, {"value": 2}, deep)
        )
        return value is result, trace

    assert scenario(False) == scenario(True)


@pytest.mark.parametrize("component", ("camera", "grid"))
def test_sync_captures_update_before_field_getters(component):
    module, owner = (
        (camera, "Camera") if component == "camera" else (grid, "GridCanvas")
    )

    def scenario(original):
        trace = []
        fields = (
            {
                "center": (1, 2, 3),
                "zoom": 2,
                "angles": (0, 0, 0),
                "perspective": 0,
                "mouse_pan": True,
                "mouse_zoom": False,
                "orientation": ("a", "b", "c"),
                "synced": True,
            }
            if component == "camera"
            else {"stride": 2, "shape": (2, 3), "enabled": True, "spacing": 1}
        )
        state = types.SimpleNamespace(
            update=lambda *args: trace.append(("update-original", args))
        )

        class Source:
            def __getattr__(self, name):
                trace.append(name)
                if name == "_native_state":
                    return state
                state.update = lambda *args: trace.append(("update-replaced", args))
                return fields[name]

        source = Source()
        function = (
            prior_function(
                f"model-lifecycle-prior-{component}.py", owner, "_sync_native_state", {}
            )
            if original
            else getattr(native, component + "_sync")
        )
        return function(source) is source, trace

    assert scenario(False) == scenario(True)


@pytest.mark.parametrize("kind", ("model", "camera", "grid"))
@pytest.mark.parametrize("deep", (False, True))
def test_real_model_copies_preserve_fields_and_independent_state(kind, deep):
    class Model(EventedModel):
        value: int = 1

    source = {"model": Model, "camera": Camera, "grid": GridCanvas}[kind]()
    copied = copy.deepcopy(source) if deep else copy.copy(source)
    assert copied is not source
    assert copied.model_dump() == source.model_dump()
    assert copied._change_state is not source._change_state
    if kind != "model":
        assert copied._native_state is not source._native_state
    source._delay_check_semaphore = 2
    assert copied._delay_check_semaphore == 0
    if deep:
        assert copied._primary_view is None


def test_deep_copy_clears_only_new_primary_view_and_passes_memo():
    trace = []
    memo = {}
    result = types.SimpleNamespace(_primary_view="cached")

    class Parent:
        def __deepcopy__(self, value):
            trace.append(value is memo)
            return result

    assert native.model_deep_copy(None, Parent(), memo) is result
    assert result._primary_view is None
    assert trace == [True]


@pytest.mark.parametrize("value", (0, 2, None, "provider"))
def test_semaphore_and_delayer_reference_protocol(value):
    source = types.SimpleNamespace(_change_state=types.SimpleNamespace(depth="old"))
    native.model_semaphore_set(source, value)
    assert native.model_semaphore_get(source) is value
    delayer = evented_model.ComparisonDelayer(source)
    assert delayer._target is source


@pytest.mark.parametrize("mode", (2, 3, 4))
def test_camera_cache_pop_and_identity(mode):
    camera_ = Camera(center=(1, 2, 3), zoom=2, angles=(3, 4, 5))
    if mode in (2, 3):
        camera_._cache_state(mode)
    state = camera_._pop_cached_state(mode)
    if mode in (2, 3):
        assert state == camera._CameraState((1, 2, 3), 2, (3, 4, 5))
        assert camera_._pop_cached_state(mode) is None
    else:
        assert state is None


@pytest.mark.parametrize(
    "failure", (None, "pop", "center", "zoom", "angles", "factory", "empty")
)
def test_camera_cache_provider_order_and_factory_capture(monkeypatch, failure):
    def run(original):
        trace = []
        function = prior_function(
            "model-lifecycle-prior-camera.py", "Camera", "_pop_cached_state", {}
        )

        def replacement(*args):
            raise AssertionError("factory was looked up after reading state")

        def factory(*args):
            trace.append(("factory", args))
            if failure == "factory":
                raise RuntimeError("factory")
            return args

        class State:
            def __getattr__(self, name):
                trace.append(("field", name))
                if name == "center":
                    function.__globals__["_CameraState"] = replacement
                    patch.setattr(camera, "_CameraState", replacement)
                if failure == name:
                    raise RuntimeError(name)
                return {"center": (1, 2, 3), "zoom": 4, "angles": (5, 6, 7)}[name]

        class Provider:
            def pop_cache(self, mode):
                assert mode is token
                trace.append("pop")
                if failure == "pop":
                    raise RuntimeError("pop")
                return None if failure == "empty" else State()

        token = object()
        source = types.SimpleNamespace(_native_state=Provider())
        function.__globals__["_CameraState"] = factory
        with monkeypatch.context() as patch:
            patch.setattr(camera, "_CameraState", factory)
            call = function if original else native.camera_pop_cache
            outcome = captured(lambda: call(source, token))
        return outcome, trace

    assert run(True) == run(False)


@pytest.mark.parametrize(
    "failure",
    (None, "existing", "value0", "vertical", "value1", "horizontal", "assign"),
)
def test_camera_orientation_provider_order_and_constructor_capture(
    monkeypatch, failure
):
    def run(original):
        trace = []
        function = prior_function(
            "model-lifecycle-prior-camera.py", "Camera", "orientation2d", {}, index=1
        )

        def late_vertical(value):
            raise AssertionError("vertical constructor was captured too late")

        def horizontal(value):
            trace.append(("horizontal", value))
            if failure == "horizontal":
                raise RuntimeError("horizontal")
            return "h:" + value

        def vertical(value):
            trace.append(("vertical", value))
            function.__globals__["HorizontalAxisOrientation"] = horizontal
            patch.setattr(camera, "HorizontalAxisOrientation", horizontal)
            if failure == "vertical":
                raise RuntimeError("vertical")
            return "v:" + value

        class Existing:
            def __getitem__(self, key):
                trace.append(("existing", key))
                if failure == "existing":
                    raise RuntimeError("existing")
                return "depth"

        class Source:
            orientation = Existing()

            def __setattr__(self, name, value):
                trace.append(("assign", name, value))
                if failure == "assign":
                    raise RuntimeError("assign")

        class Value:
            def __getitem__(self, key):
                trace.append(("value", key))
                if key == 0:
                    function.__globals__["VerticalAxisOrientation"] = late_vertical
                    patch.setattr(camera, "VerticalAxisOrientation", late_vertical)
                if failure == "value" + str(key):
                    raise RuntimeError("value" + str(key))
                return ("up", "right")[key]

        function.__globals__.update(
            VerticalAxisOrientation=vertical, HorizontalAxisOrientation=None
        )
        with monkeypatch.context() as patch:
            patch.setattr(camera, "VerticalAxisOrientation", vertical)
            patch.setattr(camera, "HorizontalAxisOrientation", None)
            call = function if original else native.camera_set_orientation2d
            outcome = captured(lambda: call(Source(), Value()))
        return outcome, trace

    assert run(True) == run(False)


@pytest.mark.parametrize("failure", (None, "state", "directions", "assign"))
def test_camera_direction_provider_dispatch_and_assignment(failure):
    def run(original):
        trace = []
        result, view, up = object(), object(), object()

        class Provider:
            def angles_for_directions(self, given_view, given_up):
                assert given_view is view and given_up is up
                trace.append("directions")
                if failure == "directions":
                    raise RuntimeError("directions")
                return result

        class Source:
            @property
            def _native_state(self):
                trace.append("state")
                if failure == "state":
                    raise RuntimeError("state")
                return Provider()

            def __setattr__(self, name, value):
                assert name == "angles" and value is result
                trace.append("assign")
                if failure == "assign":
                    raise RuntimeError("assign")

        call = (
            prior_function(
                "model-lifecycle-prior-camera.py", "Camera", "set_view_direction", {}
            )
            if original
            else native.camera_set_direction
        )
        return captured(lambda: call(Source(), view, up)), trace

    assert run(True) == run(False)


def test_lifecycle_facades_preserve_prior_signatures_and_docstrings():
    for filename, owner, names, cls in (
        (
            "camera",
            "Camera",
            (
                "__copy__",
                "model_copy",
                "_sync_native_state",
                "_pop_cached_state",
                "set_view_direction",
            ),
            Camera,
        ),
        (
            "grid",
            "GridCanvas",
            ("__copy__", "model_copy", "_sync_native_state"),
            GridCanvas,
        ),
        ("evented_model", "EventedModel", ("__copy__", "__deepcopy__"), EventedModel),
    ):
        for name in names:
            old = prior_function(
                f"model-lifecycle-prior-{filename}.py", owner, name, {}
            )
            actual = getattr(cls, name)
            assert str(inspect.signature(actual)) == str(inspect.signature(old))
            assert actual.__doc__ == old.__doc__


def test_model_native_state_and_delayer_cycles_collect():
    camera_ = Camera()
    camera_._cycle = evented_model.ComparisonDelayer(camera_)
    references = (
        weakref.ref(camera_),
        weakref.ref(camera_._native_state),
        weakref.ref(camera_._change_state),
    )
    del camera_
    gc.collect()
    assert all(reference() is None for reference in references)
