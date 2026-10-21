"""Dims namespace lifetimes and unvalidated values against pinned production.

The reference loads both unchanged Dims and unchanged EventedModel. Shared
Pydantic services and the event backend are external to this bounded packet.
These tests do not establish complete Dims or application conversion.
"""

import copy
import gc
import importlib.util
import sys
import weakref
from pathlib import Path

import pytest

from napari.components.dims import Dims
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
        "_original_dims_namespace_evented_model", "utils/events/evented_model.py"
    )
    previous = events.EventedModel
    try:
        events.EventedModel = original_model.EventedModel
        original_dims = load(
            "_original_dims_namespace_dims", "components/dims.py"
        )
    finally:
        events.EventedModel = previous
    try:
        yield original_dims.Dims
    finally:
        for name in reversed(modules):
            sys.modules.pop(name, None)


def _compare(reference, scenario):
    expected = scenario(reference)
    observed = scenario(Dims)
    assert observed == expected
    return expected


def _raw_map(model, kind):
    name = "__dict__" if kind == "fields" else "__pydantic_private__"
    return object.__getattribute__(model, name)


def _generic_edit(model, kind, edit, replacement):
    name = "__dict__" if kind == "fields" else "__pydantic_private__"
    if edit == "replace":
        object.__setattr__(model, name, replacement)
    else:
        object.__delattr__(model, name)


@pytest.mark.parametrize("kind", ["fields", "private"])
@pytest.mark.parametrize("edit", ["replace", "delete"])
def test_generic_map_edit_releases_old_payload_before_any_dims_read(
    reference, kind, edit
):
    def scenario(model_type):
        model = model_type(ndim=3)
        replacement = dict(_raw_map(model, kind))
        target = weakref.ref(model)
        trace, errors = [], []

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
                            (
                                "mapping",
                                published is replacement,
                                "adapter_payload" in published,
                            )
                        )
                        if edit == "replace":
                            published["adapter_reentered"] = True
                except Exception as error:
                    errors.append((type(error).__name__, str(error)))

        payload = Payload()
        payload_reference = weakref.ref(payload)
        _raw_map(model, kind)["adapter_payload"] = payload
        del payload
        _generic_edit(model, kind, edit, replacement)
        # Capture immediately. A facade read, gc.collect(), or owner refresh
        # here would conceal the stale observer edge this test is about.
        return (
            tuple(trace),
            payload_reference() is None,
            replacement.get("adapter_reentered", False),
            tuple(errors),
        )

    expected = _compare(reference, scenario)
    assert expected[0]
    assert expected[1] is True
    assert expected[3] == ()


@pytest.mark.parametrize("kind", ["fields", "private"])
def test_saved_old_map_alias_controls_payload_release_after_replacement(
    reference, kind
):
    def scenario(model_type):
        class Payload:
            pass

        model = model_type(ndim=3)
        old_map = _raw_map(model, kind)
        replacement = dict(old_map)
        old_map["adapter_payload"] = Payload()
        payload_reference = weakref.ref(old_map["adapter_payload"])
        _generic_edit(model, kind, "replace", replacement)
        assert _raw_map(model, kind) is replacement
        alive_with_alias = payload_reference() is not None
        old_map["adapter_old_only"] = 17
        independent = "adapter_old_only" not in replacement
        del old_map
        # Deliberately no subsequent Dims/native-owner read.
        return alive_with_alias, independent, payload_reference() is None

    assert _compare(reference, scenario) == (True, True, True)


@pytest.mark.parametrize("existing", [False, True])
def test_restore_keeps_supplied_exact_maps_and_saved_state_aliases(
    reference, existing
):
    def scenario(model_type):
        source = model_type(ndim=3)
        fields = dict(_raw_map(source, "fields"))
        private = dict(_raw_map(source, "private"))
        marker = object()
        fields["adapter_field"] = marker
        private["adapter_private"] = marker
        state = {
            "__dict__": fields,
            "__pydantic_private__": private,
            "__pydantic_extra__": None,
            "__pydantic_fields_set__": {"ndim"},
        }
        keys = tuple(fields), tuple(private)
        first = model_type() if existing else model_type.__new__(model_type)
        second = model_type.__new__(model_type)
        assert first.__setstate__(state) is None
        assert second.__setstate__(state) is None
        assert _raw_map(first, "fields") is fields is _raw_map(second, "fields")
        assert _raw_map(first, "private") is private is _raw_map(second, "private")
        assert (tuple(fields), tuple(private)) == keys
        fields["point"] = (1.0, 2.0, 3.0)
        private["_scroll_progress"] = 0.75
        return (
            first.point,
            second.point,
            first._scroll_progress,
            second._scroll_progress,
            fields["adapter_field"] is private["adapter_private"] is marker,
        )

    assert _compare(reference, scenario) == (
        (1.0, 2.0, 3.0),
        (1.0, 2.0, 3.0),
        0.75,
        0.75,
        True,
    )


class RecordingSequence:
    """Record only operations the application chooses to perform."""

    def __init__(self, trace, behavior="observe", reenter=None):
        self.trace = trace
        self.behavior = behavior
        self.reenter = reenter

    def _record(self, operation):
        self.trace.append(operation)
        if self.behavior == "raise":
            raise RuntimeError("unvalidated sequence must remain untouched")
        if self.behavior == "reenter":
            self.reenter()

    def __len__(self):
        self._record("len")
        return 2

    def __iter__(self):
        self._record("iter")
        return iter((1.0, 2.0))

    def __getitem__(self, index):
        self._record(("getitem", index))
        return (1.0, 2.0)[index]

    def __deepcopy__(self, memo):
        self.trace.append("deepcopy")
        memo[id(self)] = self
        return self


def _with_unvalidated_point(model_type, operation, sequence, source):
    if operation == "model_copy":
        return source.model_copy(update={"point": sequence})
    if operation == "construct":
        return model_type.model_construct(point=sequence)
    if operation == "restore":
        state = source.__getstate__()
        state["__dict__"] = dict(state["__dict__"], point=sequence)
        result = model_type.__new__(model_type)
        result.__setstate__(state)
        return result
    _raw_map(source, "fields")["point"] = sequence
    return copy.deepcopy(source) if operation == "deepcopy" else copy.copy(source)


@pytest.mark.parametrize(
    ("operation", "behavior"),
    [
        ("model_copy", "observe"),
        ("construct", "observe"),
        ("restore", "observe"),
        ("copy", "observe"),
        ("deepcopy", "observe"),
        ("model_copy", "raise"),
        ("construct", "raise"),
        ("restore", "raise"),
        ("model_copy", "reenter"),
    ],
)
def test_unvalidated_point_lifecycle_preserves_sequence_methods_and_errors(
    reference, operation, behavior
):
    def scenario(model_type):
        source = model_type()
        trace = []

        def reenter():
            trace.append("reenter")
            _raw_map(source, "fields")["adapter_reentry"] = True

        sequence = RecordingSequence(trace, behavior, reenter)
        try:
            result = _with_unvalidated_point(
                model_type, operation, sequence, source
            )
        except Exception as error:
            outcome = "error", type(error).__name__, str(error)
        else:
            outcome = "returned", _raw_map(result, "fields")["point"] is sequence
        return tuple(trace), outcome, "adapter_reentry" in _raw_map(source, "fields")

    expected = _compare(reference, scenario)
    assert expected[0] == (("deepcopy",) if operation == "deepcopy" else ())
    assert expected[1] == ("returned", True)
    assert expected[2] is False


@pytest.mark.parametrize("operation", ["copy", "deepcopy"])
def test_copy_provider_aliases_and_explicit_memo_preserve_pydantic_boundaries(
    reference, operation
):
    def scenario(model_type):
        source = model_type()
        trace = []
        sequence = RecordingSequence(trace)
        replacement = RecordingSequence(trace)
        fields = _raw_map(source, "fields")
        private = _raw_map(source, "private")
        fields["point"] = sequence
        private["adapter_provider"] = sequence
        if operation == "copy":
            result = copy.copy(source)
            expected = sequence
        else:
            result = copy.deepcopy(source, {id(sequence): replacement})
            expected = replacement
        copied_fields = _raw_map(result, "fields")
        copied_private = _raw_map(result, "private")
        return (
            copied_fields is not fields,
            copied_private is not private,
            copied_fields["point"] is copied_private["adapter_provider"] is expected,
            fields["point"] is private["adapter_provider"] is sequence,
            tuple(trace),
        )

    assert _compare(reference, scenario) == (True, True, True, True, ())


@pytest.mark.parametrize("kind", ["fields", "private"])
def test_saved_native_record_is_an_explicit_old_map_alias_after_generic_replace(kind):
    """A retained diagnostic record may own its exposed old map deliberately."""

    class Payload:
        pass

    model = Dims(ndim=3)
    record = (
        model._native_dictionary_record
        if kind == "fields"
        else model._native_flag_record
    )
    old_map = record.dictionary
    replacement = dict(old_map)
    old_map["adapter_payload"] = Payload()
    payload_reference = weakref.ref(old_map["adapter_payload"])
    _generic_edit(model, kind, "replace", replacement)
    assert _raw_map(model, kind) is replacement
    # The saved record's public map is a legitimate caller alias. Drop it and
    # the raw map without triggering the model's synchronization hooks.
    assert payload_reference() is not None
    del record, old_map
    gc.collect()
    assert payload_reference() is None


def test_dims_implementation_metadata_has_a_native_owner():
    model = Dims()
    fields = _raw_map(model, "fields")
    private = _raw_map(model, "private")
    point = (1.0, 2.0)
    fields["point"] = point
    private["_scroll_progress"] = 0.75
    assert model.point is point
    assert model._scroll_progress == 0.75
    assert _raw_map(model, "fields") is fields
    assert _raw_map(model, "private") is private
    try:
        storage = object.__getattribute__(model, "_native_storage")
    except AttributeError:
        return
    assert not isinstance(storage, dict), (
        "Dims implementation metadata still persists in a Python dictionary"
    )


@pytest.mark.parametrize("kind", ["fields", "private"])
def test_dictionary_subclass_self_cycle_collects_through_canonical_map(
    reference, kind
):
    def scenario(model_type):
        class Namespace(dict):
            pass

        model = model_type(ndim=3)
        namespace = Namespace(_raw_map(model, kind))
        namespace["adapter_owner"] = model
        _generic_edit(model, kind, "replace", namespace)
        assert _raw_map(model, kind) is namespace
        references = weakref.ref(model), weakref.ref(namespace)
        del model, namespace
        gc.collect()
        return tuple(target() is None for target in references)

    assert _compare(reference, scenario) == (True, True)


def test_slotted_subtype_and_callable_callback_cycle_collects(reference):
    def scenario(model_type):
        class Slotted(model_type):
            __slots__ = ("adapter_slot",)

        class Payload:
            def __init__(self, model):
                self.model = model

            def __call__(self, event):
                pass

        model = Slotted(ndim=3)
        slot_payload = Payload(model)
        callback = Payload(model)
        object.__setattr__(model, "adapter_slot", slot_payload)
        _raw_map(model, "fields")["adapter_owner"] = model
        _raw_map(model, "private")["adapter_owner"] = model
        model.events.point.connect(callback, ref="adapter-cycle")
        references = tuple(
            weakref.ref(value) for value in (model, slot_payload, callback)
        )
        del model, slot_payload, callback
        gc.collect()
        return tuple(target() is None for target in references)

    assert _compare(reference, scenario) == (True, True, True)


def test_subtype_finalizer_raw_map_reentry_and_resurrection_preserves_state(reference):
    def scenario(model_type):
        trace, resurrected = [], []

        class Finalizing(model_type):
            def __del__(self):
                fields = _raw_map(self, "fields")
                private = _raw_map(self, "private")
                trace.append(
                    ("finalize", fields["adapter_marker"], private["adapter_marker"])
                )
                fields["adapter_reentered"] = True
                private["adapter_reentered"] = True
                resurrected.append(self)

        class Payload:
            def __del__(self):
                trace.append(("payload", model_reference() is None))

        model = Finalizing(ndim=3)
        _raw_map(model, "fields")["adapter_marker"] = "field"
        _raw_map(model, "private")["adapter_marker"] = "private"
        _raw_map(model, "fields")["adapter_payload"] = Payload()
        model_reference = weakref.ref(
            model, lambda target: trace.append(("weak", target() is None))
        )
        del model
        gc.collect()
        assert trace == [("finalize", "field", "private")]
        recovered = resurrected.pop()
        assert model_reference() is recovered
        assert _raw_map(recovered, "fields")["adapter_reentered"] is True
        assert _raw_map(recovered, "private")["adapter_reentered"] is True
        del recovered
        gc.collect()
        return trace, model_reference() is None, resurrected

    assert _compare(reference, scenario) == (
        [("finalize", "field", "private"), ("weak", True), ("payload", True)],
        True,
        [],
    )


def test_native_metadata_traverses_current_records_without_obsolete_roots():
    """Inspect ownership without invoking a Dims synchronization getter."""

    model = Dims(ndim=3)
    storage = object.__getattribute__(model, "_native_storage")
    field_record = model._native_dictionary_record
    flag_record = model._native_flag_record
    old_projection = model._native_projection
    model_reference = weakref.ref(model)
    assert not isinstance(storage, dict)
    assert gc.is_tracked(storage)
    references = gc.get_referents(storage)
    assert sum(value is type(storage) for value in references) == 1
    assert sum(value is field_record for value in references) == 1
    assert sum(value is flag_record for value in references) == 1
    assert all(value is not model_reference for value in references)

    replacement = dict(_raw_map(model, "private"))
    _generic_edit(model, "private", "replace", replacement)
    # Generic mutation must publish the current record before any model read.
    references = gc.get_referents(storage)
    assert all(value is not flag_record for value in references)
    current_flags = [
        value for value in references if isinstance(value, type(flag_record))
    ]
    assert len(current_flags) == 1
    assert current_flags[0].dictionary is replacement
    assert flag_record.dictionary is not replacement

    old_fields = _raw_map(model, "fields")
    _generic_edit(model, "fields", "delete", None)
    references = gc.get_referents(storage)
    assert all(value is not old_projection for value in references)
    assert all(value is not old_fields for value in references)
    for value in references:
        if isinstance(value, type(field_record)):
            assert value.dictionary is not old_fields
