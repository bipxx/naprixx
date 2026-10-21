"""Only missing whole-container state/lookup contracts; original bodies are test-only.

The unchanged collection/move/completion/original suites supply established
behavior. This cohort adds physical allocation, live namespace/restoration,
executing globals, delayed lexical super and child callback boundaries.
"""
from __future__ import annotations

import ast
import copy
import gc
import hashlib
import importlib
import inspect
import pickle
import sys
import types
import weakref
from pathlib import Path
from types import ModuleType, SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / ".upstream/napari/src/napari"
PINNED = {
    "_typed.py": "e2ebed37048852d99c9cb6e13fc5188b21f205acda6e2c57591a0fdcada991b8",
    "_evented_list.py": "028b81d7700575ddbc34fca01202cb4970598174a0be2eb8dfe9ec1ef51ce0fe",
}


def _clone(base, loaded, label, native_candidate=False):
    result = {}
    for filename, owner in (("_typed.py", "TypedMutableSequence"),
                            ("_evented_list.py", "EventedList")):
        path = base / "utils/events/containers" / filename
        raw = path.read_bytes()
        if base == UPSTREAM:
            assert hashlib.sha256(raw).hexdigest() == PINNED[filename]
        tree = ast.parse(raw.decode("utf-8"))
        actual = loaded[filename]
        name = "_naparixx_container_" + label + filename[:-3]
        module = ModuleType(name)
        module.__dict__.update(vars(actual))
        module.__name__ = name
        module.__file__ = str(path)
        # Genuine function builtins are independent from production and the peer.
        module.__dict__["__builtins__"] = dict(vars(importlib.import_module("builtins")))
        if filename == "_evented_list.py":
            module.TypedMutableSequence = result["typed"].TypedMutableSequence
        definitions = [n for n in tree.body if isinstance(n, ast.FunctionDef)
                       or isinstance(n, ast.ClassDef) and n.name == owner
                       or isinstance(n, ast.ImportFrom) and n.module == "__future__"]
        cls = next(n for n in definitions if isinstance(n, ast.ClassDef))
        if native_candidate and filename == "_typed.py":
            # Explicit test-only source-identical physical allocation candidate.
            # No original method body is changed or used by production.
            cls.bases.insert(0, ast.Attribute(value=ast.Name(id="_native", ctx=ast.Load()),
                                             attr="TransformNamespaceBase", ctx=ast.Load()))
        body = ast.fix_missing_locations(ast.Module(body=definitions, type_ignores=[]))
        sys.modules[name] = module
        exec(compile(body, str(path), "exec", dont_inherit=True), vars(module))
        result["typed" if filename == "_typed.py" else "evented"] = module
    return SimpleNamespace(**result)


@pytest.fixture(scope="module")
def implementations():
    loaded = {name: importlib.import_module("napari.utils.events.containers." + name[:-3])
              for name in PINNED}
    pair = (_clone(UPSTREAM, loaded, "original"),
            _clone(ROOT / "src/napari", loaded, "current"))
    for impl in pair:
        assert impl.evented.TypedMutableSequence is impl.typed.TypedMutableSequence
        assert impl.typed.TypedMutableSequence.__init__.__globals__ is vars(impl.typed)
        assert impl.evented.EventedList.__init__.__globals__ is vars(impl.evented)
    yield pair
    for impl in pair:
        for module in (impl.typed, impl.evented):
            sys.modules.pop(module.__name__, None)


@pytest.fixture(scope="module")
def candidate():
    loaded = {name: importlib.import_module("napari.utils.events.containers." + name[:-3])
              for name in PINNED}
    impl = _clone(UPSTREAM, loaded, "physical_candidate", native_candidate=True)
    yield impl
    for module in (impl.typed, impl.evented):
        sys.modules.pop(module.__name__, None)


def _class(impl, kind):
    return impl.typed.TypedMutableSequence if kind == "typed" else impl.evented.EventedList


def _pair(implementations, scenario, contract=None):
    expected = scenario(implementations[0])
    if contract is not None:
        assert expected == contract
    assert scenario(implementations[1]) == expected
    return expected


def _outcome(call):
    try:
        value = call()
    except Exception as error:
        return "error", type(error).__name__, str(error)
    return "value", value


def test_exact_complete_class_metadata_and_native_adoption(implementations):
    original, current = implementations
    for kind, count in (("typed", 19), ("evented", 13)):
        left, right = _class(original, kind), _class(current, kind)
        methods = [(name, method) for name, method in vars(left).items()
                   if isinstance(method, types.FunctionType)]
        assert len(methods) == count
        for name, method in methods:
            actual = vars(right)[name]
            assert str(inspect.signature(actual)) == str(inspect.signature(method))
            assert actual.__doc__ == method.__doc__
            assert actual.__qualname__ == method.__qualname__
            assert inspect.isgeneratorfunction(actual) == inspect.isgeneratorfunction(method)
        assert left.__doc__ == right.__doc__
    assert str(inspect.signature(original.typed._noop)) == str(inspect.signature(current.typed._noop))
    native = importlib.import_module("naparixx")._native
    calls = {n.func.attr for n in ast.walk(ast.parse((ROOT / "src/napari/utils/events/containers/_typed.py").read_text()))
             if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute)}
    if "typed_container_locals" in calls:
        # Actual future installation, not the separate test-only candidate.
        assert native.TransformNamespaceBase in current.typed.TypedMutableSequence.__mro__
        assert native.TransformNamespaceBase in current.evented.EventedList.__mro__
        actual = importlib.import_module("napari.utils.events.containers._typed").TypedMutableSequence
        assert native.TransformNamespaceBase in actual.__mro__
        assert callable(native.typed_container_locals) and callable(native.evented_container_locals)


@pytest.mark.parametrize("kind", ["typed", "evented"])
def test_source_identical_native_namespace_candidate_generic_and_descriptors(candidate, kind):
    cls = _class(candidate, kind)
    native = importlib.import_module("naparixx")._native
    assert native.TransformNamespaceBase in cls.__mro__
    assert cls[int] is not None
    instance = object.__new__(cls)
    cls.__init__(instance, [1, 2], basetype=int)
    saved = vars(instance)
    assert object.__getattribute__(instance, "_list") is saved["_list"]
    reference = weakref.ref(instance)
    class Derived(cls):
        @property
        def marker(self):
            return self.__dict__["extra"]
    derived = Derived([3])
    derived.extra = object()
    assert derived.marker is vars(derived)["extra"]
    assert weakref.ref(derived)() is derived
    del derived, instance
    gc.collect()
    assert reference() is None
    assert saved["_list"] == [1, 2]


@pytest.mark.parametrize("kind", ["typed", "evented"])
def test_live_dictionary_replacement_deletion_and_backing_aliases(implementations, kind):
    def scenario(impl):
        cls = _class(impl, kind)
        sequence = cls([1, 2], basetype=int, lookup={str: str})
        old = vars(sequence)
        old_list, old_lookup = old["_list"], old["_lookup"]
        new = old.copy()
        new_list, new_lookup = [7], {str: lambda value: "new"}
        new.update(_list=new_list, _lookup=new_lookup, _basetypes=(int,))
        sequence.__dict__ = new
        a = (vars(sequence) is new, len(sequence), sequence["new"],
             object.__getattribute__(sequence, "_list") is new_list)
        sequence.insert(1, 8)
        b = (tuple(new_list), tuple(old_list), old["_lookup"] is old_lookup,
             sequence._lookup is new_lookup)
        del sequence.__dict__
        cleared = vars(sequence) == {}
        sequence.__dict__ = new
        return a, b, cleared, sequence[0]
    _pair(implementations, scenario, ((True, 1, 7, True), ((7, 8), (1, 2), True, True), True, 7))


@pytest.mark.parametrize("kind", ["typed", "evented"])
@pytest.mark.parametrize("operation", ["copy", "deepcopy", "pickle"])
def test_state_restoration_uses_actual_dictionary_without_native_mirror(implementations, kind, operation):
    def scenario(impl):
        cls = _class(impl, kind)
        sequence = cls([1, 2], basetype=int, lookup={str: str})
        sequence.extra = ["marker"]
        # Warm every exposed source state before copy/reduction.
        assert sequence["1"] == 1 and len(sequence) == 2
        namespace = vars(sequence)
        invoke = {"copy": copy.copy, "deepcopy": copy.deepcopy,
                  "pickle": lambda value: pickle.loads(pickle.dumps(value))}[operation]
        try:
            restored = invoke(sequence)
        except Exception as error:
            # Source owns any pre-existing non-pickleable emitter payload.
            return "error", type(error).__name__, str(error), tuple(sequence), vars(sequence) is namespace
        return ("value", type(restored) is cls, restored is not sequence,
                tuple(restored), vars(restored) is not namespace,
                restored.extra == ["marker"], restored._lookup is namespace["_lookup"],
                restored._list is namespace["_list"], restored.extra is sequence.extra)
    _pair(implementations, scenario)


@pytest.mark.parametrize("kind", ["typed", "evented"])
def test_owner_dictionary_payload_cycles_collect_and_saved_alias_stays_live(implementations, kind):
    def scenario(impl):
        cls = _class(impl, kind)
        sequence = cls([1, 2])
        saved = vars(sequence)
        backing = saved["_list"]
        reference = weakref.ref(sequence)
        del sequence
        gc.collect()
        alias_survives = reference() is None and saved["_list"] is backing
        other = cls([3])
        other.__dict__["cycle"] = other
        other_ref = weakref.ref(other)
        del other
        gc.collect()
        return alias_survives, other_ref() is None, tuple(backing)
    _pair(implementations, scenario, (True, True, (1, 2)))


def test_actual_transform_chain_shares_single_physical_container_ancestry():
    from napari.utils.transforms.transforms import ScaleTranslate, TransformChain
    from naparixx import _native
    chain = TransformChain()
    assert _native.TransformNamespaceBase in type(chain).__mro__
    namespace = vars(chain)
    backing = namespace["_list"]
    item = ScaleTranslate(scale=(2,))
    backing.append(item)
    assert len(chain) == 1 and chain[0] is item
    replacement = namespace.copy()
    replacement["_list"] = []
    chain.__dict__ = replacement
    assert vars(chain) is replacement and len(chain) == 0
    assert namespace["_list"] is backing and backing[0] is item


@pytest.mark.parametrize("keyed", [False, True])
def test_contains_super_lookup_occurs_only_after_lookup_fallback(implementations, monkeypatch, keyed):
    def scenario(impl):
        log = []
        sequence = impl.typed.TypedMutableSequence([1], lookup={str: str})
        class Base:
            def __contains__(self, key):
                log.append(("contains", key))
                return True
        def super_provider():
            log.append("super")
            return Base()
        with monkeypatch.context() as patch:
            patch.setitem(vars(impl.typed), "super", super_provider)
            answer = type(sequence).__contains__(sequence, "1" if keyed else 1)
        return tuple(log), answer
    _pair(implementations, scenario, (() if keyed else ("super", ("contains", 1)), True))


@pytest.mark.parametrize("operation", ["init", "setitem", "insert"])
@pytest.mark.parametrize("failure", [False, True])
def test_evented_super_capture_follows_exact_publication_and_branch(implementations, monkeypatch, operation, failure):
    def scenario(impl):
        log = []
        cls = impl.evented.EventedList
        sequence = object.__new__(cls)
        class Events:
            def inserting(self, **kwargs):
                log.append(("inserting", kwargs["index"]))
            def inserted(self, **kwargs):
                log.append(("inserted", kwargs["index"], kwargs["value"]))
            def changed(self, **kwargs):
                log.append(("changed", kwargs["old_value"], kwargs["value"]))
        events = Events()
        class Base:
            def __init__(self, *args, **kwargs):
                if not args and not kwargs:
                    return
                log.append("base-init")
                if failure:
                    raise RuntimeError("base failed")
                impl.typed.TypedMutableSequence.__init__(sequence, *args, **kwargs)
            def __setitem__(self, key, value):
                log.append(("base-set", key, value))
                if failure:
                    raise RuntimeError("base failed")
                impl.typed.TypedMutableSequence.__setitem__(sequence, key, value)
            def insert(self, index, value):
                log.append(("base-insert", index, value))
                if failure:
                    raise RuntimeError("base failed")
                impl.typed.TypedMutableSequence.insert(sequence, index, value)
        def super_provider():
            log.append(("super", getattr(sequence, "events", None) is events))
            return Base()
        def group(*args, **kwargs):
            log.append("group")
            return events
        if operation != "init":
            impl.typed.TypedMutableSequence.__init__(sequence, (), basetype=int)
            sequence._list.append(1)
            sequence.events = events
            sequence._connect_child_emitters = lambda value: log.append(("connect", value))
        with monkeypatch.context() as patch:
            patch.setitem(vars(impl.evented), "super", super_provider)
            patch.setitem(vars(impl.evented), "EmitterGroup", group)
            if operation == "init":
                outcome = _outcome(lambda: cls.__init__(sequence, [1], basetype=int))
            elif operation == "setitem":
                outcome = _outcome(lambda: cls.__setitem__(sequence, 0, 2))
            else:
                outcome = _outcome(lambda: cls.insert(sequence, 1, 2))
        values = tuple(vars(sequence).get("_list", ()))
        return tuple(log), outcome, values, sequence.events is events
    prefix = {"init": ("group", ("super", True), "base-init"),
              "setitem": (("super", True), ("base-set", 0, 2)),
              "insert": (("inserting", 1), ("super", True), ("base-insert", 1, 2))}[operation]
    if failure:
        contract = prefix, ("error", "RuntimeError", "base failed"), (() if operation == "init" else (1,)), True
    else:
        suffix = {"init": (("inserting", 0), ("super", True),
                           ("base-insert", 0, 1), ("inserted", 0, 1)),
                  "setitem": (("changed", 1, 2),),
                  "insert": (("inserted", 1, 2), ("connect", 2))}[operation]
        contract = prefix + suffix, ("value", None), ({"init": (1,), "setitem": (2,), "insert": (1, 2)}[operation]), True
    _pair(implementations, scenario, contract)


@pytest.mark.parametrize("operation", ["len", "noop", "any"])
def test_source_executing_globals_and_lazy_consumer_are_authoritative(implementations, monkeypatch, operation):
    def scenario(impl):
        log = []
        ns = vars(impl.typed)
        sequence = impl.typed.TypedMutableSequence([1, 2], basetype=(int, float))
        with monkeypatch.context() as patch:
            if operation == "len":
                def length(value):
                    log.append(("len", value is sequence._list))
                    return 113
                patch.setitem(ns, "len", length)
                answer = type(sequence).__len__(sequence)
            elif operation == "noop":
                def convert(value):
                    log.append(("noop", value))
                    return 2
                patch.setitem(ns, "_noop", convert)
                answer = sequence.index(2)
            else:
                def accepts(value, dtype):
                    log.append(("isinstance", value, dtype.__name__))
                    return True
                def consume(generator):
                    log.append(("any", isinstance(generator, types.GeneratorType), generator.__qualname__))
                    first = next(generator)
                    ns["isinstance"] = accepts
                    second = next(generator)
                    log.append(("predicates", first, second))
                    return True
                patch.setitem(ns, "any", consume)
                patch.setitem(ns, "isinstance", isinstance)
                answer = sequence._type_check(7)
        return tuple(log), answer
    contract = {"len": ((("len", True),), 113),
                "noop": ((("noop", 1),), 0),
                "any": ((("any", True, "TypedMutableSequence._type_check.<locals>.<genexpr>"),
                         ("isinstance", 7, "float"), ("predicates", True, True)), 7)}[operation]
    _pair(implementations, scenario, contract)


@pytest.mark.parametrize("operation", ["connect", "disconnect"])
@pytest.mark.parametrize("failure", [False, True])
def test_child_callback_capture_uses_fresh_events_and_original_error_prefix(implementations, monkeypatch, operation, failure):
    def scenario(impl):
        log = []
        receiver = object()
        class Events:
            current = None
            @property
            def source(self):
                log.append("source")
                return self.current
            @source.setter
            def source(self, value):
                log.append("source-store")
                self.current = value
            @property
            def connect(self):
                log.append("connect-lookup")
                return self.callback
            @property
            def disconnect(self):
                log.append("disconnect-lookup")
                return self.callback
            def callback(self, value):
                log.append(("callback", value is receiver))
                if failure:
                    raise RuntimeError("child callback failed")
        events = Events()
        class Child:
            @property
            def events(self):
                log.append("events")
                return events
        class Parent(impl.evented.EventedList):
            @property
            def _reemit_child_event(self):
                log.append("receiver")
                return receiver
        parent = object.__new__(Parent)
        child = Child()
        with monkeypatch.context() as patch:
            patch.setitem(vars(impl.evented), "SupportsEvents", Child)
            call = (parent._connect_child_emitters if operation == "connect" else parent._disconnect_child_emitters)
            outcome = _outcome(lambda: call(child))
        return tuple(log), outcome, events.current is child
    if operation == "connect":
        log = ("events", "source", "events", "source-store", "events", "connect-lookup", "receiver", ("callback", True))
    else:
        log = ("events", "disconnect-lookup", "receiver", ("callback", True))
    outcome = ("error", "RuntimeError", "child callback failed") if failure else ("value", None)
    _pair(implementations, scenario, (log, outcome, operation == "connect"))


@pytest.mark.parametrize("kind", ["typed", "evented"])
def test_original_assigned_result_or_deleted_item_survives_saved_lookup_error(implementations, kind):
    def scenario(impl):
        refs = []
        class Payload(list):
            def __init__(self):
                super().__init__([1])
                refs.append(weakref.ref(self))
        class Backing:
            def __getitem__(self, key):
                return Payload()
            def pop(self, index):
                return Payload()
        if kind == "typed":
            class Derived(impl.typed.TypedMutableSequence):
                @property
                def __newlike__(self):
                    raise RuntimeError("newlike lookup failed")
        else:
            class Derived(impl.evented.EventedList):
                def __getitem__(self, key):
                    return object()
                @property
                def _process_delete_item(self):
                    raise RuntimeError("delete hook lookup failed")
        sequence = object.__new__(Derived)
        sequence._list, sequence._lookup, sequence._basetypes = Backing(), {}, ()
        if kind == "evented":
            sequence.events = SimpleNamespace(removing=lambda **kwargs: None,
                                              removed=lambda **kwargs: None)
            sequence._delitem_indices = lambda key: [(sequence, 0)]
            sequence._disconnect_child_emitters = lambda value: None
        saved = None
        try:
            if kind == "typed":
                sequence[0]
            else:
                del sequence[0]
        except RuntimeError as error:
            message, saved = str(error), error.__traceback__
        else:
            raise AssertionError("expected source lookup failure")
        assert len(refs) == 1
        retained = refs[0]() is not None
        del saved
        gc.collect()
        return message, retained, refs[0]() is None
    _pair(implementations, scenario,
          (("newlike lookup failed" if kind == "typed" else "delete hook lookup failed"), True, True))
