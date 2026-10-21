"""Class preparation and dependency discovery against the pinned model source.

The reference implementation is unmodified and shares external dependency
adapters with the fork. The raw-class tests isolate owned metadata logic by
substituting only Pydantic's external class-construction boundary.
"""

import gc
import importlib.util
import sys
import warnings
import weakref
from pathlib import Path
from types import SimpleNamespace
from typing import ClassVar

import numpy as np
import pytest
from pydantic import Field

from napari.utils.events import evented_model as actual


@pytest.fixture(scope="module")
def reference():
    path = (
        Path(__file__).resolve().parents[1]
        / ".upstream/napari/src/napari/utils/events/evented_model.py"
    )
    name = "_original_native_model_metadata"
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    yield module
    sys.modules.pop(name, None)


@pytest.fixture(autouse=True)
def restore_encoders(reference):
    # Encoder registration deliberately updates the base model's public map.
    # Undo test-specific registrations, preserving the existing map's identity.
    snapshots = [
        (
            module.EventedModel.model_config["json_encoders"],
            dict(module.EventedModel.model_config["json_encoders"]),
        )
        for module in (reference, actual)
    ]
    yield
    for encoders, before in snapshots:
        encoders.clear()
        encoders.update(before)


def code_property(*names):
    return SimpleNamespace(
        fget=SimpleNamespace(__code__=SimpleNamespace(co_names=names))
    )


def prepare(module, monkeypatch, model, namespace):
    """Run the real metaclass with the external constructor supplying a model."""
    with monkeypatch.context() as patch:
        patch.setattr(
            module.ModelMetaclass,
            "__new__",
            staticmethod(lambda mcs, name, bases, namespace, **kwargs: model),
        )
        assert (
            module.EventedMetaclass.__new__(
                module.EventedMetaclass, "Raw", (), namespace
            )
            is model
        )


def raw_model(**changes):
    values = dict(
        model_fields={},
        model_config={"json_encoders": {}},
        __eq_operators__={},
        __properties__={},
        __non_evented_properties__=set(),
    )
    values.update(changes)
    return SimpleNamespace(**values)


def test_inherited_properties_schema_and_events(reference):
    def scenario(module):
        class Parent(module.EventedModel):
            x: int = Field(1, ge=0)
            y: int = 2

            @property
            def total(self) -> int:
                return self.x + self.y

            @total.setter
            def total(self, value):
                self.x = value - self.y

        class Child(Parent):
            z: int = 3

            @property
            def twice(self) -> int:
                return self.total * 2

        assert Child.__properties__ is not Parent.__properties__
        assert Child.__eq_operators__ is not Parent.__eq_operators__
        assert Child.__properties__["total"] is Parent.__properties__["total"]
        assert Child.__eq_operators__["total"] is Parent.__eq_operators__["total"]
        assert "twice" not in Parent.__properties__
        model = Child()
        trace = []

        def callback(event):
            trace.append((event.type, event.value))

        model.events.connect(callback)
        model.total = 10
        model.y = 5
        return (
            list(Child.__properties__),
            list(Child.__eq_operators__),
            Child.__field_dependents__,
            Child.model_json_schema(),
            model.model_dump(),
            trace,
        )

    assert scenario(reference) == scenario(actual)


@pytest.mark.parametrize("annotation", [int, list[int], np.ndarray, "int", None])
def test_property_comparators_use_return_annotations(
    reference, monkeypatch, annotation
):
    def scenario(module):
        calls = []
        comparator = object()

        def getter(self):
            return self.x

        getter.__annotations__ = {"return": annotation}
        prop = property(getter)
        model = raw_model(model_fields={"x": SimpleNamespace(annotation=int)})
        with monkeypatch.context() as patch:
            patch.setattr(
                module,
                "pick_equality_operator",
                lambda type: calls.append(type) or comparator,
            )
            prepare(module, patch, model, {"value": prop})
        assert model.__properties__["value"] is prop
        assert model.__eq_operators__["x"] is comparator
        return calls, list(model.__eq_operators__), model.__field_dependents__

    assert scenario(reference) == scenario(actual)


def test_mixin_and_non_evented_properties_preserve_baseline_discovery(reference):
    def scenario(module):
        class Mixin:
            @property
            def mixed(self):
                return self.x

        class Model(Mixin, module.EventedModel):
            x: int = 0
            __non_evented_properties__: ClassVar[set[str]] = (
                module.EventedModel.__non_evented_properties__ | {"hidden"}
            )

            @property
            def visible(self) -> int:
                return self.x

            @property
            def hidden(self) -> int:
                return self.x

        return (
            list(Model.__properties__),
            Model.__field_dependents__,
            list(Model().events.emitters),
        )

    assert scenario(reference) == scenario(actual)


def test_class_kwargs_and_subclass_hooks_pass_through(reference):
    def scenario(module):
        calls = []

        class Parent(module.EventedModel):
            x: int = 0

            def __init_subclass__(cls, marker=None, **kwargs):
                calls.append((marker, kwargs, list(cls.__eq_operators__)))
                super().__init_subclass__(**kwargs)

        class Child(Parent, marker="plugin", extra="forbid"):
            y: int = 1

            @property
            def total(self) -> int:
                return self.x + self.y

        return (
            calls,
            Child.model_config["extra"],
            Child.model_json_schema(),
            Child.__field_dependents__,
        )

    assert scenario(reference) == scenario(actual)


@pytest.mark.parametrize("failure", [None, AttributeError, RuntimeError])
def test_encoder_descriptor_reads_and_registration(reference, monkeypatch, failure):
    def scenario(module):
        calls = []

        def encoder(value):
            return value

        class CodecMeta(type):
            @property
            def _json_encode(cls):
                calls.append("encoder")
                if failure:
                    raise failure("encoder lookup")
                return encoder

        class Codec(metaclass=CodecMeta):
            pass

        model = raw_model(model_fields={"data": SimpleNamespace(annotation=Codec)})
        try:
            prepare(module, monkeypatch, model, {})
        except Exception as error:
            return calls, type(error), str(error), list(model.__eq_operators__)
        if failure is None:
            assert model.model_config["json_encoders"][Codec] is encoder
            assert module.EventedModel.model_config["json_encoders"][Codec] is encoder
            assert calls == ["encoder", "encoder"]
        else:
            assert Codec not in model.model_config["json_encoders"]
        return calls, list(model.__eq_operators__)

    assert scenario(reference) == scenario(actual)


@pytest.mark.parametrize("return_type", [int, "int"])
def test_property_descriptor_annotation_protocol(reference, monkeypatch, return_type):
    def scenario(module):
        calls = []

        class Getter:
            @property
            def __annotations__(self):
                calls.append("annotations")
                return {"return": return_type}

        getter = Getter()

        class Property(property):
            @property
            def fget(self):
                calls.append("getter")
                return getter

        prop = Property()
        model = raw_model(model_config={"dependencies": {"p": []}})
        with monkeypatch.context() as patch:
            patch.setattr(
                module,
                "pick_equality_operator",
                lambda annotation: calls.append(annotation),
            )
            prepare(module, patch, model, {"p": prop})
        return calls, list(model.__eq_operators__), model.__field_dependents__

    assert scenario(reference) == scenario(actual)


@pytest.mark.parametrize("mapping_kind", ["dict", "mapping", "sequence", "broken_keys"])
def test_inherited_mapping_unpack_protocol(reference, monkeypatch, mapping_kind):
    def scenario(module):
        calls = []
        original = {"old": object()}

        class Mapping:
            def keys(self):
                calls.append("keys")
                if mapping_kind == "broken_keys":
                    raise AttributeError("keys provider")
                return original.keys()

            def __getitem__(self, key):
                calls.append(("get", key))
                return original[key]

        inherited = {
            "dict": original,
            "mapping": Mapping(),
            "sequence": list(original.items()),
            "broken_keys": Mapping(),
        }[mapping_kind]
        model = raw_model(__eq_operators__=inherited)
        try:
            prepare(module, monkeypatch, model, {})
        except Exception as error:
            return calls, type(error), str(error)
        assert model.__eq_operators__ is not inherited
        assert model.__eq_operators__["old"] is original["old"]
        return calls, list(model.__eq_operators__)

    assert scenario(reference) == scenario(actual)


@pytest.mark.parametrize(
    "explicit", [None, {}, {"p": ["x"]}, {"p": ["missing", "x", "x"]}, {"bad": ["x"]}]
)
def test_explicit_dependency_results_warnings_and_errors(reference, explicit):
    def scenario(module):
        model = raw_model(
            model_fields={"x": object()},
            __properties__={"p": code_property("x")},
            model_config={"dependencies": explicit},
        )
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            try:
                result = module._get_field_dependents(model)
            except Exception as error:
                result = type(error), str(error)
        return result, [(item.category, str(item.message)) for item in caught]

    assert scenario(reference) == scenario(actual)


@pytest.mark.parametrize("items", [[], [()], [("p",)], [("p", [], None)], [None], [4]])
def test_dependency_items_unpack_errors(reference, items):
    model = raw_model(
        __properties__={"p": code_property()},
        model_config={"dependencies": SimpleNamespace(items=lambda: items)},
    )

    def scenario(module):
        try:
            return module._get_field_dependents(model)
        except Exception as error:
            return type(error), str(error)

    assert scenario(reference) == scenario(actual)


@pytest.mark.parametrize("visited", [(), ("b",), ("b", "c"), [], None])
def test_recursive_cycles_and_visited_protocol(reference, monkeypatch, visited):
    def scenario(module):
        properties = {
            "a": code_property("b", "c"),
            "b": code_property("x", "c", "a"),
            "c": code_property("y"),
        }
        model = raw_model(
            model_fields={"x": object(), "y": object()}, __properties__=properties
        )
        calls = []
        deps = {}
        original = module._update_dependents_from_property_code

        def hook(cls, name, prop, deps, visited=()):
            calls.append((name, prop.fget.__code__.co_names, visited))
            original(cls, name, prop, deps, visited)

        with monkeypatch.context() as patch:
            patch.setattr(module, "_update_dependents_from_property_code", hook)
            try:
                original(model, "root", properties["a"], deps, visited)
            except Exception as error:
                return deps, calls, type(error), str(error)
        return deps, calls

    assert scenario(reference) == scenario(actual)


def test_class_dependency_hook_replacement_preserves_result_identity(
    reference, monkeypatch
):
    def scenario(module):
        sentinel = object()
        model = raw_model()
        seen = []
        with monkeypatch.context() as patch:
            patch.setattr(
                module,
                "_get_field_dependents",
                lambda cls: seen.append(cls) or sentinel,
            )
            prepare(module, patch, model, {})
        assert seen == [model]
        assert model.__field_dependents__ is sentinel

    scenario(reference)
    scenario(actual)


def test_field_annotation_reads_preserve_outer_helper_capture(reference, monkeypatch):
    def scenario(module):
        calls = []

        def first(annotation):
            calls.append("first")
            return int

        def second(annotation):
            calls.append("second")
            return int

        class Field:
            @property
            def annotation(self):
                module.get_outer_type = second
                return int

        with monkeypatch.context() as patch:
            patch.setattr(module, "get_outer_type", first)
            model = raw_model(model_fields={"x": Field()})
            prepare(module, patch, model, {})
        return calls

    assert scenario(reference) == scenario(actual)


def test_unknown_dependency_format_preserves_warning_callable_capture(
    reference, monkeypatch
):
    def scenario(module):
        calls = []
        warning_adapter = SimpleNamespace(
            warn=lambda message: calls.append(("first", message))
        )

        class Field:
            def __format__(self, specification):
                warning_adapter.warn = lambda message: calls.append(("second", message))
                return "provider"

        model = raw_model(
            __properties__={"p": code_property()},
            model_config={"dependencies": {"p": [Field()]}},
        )
        with monkeypatch.context() as patch:
            patch.setattr(module, "warnings", warning_adapter)
            assert len(module._get_field_dependents(model)) == 1
        return calls

    assert scenario(reference) == scenario(actual)


def test_set_factory_preserves_dependency_method_capture(reference, monkeypatch):
    def scenario(module):
        calls = []
        deps = SimpleNamespace(
            setdefault=lambda field, default: calls.append("first") or default
        )

        def make_set():
            deps.setdefault = lambda field, default: calls.append("second") or default
            return set()

        model = raw_model(model_fields={"x": object()})
        with monkeypatch.context() as patch:
            patch.setattr(module, "set", make_set, raising=False)
            module._update_dependents_from_property_code(
                model, "p", code_property("x"), deps
            )
        return calls

    assert scenario(reference) == scenario(actual)


def test_return_annotation_reads_precede_string_type_lookup(reference, monkeypatch):
    def scenario(module):
        reads = []

        class Getter:
            @property
            def __annotations__(self):
                reads.append("annotations")
                if len(reads) == 3:
                    module.str = int
                return {"return": 0}

        prop = property(Getter())
        model = raw_model(model_config={"dependencies": {"p": []}})
        with monkeypatch.context() as patch:
            patch.setattr(module, "str", str, raising=False)
            prepare(module, patch, model, {"p": prop})
        return reads, list(model.__eq_operators__)

    assert scenario(reference) == scenario(actual)


@pytest.mark.parametrize("error_type", [TypeError, RuntimeError])
def test_dependency_iterators_preserve_provider_errors(reference, error_type):
    class BrokenEntry:
        def __iter__(self):
            raise error_type("provider iterator")

    model = raw_model(
        __properties__={"p": code_property()},
        model_config={"dependencies": SimpleNamespace(items=lambda: [BrokenEntry()])},
    )

    def scenario(module):
        with pytest.raises(error_type) as caught:
            module._get_field_dependents(model)
        return str(caught.value)

    assert scenario(reference) == scenario(actual)


@pytest.mark.parametrize("items", [[()], [("p",)], [("p", property(), None)], [None]])
def test_namespace_items_failures_preserve_partial_metadata(
    reference, monkeypatch, items
):
    def scenario(module):
        inherited = {"old": object()}
        model = raw_model(__eq_operators__=inherited)
        namespace = SimpleNamespace(items=lambda: items)
        with pytest.raises((TypeError, ValueError)) as caught:
            prepare(module, monkeypatch, model, namespace)
        assert model.__eq_operators__ is not inherited
        assert model.__eq_operators__["old"] is inherited["old"]
        return type(caught.value), str(caught.value), model.__properties__

    assert scenario(reference) == scenario(actual)


@pytest.mark.parametrize("failure", [AttributeError, RuntimeError])
def test_getter_annotation_errors_preserve_property_registration(
    reference, monkeypatch, failure
):
    def scenario(module):
        class Getter:
            @property
            def __annotations__(self):
                raise failure("annotation provider")

        prop = property(Getter())
        model = raw_model(model_config={"dependencies": {"p": []}})
        try:
            prepare(module, monkeypatch, model, {"p": prop})
        except Exception as error:
            assert model.__properties__["p"] is prop
            return type(error), str(error), list(model.__eq_operators__)
        assert model.__properties__["p"] is prop
        return list(model.__eq_operators__), model.__field_dependents__

    assert scenario(reference) == scenario(actual)


def test_dynamic_property_model_cycle_is_collected(reference):
    def scenario(module):
        owners = []

        class Model(module.EventedModel):
            x: int = 0

            @property
            def value(self) -> int:
                return self.x if owners else 0

        model = Model()
        owners.append(model)
        return weakref.ref(Model), weakref.ref(model)

    refs = [ref for module in (reference, actual) for ref in scenario(module)]
    gc.collect()
    assert all(ref() is None for ref in refs)
