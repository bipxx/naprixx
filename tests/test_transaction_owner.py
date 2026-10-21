"""Transaction queue ownership against the unchanged pinned EventedModel."""

import copy
import gc
import importlib.util
import sys
import weakref
from pathlib import Path

import pytest
from pydantic import BaseModel, Field, PrivateAttr

from napari.utils.events import evented_model as actual
from naparixx import native


@pytest.fixture(scope="module")
def reference():
    path = (
        Path(__file__).resolve().parents[1]
        / ".upstream/napari/src/napari/utils/events/evented_model.py"
    )
    name = "_original_transaction_owner_model"
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    yield module
    sys.modules.pop(name, None)


def _model_type(module):
    class Model(module.EventedModel):
        x: int = Field(0, ge=0)
        y: int = 0
        value: object = None

    return Model


def _observe(model):
    trace, callbacks = [], []
    for name in ("x", "y", "value"):

        def callback(event, name=name):
            trace.append(
                (
                    "event",
                    name,
                    event.value,
                    model._delay_check_semaphore,
                    tuple(model._changes_queue),
                )
            )

        callbacks.append(callback)
        getattr(model.events, name).connect(callback)
    return trace, callbacks


def _snapshot(model):
    return (
        tuple(model._changes_queue.items()),
        tuple(model._primary_changes),
        model._delay_check_semaphore,
    )


def _compare(reference, scenario):
    expected = scenario(reference)
    observed = scenario(actual)
    assert observed == expected
    return observed


def test_saved_queue_and_dict_views_remain_live_across_drains(reference):
    def scenario(module):
        model = _model_type(module)()
        trace, callbacks = _observe(model)
        queue = model._changes_queue
        keys, items = queue.keys(), queue.items()
        snapshots = [(tuple(keys), tuple(items))]
        with module.ComparisonDelayer(model):
            model.x = 1
            model.x = 2
            model.y = 3
            assert queue is model._changes_queue
            snapshots.append((tuple(keys), tuple(items)))
        snapshots.append((tuple(keys), tuple(items)))
        with module.ComparisonDelayer(model):
            model.y = 5
            assert queue is model._changes_queue
            snapshots.append((tuple(keys), tuple(items)))
        snapshots.append((tuple(keys), tuple(items)))
        assert callbacks
        return snapshots, trace, _snapshot(model)

    _compare(reference, scenario)


@pytest.mark.parametrize(
    "edit", ["replace", "delete", "clear", "delete_then_assign", "insert_secondary"]
)
def test_direct_queue_edits_during_delay_reach_the_next_drain(reference, edit):
    def scenario(module):
        model = _model_type(module)()
        trace, callbacks = _observe(model)
        queue = model._changes_queue
        with module.ComparisonDelayer(model):
            model.x = 1
            if edit == "replace":
                queue["x"] = 1
            elif edit == "delete":
                del queue["x"]
            elif edit == "clear":
                queue.clear()
            elif edit == "delete_then_assign":
                del queue["x"]
                model.x = 2
            else:
                queue["y"] = -1
            delayed = _snapshot(model)
        assert queue is model._changes_queue
        model.y = 4
        assert callbacks
        return delayed, trace, _snapshot(model), model.x, model.y

    _compare(reference, scenario)


@pytest.mark.parametrize("method", ["copy", "model_copy"])
def test_shallow_copies_share_live_queue_but_have_independent_depth(reference, method):
    def scenario(module):
        model = _model_type(module)()
        trace, callbacks = _observe(model)
        queue = model._changes_queue
        primary = model._primary_changes
        saved_keys = queue.keys()
        snapshots = []
        with module.ComparisonDelayer(model):
            model.x = 2
            shallow = copy.copy(model) if method == "copy" else model.model_copy()
            assert shallow._changes_queue is queue
            assert shallow._primary_changes is primary
            shallow._delay_check_semaphore = 3
            snapshots.append(
                (model._delay_check_semaphore, shallow._delay_check_semaphore)
            )
            with module.ComparisonDelayer(shallow):
                shallow.y = 7
                snapshots.append(
                    (
                        model._delay_check_semaphore,
                        shallow._delay_check_semaphore,
                        tuple(saved_keys),
                    )
                )
            assert callbacks
        assert shallow._changes_queue is queue
        snapshots.append(
            (
                model._delay_check_semaphore,
                shallow._delay_check_semaphore,
                tuple(saved_keys),
            )
        )
        return snapshots, trace, _snapshot(model), _snapshot(shallow)

    _compare(reference, scenario)


@pytest.mark.parametrize("preseeded", [False, True])
def test_deep_copy_preserves_payload_aliases_and_uses_callers_memo(
    reference, preseeded
):
    def scenario(module):
        calls = []
        memo = {}

        class Payload:
            def __deepcopy__(self, received):
                calls.append(received is memo)
                result = type(self)()
                received[id(self)] = result
                return result

        shared = Payload()
        replacement = Payload()
        model = _model_type(module)(value=shared)
        model._changes_queue.update(x=shared, y=shared, value=shared)
        model._primary_changes["value"] = None
        model._delay_check_semaphore = 2
        if preseeded:
            memo[id(shared)] = replacement
        deep = copy.deepcopy(model, memo)
        copied = deep.value
        aliases = [deep._changes_queue[name] is copied for name in ("x", "y", "value")]
        assert deep._changes_queue is not model._changes_queue
        assert deep._primary_changes is not model._primary_changes
        assert all(aliases)
        assert copied is not shared
        return (
            aliases,
            copied is replacement,
            memo[id(shared)] is copied,
            calls,
            deep._delay_check_semaphore,
            tuple(deep._primary_changes),
        )

    _compare(reference, scenario)


@pytest.mark.parametrize("failing_field", ["x", "y"])
def test_partial_comparison_failure_preserves_queue_and_recovers(
    reference, failing_field
):
    def scenario(module):
        model = _model_type(module)()
        trace, callbacks = _observe(model)
        queue = model._changes_queue
        error = LookupError("comparison failed")
        previous = model.__eq_operators__[failing_field]

        def fail(*args):
            raise error

        model.__eq_operators__[failing_field] = fail
        with pytest.raises(LookupError) as caught:
            with module.ComparisonDelayer(model):
                model.x = 2
                model.y = 3
        assert caught.value is error
        assert queue is model._changes_queue
        failed = _snapshot(model)
        model.__eq_operators__[failing_field] = previous
        model.y = 4
        assert callbacks
        return failed, trace, _snapshot(model), model.x, model.y

    _compare(reference, scenario)


@pytest.mark.parametrize("release", ["clear", "pop", "replace"])
def test_queued_payload_finalizer_can_reenter_during_direct_release(reference, release):
    def scenario(module):
        model = _model_type(module)()
        trace, callbacks = _observe(model)
        errors = []
        target = weakref.ref(model)

        class Payload:
            def __del__(self):
                owner = target()
                trace.append(("finalize", _snapshot(owner)))
                try:
                    owner.y = 7
                except Exception as error:
                    errors.append((type(error).__name__, str(error)))

        queue = model._changes_queue
        queue["x"] = Payload()
        model._primary_changes["x"] = None
        with module.ComparisonDelayer(model):
            if release == "clear":
                queue.clear()
            elif release == "pop":
                queue.pop("x")
            else:
                queue["x"] = 0
            delayed = _snapshot(model)
        assert callbacks
        assert errors == []
        assert model.y == 7
        return delayed, trace, _snapshot(model)

    _compare(reference, scenario)


def test_automatic_drain_preserves_old_payload_finalizer_and_callback_order(reference):
    def scenario(module):
        trace, errors = [], []

        class Payload:
            target = None

            def __del__(self):
                owner = self.target()
                trace.append(("finalize", _snapshot(owner)))
                try:
                    owner.y = 9
                except Exception as error:
                    errors.append((type(error).__name__, str(error)))

        model = _model_type(module)(value=Payload())
        model.value.target = weakref.ref(model)
        old_reference = weakref.ref(model.value)
        events, callbacks = _observe(model)

        def value_event(event):
            trace.append(("value callback", event.value, old_reference() is not None))

        callbacks.append(value_event)
        model.events.value.connect(value_event)
        with module.ComparisonDelayer(model):
            model.value = "new"
            assert old_reference() is not None
        assert old_reference() is None
        assert errors == []
        return trace, events, _snapshot(model), model.y

    _compare(reference, scenario)


@pytest.mark.parametrize("seeded", [False, True])
def test_replacing_queue_detaches_saved_dict_and_views(reference, seeded):
    def scenario(module):
        model = _model_type(module)()
        trace, callbacks = _observe(model)
        saved = model._changes_queue
        keys, items = saved.keys(), saved.items()
        replacement = {"x": 1, "y": -1} if seeded else {}
        with module.ComparisonDelayer(model):
            model.x = 1
            model._changes_queue = replacement
            assert model._changes_queue is replacement
            assert model._changes_queue is not saved
            delayed = _snapshot(model)
        assert model._changes_queue is replacement
        assert callbacks
        return delayed, tuple(keys), tuple(items), tuple(replacement.items()), trace

    _compare(reference, scenario)


def test_deleting_queue_preserves_missing_attribute_and_allows_recovery(reference):
    def scenario(module):
        model = _model_type(module)()
        trace, callbacks = _observe(model)
        saved = model._changes_queue
        with pytest.raises(AttributeError) as caught:
            with module.ComparisonDelayer(model):
                model.x = 1
                del model._changes_queue
                with pytest.raises(AttributeError):
                    _ = model._changes_queue
        failed = (
            str(caught.value),
            tuple(model._primary_changes),
            model._delay_check_semaphore,
        )
        replacement = {}
        model._changes_queue = replacement
        assert model._changes_queue is replacement
        model.y = 4
        assert callbacks
        return failed, tuple(saved.items()), trace, _snapshot(model)

    _compare(reference, scenario)


def test_shallow_copy_queue_replacement_detaches_only_that_queue(reference):
    def scenario(module):
        model = _model_type(module)()
        trace, callbacks = _observe(model)
        queue = model._changes_queue
        with module.ComparisonDelayer(model):
            model.x = 1
            shallow = copy.copy(model)
            replacement = {"y": 0}
            shallow._changes_queue = replacement
            assert shallow._changes_queue is replacement
            assert model._changes_queue is queue
            assert shallow._primary_changes is model._primary_changes
            shallow._delay_check_semaphore = 3
            shallow.y = 7
            delayed = _snapshot(model), _snapshot(shallow)
        drained = _snapshot(model), _snapshot(shallow)
        shallow._delay_check_semaphore = 0
        shallow._check_if_values_changed_and_emit_if_needed()
        assert callbacks
        return delayed, drained, trace, _snapshot(model), _snapshot(shallow)

    _compare(reference, scenario)


def test_shallow_copy_before_primary_access_still_shares_the_live_view(reference):
    def scenario(module):
        model = _model_type(module)()
        shallow = copy.copy(model)
        primary = model._primary_changes
        assert shallow._primary_changes is primary
        primary["x"] = None
        model._delay_check_semaphore = 2
        return (
            tuple(shallow._primary_changes.items()),
            model._delay_check_semaphore,
            shallow._delay_check_semaphore,
            shallow._changes_queue is model._changes_queue,
        )

    _compare(reference, scenario)


def test_model_construct_preserves_supplied_private_queue_identity(reference):
    def scenario(module):
        queue = {"x": 17}
        model = _model_type(module).model_construct(x=3, _changes_queue=queue)
        assert model._changes_queue is queue
        queue["y"] = 23
        return (
            tuple(model._changes_queue.items()),
            model.x,
            model.y,
            tuple(model.events.emitters),
            model.model_fields_set,
        )

    _compare(reference, scenario)


@pytest.mark.parametrize("missing_queue", [False, True])
def test_setstate_preserves_exact_supplied_state_and_missing_queue(
    reference, missing_queue
):
    def scenario(module):
        model_type = _model_type(module)
        source = model_type()
        private = dict(source.__pydantic_private__)
        queue = {"x": 17}
        if missing_queue:
            private.pop("_changes_queue", None)
        else:
            private["_changes_queue"] = queue
        marker = object()
        private["adapter_entry"] = marker
        keys_before = tuple(private)
        fields = {"x": 3, "y": 4, "value": None}
        extra = {"extension": marker}
        fields_set = {"x"}
        state = {
            "__dict__": fields,
            "__pydantic_private__": private,
            "__pydantic_extra__": extra,
            "__pydantic_fields_set__": fields_set,
        }
        model = model_type.__new__(model_type)
        assert model.__setstate__(state) is None
        assert model.__dict__ is fields
        assert model.__pydantic_private__ is private
        assert model.__pydantic_extra__ is extra
        assert model.__pydantic_fields_set__ is fields_set
        assert tuple(private) == keys_before
        assert private["adapter_entry"] is marker
        if missing_queue:
            with pytest.raises(AttributeError) as caught:
                _ = model._changes_queue
            outcome = "missing", str(caught.value)
        else:
            assert model._changes_queue is queue
            outcome = "queue", tuple(queue.items())
        assert tuple(private) == keys_before
        return outcome, tuple(model.__dict__.items()), model.model_fields_set

    _compare(reference, scenario)


def test_subclass_private_queue_redeclaration_keeps_factory_and_dispatch(reference):
    def scenario(module):
        produced = []

        def factory():
            queue = {"initial": 7}
            produced.append(queue)
            return queue

        class Model(module.EventedModel):
            x: int = 0
            _changes_queue: dict = PrivateAttr(default_factory=factory)

        model = Model()
        assert model._changes_queue is produced[0]
        replacement = {"x": 3}
        model._changes_queue = replacement
        assert model._changes_queue is replacement
        assert model.__pydantic_private__["_changes_queue"] is replacement
        del model._changes_queue
        with pytest.raises(AttributeError) as caught:
            _ = model._changes_queue
        model._changes_queue = {}
        return len(produced), str(caught.value), tuple(model._changes_queue.items())

    _compare(reference, scenario)


def test_subclass_queue_property_uses_original_pydantic_private_dispatch(reference):
    def scenario(module):
        calls = []

        class Model(module.EventedModel):
            x: int = 0
            _adapter_queue: dict = PrivateAttr(default_factory=dict)
            __non_evented_properties__ = (
                module.EventedModel.__non_evented_properties__ | {"_changes_queue"}
            )

            @property
            def _changes_queue(self):
                return self._adapter_queue

            @_changes_queue.setter
            def _changes_queue(self, value):
                calls.append("property setter")
                self._adapter_queue = value

            @_changes_queue.deleter
            def _changes_queue(self):
                calls.append("property deleter")
                self._adapter_queue.clear()

        model = Model()
        adapter = model._adapter_queue
        replacement = {"x": 17}
        model._changes_queue = replacement
        assigned = (
            model._changes_queue is replacement,
            model._changes_queue is adapter,
            model.__pydantic_private__.get("_changes_queue") is replacement,
        )
        del model._changes_queue
        after_delete = model._changes_queue is adapter
        events = []
        callback = lambda event: events.append(event.value)  # noqa: E731
        model.events.x.connect(callback)
        model.x = 1
        assert callback
        return assigned, after_delete, calls, events, tuple(adapter.items())

    _compare(reference, scenario)


@pytest.mark.parametrize("method", ["deepcopy", "model_copy"])
def test_deep_copy_entry_paths_preserve_their_memo_boundaries(reference, method):
    def scenario(module):
        memos = []

        class Payload:
            def __deepcopy__(self, memo):
                memos.append(memo)
                result = type(self)()
                memo[id(self)] = result
                return result

        payload = Payload()
        model = _model_type(module)(value=payload)
        model._changes_queue.update(value=payload, x=payload)
        model._primary_changes["value"] = None
        model._delay_check_semaphore = 2
        deep = (
            copy.deepcopy(model)
            if method == "deepcopy"
            else model.model_copy(deep=True)
        )
        assert deep._changes_queue is not model._changes_queue
        assert deep._primary_changes is not model._primary_changes
        assert deep._changes_queue["value"] is deep._changes_queue["x"]
        assert deep.value is not payload
        return (
            deep.value is deep._changes_queue["value"],
            len(memos),
            all(memo is memos[0] for memo in memos),
            deep._delay_check_semaphore,
            tuple(deep._primary_changes),
        )

    _compare(reference, scenario)


@pytest.mark.parametrize("method", ["deepcopy", "model_copy"])
def test_deep_copy_primary_aliases_preserve_field_and_private_memo_boundaries(
    reference, method
):
    def scenario(module):
        class Model(_model_type(module)):
            _primary_alias: object = PrivateAttr(None)

        model = Model()
        primary = model._primary_changes
        model.value = primary
        model._primary_alias = primary
        model._changes_queue["value"] = primary
        primary["y"] = None
        model._delay_check_semaphore = 2
        memo = {}
        deep = (
            copy.deepcopy(model, memo)
            if method == "deepcopy"
            else model.model_copy(deep=True)
        )
        copied_primary = deep._primary_changes
        assert copied_primary is not primary
        assert deep._primary_alias is copied_primary
        assert deep._changes_queue["value"] is copied_primary
        aliases = (
            deep.value is copied_primary,
            deep.value is not primary,
            memo.get(id(primary)) is copied_primary,
            tuple(copied_primary.items()),
        )
        deep._changes_queue.clear()
        copied_primary.clear()
        deep._delay_check_semaphore = 0
        trace, callbacks = _observe(deep)
        with module.ComparisonDelayer(deep):
            deep.x = 3
            deep._primary_alias["y"] = None
            deep._changes_queue["y"] = -1
        assert callbacks
        return (
            aliases,
            trace,
            _snapshot(deep),
            tuple(deep.value.items()),
            tuple(primary.items()),
            model.x,
        )

    _compare(reference, scenario)


def test_deep_copy_honors_preseeded_primary_alias_and_its_live_mutations(reference):
    def scenario(module):
        class Model(_model_type(module)):
            _primary_alias: object = PrivateAttr(None)

        model = Model()
        primary = model._primary_changes
        model.value = primary
        model._primary_alias = primary
        model._changes_queue["value"] = primary
        primary["y"] = None
        replacement = {"y": None}
        memo = {id(primary): replacement}
        deep = copy.deepcopy(model, memo)
        assert deep._primary_changes is replacement
        assert deep._primary_alias is replacement
        assert deep.value is replacement
        assert deep._changes_queue["value"] is replacement
        assert memo[id(primary)] is replacement
        deep._changes_queue.clear()
        replacement.clear()
        trace, callbacks = _observe(deep)
        with module.ComparisonDelayer(deep):
            deep.x = 3
            replacement["y"] = None
            deep._changes_queue["y"] = -1
        assert callbacks
        assert deep._primary_changes is replacement
        return (
            trace,
            _snapshot(deep),
            tuple(replacement.items()),
            tuple(primary.items()),
        )

    _compare(reference, scenario)


def _advance(iterator):
    try:
        return "item", next(iterator)
    except StopIteration:
        return ("stop",)
    except RuntimeError as error:
        return "error", str(error)


@pytest.mark.parametrize(
    "mutation", ["add", "remove", "replace_consumed", "replace_future"]
)
def test_saved_primary_iterator_preserves_live_dict_mutation_rules(reference, mutation):
    def scenario(module):
        model = _model_type(module)()
        primary = model._primary_changes
        primary["x"] = None
        primary["y"] = None
        iterator = iter(primary)
        assert next(iterator) == "x"
        if mutation == "add":
            primary["value"] = None
        elif mutation == "remove":
            del primary["y"]
        elif mutation == "replace_consumed":
            del primary["x"]
            primary["value"] = None
        else:
            del primary["y"]
            primary["value"] = None
        observations = [_advance(iterator)]
        if mutation == "add":
            del primary["value"]
        elif mutation == "remove":
            primary["y"] = None
        observations.extend(_advance(iterator) for _ in range(3))
        return observations

    _compare(reference, scenario)


@pytest.mark.parametrize("mutation", ["add", "replace_consumed"])
def test_comparator_primary_mutation_uses_live_iteration_and_recovers(
    reference, mutation
):
    def scenario(module):
        model = _model_type(module)()
        trace, callbacks = _observe(model)
        previous = model.__eq_operators__["x"]

        def compare(current, old):
            if mutation == "replace_consumed":
                del model._primary_changes["x"]
            model._primary_changes["value"] = None
            return previous(current, old)

        model.__eq_operators__["x"] = compare
        with pytest.raises(RuntimeError) as caught:
            with module.ComparisonDelayer(model):
                model.x = 1
                model.y = 2
        failed = str(caught.value), _snapshot(model)
        model.__eq_operators__["x"] = previous
        model._primary_changes.pop("value")
        model.y = 3
        assert callbacks
        return failed, trace, _snapshot(model)

    _compare(reference, scenario)


@pytest.mark.parametrize("mutation", ["add", "remove_future", "replace_consumed"])
def test_secondary_comparison_preserves_live_queue_iteration(reference, mutation):
    def scenario(module):
        mutate = True

        class Model(_model_type(module)):
            def _check_if_differ(self, name, old_value):
                result = super()._check_if_differ(name, old_value)
                if name == "y" and mutate:
                    if mutation == "remove_future":
                        del self._changes_queue["value"]
                    else:
                        if mutation == "replace_consumed":
                            del self._changes_queue["y"]
                        self._changes_queue["adapter"] = False
                return result

        model = Model()
        trace, callbacks = _observe(model)
        with pytest.raises(RuntimeError) as caught:
            with module.ComparisonDelayer(model):
                model.x = 1
                model._changes_queue.update(y=0, value=None)
        failed = str(caught.value), _snapshot(model)
        mutate = False
        model._changes_queue.pop("adapter", None)
        model.y = 3
        assert callbacks
        return failed, trace, _snapshot(model)

    _compare(reference, scenario)


def _assert_no_base_private_storage(model):
    descriptor = BaseModel.__dict__["__pydantic_private__"]
    with pytest.raises(AttributeError):
        descriptor.__get__(model, type(model))


def test_ordinary_private_map_assignment_after_none_preserves_error(reference):
    def scenario(module):
        model = _model_type(module)()
        private_mapping = model.__pydantic_private__
        object.__setattr__(model, "__pydantic_private__", None)
        try:
            with pytest.raises(TypeError) as caught:
                model.__pydantic_private__ = {"adapter": 7}
            assert model.__pydantic_private__ is None
            error = type(caught.value).__name__, str(caught.value)
        finally:
            object.__setattr__(model, "__pydantic_private__", private_mapping)
        assert model.__pydantic_private__ is private_mapping
        return error

    _compare(reference, scenario)


def test_native_owner_is_exact_private_authority_and_read_only():
    model = _model_type(actual)()
    owner = native.model_transaction_owner(model)
    assert isinstance(owner, native.ModelTransactionOwner)
    assert owner is native.model_transaction_owner(model)
    assert owner is model._naparixx_transaction_owner
    assert owner.private_mapping is model.__pydantic_private__
    assert gc.is_tracked(owner)
    _assert_no_base_private_storage(model)
    with pytest.raises(AttributeError):
        owner.private_mapping = {}
    with pytest.raises(AttributeError):
        owner.adapter_owner = model
    with pytest.raises(AttributeError):
        _ = owner.__dict__
    with pytest.raises(TypeError):
        native.ModelTransactionOwner()


def test_native_owner_preserves_missing_none_and_deleted_private_maps():
    model_type = _model_type(actual)
    model = model_type.__new__(model_type)
    with pytest.raises(AttributeError):
        _ = model.__pydantic_private__
    object.__setattr__(model, "__pydantic_private__", None)
    owner = native.model_transaction_owner(model)
    assert model.__pydantic_private__ is None
    assert owner.private_mapping is None
    _assert_no_base_private_storage(model)
    object.__setattr__(model, "__pydantic_private__", {"adapter": 7})
    assert owner.private_mapping is model.__pydantic_private__
    object.__delattr__(model, "__pydantic_private__")
    assert owner.private_mapping is None
    with pytest.raises(AttributeError):
        _ = model.__pydantic_private__
    _assert_no_base_private_storage(model)


@pytest.mark.parametrize("drop", ["model", "owner_slot"])
def test_saved_owner_retains_private_graph_until_owner_release(drop):
    class Payload:
        pass

    model = _model_type(actual)()
    payload = Payload()
    model._changes_queue["adapter"] = payload
    owner = native.model_transaction_owner(model)
    model_reference = weakref.ref(model)
    owner_reference = weakref.ref(owner)
    payload_reference = weakref.ref(payload)
    journal_reference = weakref.ref(model._change_state)
    del payload
    if drop == "owner_slot":
        del model._naparixx_transaction_owner
        with pytest.raises(AttributeError):
            _ = model.__pydantic_private__
    del model
    gc.collect()
    assert model_reference() is None
    assert payload_reference() is not None
    assert journal_reference() is not None
    assert owner.private_mapping["_changes_queue"]["adapter"] is payload_reference()
    del owner
    gc.collect()
    assert owner_reference() is None
    assert payload_reference() is None
    assert journal_reference() is None


def test_shallow_copy_has_distinct_owner_with_shared_queue_and_no_second_storage():
    model = _model_type(actual)()
    model._changes_queue["x"] = 17
    shallow = copy.copy(model)
    owner = native.model_transaction_owner(model)
    shallow_owner = native.model_transaction_owner(shallow)
    assert shallow_owner is not owner
    assert owner.private_mapping is model.__pydantic_private__
    assert shallow_owner.private_mapping is shallow.__pydantic_private__
    assert shallow_owner.private_mapping is not owner.private_mapping
    assert shallow._changes_queue is model._changes_queue
    _assert_no_base_private_storage(model)
    _assert_no_base_private_storage(shallow)


def test_deep_copy_owner_uses_exact_new_map_and_shared_payload_memo():
    class Payload:
        pass

    payload = Payload()
    model = _model_type(actual)(value=payload)
    model._changes_queue["value"] = payload
    memo = {}
    deep = copy.deepcopy(model, memo)
    owner = native.model_transaction_owner(model)
    deep_owner = native.model_transaction_owner(deep)
    assert deep_owner is not owner
    assert deep_owner.private_mapping is deep.__pydantic_private__
    assert deep_owner.private_mapping is not owner.private_mapping
    assert deep._changes_queue["value"] is deep.value
    assert deep.value is memo[id(payload)]
    assert deep.value is not payload
    _assert_no_base_private_storage(deep)


def test_payload_model_owner_cycle_is_collectible():
    def scenario():
        class Payload:
            pass

        model = _model_type(actual)()
        payload = Payload()
        payload.model = model
        model._changes_queue["adapter"] = payload
        owner = native.model_transaction_owner(model)
        return [
            weakref.ref(model),
            weakref.ref(payload),
            weakref.ref(owner),
            weakref.ref(model._change_state),
        ]

    references = scenario()
    gc.collect()
    assert all(reference() is None for reference in references)


def test_unready_owner_heap_subtype_cycle_is_collectible():
    def scenario():
        class Owner(native.ModelTransactionOwner):
            pass

        owner = Owner.__new__(Owner)
        Owner.default = owner
        return weakref.ref(Owner), weakref.ref(owner)

    references = scenario()
    gc.collect()
    assert all(reference() is None for reference in references)


@pytest.mark.parametrize("edit", ["replace", "pop"])
def test_saved_private_map_queue_edits_release_old_payload_immediately(edit):
    model = _model_type(actual)()
    owner = native.model_transaction_owner(model)
    private = model.__pydantic_private__
    replacement = {}
    trace = []
    target = weakref.ref(model)

    class Payload:
        def __del__(self):
            current = target()
            trace.append(
                (
                    current.__pydantic_private__ is private,
                    native.model_transaction_owner(current).private_mapping is private,
                    private.get("_changes_queue") is replacement,
                    "_changes_queue" not in private,
                )
            )

    payload = Payload()
    reference = weakref.ref(payload)
    private["_changes_queue"] = {"adapter": payload}
    del payload
    if edit == "replace":
        private["_changes_queue"] = replacement
        assert model._changes_queue is replacement
    else:
        private.pop("_changes_queue")
        with pytest.raises(AttributeError):
            _ = model._changes_queue
    assert reference() is None
    assert trace == [(True, True, edit == "replace", edit == "pop")]
    assert owner.private_mapping is private


@pytest.mark.parametrize("edit", ["replace", "delete"])
def test_private_map_publication_precedes_old_payload_finalizer_reentry(edit):
    model = _model_type(actual)()
    owner = native.model_transaction_owner(model)
    replacement = dict(model.__pydantic_private__)
    replacement["_changes_queue"] = {}
    target = weakref.ref(model)
    trace, errors = [], []

    class Payload:
        def __del__(self):
            current = target()
            try:
                if edit == "replace":
                    private = current.__pydantic_private__
                    trace.append(
                        (private is replacement, owner.private_mapping is replacement)
                    )
                    current._changes_queue["reentered"] = 17
                else:
                    trace.append((owner.private_mapping is None,))
                    try:
                        _ = current.__pydantic_private__
                    except AttributeError:
                        trace.append("missing")
            except Exception as error:
                errors.append((type(error).__name__, str(error)))

    payload = Payload()
    reference = weakref.ref(payload)
    model.__pydantic_private__["adapter_payload"] = payload
    del payload
    if edit == "replace":
        model.__pydantic_private__ = replacement
        assert model._changes_queue == {"reentered": 17}
        assert trace == [(True, True)]
    else:
        del model.__pydantic_private__
        assert trace == [(True,), "missing"]
    assert reference() is None
    assert errors == []
    _assert_no_base_private_storage(model)
