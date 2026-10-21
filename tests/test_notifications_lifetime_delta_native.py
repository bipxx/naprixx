"""Only the three newly observed notification source contracts.

Reuse the held complete-source fixture; no application source mount or fallback.
The existing 49 cases and 157 assertion nodes are unchanged. Keyword expansion
uses the settled backend C-callable source primitive review; no new C probe here.
"""

from __future__ import annotations

import hashlib
import importlib.util
import sys
import weakref
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / ".artifacts/test_notifications_native_next.py"
assert hashlib.sha256(FIXTURE.read_bytes()).hexdigest() == (
    "4c86345e8ad439da247b2bd6ccd97c2e714675537fe1636e5da261c62f0fe0eb"
)
SPEC = importlib.util.spec_from_file_location(
    "_naparixx_notifications_lifetime_fixture", FIXTURE
)
assert SPEC is not None and SPEC.loader is not None
ORACLE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ORACLE
SPEC.loader.exec_module(ORACLE)
families = ORACLE.families


def test_lower_result_released_before_message_publication(families, monkeypatch):
    def scenario(family):
        trace = []
        references = []

        class LowerResult(str):
            def __del__(self):
                trace.append("lower-drop")

        class UpperResult(str):
            def lower(self):
                value = LowerResult("warning")
                references.append(weakref.ref(value))
                return value

        class Severity:
            def __str__(self):
                return UpperResult("WARNING")

        class Record(family.module.Notification):
            def __setattr__(self, name, value):
                if name == "_message":
                    trace.append(("message-store", references[0]() is not None))
                super().__setattr__(name, value)

        def base(self, **kwargs):
            assert kwargs["type_name"] is references[0]()
            trace.append(("base", references[0]() is not None))

        with monkeypatch.context() as patch:
            patch.setitem(
                vars(family.module), "NotificationSeverity", lambda value: Severity()
            )
            patch.setattr(family.module.Event, "__init__", base)
            patch.setitem(
                vars(family.module), "datetime", SimpleNamespace(now=lambda: "clock")
            )
            obj = Record("message", "warning")
        assert obj.message == "message" and obj.date == "clock"
        assert trace == [("base", True), "lower-drop", ("message-store", False)]
        return trace

    ORACLE._pair(families, scenario)


@pytest.mark.parametrize("failure", [False, True])
def test_ansi_assigned_kv_survive_callbacks_then_source_cleanup(families, failure):
    def scenario(family):
        trace = []
        references = {}
        marker = RuntimeError("value formatting")

        class Key:
            def __init__(self, label):
                self.label = label

            def __format__(self, spec):
                trace.append(("key-format", self.label))
                return self.label

            def __del__(self):
                trace.append(("drop", self.label))

        class Value:
            def __init__(self, label):
                self.label = label

            def __format__(self, spec):
                label = self.label
                trace.append(
                    ("value-format", label, references["k" + label[-1]]() is not None)
                )
                if failure:
                    # A saved error frame must not hide the assigned-v release.
                    del self, spec
                    raise marker
                return label

            def __del__(self):
                trace.append(("drop", self.label))

        class Items:
            def __init__(self):
                self.index = 0

            def __iter__(self):
                return self

            def __next__(self):
                if self.index:
                    previous = str(self.index - 1)
                    trace.append(
                        (
                            "next",
                            references["k" + previous]() is not None,
                            references["v" + previous]() is not None,
                        )
                    )
                if self.index == 2:
                    raise StopIteration
                suffix = str(self.index)
                self.index += 1
                key, value = Key("k" + suffix), Value("v" + suffix)
                references[key.label] = weakref.ref(key)
                references[value.label] = weakref.ref(value)
                return key, value

            def __del__(self):
                trace.append("items-drop")

        class Entry(dict):
            def items(self):
                return Items()

        generator = family.tracebacks.ansi2html("\x1b[31mbody", {31: Entry()})
        assert next(generator) == ""
        if failure:
            with pytest.raises(RuntimeError) as caught:
                next(generator)
            assert caught.value is marker
            assert trace == [
                ("key-format", "k0"),
                ("value-format", "v0", True),
                "items-drop",
                ("drop", "v0"),
                ("drop", "k0"),
            ]
            assert references["k0"]() is None and references["v0"]() is None
        else:
            assert tuple(generator) == (
                '<span style="k0: v0; k1: v1">',
                "body",
                "</span>",
            )
            assert trace == [
                ("key-format", "k0"),
                ("value-format", "v0", True),
                ("next", True, True),
                ("drop", "k0"),
                ("drop", "v0"),
                ("key-format", "k1"),
                ("value-format", "v1", True),
                ("next", True, True),
                "items-drop",
                ("drop", "v1"),
                ("drop", "k1"),
            ]
            assert all(reference() is None for reference in references.values())
        return trace

    ORACLE._pair(families, scenario)


def test_fresh_valueerror_factory_class_invalid_constructor_diagnostic(
    families, monkeypatch
):
    def scenario(family):
        class BadError(Exception):
            def __new__(cls):
                return "not an exception"

        class Enum(family.misc.StringEnum):
            VALUE = "value"

        messages = []

        def factory(message):
            messages.append(message)
            return BadError

        with monkeypatch.context() as patch:
            patch.setitem(vars(family.misc), "ValueError", factory)
            with pytest.raises(TypeError) as caught:
                Enum._missing_(object())
        assert len(messages) == 1 and "may only be called" in messages[0]
        assert str(caught.value) == (
            f"calling {BadError!r} should have returned an instance of BaseException, "
            f"not {str!r}"
        )
        # The local class qualname is common to all three scenario executions.
        return str(caught.value)

    ORACLE._pair(families, scenario)
