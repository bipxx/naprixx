"""Report unresolved native Dims API differences against pinned source.

This is a gap probe, not a passing compatibility test. It exits with status 1
while any observed behavior differs and leaves the full-port flag unchanged.
"""

import argparse
import hashlib
import importlib.util
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from napari.components import Dims

ROOT = Path(__file__).resolve().parents[1]


def reference_class(upstream):
    checkout = ROOT / ".upstream/napari"
    revision = subprocess.run(
        ["git", "-C", str(checkout), "rev-parse", "HEAD"],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.strip()
    if revision != upstream["commit"]:
        raise SystemExit(f"Unexpected upstream commit: {revision}")
    source = checkout / "src/napari/components/dims.py"
    spec = importlib.util.spec_from_file_location("_dims_gap_reference", source)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module.Dims, hashlib.sha256(source.read_bytes()).hexdigest()


def observations(base):
    result = {}
    model = base()
    fields = model.__dict__
    result["dictionary_identity"] = fields is model.__dict__
    fields["point"] = (1.0, 1.0)
    result["dictionary_mutation"] = list(model.point)

    model = base(ndim=3)
    fields = model.__dict__
    result["field_value_identity"] = model.point is model.point
    result["dictionary_field_identity"] = fields["point"] is model.point
    model.point = (1, 1, 1)
    result["dictionary_alias_after_assignment"] = {
        "still_current": fields is model.__dict__,
        "old_point": list(fields["point"]),
    }

    del model.point
    model.point = (0, 0, 0)
    result["field_reinsert_dictionary_order"] = list(model.__dict__)

    model = base(ndim=3)
    saved = model.__getstate__()
    restored = base.__new__(base)
    restored.__setstate__(saved)
    restored.point = (1, 1, 1)
    result["restored_state_independence"] = {
        "original_point": list(model.point),
        "restored_point": list(restored.point),
    }

    model = base()
    try:
        del model.point
        result["field_deletion"] = {
            "present": hasattr(model, "point"),
            "serialized": "point" in model.model_dump(),
        }
    except Exception as failure:
        result["field_deletion"] = {"error": type(failure).__name__}

    model = base().model_copy(update={"point": object()})
    try:
        result["opaque_value_unrelated_property"] = list(model.displayed)
    except Exception as failure:
        result["opaque_value_unrelated_property"] = {"error": type(failure).__name__}

    class PluginSchema(base):
        point: tuple[int, ...] = ()

    model = PluginSchema(point=(1, 2))
    result["plugin_field_types"] = [type(value).__name__ for value in model.point]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output", type=Path, default=ROOT / ".artifacts/dims-owner-contract-gaps.json"
    )
    args = parser.parse_args()
    upstream = json.loads((ROOT / "upstream.json").read_text(encoding="utf-8"))
    original, reference_hash = reference_class(upstream)
    expected, actual = observations(original), observations(Dims)
    checks = {
        name: {
            "baseline": expected[name],
            "fork": value,
            "matches_baseline": expected[name] == value,
        }
        for name, value in actual.items()
    }
    report = {
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "baseline": upstream,
        "reference_source_sha256": reference_hash,
        "comparison_scope": (
            "Unmodified pinned Dims class versus fork Dims with the current "
            "dependencies and EventedModel adapter in both cases"
        ),
        "source_hashes": {
            name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
            for name in (
                "src/napari/components/dims.py",
                "cpp/dims_model_bindings.cpp",
                "cpp/dims_dictionary.cpp",
                "cpp/include/dims_dictionary.hpp",
                "cpp/dims_flags_bindings.cpp",
                "cpp/include/dims_flags_bindings.hpp",
                "cpp/include/application.hpp",
                "cpp/application_bindings.cpp",
                "cpp/application.cpp",
                "tools/probe_dims_contract.py",
            )
        },
        "checks": checks,
        "differences": sum(not row["matches_baseline"] for row in checks.values()),
        "complete_application_port": False,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(checks, indent=2))
    print(f"Confirmed differences: {report['differences']}; report: {args.output}")
    raise SystemExit(bool(report["differences"]))


if __name__ == "__main__":
    main()
