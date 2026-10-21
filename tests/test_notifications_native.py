"""Finite pinned/current notification, enum and traceback owner contracts.

Original bodies exist only in registered test namespaces. Application imports
always resolve the actual fork. The layout candidate explicitly uses the already
installed VispyNamespaceBase; installed adoption requires NotificationNamespaceBase.
Worker exception-frame transport and unchanged manager/Qt suites are reused.
"""

from __future__ import annotations

import ast
import builtins
import copy
import gc
import hashlib
import importlib
import inspect
import json
import pickle
import sys
import types
import weakref
from enum import StrEnum, auto
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
BEFORE = ".artifacts/viewer-notifications-before/index.json"
BEFORE_SHA = "0de81c33a33e249c904d98c591a35318cfadf83b6630bca7909a2c54cc07fe0f"
SCOPE = ".artifacts/viewer-notifications-scope-next.json"
SCOPE_SHA = "01c22e83f40562ed1eb6faebb48433c2cca9ee95e406f30e15f3d2d0ab32c28c"
HASHES = {
    "src/napari/utils/notifications.py": (
        "19fb6e77d2440aaddd47dcef63430b1daa6257cd09c5ed6947531a786016cb43"
    ),
    ".upstream/napari/src/napari/utils/notifications.py": (
        "19fb6e77d2440aaddd47dcef63430b1daa6257cd09c5ed6947531a786016cb43"
    ),
    "src/napari/utils/_tracebacks.py": (
        "47528709d7098863c45c5acbdfd4db31ea54a35a35a4741f2c9f6ad08fa00e18"
    ),
    ".upstream/napari/src/napari/utils/_tracebacks.py": (
        "47528709d7098863c45c5acbdfd4db31ea54a35a35a4741f2c9f6ad08fa00e18"
    ),
    "src/napari/utils/misc.py": (
        "3a2eef0df72e9b9ac4ac7d0ceea2c3e38b837bacda7c7e84c36bf5339f25cea5"
    ),
    ".upstream/napari/src/napari/utils/misc.py": (
        "082c9f867281eecdad79ca9a34d9401b6db7e025c6a40be233441a44d5563bec"
    ),
}
ORIGINAL_SELECTORS = (
    ".upstream/napari/src/napari/utils/_tests/test_notification_manager.py",
    ".upstream/napari/src/napari/_qt/_tests/test_qt_notifications.py::test_notification_display",
    ".upstream/napari/src/napari/_qt/_tests/test_qt_notifications.py::test_notification_error",
    ".upstream/napari/src/napari/_qt/_tests/test_qt_notifications.py::test_show_notification_from_thread",
)
REUSED_WORKER_SELECTOR = (
    "tests/test_viewer_workers_native.py::"
    "test_actual_notification_record_holds_and_releases_source_worker_model_leases"
)


def _snapshot(path):
    raw = (ROOT / BEFORE).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == BEFORE_SHA
    row = next(r for r in json.loads(raw)["files"] if r["path"] == path)
    source = (ROOT / row["before"]).read_bytes()
    assert hashlib.sha256(source).hexdigest() == HASHES[path] == row["before_sha256"]
    return ast.parse(source), source, ROOT / row["before"]


def _module(name, tree, path, monkeypatch, namespace=None):
    module = types.ModuleType(name)
    module.__dict__.update(namespace or {})
    module.__dict__["__name__"] = name
    monkeypatch.setitem(sys.modules, name, module)
    exec(
        compile(ast.fix_missing_locations(tree), str(path), "exec", dont_inherit=True),
        module.__dict__,
    )
    return module


def _source_family(mode, monkeypatch, candidate=False):
    prefix = ".upstream/napari/" if mode == "pinned" else ""
    misc, _, misc_path = _snapshot(prefix + "src/napari/utils/misc.py")
    original_enum = next(
        n for n in misc.body if isinstance(n, ast.ClassDef) and n.name == "StringEnum"
    )
    futures = [
        n
        for n in misc.body
        if isinstance(n, ast.ImportFrom) and n.module == "__future__"
    ]
    enum_tree = ast.Module(
        body=[*futures, copy.deepcopy(original_enum)], type_ignores=[]
    )
    misc_module = _module(
        "_naparixx_notification_enum_" + mode,
        enum_tree,
        misc_path,
        monkeypatch,
        {"StrEnum": StrEnum, "builtins": builtins},
    )
    tree, _, path = _snapshot(prefix + "src/napari/utils/notifications.py")
    tree = copy.deepcopy(tree)
    tree.body = [
        n
        for n in tree.body
        if not (isinstance(n, ast.ImportFrom) and n.module == "napari.utils.misc")
    ]
    namespace = {"StringEnum": misc_module.StringEnum}
    if candidate:
        from naparixx import _native

        namespace["_candidate_namespace"] = _native.VispyNamespaceBase
        for node in tree.body:
            if isinstance(node, ast.ClassDef) and node.name in {
                "Notification",
                "NotificationManager",
            }:
                node.bases.insert(
                    0, ast.Name(id="_candidate_namespace", ctx=ast.Load())
                )
    notifications = _module(
        "_naparixx_notifications_" + mode, tree, path, monkeypatch, namespace
    )
    tb_tree, _, tb_path = _snapshot(prefix + "src/napari/utils/_tracebacks.py")
    tracebacks = _module(
        "_naparixx_notification_tracebacks_" + mode, tb_tree, tb_path, monkeypatch
    )
    return SimpleNamespace(
        mode=mode, module=notifications, misc=misc_module, tracebacks=tracebacks
    )


@pytest.fixture
def families(monkeypatch):
    scope_raw = (ROOT / SCOPE).read_bytes()
    assert hashlib.sha256(scope_raw).hexdigest() == SCOPE_SHA
    actual = SimpleNamespace(
        mode="actual",
        module=importlib.import_module("napari.utils.notifications"),
        misc=importlib.import_module("napari.utils.misc"),
        tracebacks=importlib.import_module("napari.utils._tracebacks"),
    )
    return (
        _source_family("pinned", monkeypatch),
        _source_family("current_fork", monkeypatch),
        actual,
    )


def _pair(families, scenario):
    outcomes = [scenario(f) for f in families]
    assert outcomes[1:] == outcomes[:1] * 2
    return outcomes[0]


def _pick(tree, qualified, accessor=None):
    parts = qualified.split(".")
    nodes = tree.body
    for part in parts[:-1]:
        nodes = next(
            n for n in nodes if isinstance(n, ast.ClassDef) and n.name == part
        ).body
    matches = [
        n for n in nodes if isinstance(n, ast.FunctionDef) and n.name == parts[-1]
    ]
    if accessor:
        matches = [
            n
            for n in matches
            if any(
                isinstance(d, ast.Attribute) and d.attr == "setter"
                for d in n.decorator_list
            )
            == (accessor == "setter")
        ]
    assert len(matches) == 1
    return matches[0]


def _function(module, qualified, accessor=None):
    parts = qualified.split(".")
    owner = module
    for part in parts[:-1]:
        owner = getattr(owner, part)
    value = inspect.getattr_static(owner, parts[-1])
    if isinstance(value, property):
        return value.fset if accessor == "setter" else value.fget
    if isinstance(value, (classmethod, staticmethod)):
        return value.__func__
    return value


def _bare(family):
    obj = object.__new__(family.module.NotificationManager)
    obj.records = []
    obj.exit_on_error = False
    obj.catch_error = True
    obj.notification_ready = obj.changed = lambda record: None
    obj._originals_thread_except_hooks = []
    obj._originals_except_hooks = []
    obj._original_showwarnings_hooks = []
    obj._seen_warnings = set()
    return obj


def test_all45_metadata_and_actual_plain_namespace_adoption(families):
    scope = json.loads((ROOT / SCOPE).read_bytes())
    notification_tree, _, _ = _snapshot(
        ".upstream/napari/src/napari/utils/notifications.py"
    )
    actual_tree = ast.parse((ROOT / "src/napari/utils/notifications.py").read_bytes())
    changed = 0
    for row in scope["core"]["definitions"]:
        old = _pick(notification_tree, row["qualified_name"], row["accessor"])
        current = _pick(actual_tree, row["qualified_name"], row["accessor"])
        changed += ast.dump(ast.Module(body=old.body, type_ignores=[])) != ast.dump(
            ast.Module(body=current.body, type_ignores=[])
        )
        lhs, rhs = copy.deepcopy(old), copy.deepcopy(current)
        lhs.body = rhs.body = []
        assert ast.dump(lhs, include_attributes=False) == ast.dump(
            rhs, include_attributes=False
        )
        originals = [
            _function(f.module, row["qualified_name"], row["accessor"])
            for f in families
        ]
        assert len({str(inspect.signature(fn)) for fn in originals}) == 1
        assert len({fn.__doc__ for fn in originals}) == 1
        assert len({fn.__qualname__ for fn in originals}) == 1
    assert changed in (0, 34)
    from naparixx import _native

    if changed:
        assert issubclass(
            families[-1].module.Notification, _native.NotificationNamespaceBase
        )
        assert issubclass(
            families[-1].module.NotificationManager, _native.NotificationNamespaceBase
        )
        assert not issubclass(
            families[-1].module.NotificationSeverity, _native.NotificationNamespaceBase
        )
    for name in (
        "_generate_next_value_",
        "__str__",
        "_missing_",
        "__eq__",
        "__ne__",
        "__hash__",
        "keys",
    ):
        functions = [_function(f.misc, "StringEnum." + name) for f in families]
        assert len({str(inspect.signature(fn)) for fn in functions}) == 1
        assert len({fn.__doc__ for fn in functions}) == 1
    for name in ("get_tb_formatter", "ansi2html"):
        functions = [getattr(f.tracebacks, name) for f in families]
        assert len({str(inspect.signature(fn)) for fn in functions}) == 1
        assert len({fn.__doc__ for fn in functions}) == 1
        if name == "ansi2html":
            assert all(inspect.isgeneratorfunction(fn) for fn in functions)
    assert isinstance(
        families[-1].module.notification_manager,
        families[-1].module.NotificationManager,
    )


@pytest.mark.parametrize("kind", ["notification", "manager"])
def test_canonical_dictionary_saved_replaced_deleted_aliases_and_gc(
    families, monkeypatch, kind
):
    candidate = _source_family("layout_candidate", monkeypatch, candidate=True)
    for family in (*families, candidate):
        cls = (
            family.module.Notification
            if kind == "notification"
            else family.module.NotificationManager
        )
        obj = object.__new__(cls)
        cls.__init__(obj, "original") if kind == "notification" else cls.__init__(obj)
        saved = vars(obj)
        assert saved is obj.__dict__
        replacement = saved.copy()
        obj.__dict__ = replacement
        assert vars(obj) is replacement and saved is not replacement
        field = "_message" if kind == "notification" else "records"
        marker = object()
        replacement[field] = marker
        assert (
            getattr(obj, "message" if kind == "notification" else "records") is marker
        )
        del replacement[field]
        with pytest.raises(AttributeError):
            getattr(obj, "message" if kind == "notification" else "records")
        replacement.update(saved)
        replacement["loop"] = obj
        reference = weakref.ref(obj)
        del obj, replacement
        gc.collect()
        assert reference() is None


@pytest.mark.parametrize("operation", ["copy", "deepcopy", "pickle"])
def test_plain_namespace_restoration_preserves_original_success_or_failure(
    families, monkeypatch, operation
):
    candidate = _source_family("layout_candidate", monkeypatch, candidate=True)

    def scenario(family):
        obj = family.module.Notification("saved", actions=[("go", None)])
        obj.metadata = {"payload": [1, 2]}
        try:
            restored = {
                "copy": copy.copy,
                "deepcopy": copy.deepcopy,
                "pickle": lambda value: pickle.loads(pickle.dumps(value)),
            }[operation](obj)
        except Exception as error:
            return ("error", type(error).__name__, str(error))
        assert restored is not obj and vars(restored) is not vars(obj)
        assert restored.message == "saved" and restored.metadata == {"payload": [1, 2]}
        assert restored.actions == [("go", None)]
        assert (restored.metadata is obj.metadata) == (operation == "copy")
        return ("ok", str(restored.severity), restored.type)

    result = _pair(families, scenario)
    assert scenario(candidate) == result


@pytest.mark.parametrize("failure", [None, "base", "date"])
def test_notification_constructor_super_binding_and_partial_publication(
    families, monkeypatch, failure
):
    def scenario(family):
        trace = []
        error = RuntimeError("constructor boundary")
        actions = [("action", lambda: None)]

        class Severity:
            def __str__(self):
                trace.append("severity-string")
                return "WARNING"

        severity = Severity()

        def convert(value):
            trace.append(("severity-convert", value))
            return severity

        class BaseInit:
            def __get__(self, obj, owner):
                trace.append("super-bind")

                def initialize(**kwargs):
                    trace.append(("base-call", kwargs))
                    if failure == "base":
                        raise error

                return initialize

        def now():
            trace.append("date")
            if failure == "date":
                raise error
            return "clock"

        obj = object.__new__(family.module.Notification)
        with monkeypatch.context() as patch:
            patch.setitem(vars(family.module), "NotificationSeverity", convert)
            patch.setattr(family.module.Event, "__init__", BaseInit())
            patch.setitem(vars(family.module), "datetime", SimpleNamespace(now=now))
            if failure:
                with pytest.raises(RuntimeError) as caught:
                    family.module.Notification.__init__(
                        obj, "payload", "warning", actions, custom=7
                    )
                assert caught.value is error
            else:
                family.module.Notification.__init__(
                    obj, "payload", "warning", actions, custom=7
                )
        assert trace[:4] == [
            ("severity-convert", "warning"),
            "super-bind",
            "severity-string",
            ("base-call", {"type_name": "warning", "custom": 7}),
        ]
        assert obj.severity is severity
        assert ("_message" in vars(obj)) == (failure != "base")
        assert ("actions" in vars(obj)) == (failure != "base")
        if failure != "base":
            assert obj._message == "payload" and obj.actions is actions
            assert trace[-1] == "date"
        assert ("date" in vars(obj)) == (failure is None)
        return tuple(trace), tuple(vars(obj))

    _pair(families, scenario)


@pytest.mark.parametrize("fail_emitter", [False, True])
def test_manager_constructor_publication_and_exact_emitter_alias(
    families, monkeypatch, fail_emitter
):
    def scenario(family):
        trace = []
        error = LookupError("emitter constructor")
        emitter = object()

        class Manager(family.module.NotificationManager):
            def __setattr__(self, name, value):
                trace.append(("store", name))
                super().__setattr__(name, value)

        obj = object.__new__(Manager)

        def getenv(name):
            assert obj.records == []
            trace.append(("env", name))
            return "True" if name == "NAPARI_EXIT_ON_ERROR" else "False"

        def event_emitter(**kwargs):
            assert kwargs == {"source": obj, "event_class": family.module.Notification}
            assert obj.exit_on_error is True and obj.catch_error is False
            trace.append("emitter")
            if fail_emitter:
                raise error
            return emitter

        with monkeypatch.context() as patch:
            patch.setitem(vars(family.module), "os", SimpleNamespace(getenv=getenv))
            patch.setitem(vars(family.module), "EventEmitter", event_emitter)
            if fail_emitter:
                with pytest.raises(LookupError) as caught:
                    Manager.__init__(obj)
                assert caught.value is error
            else:
                Manager.__init__(obj)
        assert trace[:6] == [
            ("store", "records"),
            ("env", "NAPARI_EXIT_ON_ERROR"),
            ("store", "exit_on_error"),
            ("env", "NAPARI_CATCH_ERRORS"),
            ("store", "catch_error"),
            "emitter",
        ]
        assert ("notification_ready" in vars(obj)) is not fail_emitter
        if not fail_emitter:
            assert obj.notification_ready is obj.changed is emitter
            assert trace[6:] == [
                ("store", name)
                for name in (
                    "notification_ready",
                    "changed",
                    "_originals_except_hooks",
                    "_original_showwarnings_hooks",
                    "_originals_thread_except_hooks",
                    "_seen_warnings",
                )
            ]
            assert obj._originals_except_hooks is not obj._original_showwarnings_hooks
        return trace, tuple(vars(obj))

    _pair(families, scenario)


@pytest.mark.parametrize("mode", ["nested", "install-fail", "restore-fail"])
def test_hook_stacks_nested_and_source_partial_failure_order(
    families, monkeypatch, mode
):
    def scenario(family):
        trace = []
        error = LookupError("hook boundary")
        manager = _bare(family)

        def label(value):
            return value.__func__.__name__ if inspect.ismethod(value) else value

        class Hooks:
            def __init__(self, name, **values):
                object.__setattr__(self, "name", name)
                for key, value in values.items():
                    object.__setattr__(self, key, value)

            def __setattr__(self, name, value):
                trace.append(("store", self.name, name, label(value)))
                object.__setattr__(self, name, value)

        class Stack(list):
            def __init__(self, name):
                self.name = name
                self.fail = False

            def append(self, value):
                trace.append(("append", self.name, label(value)))
                return super().append(value)

            def pop(self):
                trace.append(("pop", self.name))
                if self.fail:
                    raise error
                return super().pop()

        thread = Hooks("thread", excepthook="thread0")
        system = Hooks("sys", excepthook="sys0")
        warnings = Hooks("warnings", showwarning="warning0")
        manager._originals_thread_except_hooks = Stack("thread")
        manager._originals_except_hooks = Stack("sys")
        manager._original_showwarnings_hooks = Stack("warnings")
        with monkeypatch.context() as patch:
            patch.setitem(vars(family.module), "threading", thread)
            patch.setitem(vars(family.module), "sys", system)
            patch.setitem(vars(family.module), "warnings", warnings)
            if mode == "install-fail":

                def denied(self):
                    raise error

                patch.setattr(
                    family.module.NotificationManager,
                    "receive_thread_error",
                    property(denied),
                )
                with pytest.raises(LookupError) as caught:
                    manager.install_hooks()
                assert caught.value is error
                assert trace == [("append", "thread", "thread0")]
                assert (
                    thread.excepthook == "thread0"
                    and not manager._originals_except_hooks
                )
                return trace
            assert manager.__enter__() is manager
            assert trace == [
                ("append", "thread", "thread0"),
                ("store", "thread", "excepthook", "receive_thread_error"),
                ("append", "sys", "sys0"),
                ("append", "warnings", "warning0"),
                ("store", "sys", "excepthook", "receive_error"),
                ("store", "warnings", "showwarning", "receive_warning"),
            ]
            if mode == "restore-fail":
                manager._originals_except_hooks.fail = True
                with pytest.raises(LookupError) as caught:
                    manager.restore_hooks()
                assert caught.value is error
                assert trace[-3:] == [
                    ("pop", "thread"),
                    ("store", "thread", "excepthook", "thread0"),
                    ("pop", "sys"),
                ]
                assert not manager._originals_thread_except_hooks
                assert manager._originals_except_hooks == ["sys0"]
                assert manager._original_showwarnings_hooks == ["warning0"]
                assert system.excepthook.__self__ is manager
                return trace
            object.__setattr__(thread, "excepthook", "thread1")
            object.__setattr__(system, "excepthook", "sys1")
            object.__setattr__(warnings, "showwarning", "warning1")
            manager.install_hooks()
            assert manager._originals_thread_except_hooks == ["thread0", "thread1"]
            assert manager._originals_except_hooks == ["sys0", "sys1"]
            assert manager._original_showwarnings_hooks == ["warning0", "warning1"]
            assert manager.__exit__(None, None, None) is None
            assert (thread.excepthook, system.excepthook, warnings.showwarning) == (
                "thread1",
                "sys1",
                "warning1",
            )
            manager.restore_hooks()
            assert (thread.excepthook, system.excepthook, warnings.showwarning) == (
                "thread0",
                "sys0",
                "warning0",
            )
            assert trace[-6:] == [
                ("pop", "thread"),
                ("store", "thread", "excepthook", "thread0"),
                ("pop", "sys"),
                ("store", "sys", "excepthook", "sys0"),
                ("pop", "warnings"),
                ("store", "warnings", "showwarning", "warning0"),
            ]
        return trace

    _pair(families, scenario)


@pytest.mark.parametrize("fail", [False, True])
def test_dispatch_append_reentry_fresh_callback_and_discarded_return(families, fail):
    def scenario(family):
        manager = _bare(family)
        trace = []
        error = RuntimeError("callback")

        class Records(list):
            def append(self, value):
                trace.append(("append", value))
                super().append(value)

        class Discarded:
            def __del__(self):
                trace.append("discarded-return")

        def second(value):
            trace.append(("second", value))
            return Discarded()

        def first(value):
            assert manager.records[-1] is value
            trace.append(("first", value))
            if fail:
                raise error
            manager.notification_ready = second
            assert manager.dispatch("inner") is None
            return Discarded()

        manager.records = Records()
        manager.notification_ready = first
        if fail:
            with pytest.raises(RuntimeError) as caught:
                manager.dispatch("outer")
            assert caught.value is error and manager.records == ["outer"]
            assert trace == [("append", "outer"), ("first", "outer")]
        else:
            assert manager.dispatch("outer") is None
            assert manager.records == ["outer", "inner"]
            assert trace == [
                ("append", "outer"),
                ("first", "outer"),
                ("append", "inner"),
                ("second", "inner"),
                "discarded-return",
                "discarded-return",
            ]
        return trace

    _pair(families, scenario)


@pytest.mark.parametrize("route", ["keyboard", "exit", "no-catch", "normal"])
def test_error_routing_keeps_value_traceback_and_source_branch_order(
    families, monkeypatch, route
):
    def scenario(family):
        manager = _bare(family)
        manager.exit_on_error = route == "exit"
        manager.catch_error = route != "no-catch"
        supplied_traceback = object()
        error = KeyboardInterrupt() if route == "keyboard" else RuntimeError("value")
        trace = []

        def exit_(message):
            trace.append(("exit", message))
            raise SystemExit(message)

        def fallback(kind, value, tb):
            assert kind is type(error) and value is error and tb is supplied_traceback
            trace.append("fallback")

        manager.notification_ready = lambda record: trace.append(
            (
                "record",
                record.exception is error,
                record.exception.__traceback__ is None,
            )
        )
        with monkeypatch.context() as patch:
            patch.setitem(
                vars(family.module),
                "sys",
                SimpleNamespace(exit=exit_, __excepthook__=fallback),
            )
            if route in {"keyboard", "exit"}:
                with pytest.raises(SystemExit) as caught:
                    manager.receive_error(
                        type(error), error, supplied_traceback, object()
                    )
                assert str(caught.value) == (
                    "Closed by KeyboardInterrupt"
                    if route == "keyboard"
                    else "Exit on error"
                )
            else:
                assert (
                    manager.receive_error(
                        type(error), error, supplied_traceback, object()
                    )
                    is None
                )
        expected = {
            "keyboard": [("exit", "Closed by KeyboardInterrupt")],
            "exit": ["fallback", ("exit", "Exit on error")],
            "no-catch": ["fallback"],
            "normal": [("record", True, True)],
        }
        assert trace == expected[route]
        assert len(manager.records) == (route == "normal")
        return trace

    _pair(families, scenario)


@pytest.mark.parametrize("method", ["receive_info", "show_info", "show_error"])
def test_factory_capture_precedes_rhs_and_ignores_class_receiver(
    families, monkeypatch, method
):
    def scenario(family):
        trace = []
        marker = object()
        manager = _bare(family)

        def selected(value):
            assert value is marker
            trace.append("selected-dispatch")

        manager.dispatch = selected

        def factory(*args, **kwargs):
            trace.append(("factory", args, {k: str(v) for k, v in kwargs.items()}))
            manager.dispatch = lambda value: trace.append("replacement-dispatch")
            return marker

        with monkeypatch.context() as patch:
            patch.setitem(vars(family.module), "Notification", factory)
            patch.setitem(vars(family.module), "notification_manager", manager)
            if method == "receive_info":
                manager.receive_info("hello")
                assert trace == [
                    ("factory", ("hello", "INFO"), {}),
                    "selected-dispatch",
                ]
            else:
                getattr(family.module, method)("hello")
                assert trace == [
                    (
                        "factory",
                        ("hello",),
                        {"severity": "info" if method == "show_info" else "error"},
                    ),
                    "selected-dispatch",
                ]
        original = family.module.Notification
        for name, target in (
            ()
            if method != "receive_info"
            else (
                ("from_exception", "ErrorNotification"),
                ("from_warning", "WarningNotification"),
            )
        ):
            payload, kwargs = object(), {"extra": object()}
            with monkeypatch.context() as patch:
                patch.setitem(
                    vars(family.module), target, lambda value, **kw: (value, kw)
                )
                result = original.__dict__[name].__func__(object(), payload, **kwargs)
            assert result[0] is payload and result[1] == kwargs
        return trace

    _pair(families, scenario)


@pytest.mark.parametrize("case", ["str", "warning", "factory-fail", "empty-args"])
def test_warning_dedup_and_added_key_survives_factory_error(
    families, monkeypatch, case
):
    def scenario(family):
        manager = _bare(family)
        trace = []
        error = RuntimeError("warning factory")
        message = (
            "raw"
            if case == "str"
            else UserWarning(*([] if case == "empty-args" else ["raw"]))
        )
        marker = object()

        def factory(value, **kwargs):
            assert value is message and kwargs == {
                "filename": "source.py",
                "lineno": 17,
            }
            assert manager._seen_warnings == {("raw", UserWarning, "source.py", 17)}
            trace.append("factory")
            if case == "factory-fail":
                raise error
            return marker

        manager.dispatch = lambda value: trace.append(("dispatch", value is marker))
        with monkeypatch.context() as patch:
            patch.setitem(
                vars(family.module),
                "Notification",
                SimpleNamespace(from_warning=factory),
            )
            if case == "empty-args":
                with pytest.raises(IndexError):
                    manager.receive_warning(message, UserWarning, "source.py", 17)
                assert not manager._seen_warnings and not trace
            elif case == "factory-fail":
                with pytest.raises(RuntimeError) as caught:
                    manager.receive_warning(message, UserWarning, "source.py", 17)
                assert caught.value is error
                manager.receive_warning(message, UserWarning, "source.py", 17)
                assert trace == ["factory"]
            else:
                manager.receive_warning(
                    message, UserWarning, "source.py", 17, object(), object()
                )
                manager.receive_warning(message, UserWarning, "source.py", 17)
                assert trace == ["factory", ("dispatch", True)]
        return trace, tuple(manager._seen_warnings)

    _pair(families, scenario)


@pytest.mark.parametrize("kind", ["ErrorNotification", "WarningNotification"])
def test_error_warning_eager_defaults_actions_and_ignored_extra_args(
    families, monkeypatch, kind
):
    def scenario(family):
        trace = []
        actions = [("go", lambda: None)]

        class Payload(Exception):
            @property
            def message(self):
                trace.append("message")
                return "selected"

            @property
            def actions(self):
                trace.append("actions")
                return actions

            def __str__(self):
                trace.append("eager-string")
                return "unused default"

        payload = Payload()
        calls = []

        def base(self, *args, **kwargs):
            calls.append((args[0], str(args[1]), args[2] is actions, kwargs))

        with monkeypatch.context() as patch:
            patch.setattr(family.module.Notification, "__init__", base)
            cls = getattr(family.module, kind)
            obj = cls(
                payload,
                *(
                    ["file.py", 12, "ignored"]
                    if kind == "WarningNotification"
                    else ["ignored"]
                ),
                unrelated=object(),
            )
        assert trace == ["eager-string", "message", "actions"]
        assert calls == [
            (
                "selected",
                "warning" if kind == "WarningNotification" else "error",
                True,
                {},
            )
        ]
        assert (
            getattr(obj, "warning" if kind == "WarningNotification" else "exception")
            is payload
        )
        if kind == "WarningNotification":
            assert obj.filename == "file.py" and obj.lineno == 12
        return trace, calls

    _pair(families, scenario)


def test_severity_and_string_enum_real_identity_dynamic_policy(families, monkeypatch):
    def scenario(family):
        severity = family.module.NotificationSeverity
        assert severity("WARNING") is severity.WARNING
        assert severity.WARNING == "warning" and severity.WARNING != "WARNING"
        assert severity.DEBUG < severity.INFO <= severity.WARNING < severity.ERROR
        assert (
            severity.NONE.as_icon() == ""
            and severity.WARNING.as_icon() == "\u26a0\ufe0f"
        )
        assert hash(severity.WARNING) == hash("warning")
        assert severity.keys() == ["error", "warning", "info", "debug", "none"]

        class Custom(family.misc.StringEnum):
            A = auto()
            B = auto()

        assert Custom("A") is Custom.A and Custom.A == "a"
        assert Custom.A != Custom.B and Custom.A != severity.INFO
        with pytest.raises(ValueError, match="may only be called"):
            Custom(severity.INFO)
        with pytest.raises(ValueError, match="may only be called"):
            Custom(7)
        with monkeypatch.context() as patch:
            patch.setitem(vars(family.module), "name2num", {"info": 100, "warning": 1})
            assert severity.INFO > severity.WARNING
        return [member.value for member in severity], Custom.keys()

    _pair(families, scenario)


def _code_literals(function):
    def walk(value):
        yield value
        if isinstance(value, tuple):
            for child in value:
                yield from walk(child)

    for value in function.__code__.co_consts:
        yield from walk(value)


@pytest.mark.parametrize("method", ["as_html", "as_text", "__str__"])
def test_lazy_formatter_import_and_three_fresh_exception_reads(
    families, monkeypatch, method
):
    def scenario(family):
        trace = []
        exceptions = [ValueError("class"), TypeError("value"), IndexError("traceback")]

        class Record(family.module.ErrorNotification):
            @property
            def exception(self):
                index = sum(
                    isinstance(value, tuple) and value[0] == "exception"
                    for value in trace
                )
                trace.append(("exception", index))
                return exceptions[index]

        fn = getattr(family.module.ErrorNotification, method)
        constants = tuple(_code_literals(fn))
        fromlist_literal = next(
            value for value in constants if value == ("get_tb_formatter",)
        )

        def formatter(info, **kwargs):
            assert (
                info[0] is ValueError and info[1] is exceptions[1] and info[2] is None
            )
            assert kwargs == (
                {"as_html": True}
                if method == "as_html"
                else {
                    "as_html": False,
                    **({"color": "NoColor"} if method == "as_text" else {}),
                }
            )
            if method == "as_text":
                assert kwargs["color"] is next(
                    value for value in constants if value == "NoColor"
                )
            trace.append(("format", kwargs))
            return "formatted"

        def factory():
            trace.append("factory")
            return formatter

        original_import = fn.__builtins__["__import__"]

        def import_(name, globals=None, locals=None, fromlist=(), level=0):
            if name == "napari.utils._tracebacks":
                assert fromlist is fromlist_literal and level == 0
                trace.append("import")
                return SimpleNamespace(get_tb_formatter=factory)
            return original_import(name, globals, locals, fromlist, level)

        with monkeypatch.context() as patch:
            patch.setitem(fn.__builtins__, "__import__", import_)
            assert fn(object.__new__(Record)) == "formatted"
        assert trace[:5] == [
            "import",
            "factory",
            ("exception", 0),
            ("exception", 1),
            ("exception", 2),
        ]
        return trace

    _pair(families, scenario)


@pytest.mark.parametrize("kind", ["notification", "warning"])
def test_message_alias_and_fstring_fresh_field_order(families, kind):
    def scenario(family):
        trace = []
        if kind == "notification":

            class Severity:
                def __str__(self):
                    trace.append("severity-string")
                    return "info"

            class Record(family.module.Notification):
                @property
                def message(self):
                    trace.append("message-read")
                    return self._message

            obj = object.__new__(Record)
            obj.severity = Severity()
            marker = object()
            family.module.Notification.message.fset(obj, marker)
            assert family.module.Notification.message.fget(obj) is marker
            obj._message = "message"
            assert family.module.Notification.__str__(obj) == "INFO: message"
            assert trace == ["severity-string", "message-read"]
        else:

            class FirstWarning(UserWarning):
                pass

            class SecondWarning(UserWarning):
                def __str__(self):
                    trace.append("warning-string")
                    return "second"

            class Record(family.module.WarningNotification):
                @property
                def warning(self):
                    trace.append("warning-read")
                    return self._warning

            obj = object.__new__(Record)
            obj._warning = FirstWarning("first")

            class Filename:
                def __format__(self, spec):
                    trace.append("filename-format")
                    obj._warning = SecondWarning()
                    return "file.py"

            class Line:
                def __format__(self, spec):
                    trace.append("line-format")
                    return "12"

            obj.filename, obj.lineno = Filename(), Line()
            assert (
                family.module.WarningNotification.__str__(obj)
                == "file.py:12: FirstWarning: second!"
            )
            assert trace == [
                "warning-read",
                "filename-format",
                "line-format",
                "warning-read",
                "warning-string",
            ]
        return trace

    _pair(families, scenario)


@pytest.mark.parametrize("mode", ["below", "print", "error"])
def test_console_source_import_threshold_log_and_same_error_reraise(
    families, monkeypatch, mode
):
    def scenario(family):
        trace = []
        error = RuntimeError("formatting")
        obj = family.module.Notification("message", "info")
        level = (
            family.module.NotificationSeverity.WARNING
            if mode == "below"
            else family.module.NotificationSeverity.NONE
        )
        fn = family.module.show_console_notification
        original_import = fn.__builtins__["__import__"]

        def get_settings():
            trace.append("settings")
            return SimpleNamespace(
                application=SimpleNamespace(console_notification_level=level)
            )

        def import_(name, globals=None, locals=None, fromlist=(), level=0):
            if name == "napari.settings":
                assert fromlist == ("get_settings",) and level == 0
                trace.append("import")
                return SimpleNamespace(get_settings=get_settings)
            return original_import(name, globals, locals, fromlist, level)

        def print_(value):
            assert value is obj
            trace.append("print")
            if mode == "error":
                raise error

        def logger(name):
            assert name == "napari"
            trace.append("logger")
            return SimpleNamespace(
                exception=lambda message: trace.append(
                    ("log", message.startswith("An error occurred"))
                )
            )

        with monkeypatch.context() as patch:
            patch.setitem(fn.__builtins__, "__import__", import_)
            patch.setitem(vars(family.module), "print", print_)
            patch.setitem(
                vars(family.module), "logging", SimpleNamespace(getLogger=logger)
            )
            if mode == "error":
                with pytest.raises(RuntimeError) as caught:
                    fn(obj)
                assert caught.value is error
            else:
                assert fn(obj) is None
        assert (
            trace
            == {
                "below": ["import", "settings"],
                "print": ["import", "settings", "print"],
                "error": ["import", "settings", "print", "logger", ("log", True)],
            }[mode]
        )
        return trace

    _pair(families, scenario)


def test_record_actions_and_saved_list_keep_only_original_traceback_roots(families):
    class Payload:
        pass

    def make_error(payload):
        try:
            raise RuntimeError("saved record")
        except RuntimeError as error:
            return error

    for family in families:
        payload = Payload()
        reference = weakref.ref(payload)
        error = make_error(payload)
        traceback = error.__traceback__

        def action(value=payload):
            return value

        actions = [("retain", action)]
        error.actions = actions
        record = family.module.ErrorNotification(error)
        assert record.exception is error and record.exception.__traceback__ is traceback
        assert record.actions is actions and record.actions[0][1]() is payload
        manager = _bare(family)
        manager.dispatch(record)
        saved_records = manager.records
        saved_record = saved_records.pop()
        del payload, error, traceback, action, actions, record
        gc.collect()
        assert (
            reference() is not None and saved_record.exception.__traceback__ is not None
        )
        saved_records.append(saved_record)
        del saved_record, manager
        gc.collect()
        assert reference() is not None
        saved_records.clear()
        del saved_records
        gc.collect()
        assert reference() is None


@pytest.mark.parametrize(
    "branch", ["ipython-old", "ipython-new", "fallback", "import-error"]
)
def test_real_formatter_function_cells_numpy_context_and_external_choices(
    families, monkeypatch, branch
):
    import numpy as np

    def scenario(family):
        trace = []
        info = (ValueError, ValueError("marker"), None)
        before = np.get_printoptions()
        fn = family.tracebacks.get_tb_formatter
        original_import = fn.__builtins__["__import__"]
        import_error = RuntimeError("second IPython import")

        class VerboseTB:
            def __init__(self, **kwargs):
                trace.append(("verbose", kwargs))

            def text(self, *args):
                assert args == info
                assert np.get_printoptions()["precision"] == 5
                assert np.get_printoptions()["threshold"] == 10
                trace.append("text")
                return "plain text\n"

        ipython = SimpleNamespace(
            version_info=(9, 0) if branch == "ipython-new" else (8, 0),
            core=SimpleNamespace(ultratb=SimpleNamespace(VerboseTB=VerboseTB)),
        )

        def format_exception(*args):
            assert args == info and np.get_printoptions()["edgeitems"] == 2
            trace.append("traceback")
            return ["plain ", "text\n"]

        traceback_module = SimpleNamespace(format_exception=format_exception)

        def import_(name, globals=None, locals=None, fromlist=(), level=0):
            if name in {"IPython", "IPython.core.ultratb"}:
                trace.append(("import", name))
                if branch == "fallback":
                    raise ModuleNotFoundError("controlled unavailable IPython")
                if branch == "import-error" and name == "IPython.core.ultratb":
                    raise import_error
                return ipython
            if name == "traceback" and branch == "fallback":
                trace.append(("import", name))
                return traceback_module
            return original_import(name, globals, locals, fromlist, level)

        with monkeypatch.context() as patch:
            patch.setitem(fn.__builtins__, "__import__", import_)
            if branch == "import-error":
                with pytest.raises(RuntimeError) as caught:
                    fn()
                assert caught.value is import_error
                assert trace == [
                    ("import", "IPython"),
                    ("import", "IPython.core.ultratb"),
                ]
                assert np.get_printoptions() == before
                return trace, "same-import-error"
            formatter = fn()
        assert isinstance(formatter, types.FunctionType)
        assert formatter.__name__ == "format_exc_info"
        assert formatter.__qualname__ == "get_tb_formatter.<locals>.format_exc_info"
        assert (
            str(inspect.signature(formatter))
            == "(info: 'ExcInfo', as_html: 'bool', color: 'str' = 'Neutral') -> 'str'"
        )
        assert formatter.__code__.co_freevars == (
            ("traceback",) if branch == "fallback" else ("IPython",)
        )
        assert formatter.__closure__[0].cell_contents is (
            traceback_module if branch == "fallback" else ipython
        )
        assert formatter(info, False) == "plain text\n"
        html = formatter(info, True)
        assert html == (
            "<pre>plain text\n</pre>"
            if branch == "fallback"
            else (
                "<span style='font-family: monaco,courier,monospace;'>"
                "plain&nbsp;text<br></span>"
            )
        )
        assert np.get_printoptions() == before
        if branch != "fallback":
            expected = (
                {"theme_name": "neutral"}
                if branch == "ipython-new"
                else {"color_scheme": "Neutral"}
            )
            assert [
                entry[1]
                for entry in trace
                if isinstance(entry, tuple) and entry[0] == "verbose"
            ] == [expected, expected]
        return trace, html

    _pair(families, scenario)


@pytest.mark.parametrize("mode", ["styles", "invalid", "send-close", "throw"])
def test_ansi_generator_live_styles_partial_protocol_and_identity(families, mode):
    def scenario(family):
        styles = {31: {"color": "red"}}
        string = "\x1b[31mred\x1b[0m!" if mode != "invalid" else "\x1b[mplain"
        generator = family.tracebacks.ansi2html(string, styles)
        assert isinstance(generator, types.GeneratorType)
        assert iter(generator) is generator
        assert next(generator) == ""
        if mode == "throw":
            marker = RuntimeError("injected")
            with pytest.raises(RuntimeError) as caught:
                generator.throw(marker)
            assert caught.value is marker
            with pytest.raises(StopIteration):
                next(generator)
            return "throw-same-error"
        if mode == "send-close":
            assert generator.send(object()) == '<span style="color: red">'
            assert generator.close() is None
            with pytest.raises(StopIteration):
                next(generator)
            return "closed"
        if mode == "styles":
            styles[31] = {"color": "blue"}
            assert tuple(generator) == (
                '<span style="color: blue">',
                "red",
                "</span>",
                "!",
            )
            return "live-styles"
        assert tuple(generator) == ("plain",)
        return "invalid-reset"

    _pair(families, scenario)


def _event_helpers(monkeypatch):
    path = ROOT / ".artifacts/viewer-notification-event-helpers-next.json"
    raw = path.read_bytes()
    assert (
        hashlib.sha256(raw).hexdigest()
        == "023aaae8b8349a87313dd86522d3142db0a2bd5ec0c314e42521337629333bd0"
    )
    proposal = json.loads(raw)
    actual = importlib.import_module("napari.utils.events.event")
    result = []
    for mode, source_path in (
        ("pinned", ".upstream/napari/src/napari/utils/events/event.py"),
        ("current_fork", "src/napari/utils/events/event.py"),
    ):
        row = next(row for row in proposal["files"] if row["path"] == source_path)
        source = (ROOT / row["before"]).read_bytes()
        assert hashlib.sha256(source).hexdigest() == row["before_sha256"]
        tree = ast.parse(source)
        cls = next(
            node
            for node in tree.body
            if isinstance(node, ast.ClassDef) and node.name == "Event"
        )
        nodes = [
            node
            for node in cls.body
            if isinstance(node, ast.FunctionDef)
            and node.name in {"__str__", "__getattr__"}
        ]
        holder = ast.ClassDef(
            name="Event", bases=[], keywords=[], body=nodes, decorator_list=[]
        )
        futures = [
            node
            for node in tree.body
            if isinstance(node, ast.ImportFrom) and node.module == "__future__"
        ]
        module = _module(
            "_naparixx_notification_event_" + mode,
            ast.Module(body=[*futures, holder], type_ignores=[]),
            ROOT / row["before"],
            monkeypatch,
            dict(vars(actual)),
        )
        result.append((mode, module.Event.__str__, module.Event.__getattr__))
    result.append(("actual", actual.Event.__str__, actual.Event.__getattr__))
    for index in (1, 2):
        assert len({str(inspect.signature(row[index])) for row in result}) == 1
        assert len({row[index].__doc__ for row in result}) == 1
    return actual.Event, result


def test_event_dynamic_class_name_returns_original_object(monkeypatch):
    event, implementations = _event_helpers(monkeypatch)

    class Name(str):
        pass

    marker = Name("DynamicEvent")

    class DynamicEvent(event):
        def __getattribute__(self, name):
            if name == "__class__":
                return SimpleNamespace(__name__=marker)
            return object.__getattribute__(self, name)

    obj = object.__new__(DynamicEvent)
    for _, string, _ in implementations:
        assert string(obj) is marker


def test_event_fresh_global_builtin_object_getter_and_nameerror(monkeypatch):
    event, implementations = _event_helpers(monkeypatch)
    obj = object.__new__(event)
    marker = object()
    failure = LookupError("event getter")
    real_object = object

    def scenario(getter):
        trace = []

        def selected(receiver, name):
            assert receiver is obj and name == "requested"
            trace.append("selected")
            return marker

        def rejected(receiver, name):
            assert receiver is obj and name == "requested"
            trace.append("rejected")
            raise failure

        class Provider:
            def __init__(self, callback):
                self.callback = callback

            def __getattribute__(self, name):
                if name == "__getattribute__":
                    trace.append("bind")
                    return real_object.__getattribute__(self, "callback")
                return real_object.__getattribute__(self, name)

        with monkeypatch.context() as patch:
            patch.setitem(getter.__globals__, "object", Provider(selected))
            assert getter(obj, "requested") is marker
            patch.setitem(getter.__globals__, "object", Provider(rejected))
            with pytest.raises(LookupError) as caught:
                getter(obj, "requested")
            assert caught.value is failure
            patch.delitem(getter.__globals__, "object")
            patch.setitem(getter.__builtins__, "object", Provider(selected))
            assert getter(obj, "requested") is marker
        assert trace == ["bind", "selected", "bind", "rejected", "bind", "selected"]
        # Do not run pytest helpers while the source-required builtins name is
        # deliberately absent. Restore both dictionaries before the assertion.
        absent = real_object()
        global_object = getter.__globals__.pop("object", absent)
        builtin_object = getter.__builtins__.pop("object")
        caught_name = None
        try:
            try:
                getter(obj, "requested")
            except NameError as error:
                caught_name = error.name
        finally:
            getter.__builtins__["object"] = builtin_object
            if global_object is not absent:
                getter.__globals__["object"] = global_object
        assert caught_name == "object"

    for _, _, getter in implementations:
        scenario(getter)
