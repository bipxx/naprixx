"""Default/reset/update/equality/context contracts for native model helpers.

The reference model module is unmodified; it shares dependency adapters with
the fork. Annotation algorithms have a separate pinned-module test suite.
"""

import gc
import importlib.util
import sys
import weakref
from pathlib import Path
from types import SimpleNamespace

import pytest
from pydantic import ConfigDict, Field, ValidationError

from napari.utils.events import evented_model as actual


@pytest.fixture(scope="module")
def reference():
    path = (
        Path(__file__).resolve().parents[1]
        / ".upstream/napari/src/napari/utils/events/evented_model.py"
    )
    name = "_original_native_model_helpers"
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    yield module
    sys.modules.pop(name, None)


def model_types(module, frozen=False):
    class Child(module.EventedModel):
        x: int = Field(3, ge=0)
        fixed: int = Field(4, frozen=True)

    class Parent(module.EventedModel):
        child: Child = Field(default_factory=Child)
        y: int = 5
        hidden: int = Field(8, exclude=True)
        model_config = ConfigDict(frozen=frozen)

    return Child, Parent


@pytest.mark.parametrize("frozen", [False, True])
def test_nested_defaults_reset_sources_and_frozen_fields(reference, frozen):
    observations = []

    def scenario(module):
        child_type, parent_type = model_types(module, frozen)
        model = parent_type()
        child = model.child
        child.x = 12
        child.__dict__["fixed"] = 9
        model.__dict__.update(y=17, hidden=20)
        assert model.events.source is model
        assert child.events.source is child
        assert child.events.x.source is child
        assert "fixed" not in child.events.emitters
        assert module.get_defaults(parent_type) == module.get_defaults(model)
        model.reset()
        assert model.child is child
        observations.append(
            (model.model_dump(), model.hidden, child.fixed, list(model.events.emitters))
        )
        assert child_type.model_fields["fixed"].frozen

    scenario(reference)
    scenario(actual)
    assert observations[0] == observations[1]


def test_initialization_preserves_property_sequence_length_protocol(reference):
    def scenario(module):

        class Model(module.EventedModel):
            value: int = 0

        class Properties(dict):
            def __len__(self):
                raise ValueError("property sequence")

        Model.__properties__ = Properties(Model.__properties__)
        with pytest.raises(ValueError, match="property sequence"):
            Model()

    scenario(reference)
    scenario(actual)


@pytest.mark.parametrize("factory_error", [TypeError, RuntimeError])
def test_default_factory_retry_and_exclusion(reference, factory_error):
    observations = []

    def scenario(module):
        calls = []

        class AdapterField:
            exclude = False
            annotation = int

            def get_default(self, **kwargs):
                calls.append(kwargs)
                if not kwargs:
                    return None
                if "validated_data" in kwargs:
                    raise factory_error("factory boundary")
                return 13

        excluded = SimpleNamespace(exclude=True)
        schema = SimpleNamespace(
            model_fields={"value": AdapterField(), "excluded": excluded}
        )
        try:
            result = module.get_defaults(schema)
        except Exception as error:
            result = type(error), str(error)
        observations.append((result, calls))

    scenario(reference)
    scenario(actual)
    assert observations[0] == observations[1]


def test_nested_update_recurse_truth_and_child_override(reference):
    observations = []

    def scenario(module):
        calls = []
        child_type, parent_type = model_types(module)

        class Child(child_type):
            def update(self, values, recurse=True):
                calls.append(("child", values, recurse is flag))
                super().update(values, recurse=recurse)

        class Flag:
            def __bool__(self):
                calls.append("truth")
                return True

        flag = Flag()
        model = parent_type(child=Child())
        child = model.child
        model.update({"y": 7, "child": {"x": 11}}, recurse=flag)
        assert model.child is child
        observations.append((model.model_dump(), calls))

    scenario(reference)
    scenario(actual)
    assert observations[0] == observations[1]


@pytest.mark.parametrize("row", [(), ("y",), ("y", 7, "extra"), None, 4])
def test_malformed_update_items_restore_event_blocking(reference, row):
    observations = []

    def scenario(module):
        _, parent_type = model_types(module)
        model = parent_type()
        changes = []

        def callback(event):
            changes.append(event.value)

        model.events.y.connect(callback)

        class Values(dict):
            def items(self):
                yield ("y", 6)
                yield row

        with pytest.raises((ValueError, TypeError)) as caught:
            model.update(Values())
        model.y = 9
        observations.append((type(caught.value), str(caught.value), model.y, changes))

    scenario(reference)
    scenario(actual)
    assert observations[0] == observations[1]


def test_partial_validation_failure_and_recovery(reference):
    observations = []

    def scenario(module):
        _, parent_type = model_types(module)
        model = parent_type()
        with pytest.raises(ValidationError):
            model.update({"y": 7, "child": {"x": -1}})
        model.update({"child": {"x": 10}})
        observations.append(model.model_dump())

    scenario(reference)
    scenario(actual)
    assert observations[0] == observations[1]


def test_dict_class_protocol_is_accepted(reference):
    class Values:
        @property
        def __class__(self):
            return dict

        def items(self):
            return [("y", 19)]

    def scenario(module):
        _, parent_type = model_types(module)
        model = parent_type()
        model.update(Values())
        assert model.y == 19

    scenario(reference)
    scenario(actual)


@pytest.mark.parametrize("value", [None, [], (), "value", 17])
def test_unsupported_update_errors_match(reference, value):
    observations = []

    def scenario(module):
        _, parent_type = model_types(module)
        with pytest.raises(TypeError) as caught:
            parent_type().update(value)
        observations.append(str(caught.value))

    scenario(reference)
    scenario(actual)
    assert observations[0] == observations[1]


def test_equality_retains_non_boolean_dump_comparison(reference):
    marker = object()

    class Dump:
        def __eq__(self, other):
            return marker

    def scenario(module):

        class Model(module.EventedModel):
            value: int = 0

            def model_dump(self, **kwargs):
                return Dump()

        model = Model()
        assert (model == object()) is marker
        assert model == model

    scenario(reference)
    scenario(actual)


@pytest.mark.parametrize("before_exists", [False, True])
def test_nested_enum_context_restores_identity_and_replaced_config(
    reference, before_exists
):
    def scenario(module):
        _, parent_type = model_types(module)
        model = parent_type()
        before, outer, inner = object(), object(), object()
        if before_exists:
            model.model_config["use_enum_values"] = before
        else:
            model.model_config.pop("use_enum_values", None)
        with pytest.raises(ValueError, match="body"):
            with model.enums_as_values(outer):
                assert model.model_config["use_enum_values"] is outer
                with model.enums_as_values(inner):
                    assert model.model_config["use_enum_values"] is inner
                assert model.model_config["use_enum_values"] is outer
                object.__setattr__(model, "model_config", {"use_enum_values": None})
                raise ValueError("body")
        if before_exists:
            assert model.model_config["use_enum_values"] is before
        else:
            assert "use_enum_values" not in model.model_config

    scenario(reference)
    scenario(actual)


def test_enum_context_and_model_cycle_are_collectible():
    _, parent_type = model_types(actual)
    model = parent_type()
    context = model.enums_as_values()
    context.__enter__()
    object.__setattr__(model, "adapter_context", context)
    references = [weakref.ref(model), weakref.ref(model._change_state)]
    del model, context
    gc.collect()
    assert all(reference() is None for reference in references)


@pytest.mark.parametrize(
    "decision", ["suppress", "propagate", "exit_error", "truth_error"]
)
def test_update_cleanup_exception_state_suppression_and_chaining(
    reference, monkeypatch, decision
):
    observations = []

    def scenario(module):
        trace = []
        original = ValueError("assignment")
        cleanup = RuntimeError("cleanup")
        outer = LookupError("caller")
        sentinel = object()

        class Decision:
            def __bool__(self):
                trace.append(("truth", sys.exception() is original))
                if decision == "truth_error":
                    raise cleanup
                return decision == "suppress"

        class Context:
            count = 1

            def __enter__(self):
                trace.append(("enter", sys.exception() is outer))
                return self

            def __exit__(self, kind, value, traceback):
                trace.append(
                    (
                        "exit",
                        kind is ValueError,
                        value is original,
                        traceback is original.__traceback__,
                        sys.exception() is original,
                    )
                )
                if decision == "exit_error":
                    raise cleanup
                return Decision()

        class Events:
            def blocker(self):
                return Context()

            def __call__(self, event):
                trace.append(("event", event is sentinel, sys.exception() is outer))

        class Model:
            events = Events()

            @property
            def value(self):
                return 0

            @value.setter
            def value(self, value):
                raise original

        model = Model()
        with monkeypatch.context() as patch:
            patch.setattr(module, "Event", lambda source: sentinel)
            try:
                raise outer
            except LookupError:
                try:
                    module.EventedModel.update(model, {"value": 7})
                except Exception as error:
                    trace.append(
                        (
                            "raised",
                            error is original,
                            error is cleanup,
                            error.__context__ is original,
                        )
                    )
                assert sys.exception() is outer
        observations.append(trace)

    scenario(reference)
    scenario(actual)
    assert observations[0] == observations[1]


def test_update_captures_class_context_methods_before_enter(reference):
    observations = []

    def scenario(module):
        trace = []

        class Context:
            count = 0

            def __enter__(self):
                trace.append("enter")
                self.__exit__ = lambda *args: trace.append("instance exit")
                type(self).__exit__ = lambda *args: trace.append("changed class exit")
                return self

            def __exit__(self, *args):
                trace.append("original class exit")

        context = Context()
        model = SimpleNamespace(
            value=0, events=SimpleNamespace(blocker=lambda: context)
        )
        module.EventedModel.update(model, {"value": 7})
        observations.append((model.value, trace))

    scenario(reference)
    scenario(actual)
    assert observations[0] == observations[1]


@pytest.mark.parametrize("missing", ["enter", "exit"])
def test_missing_context_protocol_errors_match(reference, missing):
    observations = []

    def scenario(module):

        class Context:
            def __enter__(self):
                pytest.fail("Missing methods must be detected before entry")

            def __exit__(self, *args):
                pytest.fail("The context must never have been entered")

        delattr(Context, f"__{missing}__")
        context = Context()
        model = SimpleNamespace(
            value=0, events=SimpleNamespace(blocker=lambda: context)
        )
        with pytest.raises(TypeError) as caught:
            module.EventedModel.update(model, {"value": 7})
        observations.append(str(caught.value))

    scenario(reference)
    scenario(actual)
    assert observations[0] == observations[1]
