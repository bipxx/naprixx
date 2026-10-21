"""Register source responsibilities without treating native calls as completion."""

import argparse
import ast
import hashlib
import json
import subprocess
from collections import Counter
from pathlib import Path

if __package__:
    from .conversion_review import apply_reviews
else:
    from conversion_review import apply_reviews

ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = (ast.FunctionDef, ast.AsyncFunctionDef)
DEFINITIONS = (*FUNCTIONS, ast.ClassDef)
PACKAGES = ("napari", "napari_builtins", "naparixx")


def excluded(path):
    return any(part in {"_tests", "__pycache__"} for part in path.parts) or (
        path.name == "conftest.py"
    )


def digest(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def dotted_name(node):
    if isinstance(node, ast.Name):
        return node.id
    if isinstance(node, ast.Attribute):
        parent = dotted_name(node.value)
        if parent:
            return f"{parent}.{node.attr}"
    return None


def imports(tree, package_parts):
    aliases = {}
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                aliases[alias.asname or alias.name.split(".")[0]] = (
                    alias.name if alias.asname else alias.name.split(".")[0]
                )
        elif isinstance(node, ast.ImportFrom):
            if node.level:
                if node.level > len(package_parts):
                    continue
                parts = package_parts[: len(package_parts) - node.level + 1]
                module = (
                    ".".join((*parts, *node.module.split(".")))
                    if node.module
                    else (".".join(parts))
                )
            else:
                module = node.module
            if not module:
                continue
            for alias in node.names:
                aliases[alias.asname or alias.name] = f"{module}.{alias.name}"
    return aliases


def resolve_name(name, aliases):
    if name is None:
        return None
    first, _, rest = name.partition(".")
    resolved = aliases.get(first, first)
    return f"{resolved}.{rest}" if rest else resolved


def own_nodes(node):
    """Exclude nested definitions so their calls belong to their own records."""
    for child in ast.iter_child_nodes(node):
        if isinstance(child, DEFINITIONS):
            continue
        yield child
        yield from own_nodes(child)


def native_evidence(nodes, aliases):
    calls = []
    dispatch = []
    for node in nodes:
        if not isinstance(node, ast.Call):
            continue
        resolved = resolve_name(dotted_name(node.func), aliases)
        if resolved and resolved.startswith(
            (
                "naparixx._native.",
                "naparixx.native.",
                "naparixx._qt_native.",
                "naparixx._qt5_native.",
            )
        ):
            calls.append({"line": node.lineno, "target": resolved})
        elif resolved == "naparixx._backend.dispatch":
            adapter = node.args[0] if node.args else None
            dispatch.append(
                {
                    "line": node.lineno,
                    "adapter": (
                        adapter.value
                        if isinstance(adapter, ast.Constant)
                        else "unresolved"
                    ),
                }
            )
    return calls, dispatch


def call_evidence(nodes, aliases):
    """Record static names and dynamic expressions for an explicit call review."""
    result = []
    for node in nodes:
        if isinstance(node, ast.Call):
            target = resolve_name(dotted_name(node.func), aliases)
            result.append(
                {
                    "line": node.lineno,
                    "target": target or f"expression:{ast.unparse(node.func)}",
                    "dynamic": target is None,
                    "imported": (
                        (dotted_name(node.func) or "").partition(".")[0] in aliases
                    ),
                }
            )
    return result


def definitions(tree, relative, aliases):
    result = []
    occurrences = Counter()

    def visit(node, parents):
        for child in ast.iter_child_nodes(node):
            if not isinstance(child, DEFINITIONS):
                visit(child, parents)
                continue
            qualified = ".".join((*parents, child.name))
            kind = "class" if isinstance(child, ast.ClassDef) else "function"
            occurrences[(kind, qualified)] += 1
            nodes = list(own_nodes(child))
            calls, dispatch = native_evidence(nodes, aliases)
            result.append(
                {
                    "id": f"{relative}:{child.lineno}:{qualified}",
                    "review_key": (
                        f"{relative}:{kind}:{qualified}:"
                        f"{occurrences[(kind, qualified)]}"
                    ),
                    "name": qualified,
                    "kind": kind,
                    "line": child.lineno,
                    "end_line": child.end_lineno,
                    "body_sha256": digest(ast.dump(child, include_attributes=False)),
                    "review_status": "unreviewed",
                    "native_call_evidence": calls,
                    "native_dispatch_evidence": dispatch,
                    "call_evidence": call_evidence(nodes, aliases),
                }
            )
            visit(child, (*parents, child.name))

    visit(tree, ())
    return result


def module_record(path, source, claimed):
    relative = path.relative_to(source).as_posix()
    text = path.read_text(encoding="utf-8")
    tree = ast.parse(text, filename=relative)
    source_parts = Path(relative).parts
    aliases = imports(tree, source_parts[:-1])
    calls, dispatch = native_evidence(ast.walk(tree), aliases)
    package = source_parts[0]
    area = source_parts[1] if len(source_parts) > 2 else "top_level"
    declared = claimed.get(relative)
    return {
        "file": relative,
        "review_key": f"{relative}:module",
        "package": package,
        "area": area,
        "lines": len(text.splitlines()),
        "source_sha256": digest(text),
        "scope": "tooling" if area == "benchmarks" else "runtime",
        "migration_state": "partial" if declared or calls or dispatch else "unreviewed",
        "declared_responsibilities": declared,
        "native_call_evidence": calls,
        "native_dispatch_evidence": dispatch,
        "call_evidence": call_evidence(own_nodes(tree), aliases),
        "review_status": "unreviewed",
        "definitions": definitions(tree, relative, aliases),
    }


def collect(source, claimed):
    rows = []
    for package in PACKAGES:
        location = source / package
        if not location.is_dir():
            continue
        for path in sorted(location.rglob("*.py")):
            if not excluded(path.relative_to(source)):
                rows.append(module_record(path, source, claimed))
    return rows


def pinned_source(upstream):
    checkout = ROOT / ".upstream/napari"
    if not checkout.is_dir():
        raise SystemExit("Run tools/fetch_upstream.py first.")
    actual = subprocess.run(
        ["git", "-C", str(checkout), "rev-parse", "HEAD"],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.strip()
    if actual != upstream["commit"]:
        raise SystemExit(f"Unexpected upstream commit: {actual}")
    changes = subprocess.run(
        [
            "git",
            "-C",
            str(checkout),
            "status",
            "--porcelain",
            "--untracked-files=no",
            "--",
            "src",
        ],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.strip()
    if changes:
        raise SystemExit("Pinned upstream source has tracked modifications.")
    return checkout / "src"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output", type=Path, default=ROOT / ".artifacts/responsibility-register.json"
    )
    parser.add_argument(
        "--reviews", type=Path, default=ROOT / "docs/conversion-reviews.json"
    )
    parser.add_argument(
        "--dependencies",
        type=Path,
        default=ROOT / "docs/conversion-dependencies.json",
    )
    parser.add_argument(
        "--check-complete",
        action="store_true",
        help="Fail if responsibility reviews are missing, stale or incomplete.",
    )
    args = parser.parse_args()
    upstream = json.loads((ROOT / "upstream.json").read_text(encoding="utf-8"))
    migration = json.loads((ROOT / "migration.json").read_text(encoding="utf-8"))
    claimed = {
        (
            row["source"]
            if row["source"].partition("/")[0] in PACKAGES
            else "napari/" + row["source"]
        ): row
        for row in migration["application_components"]
    }
    baseline = collect(pinned_source(upstream), {})
    current = collect(ROOT / "src", claimed)
    existing = {row["file"]: row for row in current}
    missing = sorted(set(claimed) - set(existing))
    if missing:
        raise SystemExit(f"Manifest refers to missing source files: {missing}")
    reference = {row["file"]: row for row in baseline}
    for row in current:
        original = reference.get(row["file"])
        row["baseline_sha256"] = original["source_sha256"] if original else None
        row["changed_from_baseline"] = (
            row["source_sha256"] != original["source_sha256"] if original else None
        )
    review_result = apply_reviews(
        ROOT,
        current,
        upstream["commit"],
        args.reviews,
        args.dependencies,
        baseline=baseline,
    )
    report = {
        "upstream": upstream,
        "distribution": migration["distribution"],
        "native_version": migration["native_version"],
        "complete_application_port": migration["complete_application_port"],
        "audit_status": "Source discovery and persisted review validation complete",
        "review_validation": review_result,
        "summary": {
            "baseline_modules": len(baseline),
            "baseline_napari_modules": sum(
                row["package"] == "napari" for row in baseline
            ),
            "current_modules": len(current),
            "declared_partial_modules": len(claimed),
            "modules_with_native_evidence": sum(
                bool(row["native_call_evidence"] or row["native_dispatch_evidence"])
                for row in current
            ),
            "current_definitions": sum(len(row["definitions"]) for row in current),
            "definitions_reviewed": sum(
                definition["review_status"] == "reviewed"
                for row in current
                for definition in row["definitions"]
            ),
            "modules_reviewed": sum(
                row["review_status"] == "reviewed" for row in current
            ),
            "current_modules_by_package": dict(
                sorted(Counter(row["package"] for row in current).items())
            ),
            "removed_baseline_modules": sorted(set(reference) - set(existing)),
            "added_modules": sorted(set(existing) - set(reference)),
        },
        "limitations": [
            "Native calls and decorators do not establish full conversion.",
            "Every definition needs review; none is marked native automatically.",
            "Module reviews cover fields, inheritance, exports and callbacks.",
            "Reviews are source-level attestations, not automatic semantic proofs.",
            "The review gate alone does not establish M9 behavior or release parity.",
            "Fields, inheritance, exports, callbacks and dependency calls need review.",
            "Import aliases are collected per module; scope shadowing needs review.",
            "Calls through native state variables are not resolved statically.",
            "Benchmarks are tooling; vendor and builtin plugin code need scope review.",
            "This does not measure behavior, performance or a conversion percentage.",
        ],
        "modules": current,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["summary"], indent=2))
    print(f"Responsibility register: {args.output}")
    print(
        "Review gate: "
        f"{review_result['reviewed']} reviewed, "
        f"{review_result['unreviewed']} unreviewed, "
        f"{review_result['invalid']} invalid, "
        f"{len(review_result['issues'])} issues"
    )
    if (args.check_complete or migration["complete_application_port"]) and not (
        review_result["complete"]
    ):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
