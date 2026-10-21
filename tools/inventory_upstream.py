"""Inventory the pinned Python implementation without implying port coverage."""

import argparse
import ast
import json
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source = ROOT / ".upstream" / "napari" / "src" / "napari"
    if not source.is_dir():
        raise SystemExit("Run tools/fetch_upstream.py first.")
    groups = Counter()
    rows = []
    for path in sorted(source.rglob("*.py")):
        relative = path.relative_to(source)
        if "_tests" in relative.parts or path.name == "conftest.py":
            continue
        text = path.read_text(encoding="utf-8")
        tree = ast.parse(text)
        group = relative.parts[0] if len(relative.parts) > 1 else "top_level"
        groups[group] += 1
        rows.append(
            {
                "file": relative.as_posix(),
                "lines": len(text.splitlines()),
                "classes": sum(
                    isinstance(node, ast.ClassDef) for node in ast.walk(tree)
                ),
                "functions_and_methods": sum(
                    isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
                    for node in ast.walk(tree)
                ),
            }
        )
    migration = json.loads((ROOT / "migration.json").read_text(encoding="utf-8"))
    report = {
        "upstream": json.loads((ROOT / "upstream.json").read_text(encoding="utf-8")),
        "python_modules": len(rows),
        "python_lines": sum(row["lines"] for row in rows),
        "modules_by_area": dict(sorted(groups.items())),
        "native_operation_count": migration["numerical_operations"],
        "migration": migration,
        "scope": (
            f"Selected responsibilities in {len(migration['application_components'])} "
            "source modules use handwritten native implementations; "
            "Python facades and remaining application code are retained."
        ),
        "files": rows,
    }
    result = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result, encoding="utf-8")
        print(f"{len(rows)} Python modules; inventory written to {args.output}")
    else:
        print(result, end="")


if __name__ == "__main__":
    main()
