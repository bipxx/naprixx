"""Public model contracts for C++-owned dimensional fields."""

import copy
import ctypes
import gc
import importlib.util
import sys
import weakref
from decimal import Decimal
from pathlib import Path
from types import GeneratorType

import pint
import pytest
from pydantic import (
    ConfigDict,
    Field,
    ValidationError,
    field_validator,
    model_validator,
)

from napari.components import Dims
from napari.components.dims import RangeTuple

FIELDS = {
    "ndim",
    "ndisplay",
    "order",
    "axis_labels",
    "rollable",
    "range",
    "margin_left",
    "margin_right",
    "point",
    "last_used",
}


@pytest.fixture(scope="module")
def original():
    path = (
        Path(__file__).resolve().parents[1]
        / ".upstream/napari/src/napari/components/dims.py"
    )
    spec = importlib.util.spec_from_file_location("_original_owned_dims", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    yield module.Dims
    sys.modules.pop(spec.name, None)


def assert_native_storage(model):
    # The exact dict representation is controlled by a native observer; it
    # cannot independently mutate canonical numeric state on watcher builds.
    assert type(model.__dict__) is dict
    assert model.__dict__ is object.__getattribute__(model, "__dict__")
    record = model._native_dictionary_record
    assert record.dictionary is model.__dict__
    assert record.paused == 0
    assert model._native_boundary_depth == 0
    assert model._native_candidate is None


def test_fields_use_one_native_owner_through_assignments():
    model = Dims(ndim=3)
    owner = model._native_state
    operations = (
        ("range", ((-10, 20, 0.5),) * 3),
        ("point", (4, 5, 6)),
        ("margin_left", (1, 2, 3)),
        ("order", (2, 1, 0)),
        ("ndisplay", 3),
        ("ndim", 5),
        ("axis_labels", ("a", "b")),
    )
    for name, value in operations:
        setattr(model, name, value)
        assert model._native_state is owner
        assert_native_storage(model)
    assert all(isinstance(value, RangeTuple) for value in model.range)


@pytest.mark.parametrize(
    "operation", [copy.copy, copy.deepcopy, lambda value: value.model_copy()]
)
def test_model_copies_own_independent_state(operation):
    model = Dims(ndim=3)
    cloned = operation(model)
    assert cloned._native_state is not model._native_state
    assert_native_storage(cloned)
    cloned.point = (1, 1, 1)
    assert model.point == (0, 0, 0)
    assert cloned.current_step == (1, 1, 1)


@pytest.mark.parametrize("field", sorted(FIELDS))
def test_committed_field_object_identity_matches_baseline(original, field):
    for base in (Dims, original):
        model = base(ndim=3)
        value = getattr(model, field)
        assert getattr(model, field) is value
        assert model.__dict__[field] is value
        old_point = model.point
        model.point = (1, 1, 1)
        assert model.point is model.point
        assert model.__dict__["point"] is model.point
        assert old_point == (0, 0, 0)


def test_field_representations_cannot_mutate_native_state():
    model = Dims(ndim=3)
    representation = model._native_projection
    with pytest.raises(TypeError):
        representation["point"] = (1, 1, 1)
    assert model.point == model._native_state.point == (0, 0, 0)
    model.point = (1, 1, 1)
    assert model.point == model._native_state.point == (1, 1, 1)
    assert representation["point"] == (0, 0, 0)
    assert_native_storage(model)


@pytest.mark.parametrize("operation", ["set", "update", "union", "c_set", "c_update"])
def test_live_dictionary_writes_preserve_aliases_without_events(original, operation):
    observations = []
    for base in (Dims, original):
        model = base(ndim=3)
        owner = model._native_state if base is Dims else None
        fields = model.__dict__
        field_set = set(model.__pydantic_fields_set__)
        events = []
        model.events.point.connect(
            lambda event, events=events: events.append(event.type)
        )
        point = (0.5, 1.0, 1.5)
        if operation == "set":
            dict.__setitem__(fields, "point", point)
        elif operation == "update":
            dict.update(fields, {"point": point})
        elif operation == "union":
            fields |= {"point": point}
        else:
            function = getattr(
                ctypes.pythonapi,
                "PyDict_SetItem" if operation == "c_set" else "PyDict_Update",
            )
            function.restype = ctypes.c_int
            function.argtypes = [ctypes.py_object] * (3 if operation == "c_set" else 2)
            arguments = (
                (fields, "point", point)
                if operation == "c_set"
                else (fields, {"point": point})
            )
            assert function(*arguments) == 0
        if owner is not None:
            assert owner.point == point
        assert type(fields) is dict
        assert fields is model.__dict__ is vars(model)
        assert model.point is fields["point"] is point
        assert model.__pydantic_fields_set__ == field_set
        assert events == []
        observations.append((model.current_step, model.model_dump(), list(fields)))
        model.point = (1, 0, 0)
        assert fields is not model.__dict__
        fields["point"] = (10.0, 10.0, 10.0)
        assert model.point == (1, 0, 0)
        if owner is not None:
            assert owner.point == (1, 0, 0)
    assert observations[0] == observations[1]


def test_live_dictionary_views_clear_clone_and_reinsertion_match_baseline(original):
    observations = []
    for base in (Dims, original):
        model = base(ndim=3)
        owner = model._native_state if base is Dims else None
        fields = model.__dict__
        keys, values, items = fields.keys(), fields.values(), fields.items()
        saved = fields.copy()
        iterator = iter(fields)
        fields.pop("point")
        with pytest.raises(RuntimeError):
            next(iterator)
        assert "point" not in keys
        assert not hasattr(model, "point")
        point = (1.0, 0.0, 0.0)
        assert fields.setdefault("point", point) is point
        observations.append((list(keys), list(values), list(items)))
        fields.clear()
        assert not keys and not hasattr(model, "point")
        # An empty dict update takes CPython's bulk-clone watcher path.
        dict.update(fields, saved)
        if owner is not None:
            assert owner.point == saved["point"]
        assert model.point is saved["point"]
        assert model.current_step == (0, 0, 0)
        observations.append((list(keys), list(values), list(items)))
    assert observations[:2] == observations[2:]


def test_restored_dictionary_aliases_update_independent_native_owners(original):
    observations = []
    for base in (Dims, original):
        source = base(ndim=3)
        saved = source.__getstate__()
        restored = base.__new__(base)
        restored.__setstate__(saved)
        second = base.__new__(base)
        second.__setstate__(saved)
        fields = saved["__dict__"]
        assert source.__dict__ is restored.__dict__ is second.__dict__ is fields
        owners = (
            [m._native_state for m in (source, restored, second)]
            if base is Dims
            else []
        )
        fields["point"] = (1.0, 1.0, 1.0)
        for owner in owners:
            assert owner.point == (1, 1, 1)
        restored.point = (0, 0, 0)
        fields["point"] = (2.0, 1.0, 0.0)
        observations.append(tuple(m.point for m in (source, restored, second)))
        if owners:
            assert len({id(owner) for owner in owners}) == 3
            assert [owner.point for owner in owners] == list(observations[-1])
    assert observations[0] == observations[1]


@pytest.mark.parametrize("generic", [False, True])
def test_dictionary_replacement_adopts_existing_fields(original, generic):
    observations = []
    for base in (Dims, original):
        model = base(ndim=3)
        owner = model._native_state if base is Dims else None
        old_fields = model.__dict__
        fields = dict(old_fields, point=(2.0, 1.0, 0.0))
        if generic:
            object.__setattr__(model, "__dict__", fields)
            # Generic low-level replacement is adopted on public model access.
            assert model.__dict__ is fields
        else:
            model.__dict__ = fields
        if owner is not None:
            assert owner.point == fields["point"]
        observations.append((model.point, model.current_step, model.model_dump()))
        old_fields["point"] = (9.0, 9.0, 9.0)
        assert model.point == (2, 1, 0)
        if owner is not None:
            assert owner.point == (2, 1, 0)
    assert observations[0] == observations[1]


def test_live_dictionary_provider_cycles_are_visible_to_gc():
    class Provider(list):
        pass

    def make_cycle():
        model = Dims(ndim=3)
        provider = Provider([0, 0, 0])
        provider.model = model
        model.__dict__["point"] = provider
        return tuple(
            weakref.ref(value)
            for value in (
                model,
                provider,
                model._native_state,
                model._native_dictionary_record,
            )
        )

    references = make_cycle()
    gc.collect()
    assert all(reference() is None for reference in references)


@pytest.mark.parametrize("name", ["_play_ready", "_scroll_progress", "_validating"])
@pytest.mark.parametrize("value", [False, 0, -2, 0.25, -0.0, float("inf")])
def test_native_application_flags_preserve_primitive_type_and_identity(
    original, name, value
):
    actual, expected = Dims(ndim=3), original(ndim=3)
    owner = actual._native_flags
    for model in (actual, expected):
        setattr(model, name, value)
        assert getattr(model, name) is value
        assert model.__pydantic_private__[name] is value
    assert actual._native_flags is owner
    native = getattr(owner, name[1:])
    assert type(native) is type(value)
    assert native == value
    actual.__pydantic_private__[name] = 3.5
    assert getattr(owner, name[1:]) == 3.5
    assert actual._native_flag_record.dictionary is actual.__pydantic_private__


@pytest.mark.parametrize(
    "operation", [copy.copy, copy.deepcopy, lambda value: value.model_copy()]
)
def test_flag_copies_are_independent_but_restoration_preserves_private_aliases(
    original, operation
):
    observations = []
    for base in (Dims, original):
        source = base(ndim=3)
        source._scroll_progress = 0.25
        copied = operation(source)
        copied._scroll_progress = 0.5
        saved = source.__getstate__()
        first, second = base.__new__(base), base.__new__(base)
        first.__setstate__(saved)
        second.__setstate__(saved)
        first._scroll_progress = 0.75
        observations.append(
            tuple(m._scroll_progress for m in (source, copied, first, second))
        )
        if base is Dims:
            assert source._native_flags is not copied._native_flags
            assert first._native_flags is second._native_flags
            assert first._native_flag_record is second._native_flag_record
            assert first._native_flags is not source._native_flags
            assert (
                tuple(
                    m._native_flags.scroll_progress
                    for m in (source, copied, first, second)
                )
                == observations[-1]
            )
    assert observations[0] == observations[1] == (0.25, 0.5, 0.75, 0.75)


@pytest.mark.parametrize("name", ["_play_ready", "_scroll_progress", "_validating"])
def test_flag_deletion_and_private_dictionary_replacement_match_baseline(
    original, name
):
    for base in (Dims, original):
        model = base(ndim=3)
        old = model.__pydantic_private__
        owner = model._native_flags if base is Dims else None
        delattr(model, name)
        assert not hasattr(model, name)
        if owner is not None:
            with pytest.raises(AttributeError, match="Deleted application flag"):
                getattr(owner, name[1:])
        replacement = dict(old)
        replacement[name] = 0.5
        model.__pydantic_private__ = replacement
        assert getattr(model, name) == 0.5
        old[name] = 7
        assert getattr(model, name) == 0.5
        if owner is not None:
            assert model._native_flags is not owner
            assert getattr(model._native_flags, name[1:]) == 0.5


def test_flag_provider_truth_aliases_and_cycles_match_baseline(original):
    class Provider:
        def __init__(self):
            self.calls = 0

        def __bool__(self):
            self.calls += 1
            return True

    observations = []
    for base in (Dims, original):
        model = base()
        provider = Provider()
        provider.model = model
        model._validating = provider
        assert model._validating is provider
        model.point = (1, 1)
        observations.append((provider.calls, model.point))
        if base is Dims:
            with pytest.raises(TypeError, match="Python provider"):
                _ = model._native_flags.validating
            flag_reference = weakref.ref(model._native_flags)
            record_reference = weakref.ref(model._native_flag_record)
        references = (weakref.ref(model), weakref.ref(provider))
        del model, provider
        gc.collect()
        assert all(reference() is None for reference in references)
        if base is Dims:
            assert flag_reference() is record_reference() is None
    assert observations[0] == observations[1]


@pytest.mark.parametrize("existing", [False, True])
def test_restoration_preserves_source_and_reusable_state(original, existing):
    traces = []
    for base in (Dims, original):
        source = base(ndim=3)
        saved = source.__getstate__()
        restored = base(ndim=4) if existing else base.__new__(base)
        restored.__setstate__(saved)
        second = base.__new__(base)
        second.__setstate__(saved)
        restored.point = (1, 1, 1)
        second.point = (2, 0, 0)
        traces.append(
            (source.point, restored.point, second.point, dict(saved["__dict__"]))
        )
        assert restored.__pydantic_private__ is saved["__pydantic_private__"]
        assert second.__pydantic_private__ is restored.__pydantic_private__
        restored._scroll_progress = 0.25
        assert second._scroll_progress == 0.25
        assert source._scroll_progress == 0
        if base is Dims:
            assert len({id(m._native_state) for m in (source, restored, second)}) == 3
            for model in (source, restored, second):
                assert_native_storage(model)
    assert traces[0] == traces[1]


def test_restoration_keeps_trusted_provider_aliases_and_deleted_fields(original):
    observations = []
    for base in (Dims, original):
        provider = [0, 1, 0]
        source = base(ndim=3).model_copy(update={"point": provider})
        del source.axis_labels
        saved = source.__getstate__()
        restored = base.__new__(base)
        restored.__setstate__(saved)
        assert restored.point is source.point is provider
        provider[0] = 2
        observations.append(
            (restored.current_step, restored.model_dump(warnings=False))
        )
        assert not hasattr(restored, "axis_labels")
        assert restored.__pydantic_fields_set__ == source.__pydantic_fields_set__
        if base is Dims:
            assert restored._native_state is not source._native_state
            assert_native_storage(restored)
    assert observations[0] == observations[1]


def test_plugin_validator_keeps_committed_value_alias(original):
    for base in (Dims, original):

        class Plugin(base):
            observed: list = []

            @model_validator(mode="after")
            def capture_point(self):
                self.observed.append(self.point)
                return self

        model = Plugin(ndim=3)
        assert model.observed[-1] is model.point
        model.point = (1, 1, 1)
        assert model.observed[-1] is model.point


@pytest.mark.parametrize("previous", [False, True, None, "ready", Decimal("1"), []])
def test_native_validation_context_preserves_nested_values(original, previous):
    for base in (Dims, original):
        model = base(ndim=3)
        model._validating = previous
        context = model._validating_ctx()
        assert type(context).__name__ == "_GeneratorContextManager"
        assert isinstance(context.gen, GeneratorType)
        assert model._validating is previous
        with context as entered:
            assert entered is None
            assert model._validating is True
            with model._validating_ctx():
                assert model._validating is True
            assert model._validating is True
        assert model._validating is previous


@pytest.mark.parametrize(
    "error_type",
    [
        ValueError,
        RuntimeError,
        StopIteration,
        KeyboardInterrupt,
        GeneratorExit,
    ],
)
def test_native_validation_context_propagates_original_exception(original, error_type):
    for base in (Dims, original):
        model = base()
        error = error_type("body")
        with pytest.raises(error_type) as caught:
            with model._validating_ctx():
                raise error
        assert caught.value is error
        assert model._validating is False


def test_native_validation_context_decorator_recreates_context(original):
    for base in (Dims, original):
        model = base()
        observed = []

        @model._validating_ctx()
        def callback(value, model=model, observed=observed):
            observed.append(model._validating)
            return value + 1

        assert callback(1) == 2
        assert callback(2) == 3
        assert observed == [True, True]
        assert model._validating is False


@pytest.mark.parametrize("cycle", [False, True])
def test_abandoned_validation_context_restores_and_releases_model(original, cycle):
    for base in (Dims, original):
        model = base()
        context = model._validating_ctx()
        context.__enter__()
        iterator_ref = weakref.ref(context.gen)
        if cycle:
            context.cycle = context
        assert model._validating is True
        del context
        gc.collect()
        assert iterator_ref() is None
        assert model._validating is False
        model_ref = weakref.ref(model)
        del model
        gc.collect()
        assert model_ref() is None


@pytest.mark.parametrize("operation", ["send", "throw", "close"])
def test_validation_iterator_protocol_matches_baseline(original, operation):
    observations = []
    for base in (Dims, original):
        model = base()
        iterator = model._validating_ctx().gen
        assert iter(iterator) is iterator
        assert next(iterator) is None
        assert model._validating is True
        if operation == "send":
            with pytest.raises(StopIteration):
                iterator.send(3)
        elif operation == "throw":
            error = ValueError("injected")
            with pytest.raises(ValueError) as caught:
                iterator.throw(error)
            assert caught.value is error
        else:
            assert iterator.close() is None
        assert model._validating is False
        with pytest.raises(StopIteration):
            next(iterator)
        observations.append(model.point)
    assert observations[0] == observations[1]


@pytest.mark.parametrize("failure", ["True", "False"])
def test_validation_context_plugin_setter_failure_matches_baseline(original, failure):
    observations = []
    for base in (Dims, original):

        class Plugin(base):
            _fail_context: str | None = None

            def __setattr__(self, name, value):
                super().__setattr__(name, value)
                if name == "_validating" and self._fail_context == str(value):
                    raise RuntimeError(f"flag {value}")

        model = Plugin()
        model._fail_context = failure
        with pytest.raises(RuntimeError) as caught:
            with model._validating_ctx():
                raise ValueError("body")
        error = caught.value
        observations.append(
            (
                model._validating,
                str(error),
                type(error.__context__),
                str(error.__context__) if error.__context__ is not None else None,
            )
        )
    assert observations[0] == observations[1]


def test_public_views_and_serialization_match_original(original):
    kwargs = {"ndim": 4, "point": (1, 2, 0, 0), "axis_labels": ("x", "y")}
    actual, expected = Dims(**kwargs), original(**kwargs)
    assert dict(actual) == dict(expected)
    assert actual.__dict__ == expected.__dict__
    assert repr(actual) == repr(expected)
    for options in (
        {},
        {"exclude_unset": True},
        {"exclude_defaults": True},
        {"exclude_none": True},
    ):
        assert actual.model_dump(**options) == expected.model_dump(**options)
        assert actual.model_dump_json(**options) == expected.model_dump_json(**options)
    assert (
        Dims.model_validate_json(actual.model_dump_json()).model_dump()
        == expected.model_dump()
    )
    assert_native_storage(actual)


@pytest.mark.parametrize(
    "name,value",
    [
        ("order", (0, 0, 1, 2)),
        ("point", ("not a number",)),
        ("range", ((3, 1, 1),)),
    ],
)
def test_failed_assignment_preserves_original_partial_state(original, name, value):
    actual, expected = Dims(ndim=4), original(ndim=4)
    owner = actual._native_state
    for model in (actual, expected):
        with pytest.raises(ValidationError):
            setattr(model, name, value)
    assert actual.model_dump() == expected.model_dump()
    assert actual.displayed == expected.displayed
    assert actual._native_state is owner
    assert_native_storage(actual)
    actual.order = (3, 2, 1, 0)
    expected.order = (3, 2, 1, 0)
    assert actual.model_dump() == expected.model_dump()


def test_callbacks_see_committed_state_and_can_reenter(original):
    models = [Dims(ndim=3), original(ndim=3)]
    traces = [[], []]
    for model, trace in zip(models, traces, strict=True):

        def receive(event, model=model, trace=trace):
            trace.append((event.type, model.point, model.current_step))
            if model.point == (1, 1, 1):
                model.point = (2, 1, 1)

        model.events.point.connect(receive)
        model.point = (1, 1, 1)
    assert traces[0] == traces[1]
    assert models[0].model_dump() == models[1].model_dump()
    assert_native_storage(models[0])


def test_schema_subclasses_and_additional_field_validators(original):
    def subclass(base):
        class Extension(base):
            model_config = ConfigDict(extra="allow")
            marker: int = 0
            observations: list = []

            @field_validator("marker")
            def use_other_fields(cls, value, info):
                if value:
                    info.data["point"] = (1.0,) * info.data["ndim"]
                return value

            @model_validator(mode="after")
            def check_computed_fields(self):
                if self.marker:
                    # validate_assignment exposes a copy through info.data;
                    # changing it does not update the actual fields upstream.
                    assert self.current_step == (0,) * self.ndim
                self.observations.append((self.point, self.current_step))
                return self

        return Extension

    actual = subclass(Dims)(ndim=3)
    expected = subclass(original)(ndim=3)
    actual.marker = expected.marker = 4
    assert actual.model_dump() == expected.model_dump()
    actual = actual.model_copy(update={"custom": "plugin extra"})
    expected = expected.model_copy(update={"custom": "plugin extra"})
    assert actual.custom == expected.custom
    assert actual.model_dump() == expected.model_dump()
    assert_native_storage(actual)


def test_failed_normalization_retains_primary_field_updates(original):
    models = [
        base(ndim=3).model_copy(update={"order": (0, 0, 1), "point": (99, 1, 0)})
        for base in (Dims, original)
    ]
    for model in models:
        with pytest.raises(ValidationError, match="Invalid ordering"):
            model.ndim = 5
    assert models[0].model_dump() == models[1].model_dump()
    assert models[0].point == (0, 0, 2, 1, 0)
    assert_native_storage(models[0])


def test_unvalidated_assignment_keeps_mutable_provider_identity(original):
    def subclass(base):
        class Unchecked(base):
            model_config = ConfigDict(validate_assignment=False)

        return Unchecked

    values = [1, 0, 0]
    models = [subclass(base)(ndim=3) for base in (Dims, original)]
    for model in models:
        model.point = values
        assert model.point is values
    values[0] = 2
    assert models[0].current_step == models[1].current_step == (2, 0, 0)
    assert_native_storage(models[0])


def test_model_construct_preserves_unvalidated_fields_and_copies(original):
    values = [99, 0, 0]
    actual = Dims.model_construct(ndim=3, point=values)
    expected = original.model_construct(ndim=3, point=values)
    assert actual.point is expected.point is values
    assert actual.model_dump(warnings=False) == expected.model_dump(warnings=False)
    assert actual.__pydantic_fields_set__ == expected.__pydantic_fields_set__
    assert_native_storage(actual)
    cloned = actual.model_copy()
    assert cloned._native_state is not actual._native_state
    assert cloned.point is values
    assert_native_storage(cloned)


@pytest.mark.parametrize("validate_assignment", [True, False])
def test_constructed_models_keep_native_owner_through_assignment(
    original, validate_assignment
):
    def subclass(base):
        class Constructed(base):
            model_config = ConfigDict(validate_assignment=validate_assignment)

        return Constructed

    kwargs = {"ndim": 3, "range": (RangeTuple(0, 2, 1),) * 3}
    actual, expected = [
        subclass(base).model_construct(**kwargs) for base in (Dims, original)
    ]
    state = actual._native_state
    values = [1, 1, 1]
    actual.point = expected.point = values
    assert actual.model_dump(warnings=False) == expected.model_dump(warnings=False)
    assert actual.current_step == expected.current_step
    assert actual._native_state is state
    if not validate_assignment:
        assert actual.point is expected.point is values
    assert_native_storage(actual)


def test_unit_adapter_accepts_clearing_value(original):
    actual, expected = [
        base(ndim=3, units=(pint.Unit("pixel"),) * 3) for base in (Dims, original)
    ]
    actual.units = expected.units = None
    assert actual.units is expected.units is None
    assert actual.model_dump() == expected.model_dump()
    assert_native_storage(actual)


def test_trusted_update_preserves_external_input_identity_and_mutation(original):
    input_values = [1, 0, 0]
    actual = Dims(ndim=3).model_copy(update={"point": input_values})
    expected = original(ndim=3).model_copy(update={"point": input_values})
    assert actual.point is input_values and expected.point is input_values
    input_values[0] = 2
    assert actual.current_step == expected.current_step == (2, 0, 0)
    assert_native_storage(actual)
    for model in (actual.model_copy(deep=True), copy.deepcopy(actual)):
        assert isinstance(model.point, list)
        assert model.point is not input_values
        model.point[0] = 0
        assert model.current_step == (0, 0, 0)
        assert actual.current_step == (2, 0, 0)
        assert_native_storage(model)


def test_unvalidated_opaque_values_are_owned_by_gc_visible_adapter(original):
    opaque = object()
    actual = Dims().model_copy(update={"point": opaque})
    expected = original().model_copy(update={"point": opaque})
    assert actual.point is expected.point is opaque
    assert_native_storage(actual)


PROPERTIES = (
    "nsteps",
    "current_step",
    "margin_left_step",
    "margin_right_step",
    "thickness",
    "thickness_step",
    "displayed",
    "not_displayed",
    "displayed_order",
)


def property_observation(model, name):
    try:
        result = getattr(model, name)
        return {"value": result, "type": type(result).__name__}
    except Exception as failure:
        return {"error": type(failure).__name__, "message": str(failure)}


@pytest.mark.parametrize("name", sorted(FIELDS))
def test_deleted_field_access_serialization_copy_and_recovery(original, name):
    actual, expected = Dims(ndim=3), original(ndim=3)
    value = getattr(expected, name)
    owner = actual._native_state
    delattr(actual, name)
    delattr(expected, name)
    assert not hasattr(actual, name) and not hasattr(expected, name)
    assert actual.model_dump() == expected.model_dump()
    assert actual.model_dump_json() == expected.model_dump_json()
    assert actual.__pydantic_fields_set__ == expected.__pydantic_fields_set__
    for prop in PROPERTIES:
        assert property_observation(actual, prop) == property_observation(
            expected, prop
        )
    for clone in (copy.copy(actual), copy.deepcopy(actual), actual.model_copy()):
        assert not hasattr(clone, name)
        assert clone.model_dump() == expected.model_dump()
        assert clone._native_state is not owner
        assert_native_storage(clone)
    for model in (actual, expected):
        with pytest.raises(AttributeError):
            delattr(model, name)
        setattr(model, name, value)
    assert actual.model_dump() == expected.model_dump()
    assert actual._native_state is owner
    assert_native_storage(actual)


def test_frozen_field_deletion_is_checked_before_native_mutation(original):
    def subclass(base):
        class FrozenDimension(base):
            ndim: int = Field(default=3, frozen=True)

        return FrozenDimension

    actual, expected = [subclass(base)() for base in (Dims, original)]
    for model in (actual, expected):
        with pytest.raises(ValidationError, match="Field is frozen"):
            del model.ndim
    assert actual.ndim == expected.ndim == 3
    assert actual.model_dump() == expected.model_dump()
    assert_native_storage(actual)


@pytest.mark.parametrize(
    "field,value",
    [
        ("point", object()),
        ("point", ("bad", 0, 0)),
        ("point", None),
        ("order", [2, 0, 1]),
        ("ndisplay", -1),
        ("ndisplay", 100),
        ("range", (RangeTuple(0.0, 1e40, 1.0),) * 3),
        ("range", (RangeTuple(0.0, float("nan"), 1.0),) * 3),
        ("point", (float("inf"), 0.0, 0.0)),
    ],
)
def test_trusted_provider_properties_use_only_their_dependencies(
    original, field, value
):
    actual, expected = [
        base(ndim=3).model_copy(update={field: value}) for base in (Dims, original)
    ]
    for name in PROPERTIES:
        assert property_observation(actual, name) == property_observation(
            expected, name
        )
    assert_native_storage(actual)


@pytest.mark.parametrize("element_type", [int, Decimal])
def test_plugin_field_schema_preserves_declared_numeric_types(original, element_type):
    def subclass(base):
        class PluginSchema(base):
            point: tuple[element_type, ...] = ()

        return PluginSchema

    actual, expected = [subclass(base)(point=(1, 2)) for base in (Dims, original)]
    assert [type(value) for value in actual.point] == [
        type(value) for value in expected.point
    ]
    assert actual.model_dump() == expected.model_dump()
    assert actual.model_dump_json() == expected.model_dump_json()
    actual.point = expected.point = (2, 1)
    assert actual.point == expected.point
    assert property_observation(actual, "current_step") == property_observation(
        expected, "current_step"
    )
    for value in actual.point:
        assert isinstance(value, element_type)
    assert_native_storage(actual)


def test_plugin_computed_property_overrides_are_used(original):
    def subclass(base):
        class PluginProperties(base):
            @property
            def displayed(self):
                return (2, 0, 1)

            @property
            def margin_left_step(self):
                return (1, 2, 3)

        return PluginProperties

    actual, expected = [subclass(base)(ndim=3) for base in (Dims, original)]
    assert actual.displayed_order == expected.displayed_order == (1, 2, 0)
    assert actual.thickness_step == expected.thickness_step == (1, 2, 3)


def test_plugin_replaced_validator_still_adopts_native_state(original):
    def subclass(base):
        class OptionalPoint(base):
            point: tuple[float, ...] | None = None

            @field_validator("point", mode="before")
            def _as_tuple(value):
                return None if value is None else tuple(value)

            @model_validator(mode="after")
            def _check_dims(self):
                return self

        return OptionalPoint

    actual, expected = [subclass(base)() for base in (Dims, original)]
    assert actual.point is expected.point is None
    assert actual.model_dump() == expected.model_dump()
    assert actual.displayed == expected.displayed
    assert property_observation(actual, "current_step") == property_observation(
        expected, "current_step"
    )
    assert actual._native_state is not None
    assert_native_storage(actual)


@pytest.mark.parametrize(
    "name,value",
    [
        ("current_step", (1, 1)),
        ("margin_left_step", (1, 2)),
        ("margin_right_step", (2, 1)),
        ("thickness", (3, 5)),
        ("thickness_step", (3, 5)),
        ("nsteps", (5, 5)),
        ("nsteps", (1, 1)),
        ("current_step", ("bad", 1)),
        ("thickness", None),
    ],
)
def test_native_computed_setters_preserve_events_and_errors(original, name, value):
    models = [Dims(), original()]
    traces = [[], []]
    errors = []
    for model, trace in zip(models, traces, strict=True):

        def receive(event, trace=trace):
            trace.append((event.type, event.value))

        model.events.connect(receive)
        try:
            setattr(model, name, value)
        except Exception as failure:
            errors.append((type(failure).__name__, str(failure)))
        else:
            errors.append(None)
    assert models[0].model_dump() == models[1].model_dump()
    assert errors[0] == errors[1]
    assert traces[0] == traces[1]
    assert_native_storage(models[0])


@pytest.mark.parametrize(
    "method,args",
    [
        ("reset", ()),
        ("transpose", ()),
        ("roll", ()),
        ("_focus_up", ()),
        ("_focus_down", ()),
        ("_go_to_center_step", ()),
        ("_increment_dims_right", ()),
        ("_increment_dims_left", (-1,)),
        ("_increment_dims_right", (99,)),
        ("set_current_step", (1, 1)),
        ("set_point", (1, None)),
        ("set_point", (None, 1)),
        ("set_axis_label", (2, "z")),
        ("set_range", (0, (-2, 4, 1))),
    ],
)
def test_native_navigation_and_axis_updates_match_original(original, method, args):
    models = [
        base(ndim=4, point=(1, 0, 1, 0), last_used=0) for base in (Dims, original)
    ]
    traces, errors = [[], []], []
    for model, trace in zip(models, traces, strict=True):

        def receive(event, trace=trace):
            trace.append((event.type, event.value))

        model.events.connect(receive)
        try:
            getattr(model, method)(*args)
        except Exception as failure:
            errors.append((type(failure).__name__, str(failure)))
        else:
            errors.append(None)
    assert models[0].model_dump() == models[1].model_dump()
    assert errors[0] == errors[1]
    assert traces[0] == traces[1]
    assert_native_storage(models[0])


def test_increment_preserves_plugin_set_current_step_hook(original):
    def subclass(base):
        class MethodHook(base):
            calls: list = []

            def set_current_step(self, axis, value):
                self.calls.append((axis, value))
                super().set_current_step(axis, value)

        return MethodHook

    actual, expected = [subclass(base)(ndim=3) for base in (Dims, original)]
    actual._increment_dims_right()
    expected._increment_dims_right()
    assert actual.calls == expected.calls == [(0, 1)]
    assert actual.model_dump() == expected.model_dump()


def test_reset_observes_dimension_changes_from_callbacks(original):
    models = [
        base(ndim=3, range=((0, 10, 1),) * 3, point=(1, 2, 3))
        for base in (Dims, original)
    ]
    traces = [[], []]
    for model, trace in zip(models, traces, strict=True):

        def receive(event, model=model, trace=trace):
            trace.append((event.type, model.ndim, model.point))
            if event.type == "range" and model.ndim == 3:
                model.ndim = 4

        model.events.connect(receive)
        model.reset()
    assert models[0].model_dump() == models[1].model_dump()
    assert traces[0] == traces[1]
    assert models[0].ndim == 4


def test_normalization_reads_point_changed_by_plugin_range_hook(original):
    def subclass(base):
        class RangeHook(base):
            @model_validator(mode="after")
            def change_pending_point(self):
                if self._validating and self.point == (1.0, 2.0):
                    self.point = (1.5, 1.0)
                return self

        return RangeHook

    actual, expected = [subclass(base)(point=(1, 2)) for base in (Dims, original)]
    assert actual.point == expected.point == (1.5, 1.0)
    assert actual.model_dump() == expected.model_dump()


@pytest.mark.parametrize(
    "values,length,pad",
    [
        ((1, 2), 2, None),
        ((1, 2), 0, 0),
        ((1, 2), 4, None),
        ([1, 2], 1, 0),
        ([1, 2], 4, 0),
        (None, 2, 0),
    ],
)
def test_native_ensure_len_preserves_protocol_and_identity(
    original, values, length, pad
):
    from napari.components.dims import ensure_len

    reference = sys.modules[original.__module__].ensure_len
    results = []
    for operation in (ensure_len, reference):
        try:
            result = operation(values, length, pad)
            results.append((result, type(result), result is values))
        except Exception as failure:
            results.append((type(failure), str(failure)))
    assert results[0] == results[1]
