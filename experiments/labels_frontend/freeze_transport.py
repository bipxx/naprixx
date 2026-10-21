"""Freeze full source/shell compiler controls without importing the app."""
import ast
import copy
import hashlib
import json
from pathlib import Path
import sys
import types

from inspect_inputs import ROOT, SELECTION, STAGE, load_inputs, ref, walk

FIELDS = ("co_argcount", "co_posonlyargcount", "co_kwonlyargcount", "co_nlocals", "co_stacksize",
          "co_flags", "co_code", "co_consts", "co_names", "co_varnames", "co_freevars", "co_cellvars",
          "co_name", "co_qualname", "co_firstlineno", "co_exceptiontable", "co_linetable", "co_filename")


def atom(value, portable=False):
    if value is None:
        return ["NONE"]
    if value is Ellipsis:
        return ["ELLIPSIS"]
    if isinstance(value, types.CodeType):
        return ["CODE", profile(value, portable)]
    if isinstance(value, tuple):
        return ["TUPLE", [atom(item, portable) for item in value]]
    if type(value) is bytes:
        return ["BYTES", value.hex()]
    if type(value) is bool:
        return ["BOOL", value]
    if type(value) is int:
        return ["INT", value]
    if type(value) is str:
        return ["STR", value]
    if type(value) is float:
        return ["FLOAT", value]
    if type(value) is complex:
        return ["COMPLEX", value.real, value.imag]
    raise TypeError(type(value))


def profile(code, portable=False):
    return [["OWNER_FILENAME"] if portable and field == "co_filename" else atom(getattr(code, field), portable)
            for field in FIELDS]


def shells(path, prefix, count):
    text = path.read_text()
    tree = ast.parse(text)
    for node in ast.walk(tree):
        if isinstance(node, (ast.Import, ast.ImportFrom, ast.Global, ast.Nonlocal)):
            raise AssertionError("Transport contains non-inert namespace policy")
        if isinstance(node, ast.Attribute):
            assert node.attr in ("__call__", "send", "throw")
            assert isinstance(node.value, ast.Name) and node.value.id == "_state" or (
                isinstance(node.value, ast.Constant) and node.value.value is Ellipsis and node.attr == "__call__")
        if isinstance(node, ast.Call):
            assert isinstance(node.func, ast.Attribute)
        if isinstance(node, ast.Lambda):
            assert isinstance(node.body, (ast.Name, ast.Tuple))
    codes = list(walk(compile(text, str(path), "exec", dont_inherit=True)))
    result = []
    for entry in range(count):
        matches = [code for code in codes if code.co_name == prefix + str(entry)]
        assert len(matches) == 1
        result.append(matches[0])
    return result


def selection_ast(node):
    # The reviewed selection used 3.13's omission of empty-list fields. Apply
    # that representation explicitly on all minors; compiler profiles remain
    # complete and retain every native co_* field and nested code constant.
    node = copy.deepcopy(node)
    for child in ast.walk(node):
        child._fields = tuple(name for name in child._fields
                              if not isinstance(getattr(child, name, None), list)
                              or getattr(child, name))
    return ast.dump(node, include_attributes=False)
