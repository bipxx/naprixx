"""Camera dictionary publication against unchanged pinned production bodies.

The reference loads both Camera and EventedModel unchanged. Installed Pydantic,
event delivery, and imported utility services are shared, so these assertions
cover dictionary publication and caller aliases, not complete Camera parity.
No QApplication is constructed.
"""

import copy
import gc
import importlib.util
import sys
import weakref
from pathlib import Path

import pytest

from napari.components.camera import Camera
from napari.utils import events


@pytest.fixture(scope="module")
def reference():
    upstream = Path(__file__).resolve().parents[1] / ".upstream/napari/src/napari"
    modules = []

    def load(name, relative):
        spec = importlib.util.spec_from_file_location(name, upstream / relative)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        modules.append(name)
        spec.loader.exec_module(module)
        return module

    original_model = load(
        "_original_camera_namespace_evented_model", "utils/events/evented_model.py"
    )
    previous = events.EventedModel
    try:
        events.EventedModel = original_model.EventedModel
        original_camera = load(
            "_original_camera_namespace_camera", "components/camera.py"
        )
    finally:
        events.EventedModel = previous
    try:
        yield original_camera.Camera
    finally:
        for name in reversed(modules):
            sys.modules.pop(name, None)


def _raw_map(model, kind):
    name = "__dict__" if kind == "fields" else "__pydantic_private__"
    return object.__getattribute__(model, name)


def _generic_edit(model, kind, edit, replacement):
    name = "__dict__" if kind == "fields" else "__pydantic_private__"
    if edit == "replace":
        object.__setattr__(model, name, replacement)
    else:
        object.__delattr__(model, name)


def _compare(reference, scenario, contract):
    # Establish the pinned assertion before the installed assertion can fail.
    expected = scenario(reference)
    assert expected == contract
    assert scenario(Camera) == expected


def _payload(model, kind, replacement, trace, errors):
    target = weakref.ref(model)

    class Payload:
        def __del__(self):
            current = target()
            if current is None:
                return
            try:
                try:
                    published = _raw_map(current, kind)
                except AttributeError:
                    trace.append(("missing",))
                else:
                    trace.append(
                        ("mapping", published is replacement,
                         "adapter_payload" in published)
                    )
                    published["adapter_reentered"] = True
            except Exception as error:
                errors.append((type(error).__name__, str(error)))

    return Payload()


@pytest.mark.parametrize("kind", ["fields", "private"])
@pytest.mark.parametrize("edit", ["replace", "delete"])
def test_generic_map_edit_releases_payload_before_any_camera_read(
    reference, kind, edit
):
    def scenario(model_type):
        model = model_type()
        replacement = dict(_raw_map(model, kind))
        trace, errors = [], []
        payload = _payload(model, kind, replacement, trace, errors)
        payload_reference = weakref.ref(payload)
        _raw_map(model, kind)["adapter_payload"] = payload
        del payload
        _generic_edit(model, kind, edit, replacement)
        # Capture before any facade/native-state read, collection, or refresh.
        return (
            tuple(trace), payload_reference() is None,
            replacement.get("adapter_reentered", False), tuple(errors),
        )

    if edit == "replace":
        contract = ((("mapping", True, False),), True, True, ())
    elif kind == "fields":
        # object.__getattribute__ lazily publishes a new empty field dict.
        contract = ((("mapping", False, False),), True, False, ())
    else:
        contract = ((("missing",),), True, False, ())
    _compare(reference, scenario, contract)


@pytest.mark.parametrize("kind", ["fields", "private"])
def test_saved_old_map_alias_controls_immediate_payload_release(reference, kind):
    def scenario(model_type):
        model = model_type()
        old_map = _raw_map(model, kind)
        replacement = dict(old_map)
        trace, errors = [], []
        payload = _payload(model, kind, replacement, trace, errors)
        payload_reference = weakref.ref(payload)
        old_map["adapter_payload"] = payload
        del payload
        _generic_edit(model, kind, "replace", replacement)
        alive_with_alias = payload_reference() is not None
        trace_with_alias = tuple(trace)
        old_map["adapter_old_only"] = 17
        independent = "adapter_old_only" not in replacement
        del old_map
        # Dropping the last caller alias must release before a Camera read.
        return (
            alive_with_alias, trace_with_alias, independent,
            tuple(trace), payload_reference() is None,
            replacement.get("adapter_reentered", False), tuple(errors),
        )

    _compare(
        reference, scenario,
        (True, (), True, (("mapping", True, False),), True, True, ()),
    )


@pytest.mark.parametrize("kind", ["fields", "private"])
def test_saved_values_view_owns_old_map_until_caller_drops_it(reference, kind):
    def scenario(model_type):
        model = model_type()
        old_map = _raw_map(model, kind)
        replacement = dict(old_map)
        trace, errors = [], []
        payload = _payload(model, kind, replacement, trace, errors)
        payload_reference = weakref.ref(payload)
        old_map["adapter_payload"] = payload
        saved_view = old_map.values()
        del payload, old_map
        _generic_edit(model, kind, "replace", replacement)
        alive_with_view = payload_reference() is not None
        trace_with_view = tuple(trace)
        del saved_view
        # A values view is an explicit owning caller alias, not an observer.
        return (
            alive_with_view, trace_with_view, tuple(trace),
            payload_reference() is None,
            replacement.get("adapter_reentered", False), tuple(errors),
        )

    _compare(
        reference, scenario,
        (True, (), (("mapping", True, False),), True, True, ()),
    )


@pytest.mark.parametrize("kind", ["fields", "private"])
def test_dictionary_subclass_owner_cycle_collects_from_current_map(reference, kind):
    def scenario(model_type):
        class Namespace(dict):
            pass

        model = model_type()
        namespace = Namespace(_raw_map(model, kind))
        namespace["adapter_owner"] = model
        _generic_edit(model, kind, "replace", namespace)
        assert _raw_map(model, kind) is namespace
        references = weakref.ref(model), weakref.ref(namespace)
        del model, namespace
        gc.collect()
        return tuple(target() is None for target in references)

    _compare(reference, scenario, (True, True))


@pytest.mark.parametrize("operation", ["copy", "deepcopy", "restore"])
def test_slotted_subtype_lifecycle_preserves_exact_map_and_payload_aliases(
    reference, operation
):
    def scenario(model_type):
        class Slotted(model_type):
            __slots__ = ("adapter_slot",)

        source = Slotted()
        fields = _raw_map(source, "fields")
        private = _raw_map(source, "private")
        marker = object()
        fields["adapter_marker"] = private["adapter_marker"] = marker
        if operation == "restore":
            result = Slotted.__new__(Slotted)
            state = {
                "__dict__": fields,
                "__pydantic_private__": private,
                "__pydantic_extra__": None,
                "__pydantic_fields_set__": set(),
            }
            assert result.__setstate__(state) is None
            field_identity = _raw_map(result, "fields") is fields
            private_identity = _raw_map(result, "private") is private
            expected_marker = marker
        else:
            expected_marker = object() if operation == "deepcopy" else marker
            result = (
                copy.deepcopy(source, {id(marker): expected_marker})
                if operation == "deepcopy" else copy.copy(source)
            )
            field_identity = _raw_map(result, "fields") is not fields
            private_identity = _raw_map(result, "private") is not private
        object.__setattr__(result, "adapter_slot", expected_marker)
        return (
            type(result) is Slotted, field_identity, private_identity,
            _raw_map(result, "fields")["adapter_marker"]
            is _raw_map(result, "private")["adapter_marker"] is expected_marker,
            object.__getattribute__(result, "adapter_slot") is expected_marker,
            fields["adapter_marker"] is private["adapter_marker"] is marker,
        )

    _compare(reference, scenario, (True, True, True, True, True, True))


def test_caller_saved_native_record_is_an_explicit_old_map_owner():
    """The native diagnostic has no pinned counterpart; saved aliases may own."""

    class Payload:
        pass

    model = Camera()
    state = _raw_map(model, "private")["_native_state"]
    record = state._model_dictionary_record
    old_map = record.dictionary
    assert old_map is _raw_map(model, "fields")
    assert record.owner is state
    assert record.model_ref() is model
    replacement = dict(old_map)
    old_map["adapter_payload"] = Payload()
    payload_reference = weakref.ref(old_map["adapter_payload"])
    _generic_edit(model, "fields", "replace", replacement)
    assert record.dictionary is old_map
    assert payload_reference() is not None
    del record, old_map, state
    # The model stays alive, but no caller still retains the old record/map.
    # Capture without a native-state getter or a collection concealing its edge.
    assert payload_reference() is None


def test_guarded_sync_rotation_releases_successor_pause_and_uses_current_map():
    """Native-only guard cleanup follows the active record after publication."""

    model = Camera()
    state = _raw_map(model, "private")["_native_state"]
    old_record = state._model_dictionary_record
    old_map = _raw_map(model, "fields")
    replacement = dict(old_map, zoom=17.0, angles=(0.0, 90.0, 0.0))
    trace = []

    class RotateOnFloat:
        def __float__(self):
            trace.append("float")
            _generic_edit(model, "fields", "replace", replacement)
            return 11.0

    old_map["zoom"] = RotateOnFloat()
    assert model._sync_native_state() is model
    assert trace == ["float"]
    assert _raw_map(model, "fields") is replacement
    # Captured arguments from the retired map must not overwrite its successor.
    assert replacement["zoom"] == 17.0
    current_record = state._model_dictionary_record
    assert current_record is not old_record
    assert current_record.owner is state
    assert current_record.dictionary is replacement
    assert old_record.dictionary is old_map
    assert old_record.watched is False
    assert state.zoom == 17.0
    assert state.angles == (0.0, 90.0, 0.0)
    replacement["zoom"] = 23.0
    assert state.zoom == 23.0


def test_require_pins_provider_and_discards_conversion_from_retired_map():
    """Native-only provider conversion can retire its borrowed field/record."""

    model = Camera()
    state = _raw_map(model, "private")["_native_state"]
    old_map = _raw_map(model, "fields")
    old_record_reference = weakref.ref(state._model_dictionary_record)
    replacement = dict(old_map, zoom=17.0)
    trace = []

    class RotateOnFloat:
        def __float__(self):
            trace.append("float")
            # Neither a caller variable nor the old map retains this provider.
            # The in-flight conversion must keep it alive across publication.
            old_map.pop("zoom")
            _generic_edit(model, "fields", "replace", replacement)
            trace.append(("published", provider_reference() is self))
            return 11.0

        def __del__(self):
            trace.append("provider-released")

    provider = RotateOnFloat()
    provider_reference = weakref.ref(provider)
    old_map["zoom"] = provider
    del provider
    assert state.zoom == 17.0
    assert trace == ["float", ("published", True), "provider-released"]
    assert provider_reference() is None
    assert old_record_reference() is None
    assert _raw_map(model, "fields") is replacement
    assert state._model_dictionary_record.dictionary is replacement
    assert replacement["zoom"] == 17.0


def test_native_publication_stops_old_map_writes_after_payload_rotation():
    """Native-only payload decref can publish a successor during state.update."""

    model = Camera()
    state = _raw_map(model, "private")["_native_state"]
    old_record = state._model_dictionary_record
    old_map = _raw_map(model, "fields")
    replacement = dict(old_map, center=(9.0, 8.0, 7.0), zoom=17.0)
    target = weakref.ref(model)
    trace = []

    class Payload:
        def __del__(self):
            current = target()
            if current is not None:
                _generic_edit(current, "fields", "replace", replacement)
                trace.append(state._model_dictionary_record.dictionary is replacement)

    old_map["center"] = Payload()
    state.update(
        (4.0, 5.0, 6.0), 3.0, (0.0, 90.0, 0.0), 40.0,
        False, False, ("away", "up", "left"), False,
    )
    assert trace == [True]
    assert _raw_map(model, "fields") is replacement
    current_record = state._model_dictionary_record
    assert current_record is not old_record
    assert current_record.owner is state
    assert current_record.dictionary is replacement
    assert old_record.dictionary is old_map
    assert old_record.watched is False
    # The center write caused retirement; later writes to that map must stop.
    assert old_map["center"] == (4.0, 5.0, 6.0)
    assert old_map["zoom"] == 1.0
    assert old_map["angles"] == (0.0, 0.0, 0.0)
    assert old_map["perspective"] == 0.0
    assert state.center == (9.0, 8.0, 7.0)
    assert state.zoom == 17.0
    replacement["zoom"] = 23.0
    assert state.zoom == 23.0


def test_passive_saved_record_retains_old_map_without_authority_over_core():
    """Native-only diagnostic aliases retain old data, not active authority."""

    model = Camera()
    state = _raw_map(model, "private")["_native_state"]
    old_record = state._model_dictionary_record
    old_map = old_record.dictionary
    replacement = dict(old_map, zoom=7.0, angles=(0.0, 0.0, 0.0))
    _generic_edit(model, "fields", "replace", replacement)
    current_record = state._model_dictionary_record
    assert current_record is not old_record
    assert current_record.owner is state
    assert current_record.dictionary is replacement
    assert old_record.owner is state
    assert old_record.model_ref() is model
    assert old_record.dictionary is old_map
    assert old_record.watched is False
    old_map["zoom"] = 99.0
    old_map["angles"] = (0.0, 90.0, 0.0)
    assert state.zoom == old_record.owner.zoom == 7.0
    assert state.angles == (0.0, 0.0, 0.0)
    replacement["zoom"] = 8.0
    assert state.zoom == 8.0


@pytest.mark.parametrize("hook_kind", ["setattr", "descriptor"])
def test_native_backlink_publication_bypasses_rejecting_subtype_hooks(hook_kind):
    """Native metadata uses physical storage; caller writes retain their hooks."""

    from naparixx import _native

    model = Camera()
    calls = []
    armed_reentry = False
    reentrant_map = None

    def reject_backlink(value):
        calls.append("backlink-write")
        if armed_reentry:
            # If native implementation metadata dispatches this hook, it can
            # rotate application authority and then reject the outer update.
            _generic_edit(model, "fields", "replace", reentrant_map)
        raise RuntimeError("caller backlink write rejected")

    if hook_kind == "setattr":
        class HookedState(_native.CameraState):
            def __setattr__(self, name, value):
                if name == "_model_dictionary_record":
                    reject_backlink(value)
                super().__setattr__(name, value)
    else:
        class HookedState(_native.CameraState):
            @property
            def _model_dictionary_record(self):
                return object.__getattribute__(self, "__dict__")[
                    "_model_dictionary_record"
                ]

            @_model_dictionary_record.setter
            def _model_dictionary_record(self, value):
                reject_backlink(value)

    state = HookedState()
    _raw_map(model, "private")["_native_state"] = state
    # Initial linking must use the same physical metadata route as rotation.
    assert model._sync_native_state() is model
    assert calls == []
    state_dictionary = object.__getattribute__(state, "__dict__")
    old_record = state_dictionary["_model_dictionary_record"]
    old_map = _raw_map(model, "fields")
    assert old_record.owner is state
    assert old_record.dictionary is old_map
    assert state._model_dictionary_record is old_record

    # The hook is effective for a public caller. Its rejected write leaves
    # physical metadata and application/core authority immediately consistent.
    with pytest.raises(RuntimeError, match="caller backlink write rejected"):
        state._model_dictionary_record = object()
    assert calls == ["backlink-write"]
    assert state_dictionary["_model_dictionary_record"] is old_record
    assert _raw_map(model, "fields") is old_map
    assert old_record.dictionary is old_map
    assert state.zoom == 1.0

    calls.clear()
    replacement = dict(old_map, zoom=7.0)
    reentrant_map = dict(old_map, zoom=99.0)
    armed_reentry = True
    # This is implementation metadata, so neither subclass hook runs. In
    # particular, their rejection/reentry cannot happen after physical commit.
    _generic_edit(model, "fields", "replace", replacement)
    assert calls == []
    assert _raw_map(model, "fields") is replacement
    assert object.__getattribute__(state, "__dict__") is state_dictionary
    current_record = state_dictionary["_model_dictionary_record"]
    assert current_record is not old_record
    assert current_record.owner is state
    assert current_record.dictionary is replacement
    assert state._model_dictionary_record is current_record
    assert old_record.dictionary is old_map
    assert old_record.watched is False
    assert state.zoom == 7.0

    replacement["zoom"] = 8.0
    assert state.zoom == 8.0
    state.update(
        (0.0, 0.0, 0.0), 9.0, (0.0, 0.0, 0.0), 0.0,
        True, True, ("towards", "down", "right"), True,
    )
    assert calls == []
    assert state_dictionary["_model_dictionary_record"] is current_record
    assert _raw_map(model, "fields") is replacement
    assert replacement["zoom"] == state.zoom == 9.0
    assert old_map["zoom"] == 1.0
