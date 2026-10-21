"""Compile-only selected ABI inventory; no application imports/execution."""
import hashlib
import json
from pathlib import Path
import sys
import types

ROOT = Path(__file__).resolve().parents[2]
STAGE = Path(__file__).resolve().parent
SELECTION = STAGE / "selection.json"


def ref(path):
    raw = path.read_bytes()
    return {"path": path.relative_to(ROOT).as_posix(),
            "sha256": hashlib.sha256(raw).hexdigest(), "bytes": len(raw)}


def walk(code):
    yield code
    for value in code.co_consts:
        if isinstance(value, types.CodeType):
            yield from walk(value)


def load_inputs():
    selection = json.loads(SELECTION.read_text())
    modules, rows = {}, []
    for entry, definition in enumerate(selection["definitions"]):
        path = ROOT / definition["module"]
        assert ref(path) == definition["source"]
        if str(path) not in modules:
            modules[str(path)] = list(walk(compile(path.read_bytes(), str(path), "exec", dont_inherit=True)))
        matches = [code for code in modules[str(path)] if code.co_qualname == definition["qualname"]]
        code = matches[definition["occurrence"] - 1]
        parameters = list(code.co_varnames[:code.co_argcount + code.co_kwonlyargcount])
        assert not code.co_flags & (0x4 | 0x8 | 0x80 | 0x200)
        rows.append((definition, code, {"entry": entry, "id": definition["id"],
            "module": definition["module"], "qualname": definition["qualname"],
            "occurrence": definition["occurrence"], "parameters": parameters,
            "posonly": code.co_posonlyargcount, "kwonly": code.co_kwonlyargcount,
            "freevars": list(code.co_freevars), "cellvars": list(code.co_cellvars),
            "generator": bool(code.co_flags & 0x20)}))
    assert len(rows) == 50
    return rows


def main():
    rows = load_inputs()
    path = STAGE / f"cp{sys.version_info.major}{sys.version_info.minor}-source-abi.json"
    path.write_text(json.dumps({"selection": ref(SELECTION), "version": sys.version,
        "status": "COMPILE_ONLY_SOURCE_ABI", "rows": [row[2] for row in rows]}, indent=2) + "\n")
    for _, _, row in rows:
        print(row["entry"], row["qualname"], row["occurrence"], row["parameters"],
              "free=", row["freevars"], "cells=", row["cellvars"], "gen=", row["generator"])


if __name__ == "__main__":
    main()
