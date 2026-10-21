"""Context provider lifetimes through the three public native entry points."""

import gc
import weakref
from types import ModuleType

import pytest

from napari.utils.events.containers import EventedList, NestableEventedList
from napari.utils.events.evented_model import EventedModel


class _PayloadModel(EventedModel):
    value: int = 0


class _ContextMethod:
    def __init__(self, name, trace, payload, *, raises=False, removes=False):
        self.name = name
        self.trace = trace
        self.payload = payload
        self.raises = raises
        self.removes = removes

    def __get__(self, instance, owner):
        name, trace = self.name, self.trace
        trace.append(("bind", name))
        if self.raises:
            raise LookupError(f"binding {name}")
        if self.removes:
            delattr(owner, name)
            gc.collect()
            # Removing the last namespace reference during descriptor binding
            # must leave the descriptor and its payload alive until it returns.
            assert self.payload.model.value == 0
            trace.append(("alive after removal", name))

        if name == "__enter__":

            def enter():
                trace.append(("call", name))
                return instance

            return enter

        def exit(kind, value, traceback):
            assert (kind, value, traceback) == (None, None, None)
            trace.append(("call", name))
            return False

        return exit


@pytest.fixture(params=["model_update", "list_move_multiple", "nested_move"])
def context_route(request):
    return request.param


def _make_context(*, inherited=False, missing=None, raises=None, removes=False):
    trace = []
    payload = ModuleType("_context_owner_payload")
    payload.model = _PayloadModel()
    payload.journal = payload.model._change_state
    # Include the native journal in a realistic adapter cycle. The context
    # namespace is its only external owner after this factory returns.
    object.__setattr__(payload.model, "adapter_owner", payload)
    references = {
        "namespace payload": weakref.ref(payload),
        "payload model": weakref.ref(payload.model),
        "payload journal": weakref.ref(payload.journal),
    }
    namespace = {"count": 0, "payload": payload}
    for name in ("__enter__", "__exit__"):
        if name == missing:
            continue
        descriptor = _ContextMethod(
            name, trace, payload, raises=name == raises, removes=removes
        )
        namespace[name] = descriptor
        references[name] = weakref.ref(descriptor)

    owner = type("ContextOwner", (), namespace)
    references["method owner type"] = weakref.ref(owner)
    if inherited:
        # Both intervening namespaces are visited without finding the method.
        middle = type("ContextMiddle", (owner,), {"payload": payload})
        owner = type("InheritedContext", (middle,), {"payload": payload})
        references["middle type"] = weakref.ref(middle)
        references["context type"] = weakref.ref(owner)
    context = owner()
    references["context"] = weakref.ref(context)
    return context, references, trace


def _invoke_route(route, context, references):
    if route == "model_update":
        target = _PayloadModel()
        references["public target"] = weakref.ref(target)
        references["public target journal"] = weakref.ref(target._change_state)
        target.events.blocker = lambda: context
        target.update({"value": 7})
        assert target.value == 7
    elif route == "list_move_multiple":
        target = EventedList([0, 1, 2])
        references["public target"] = weakref.ref(target)
        target.events.reordered.blocker = lambda: context
        assert target.move_multiple([2], 0) == 1
        assert list(target) == [2, 0, 1]
    else:
        target = NestableEventedList([[0, 1], [2]])
        references["public target"] = weakref.ref(target)
        target.events.blocker_all = lambda: context
        assert target.move((0, 0), (1, 0)) is True
        assert [list(child) for child in target] == [[1], [0, 2]]


def _assert_released(references):
    gc.collect()
    retained = [
        name for name, reference in references.items() if reference() is not None
    ]
    assert retained == []


def test_inherited_context_namespaces_and_payload_are_collectible(context_route):
    context, references, trace = _make_context(inherited=True)
    _invoke_route(context_route, context, references)
    assert trace.count(("call", "__enter__")) == 1
    assert trace.count(("call", "__exit__")) == 1
    del context
    _assert_released(references)


@pytest.mark.parametrize("missing", ["__enter__", "__exit__"])
def test_missing_context_method_releases_visited_namespaces(context_route, missing):
    def scenario():
        context, references, trace = _make_context(missing=missing)
        with pytest.raises(TypeError, match="context manager protocol"):
            _invoke_route(context_route, context, references)
        assert not any(action == "call" for action, name in trace)
        return references

    _assert_released(scenario())


@pytest.mark.parametrize("raises", ["__enter__", "__exit__"])
def test_descriptor_binding_failure_releases_namespace_and_payload(
    context_route, raises
):
    def scenario():
        context, references, trace = _make_context(raises=raises)
        with pytest.raises(LookupError, match=f"binding {raises}"):
            _invoke_route(context_route, context, references)
        assert not any(action == "call" for action, name in trace)
        return references

    _assert_released(scenario())


def test_self_removing_descriptors_survive_binding_and_then_release(context_route):
    context, references, trace = _make_context(removes=True)
    _invoke_route(context_route, context, references)
    for name in ("__enter__", "__exit__"):
        assert ("alive after removal", name) in trace
        assert trace.count(("call", name)) == 1
        # The callable returned by __get__ does not retain its descriptor.
        assert references[name]() is None
    del context
    _assert_released(references)
