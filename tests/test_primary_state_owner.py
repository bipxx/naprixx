"""Primary mapping and delay authority against the pinned EventedModel."""

import copy
import gc
import importlib.util
import sys
import weakref
from pathlib import Path

import pytest
from pydantic import PrivateAttr

from napari.utils.events import evented_model as actual
from naparixx import native


@pytest.fixture(scope="module")
def reference():
    path = (
        Path(__file__).resolve().parents[1]
        / ".upstream/napari/src/napari/utils/events/evented_model.py"
    )
    name = "_original_primary_state_owner_model"
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    yield module
    sys.modules.pop(name, None)


def _model_type(module):
    class Model(module.EventedModel):
        x: int = 0
        y: int = 0
        value: object = None

    return Model


def _compare(reference, scenario):
    expected = scenario(reference)
    observed = scenario(actual)
    assert observed == expected


def _call(function):
    try:
        return "return", function()
    except Exception as error:
        return "error", type(error).__name__, str(error)


@pytest.mark.parametrize("method", ["model_construct", "setstate"])
def test_supplied_primary_and_depth_remain_exact_private_state(reference, method):
    def scenario(module):
        model_type = _model_type(module)
        marker = object()
        primary = {"x": marker}
        depth = 7
        if method == "model_construct":
            model = model_type.model_construct(
                x=3,
                _primary_changes=primary,
                _delay_check_semaphore=depth,
            )
            private = model.__pydantic_private__
        else:
            source = model_type()
            private = dict(source.__pydantic_private__)
            private["_primary_changes"] = primary
            private["_delay_check_semaphore"] = depth
            before = tuple(private)
            state = {
                "__dict__": {"x": 3, "y": 0, "value": None},
                "__pydantic_private__": private,
                "__pydantic_extra__": None,
                "__pydantic_fields_set__": {"x"},
            }
            model = model_type.__new__(model_type)
            assert model.__setstate__(state) is None
            assert model.__pydantic_private__ is private
            assert tuple(private) == before
        assert model._primary_changes is primary
        assert private["_primary_changes"] is primary
        assert model._delay_check_semaphore is depth
        assert private["_delay_check_semaphore"] is depth
        primary["y"] = marker
        return (
            tuple(model._primary_changes),
            all(value is marker for value in model._primary_changes.values()),
            model._delay_check_semaphore,
            model.x,
        )

    _compare(reference, scenario)


@pytest.mark.parametrize("name", ["_primary_changes", "_delay_check_semaphore"])
@pytest.mark.parametrize("edit", ["replace", "delete"])
def test_saved_private_map_edits_are_immediately_authoritative(reference, name, edit):
    def scenario(module):
        model = _model_type(module)()
        private = model.__pydantic_private__
        original = getattr(model, name)
        replacement = {"x": None} if name == "_primary_changes" else object()
        if edit == "replace":
            private[name] = replacement
            assert getattr(model, name) is replacement
            outcome = "replaced", getattr(model, name) is original
        else:
            del private[name]
            with pytest.raises(AttributeError) as caught:
                getattr(model, name)
            outcome = "deleted", str(caught.value)
            private[name] = replacement
            assert getattr(model, name) is replacement
        assert model.__pydantic_private__ is private
        return outcome, name in private

    _compare(reference, scenario)


def test_direct_primary_assignment_deletion_and_recovery_preserve_identity(reference):
    def scenario(module):
        model = _model_type(module)()
        replacement = {"x": 17}
        model._primary_changes = replacement
        assert model._primary_changes is replacement
        assert model.__pydantic_private__["_primary_changes"] is replacement
        del model._primary_changes
        with pytest.raises(AttributeError) as caught:
            _ = model._primary_changes
        assert "_primary_changes" not in model.__pydantic_private__
        model._primary_changes = replacement
        assert model._primary_changes is replacement
        return str(caught.value), tuple(model._primary_changes.items())

    _compare(reference, scenario)


@pytest.mark.parametrize("key_kind", ["int", "tuple", "str_subclass", "nul"])
def test_primary_mapping_retains_arbitrary_values_and_original_key_objects(
    reference, key_kind
):
    def scenario(module):
        class Key(str):
            pass

        class Payload:
            pass

        key = {
            "int": 23,
            "tuple": ("name", 4),
            "str_subclass": Key("name"),
            "nul": "name\0suffix",
        }[key_kind]
        model = _model_type(module)()
        primary = model._primary_changes
        payload = Payload()
        payload_reference = weakref.ref(payload)
        primary[key] = payload
        del payload
        assert payload_reference() is not None
        assert primary[key] is payload_reference()
        assert primary.get(key) is payload_reference()
        assert next(iter(primary)) is key
        assert next(iter(primary.keys())) is key
        saved = tuple(primary), type(next(iter(primary))).__name__
        assert primary.pop(key) is payload_reference()
        assert payload_reference() is None
        return saved, tuple(primary)

    _compare(reference, scenario)


@pytest.mark.parametrize("method", ["keys", "items", "values"])
def test_saved_primary_mapping_views_follow_later_mutation(reference, method):
    def scenario(module):
        model = _model_type(module)()
        primary = model._primary_changes
        primary["x"] = None
        view = getattr(primary, method)()
        snapshots = [tuple(view)]
        primary["y"] = None
        snapshots.append(tuple(view))
        del primary["x"]
        snapshots.append(tuple(view))
        primary.clear()
        snapshots.append(tuple(view))
        primary["value"] = None
        snapshots.append(tuple(view))
        return snapshots

    _compare(reference, scenario)


def test_shallow_primary_aliases_share_mapping_and_keep_private_depths_independent(
    reference,
):
    def scenario(module):
        model = _model_type(module)()
        primary = model._primary_changes
        primary["x"] = None
        shallow = copy.copy(model)
        assert shallow._primary_changes is primary
        assert shallow.__pydantic_private__ is not model.__pydantic_private__
        shallow._primary_changes["y"] = None
        model._delay_check_semaphore = 5
        shallow._delay_check_semaphore = 3
        shallow.__pydantic_private__["_delay_check_semaphore"] = 7
        return (
            tuple(primary.items()),
            model._delay_check_semaphore,
            shallow._delay_check_semaphore,
            model.__pydantic_private__["_delay_check_semaphore"],
            shallow.__pydantic_private__["_delay_check_semaphore"],
        )

    _compare(reference, scenario)


@pytest.mark.parametrize("method", ["deepcopy", "model_copy"])
def test_deep_copy_primary_payloads_preserve_entry_path_memo_aliases(reference, method):
    def scenario(module):
        class Model(_model_type(module)):
            _payload_alias: object = PrivateAttr(None)

        memos = []

        class Payload:
            def __deepcopy__(self, memo):
                memos.append(memo)
                result = type(self)()
                memo[id(self)] = result
                return result

        payload = Payload()
        model = Model(value=payload)
        model._payload_alias = payload
        model._primary_changes["x"] = payload
        model._changes_queue["value"] = payload
        memo = {}
        deep = (
            copy.deepcopy(model, memo)
            if method == "deepcopy"
            else model.model_copy(deep=True)
        )
        copied_payload = deep._primary_changes["x"]
        assert copied_payload is not payload
        assert deep._payload_alias is copied_payload
        assert deep._changes_queue["value"] is copied_payload
        return (
            deep.value is copied_payload,
            memo.get(id(payload)) is copied_payload,
            len(memos),
            all(received is memos[0] for received in memos),
            deep._primary_changes is model._primary_changes,
        )

    _compare(reference, scenario)


def test_primary_payload_model_cycle_is_collectible(reference):
    def scenario(module):
        def create_cycle():
            class Payload:
                pass

            model = _model_type(module)()
            payload = Payload()
            payload.model = model
            model._primary_changes["adapter"] = payload
            assert model._primary_changes["adapter"] is payload
            return weakref.ref(model), weakref.ref(payload)

        references = create_cycle()
        gc.collect()
        return tuple(reference() is None for reference in references)

    _compare(reference, scenario)


def test_private_primary_removal_releases_payload_without_an_extra_native_edge():
    class Payload:
        pass

    model = _model_type(actual)()
    owner = native.model_transaction_owner(model)
    private = model.__pydantic_private__
    assert owner.private_mapping is private
    payload = Payload()
    payload.model = model
    payload_reference = weakref.ref(payload)
    private["_primary_changes"] = {"adapter": payload}
    assert model._primary_changes is private["_primary_changes"]
    del payload
    private.pop("_primary_changes")
    assert payload_reference() is None
    assert owner.private_mapping is private
    with pytest.raises(AttributeError):
        _ = model._primary_changes


def test_subclass_primary_and_depth_private_factories_remain_exact(reference):
    def scenario(module):
        produced = []
        depths = []

        def primary_factory():
            value = {"factory": 17}
            produced.append(value)
            return value

        def depth_factory():
            value = object()
            depths.append(value)
            return value

        class Model(module.EventedModel):
            x: int = 0
            _primary_changes: dict = PrivateAttr(default_factory=primary_factory)
            _delay_check_semaphore: object = PrivateAttr(default_factory=depth_factory)

        model = Model()
        assert model._primary_changes is produced[0]
        assert model._delay_check_semaphore is depths[0]
        return len(produced), len(depths), tuple(model._primary_changes.items())

    _compare(reference, scenario)


def test_subclass_properties_keep_original_private_set_and_delete_dispatch(reference):
    def scenario(module):
        calls = []
        adapter_primary = {}

        class Model(module.EventedModel):
            x: int = 0
            __non_evented_properties__ = (
                module.EventedModel.__non_evented_properties__
                | {"_primary_changes", "_delay_check_semaphore"}
            )

            @property
            def _primary_changes(self):
                calls.append("get_primary")
                return adapter_primary

            @_primary_changes.setter
            def _primary_changes(self, value):
                calls.append("set_primary")

            @property
            def _delay_check_semaphore(self):
                calls.append("get_depth")
                return 9

            @_delay_check_semaphore.setter
            def _delay_check_semaphore(self, value):
                calls.append("set_depth")

        model = Model()
        calls.clear()
        replacement = {"x": 17}
        model._primary_changes = replacement
        model._delay_check_semaphore = 11
        assert model.__pydantic_private__["_primary_changes"] is replacement
        assert model.__pydantic_private__["_delay_check_semaphore"] == 11
        assert model._primary_changes is adapter_primary
        assert model._delay_check_semaphore == 9
        del model._primary_changes
        del model._delay_check_semaphore
        return (
            calls,
            "_primary_changes" in model.__pydantic_private__,
            "_delay_check_semaphore" in model.__pydantic_private__,
        )

    _compare(reference, scenario)


@pytest.mark.parametrize("depth_kind", ["bool", "huge_int", "float", "none"])
def test_depth_preserves_supplied_object_and_python_arithmetic(reference, depth_kind):
    def scenario(module):
        depth = {"bool": True, "huge_int": 2**100, "float": 1.5, "none": None}[
            depth_kind
        ]
        model = _model_type(module)()
        model._delay_check_semaphore = depth
        assert model._delay_check_semaphore is depth
        assert model.__pydantic_private__["_delay_check_semaphore"] is depth
        before = type(model._delay_check_semaphore).__name__
        context = module.ComparisonDelayer(model)
        entered = _call(context.__enter__)
        after_enter = model._delay_check_semaphore
        exited = (
            _call(lambda: context.__exit__(None, None, None))
            if entered[0] == "return"
            else None
        )
        return (
            before,
            entered,
            type(after_enter).__name__,
            after_enter,
            exited,
            type(model._delay_check_semaphore).__name__,
            model._delay_check_semaphore,
        )

    _compare(reference, scenario)


def test_comparison_delayer_uses_duck_target_depth_and_check_hook(reference):
    def scenario(module):
        calls = []

        class Target:
            _delay_check_semaphore = 2

            def _check_if_values_changed_and_emit_if_needed(self):
                calls.append(self._delay_check_semaphore)

        target = Target()
        with module.ComparisonDelayer(target) as entered:
            assert entered is None
            inside = target._delay_check_semaphore
        return inside, target._delay_check_semaphore, calls

    _compare(reference, scenario)


@pytest.mark.parametrize("operation", ["enter", "leave"])
def test_delayer_arithmetic_errors_preserve_identity_and_skip_check_hook(
    reference, operation
):
    def scenario(module):
        calls = []
        error = LookupError("depth arithmetic failed")

        class Number:
            def __iadd__(self, value):
                calls.append(("add", value))
                raise error

            def __isub__(self, value):
                calls.append(("subtract", value))
                raise error

        class Target:
            def _check_if_values_changed_and_emit_if_needed(self):
                calls.append("check")

        target = Target()
        depth = Number()
        target._delay_check_semaphore = depth
        context = module.ComparisonDelayer(target)
        with pytest.raises(LookupError) as caught:
            if operation == "enter":
                context.__enter__()
            else:
                context.__exit__(None, None, None)
        assert caught.value is error
        assert target._delay_check_semaphore is depth
        return calls, str(caught.value)

    _compare(reference, scenario)


def test_delayer_arithmetic_reentry_publishes_result_before_check_hook(reference):
    def scenario(module):
        calls = []

        class Target:
            @property
            def _delay_check_semaphore(self):
                calls.append("get")
                return self._depth

            @_delay_check_semaphore.setter
            def _delay_check_semaphore(self, value):
                calls.append(("set", value))
                self._depth = value

            def _check_if_values_changed_and_emit_if_needed(self):
                calls.append(("check", self._depth))

        target = Target()

        class Number:
            def __iadd__(self, value):
                calls.append(("add", value))
                target._delay_check_semaphore = "add reentry"
                return 7

            def __isub__(self, value):
                calls.append(("subtract", value))
                target._delay_check_semaphore = "subtract reentry"
                return 3

        target._depth = Number()
        context = module.ComparisonDelayer(target)
        assert context.__enter__() is None
        entered = target._depth
        target._depth = Number()
        assert context.__exit__(None, None, None) is None
        return entered, target._depth, calls

    _compare(reference, scenario)


@pytest.mark.parametrize("cleanup", ["arithmetic", "check"])
def test_assignment_cleanup_keeps_body_exception_active_and_chains_cleanup_error(
    reference, cleanup
):
    def scenario(module):
        calls = []
        body_error = ValueError("assignment body failed")
        cleanup_error = LookupError("assignment cleanup failed")
        previous = sys.exception()

        class Number:
            def __iadd__(self, value):
                calls.append(("add", value, sys.exception() is body_error))
                return self

            def __isub__(self, value):
                calls.append(("subtract", value, sys.exception() is body_error))
                if cleanup == "arithmetic":
                    raise cleanup_error
                return 0

        class Model(_model_type(module)):
            def _setattr_impl(self, name, value):
                calls.append(("body", name, value))
                raise body_error

            def _check_if_values_changed_and_emit_if_needed(self):
                calls.append(("check", sys.exception() is body_error))
                raise cleanup_error

        model = Model()
        depth = Number()
        model._delay_check_semaphore = depth
        with pytest.raises(LookupError) as caught:
            model.x = 1
        assert caught.value is cleanup_error
        assert caught.value.__context__ is body_error
        assert sys.exception() is previous
        return (
            calls,
            model._delay_check_semaphore is depth,
            model._delay_check_semaphore == 0,
            tuple(model._primary_changes.items()),
            tuple(model._changes_queue.items()),
            model.x,
        )

    _compare(reference, scenario)


def test_delayer_arithmetic_temporaries_release_at_pinned_reentry_boundaries(reference):
    def scenario(module):
        calls = []

        class Target:
            @property
            def _delay_check_semaphore(self):
                calls.append("get")
                return self._depth

            @_delay_check_semaphore.setter
            def _delay_check_semaphore(self, value):
                calls.append(("set", value.value))
                self._depth = value.value

            def _check_if_values_changed_and_emit_if_needed(self):
                calls.append(("check", self._depth))

        target = Target()

        class Result:
            def __init__(self, value):
                self.value = value

            def __del__(self):
                calls.append(("result_finalize", target._depth))

        class Number:
            def __iadd__(self, value):
                calls.append(("add", value))
                target._depth = "add reentry"
                return Result(7)

            def __isub__(self, value):
                calls.append(("subtract", value))
                target._depth = "subtract reentry"
                return Result(3)

            def __del__(self):
                calls.append(("operand_finalize", target._depth))

        target._depth = Number()
        context = module.ComparisonDelayer(target)
        assert context.__enter__() is None
        entered = target._depth
        target._depth = Number()
        assert context.__exit__(None, None, None) is None
        return entered, target._depth, calls

    _compare(reference, scenario)


def test_delayer_exit_reloads_target_after_arithmetic_reentry(reference):
    def scenario(module):
        calls = []

        class Target:
            def __init__(self, name):
                self.name = name
                self._delay_check_semaphore = 2

            def _check_if_values_changed_and_emit_if_needed(self):
                calls.append(("check", self.name, self._delay_check_semaphore))

        original = Target("original")
        replacement = Target("replacement")
        context = module.ComparisonDelayer(original)

        class Number:
            def __isub__(self, value):
                calls.append(("subtract", value))
                context._target = replacement
                return 0

        original._delay_check_semaphore = Number()
        assert context.__exit__(None, None, None) is None
        assert context._target is replacement
        return (
            original._delay_check_semaphore,
            replacement._delay_check_semaphore,
            calls,
        )

    _compare(reference, scenario)
