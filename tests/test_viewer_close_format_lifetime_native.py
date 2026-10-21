"""One source-derived formatting temporary contract, success and cleanup."""
from __future__ import annotations

import ast
import builtins
import hashlib
import importlib
import inspect
import json
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
BEFORE_INDEX = ROOT / ".artifacts/viewer-close-before/index.json"
BEFORE_SHA256 = "fba037056d49fc6195bbe98c0da4ce2d841f1a39057b4185a277dfc7aced450d"


def _source_getter(module, directory):
    index = json.loads(BEFORE_INDEX.read_bytes())
    assert hashlib.sha256(BEFORE_INDEX.read_bytes()).hexdigest() == BEFORE_SHA256
    name = ".artifacts/viewer-close-before/" + directory + "/utils/task_status.py"
    row = next(item for item in index["files"] if item["before"] == name)
    path = ROOT / name
    raw = path.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == row["before_sha256"]
    tree = ast.parse(raw)
    original = next(
        n for n in tree.body
        if isinstance(n, ast.ClassDef) and n.name == "TaskStatusManager"
    )
    method = next(
        n for n in original.body
        if isinstance(n, ast.FunctionDef) and n.name == "get_status"
    )
    namespace = dict(vars(module))
    namespace["__builtins__"] = dict(vars(builtins))
    # This holder preserves the genuine source qualname/decorators. Receivers,
    # registered items and Status values are the actual application classes.
    owner = ast.ClassDef(
        name=original.name, bases=[], keywords=[],
        body=[method], decorator_list=[]
    )
    body = ast.fix_missing_locations(ast.Module(body=[
        ast.ImportFrom(module="__future__", names=[ast.alias(name="annotations")], level=0),
        owner,
    ], type_ignores=[]))
    assert ast.dump(body.body[1].body[0]) == ast.dump(method)
    exec(compile(body, str(path), "exec", dont_inherit=True), namespace)
    result = namespace["TaskStatusManager"].get_status
    actual = module.TaskStatusManager.get_status
    assert result.__name__ == actual.__name__
    assert result.__qualname__ == actual.__qualname__
    assert result.__module__ == actual.__module__
    assert result.__doc__ == actual.__doc__
    assert result.__annotations__ == actual.__annotations__
    assert str(inspect.signature(result)) == str(inspect.signature(actual))
    assert namespace["Status"] is module.Status
    return result


@pytest.fixture(scope="module")
def implementations():
    module = importlib.import_module("napari.utils.task_status")
    assert Path(module.__file__).resolve() == (
        ROOT / "src/napari/utils/task_status.py"
    ).resolve()
    return module, (
        ("pinned", _source_getter(module, "pinned")),
        ("current_fork", _source_getter(module, "current")),
        ("current", module.TaskStatusManager.get_status),
    )


@pytest.mark.parametrize("failure", [False, True], ids=["success", "description_error"])
def test_task_status_format_pieces_survive_until_build_or_error_cleanup(
    implementations, failure
):
    module, getters = implementations

    def scenario(getter):
        trace = []
        alive = {"provider": False}

        class ProviderText(str):
            def __new__(cls):
                alive["provider"] = True
                return str.__new__(cls, "PROVIDER")

            def __del__(self):
                alive["provider"] = False
                trace.append("provider-drop")

        class DescriptionText(str):
            def __new__(cls):
                return str.__new__(cls, "DESCRIPTION")

            def __del__(self):
                trace.append("description-drop")

        class Provider(str):
            def __format__(self, specification):
                trace.append(("provider-format", specification))
                # Never publish/store this fresh temporary in a fixture root.
                return ProviderText()

        class Description(str):
            def __format__(self, specification):
                trace.append(("description-format", specification, alive["provider"]))
                if failure:
                    raise RuntimeError("description formatting failed")
                return DescriptionText()

        provider, description = Provider("provider"), Description("description")
        manager = module.TaskStatusManager()
        task_id = manager.register_task_status(
            provider, module.Status.PENDING, description
        )
        item = manager._tasks[task_id]
        assert type(manager) is module.TaskStatusManager
        assert type(item) is module.TaskStatusItem
        state = item.state()
        assert state[0] is provider and state[2] is module.Status.PENDING
        assert state[3] is description
        try:
            result = getter(manager)
        except RuntimeError as error:
            trace.append(("caught", str(error)))
            outcome = ("error", type(error).__name__, str(error))
        else:
            assert isinstance(result, list) and result == ["PROVIDER: DESCRIPTION"]
            assert type(result[0]) is str
            trace.append(("returned", tuple(result)))
            outcome = ("value", tuple(result))
        assert not alive["provider"]
        return tuple(trace), outcome

    if failure:
        contract = (
            (("provider-format", ""),
             ("description-format", "", True),
             "provider-drop",
             ("caught", "description formatting failed")),
            ("error", "RuntimeError", "description formatting failed"),
        )
    else:
        contract = (
            (("provider-format", ""),
             ("description-format", "", True),
             "description-drop", "provider-drop",
             ("returned", ("PROVIDER: DESCRIPTION",))),
            ("value", ("PROVIDER: DESCRIPTION",)),
        )
    for name, getter in getters:
        assert scenario(getter) == contract, name
