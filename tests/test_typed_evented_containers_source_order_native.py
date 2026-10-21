"""Two finite container source-order regressions; whole-body oracles are test-only.

The existing 31-case cohort owns allocation/restoration and established behavior.
These six rows add only saved-exit release and pre-BUILD_SET global capture.
"""
from __future__ import annotations

import functools
import hashlib
import importlib.util
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
ORACLE_PATH = ROOT / "tests/test_typed_evented_containers_native.py"
ORACLE_SHA256 = "a4d69fe17a76c487c675c010fe549ceb4aeaffef0521fdb9a5d6331b23c526e4"
assert hashlib.sha256(ORACLE_PATH.read_bytes()).hexdigest() == ORACLE_SHA256
_SPEC = importlib.util.spec_from_file_location(
    "_naparixx_container_source_order_oracle", ORACLE_PATH
)
assert _SPEC is not None and _SPEC.loader is not None
_ORACLE = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_ORACLE)
implementations = _ORACLE.implementations


def _handled():
    error = sys.exception()
    return None if error is None else (type(error).__name__, str(error))


@pytest.mark.parametrize(
    "branch", ["suppressed", "reraise", "exit_failure", "truth_failure"]
)
def test_saved_exit_destruction_observes_restored_prior_exception(
    implementations, monkeypatch, branch
):
    def scenario(impl):
        log = []

        class Truth:
            def __bool__(self):
                log.append(("truth", _handled()))
                raise RuntimeError("truth failed")

        def exit_action(error_type, error, traceback):
            log.append(("exit", _handled(), error_type.__name__, str(error)))
            if branch == "exit_failure":
                raise RuntimeError("exit failed")
            if branch == "truth_failure":
                return Truth()
            return branch == "suppressed"

        class ExitLease(functools.partial):
            def __del__(self):
                # Inherited C partial.__call__ avoids a Python self frame
                # retaining this observed callable when the target raises.
                log.append(("exit-drop", _handled()))

        def enter():
            log.append(("enter", _handled()))

        class Context:
            @property
            def __enter__(self):
                return enter

            @property
            def __exit__(self):
                # The WITH stack is this callable's sole lasting owner.
                return ExitLease(exit_action)

        def suppress(error_type):
            assert error_type is ValueError
            return Context()

        class Parent(impl.evented.EventedList):
            def index(self, value):
                log.append(("body", _handled()))
                raise ValueError("body failed")

        parent = object.__new__(Parent)
        parent.events = lambda event: log.append(("delivery", _handled()))
        event = SimpleNamespace(source=object())
        with monkeypatch.context() as patch:
            patch.setitem(
                vars(impl.evented), "contextlib", SimpleNamespace(suppress=suppress)
            )
            try:
                raise LookupError("prior handled")
            except LookupError:
                outcome = _ORACLE._outcome(
                    lambda: parent._reemit_child_event(event)
                )
                log.append(("after", _handled()))
        return tuple(log), outcome

    prior = ("LookupError", "prior handled")
    body = ("ValueError", "body failed")
    log = [
        ("enter", prior),
        ("body", prior),
        ("exit", body, "ValueError", "body failed"),
    ]
    if branch == "truth_failure":
        log.append(("truth", body))
    log.append(("exit-drop", prior))
    if branch == "suppressed":
        log.append(("delivery", prior))
    log.append(("after", prior))
    outcome = {
        "suppressed": ("value", None),
        "reraise": ("error", "ValueError", "body failed"),
        "exit_failure": ("error", "RuntimeError", "exit failed"),
        "truth_failure": ("error", "RuntimeError", "truth failed"),
    }[branch]
    _ORACLE._pair(implementations, scenario, (tuple(log), outcome))


@pytest.mark.parametrize("rebinding_token", ["int", "slice"])
def test_delitem_captures_both_globals_before_build_set_hashing(
    implementations, monkeypatch, rebinding_token
):
    def scenario(impl):
        namespace = vars(impl.evented)
        hashes, represented = [], []

        class Token:
            def __init__(self, name, hash_value):
                self.name, self.hash_value = name, hash_value

            def __hash__(self):
                hashes.append(self.name)
                if self.name == rebinding_token:
                    other = "slice" if self.name == "int" else "int"
                    namespace[other] = replacement
                return self.hash_value

            def __repr__(self):
                represented.append(self.name)
                return "<" + self.name + ">"

        int_token = Token("int", 101)
        slice_token = Token("slice", 202)
        replacement = Token("replacement", 303)
        sequence = object.__new__(impl.evented.EventedList)
        sequence._lookup = {}
        with monkeypatch.context() as patch:
            patch.setitem(namespace, "int", int_token)
            patch.setitem(namespace, "slice", slice_token)
            # Reach the invalid-key branch without requiring fake dtype objects
            # to satisfy the builtin isinstance type contract.
            patch.setitem(namespace, "isinstance", lambda key, dtype: False)
            try:
                sequence._delitem_indices(object())
            except TypeError as error:
                text = str(error)
                outcome = (
                    type(error).__name__,
                    text.startswith("Deletion index must be "),
                    text.endswith(", got <class 'object'>"),
                )
            else:
                raise AssertionError("expected source invalid deletion index")
            other = "slice" if rebinding_token == "int" else "int"
            rebound = namespace[other] is replacement
        # Set repr order is intentionally immaterial. Exact initial hash order
        # and the represented member identities are the source contracts.
        return tuple(hashes[:2]), tuple(sorted(represented)), outcome, rebound

    _ORACLE._pair(
        implementations,
        scenario,
        (("int", "slice"), ("int", "slice"), ("TypeError", True, True), True),
    )
