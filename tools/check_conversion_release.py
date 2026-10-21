"""Validate proposed final release receipts; never mutate acceptance or evidence.

This tooling adds evidence checks after check_conversion_acceptance's frozen
prerequisites. Receipt/report normalization is an explicit proposed schema, not
a support decision or evidence that current application conversion is complete.
See the adjacent staged design for the complete producer contract.
"""

import argparse
import hashlib
import json
import math
import re
import statistics
import sys
import tarfile
import zipfile
from pathlib import Path, PurePosixPath
from xml.etree import ElementTree

if __package__:
    from . import audit_conversion, check_conversion_acceptance
    from .conversion_review import ReviewValidator
else:
    import audit_conversion
    import check_conversion_acceptance
    from conversion_review import ReviewValidator

COMMIT = check_conversion_acceptance.COMMIT
GATES = check_conversion_acceptance.GATES
MILESTONES = {f"M{number}" for number in range(10)}
DIAGNOSTICS = {
    "gc-reference",
    "interpreter-exit",
    "thread-gil",
    "cancellation",
    "reentry",
    "repeated-close",
    "gpu-cleanup",
}
OUTCOMES = {"passed", "failed", "skipped", "xfailed", "deselected"}
ITEM_FIELDS = {
    "acceptance_record_sha256",
    "acceptance_item_id",
    "matrix_cell_id",
    "source_inventory_sha256",
    "baseline_commit",
    "dependency_lock_sha256",
    "sdist_sha256",
    "wheel_sha256",
    "installed_binary_sha256s",
    "python_implementation_version_abi",
    "qt_binding_version_runtime_version_sdk_version",
    "os_arch_compiler_gpu_driver_display",
    "commands_cwd_env",
    "exit_codes",
    "stdout_stderr_sha256s",
    "report_sha256s",
    "collected_case_ids",
    "passed_failed_skipped_xfailed_deselected_case_ids_and_reasons",
    "original_reference_evidence",
    "measured_workload_inputs_protocol_results",
}
INDEX_FIELDS = {
    "schema_version",
    "status",
    "acceptance",
    "normalization",
    "source_inventory",
    "reviews",
    "dependencies",
    "source_audit",
    "milestones",
    "gates",
    "items",
    "independent_review",
}
REF_FIELDS = {"path", "sha256", "bytes"}
SHA = re.compile(r"[0-9a-f]{64}\Z")


class Invalid(ValueError):
    """An input does not meet the explicitly declared receipt schema."""


def require(condition, message):
    if not condition:
        raise Invalid(message)


def exact(value, fields, label):
    require(type(value) is dict, f"{label}: expected object")
    require(set(value) == set(fields), f"{label}: missing or unknown fields")
    return value


def strings(value, label, *, empty=False, unique=True):
    require(type(value) is list, f"{label}: expected list")
    require(empty or bool(value), f"{label}: empty list")
    require(
        all(type(item) is str and item.strip() for item in value),
        f"{label}: expected nonempty strings",
    )
    require(not unique or len(value) == len(set(value)), f"{label}: duplicate identity")
    return value


def sha(value, label):
    require(type(value) is str and SHA.fullmatch(value), f"{label}: invalid SHA256")
    return value


def integer(value, label, *, minimum=0):
    require(type(value) is int and value >= minimum, f"{label}: invalid integer")
    return value


def version(value):
    return type(value) is int and value == 1


def absolute_path(value, label):
    require(
        type(value) is str
        and bool(value)
        and Path(value).is_absolute()
        and ".." not in Path(value).parts,
        label + ": absolute normalized path required",
    )
    return Path(value).resolve()


def canonical(value):
    return json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
        allow_nan=False,
    ).encode("utf-8")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(data):
    def pairs(entries):
        result = {}
        for key, value in entries:
            require(key not in result, f"duplicate JSON key: {key}")
            result[key] = value
        return result

    def invalid_constant(value):
        raise Invalid(f"non-finite JSON constant: {value}")

    return json.loads(data, object_pairs_hook=pairs, parse_constant=invalid_constant)


def pointer(document, path):
    require(type(path) is str and path.startswith("/"), "invalid policy pointer")
    result = document
    for part in path[1:].split("/"):
        part = part.replace("~1", "/").replace("~0", "~")
        result = result[int(part)] if type(result) is list else result[part]
    return result


def junit(data):
    """Read actual testcase outcomes, rejecting duplicates and silent errors."""
    require(
        b"<!DOCTYPE" not in data and b"<!ENTITY" not in data,
        "JUnit: DTD/entity declarations forbidden",
    )
    tree = ElementTree.fromstring(data)
    require(tree.tag in {"testsuite", "testsuites"}, "JUnit: unexpected root")
    result = {}
    for case in tree.iter("testcase"):
        name, classname = case.get("name"), case.get("classname")
        require(name and classname, "JUnit: missing classname/name")
        identity = classname + "::" + name
        require(identity not in result, f"JUnit: duplicate testcase {identity}")
        statuses = [
            child for child in case if child.tag in {"failure", "error", "skipped"}
        ]
        require(len(statuses) <= 1, f"JUnit: ambiguous outcome {identity}")
        if not statuses:
            result[identity] = {"outcome": "passed", "reason": ""}
        else:
            child = statuses[0]
            reason = child.get("message") or child.text or ""
            kind = "failed" if child.tag in {"error", "failure"} else "skipped"
            if child.tag == "skipped" and child.get("type") == "pytest.xfail":
                kind = "xfailed"
            result[identity] = {"outcome": kind, "reason": reason.strip()}
    require(result, "JUnit: no testcase evidence")
    # Check reported aggregates too, rather than ignoring malformed counters.
    for suite in tree.iter("testsuite"):
        cases = list(suite.iter("testcase"))
        for attr, tag in (
            ("failures", "failure"),
            ("errors", "error"),
            ("skipped", "skipped"),
        ):
            if attr in suite.attrib:
                require(suite.get(attr).isdigit(), "JUnit: invalid suite counter")
                require(
                    int(suite.get(attr))
                    == sum(case.find(tag) is not None for case in cases),
                    f"JUnit: {attr} counter mismatch",
                )
        if "tests" in suite.attrib:
            require(
                suite.get("tests").isdigit() and int(suite.get("tests")) == len(cases),
                "JUnit: test counter mismatch",
            )
    return result


class Validator:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.errors = []
        self.checked = set()
        self.requirements = {}
        self.items = {}
        self.acceptance = {}
        self.index = {}

    def check(self, label, callback):
        try:
            return callback()
        except (
            Invalid,
            OSError,
            ValueError,
            TypeError,
            KeyError,
            IndexError,
            AttributeError,
            ElementTree.ParseError,
            zipfile.BadZipFile,
            tarfile.TarError,
        ) as error:
            self.errors.append({"location": label, "message": str(error)})
            return None

    def file(self, reference):
        exact(reference, REF_FIELDS, "artifact reference")
        path = reference["path"]
        require(
            type(path) is str and path and "\\" not in path and ":" not in path,
            "artifact: expected workspace relative POSIX path",
        )
        parts = PurePosixPath(path)
        require(
            not parts.is_absolute()
            and all(part not in {".", ".."} for part in path.split("/")),
            "artifact: invalid path",
        )
        actual = (self.root / path).resolve()
        require(
            actual.is_relative_to(self.root), "artifact: resolved path leaves workspace"
        )
        data = actual.read_bytes()
        require(
            len(data) == integer(reference["bytes"], "artifact.bytes"),
            "artifact: byte count mismatch",
        )
        require(
            digest(data) == sha(reference["sha256"], "artifact.sha256"),
            "artifact: stale SHA256",
        )
        self.checked.add(path)
        return data

    def document(self, reference):
        return json_bytes(self.file(reference))

    def refs(self, references):
        require(type(references) is list and references, "artifact list: missing")
        paths = []
        for reference in references:
            self.file(reference)
            paths.append(reference["path"])
        require(len(paths) == len(set(paths)), "artifact list: duplicate path")

    def prerequisites(self):
        self.acceptance = self.document(self.index["acceptance"])
        result = check_conversion_acceptance.validate_document(
            self.acceptance, self.root, check_frozen=True
        )
        require(
            result["valid"]
            and result["frozen_verified"] is True
            and result["exit_code"] == 0,
            "acceptance: frozen prerequisite validation rejected record",
        )
        require(
            self.acceptance["complete_application_port"] is False,
            "acceptance prerequisite completion flag must remain false",
        )
        require(
            self.acceptance["milestone_status"] == dict.fromkeys(MILESTONES, "open"),
            "acceptance prerequisite milestones must remain open",
        )
        require(
            set(self.acceptance["release_receipt_contract"]["required_per_item_fields"])
            == ITEM_FIELDS,
            "release contract fields changed; checker schema decision required",
        )
        require(
            {row["id"] for row in self.acceptance["final_evidence_gates"]} == GATES,
            "release gate definitions changed",
        )

    def inventory(self):
        inventory = self.document(self.index["source_inventory"])
        exact(inventory, {"schema_version", "files"}, "source inventory")
        require(version(inventory["schema_version"]), "source inventory version")
        self.refs(inventory["files"])
        expected = set()
        for directory in ("src", "cpp", "tools", "tests"):
            for path in (self.root / directory).rglob("*"):
                if (
                    path.is_file()
                    and "__pycache__" not in path.parts
                    and path.suffix
                    in {
                        ".py",
                        ".pyi",
                        ".cpp",
                        ".cc",
                        ".cxx",
                        ".h",
                        ".hpp",
                        ".hxx",
                        ".cmake",
                    }
                ):
                    expected.add(path.relative_to(self.root).as_posix())
        for name in (
            "CMakeLists.txt",
            "pyproject.toml",
            "migration.json",
            "upstream.json",
        ):
            require((self.root / name).is_file(), f"source inventory: missing {name}")
            expected.add(name)
        require(
            expected <= {row["path"] for row in inventory["files"]},
            "source inventory: current application/tooling/build inputs omitted",
        )

    def source_audit(self):
        audit = self.document(self.index["source_audit"])
        exact(
            audit,
            {
                "schema_version",
                "command",
                "report",
                "reviews",
                "dependencies",
                "source_inventory_sha256",
            },
            "source audit receipt",
        )
        require(version(audit["schema_version"]), "source audit version")
        require(
            audit["reviews"] == self.index["reviews"]
            and audit["dependencies"] == self.index["dependencies"],
            "source audit ledger mismatch",
        )
        self.document(audit["reviews"])
        self.document(audit["dependencies"])
        require(
            audit["source_inventory_sha256"]
            == self.index["source_inventory"]["sha256"],
            "source audit source mismatch",
        )
        self.command(audit["command"])
        argv = audit["command"]["argv"]
        require(
            "--check-complete" in argv
            and any(
                token.replace("\\", "/").endswith("tools/audit_conversion.py")
                for token in argv
            ),
            "source audit: actual --check-complete command required",
        )
        for flag, reference in (
            ("--reviews", audit["reviews"]),
            ("--dependencies", audit["dependencies"]),
            ("--output", audit["report"]),
        ):
            require(
                flag in argv and argv.count(flag) == 1,
                f"source audit: exact {flag} binding required",
            )
            actual = Path(argv[argv.index(flag) + 1])
            if not actual.is_absolute():
                actual = Path(audit["command"]["cwd"]) / actual
            require(
                actual.resolve() == (self.root / reference["path"]).resolve(),
                f"source audit: wrong {flag} artifact",
            )
        report = self.document(audit["report"])
        current = audit_conversion.collect(self.root / "src", {})
        baseline = audit_conversion.collect(self.root / ".upstream/napari/src", {})
        result = ReviewValidator(self.root, current, COMMIT, baseline).run(
            self.root / audit["reviews"]["path"],
            self.root / audit["dependencies"]["path"],
        )
        require(
            result["complete"]
            and result["unreviewed"] == 0
            and result["invalid"] == 0
            and result["issues"] == [],
            "current source reviews incomplete",
        )
        recorded = report["review_validation"]
        for field in (
            "complete",
            "reviewed",
            "unreviewed",
            "invalid",
            "issues",
            "unreviewed_responsibilities",
        ):
            require(recorded[field] == result[field], f"actual audit: stale {field}")
        expected = {(row["file"], row["source_sha256"]) for row in current}
        require(
            {(row["file"], row["source_sha256"]) for row in report["modules"]}
            == expected,
            "actual audit: current module inventory mismatch",
        )
        require(report["upstream"]["commit"] == COMMIT, "actual audit: wrong baseline")
        self.responsibilities = set(
            ReviewValidator(self.root, current, COMMIT, baseline).entries
        )

    def normalization(self):
        normalized = self.document(self.index["normalization"])
        exact(
            normalized,
            {
                "schema_version",
                "acceptance_record_sha256",
                "choice_resolution",
                "requirements",
                "diagnostic_oracles",
            },
            "normalization",
        )
        require(version(normalized["schema_version"]), "normalization version")
        require(
            normalized["acceptance_record_sha256"]
            == self.index["acceptance"]["sha256"],
            "normalization: stale acceptance",
        )
        choices = {row["id"]: row for row in self.acceptance["unresolved_choices"]}
        require(
            normalized["choice_resolution"]
            == choices["acceptance-validator-and-evidence-index"][
                "resolution_evidence"
            ],
            "normalization must bind actual resolved receipt-schema decision",
        )
        self.document(normalized["choice_resolution"])
        cells = {
            row["id"]: row
            for row in self.acceptance["candidate_matrix"]["cells"]
            if row["required_for_release"] is True
            and row["applicability"]["applicable"] is True
        }
        require(cells, "normalization: no required matrix cells")
        self.cells = cells
        expected = {}

        def add(identity, kind, policy, case_ids, applicable):
            strings(case_ids, identity + ".case_ids")
            strings(applicable, identity + ".matrix_cell_ids", empty=True)
            if not applicable:
                applicability = policy.get("applicability")
                require(
                    type(applicability) is dict
                    and applicability.get("applicable") is False
                    and type(applicability.get("reason")) is str
                    and applicability["reason"].strip()
                    and type(applicability.get("review")) is dict,
                    identity + ": empty scope requires reviewed nonapplicability",
                )
                self.file(applicability["review"])
            require(
                set(applicable) <= set(cells), identity + ": unknown/nonrequired cell"
            )
            for cell in applicable:
                expected[(identity, cell)] = (
                    kind,
                    digest(canonical(policy)),
                    set(case_ids),
                )

        fixtures = self.acceptance["full_fixture_contract"]
        require(
            set(fixtures["expected_collection_per_cell"]) >= set(cells),
            "normalization: missing full-fixture collection cells",
        )
        for cell in cells:
            policy = fixtures["expected_collection_per_cell"][cell]
            require(
                type(policy) is dict and "case_ids" in policy,
                "normalization resolution required: full collection needs "
                "exact case_ids",
            )
            add("full-fixtures", "full-fixtures", policy, policy["case_ids"], [cell])
            add(
                "distribution",
                "distribution",
                cells[cell],
                cells[cell]["distribution_case_ids"],
                [cell],
            )
        for policy in self.acceptance["required_workflows"]:
            add(
                "workflow:" + policy["id"],
                "workflow",
                policy,
                policy["exact_case_ids"],
                policy["applicable_matrix_cell_ids"],
            )
        providers = self.acceptance["provider_contract"]["exact_profile_case_map"]
        require(
            type(providers) is dict and providers, "normalization: provider map absent"
        )
        for name, policy in providers.items():
            require(
                type(policy) is dict
                and {"case_ids", "matrix_cell_ids", "artifact_lock"} <= set(policy),
                "normalization resolution required: provider cases/cells/artifact_lock",
            )
            self.file(policy["artifact_lock"])
            add(
                "provider:" + name,
                "provider",
                policy,
                policy["case_ids"],
                policy["matrix_cell_ids"],
            )
        for policy in self.acceptance["plugin_contract"][
            "representative_plugin_artifacts"
        ]:
            require(
                "case_ids" in policy,
                "normalization resolution required: plugin exact case_ids",
            )
            add(
                "plugin:" + policy["distribution"] + "@" + policy["version"],
                "plugin",
                policy,
                policy["case_ids"],
                policy["matrix_cell_ids"],
            )
        workload = self.acceptance["workload_contract"]
        workload_names = set()
        for suite in workload["pinned_suite_inventory"]:
            for policy in suite["selected_acceptance_cases"]:
                require(
                    type(policy) is dict
                    and {"id", "matrix_cell_ids", "case_ids", "metrics"} <= set(policy),
                    "normalization resolution required: workload cases/cells/metrics",
                )
                identity = "workload:" + suite["id"] + ":" + policy["id"]
                require(
                    identity not in workload_names, "normalization: duplicate workload"
                )
                workload_names.add(identity)
                add(
                    identity,
                    "workload",
                    policy,
                    policy["case_ids"],
                    policy["matrix_cell_ids"],
                )
        # End-to-end IDs require cases explicitly frozen in the same contract.
        e2e = workload.get("end_to_end_cases")
        require(
            type(e2e) is dict and set(e2e) == set(workload["end_to_end_workload_ids"]),
            "normalization resolution required: exact end-to-end workload cases",
        )
        for name, policy in e2e.items():
            add(
                "workload:e2e:" + name,
                "workload",
                policy,
                policy["case_ids"],
                policy["matrix_cell_ids"],
            )
        oracles = normalized["diagnostic_oracles"]
        require(
            type(oracles) is dict and set(oracles) == DIAGNOSTICS,
            "diagnostic oracle coverage",
        )
        resolution = self.document(normalized["choice_resolution"])
        require(
            resolution.get("diagnostic_oracles") == oracles,
            "diagnostic oracles must be predeclared in actual choice resolution",
        )
        for name, policy in oracles.items():
            add(
                "diagnostic:" + name,
                "diagnostic",
                policy,
                policy["case_ids"],
                policy["matrix_cell_ids"],
            )
        for cell, policy in cells.items():
            if policy["binding"] is not None:
                require(
                    ("diagnostic:gpu-cleanup", cell) in expected,
                    "GUI cell missing GPU cleanup",
                )
            for name in DIAGNOSTICS - {"gpu-cleanup"}:
                require(
                    ("diagnostic:" + name, cell) in expected,
                    "cell missing lifetime/thread diagnostic",
                )
        rows = normalized["requirements"]
        require(type(rows) is list and rows, "normalization: requirements absent")
        actual = {}
        for row in rows:
            exact(
                row,
                {
                    "acceptance_item_id",
                    "matrix_cell_id",
                    "kind",
                    "policy_pointer",
                    "policy_sha256",
                    "case_ids",
                    "allowed_baseline_outcomes",
                    "input_artifacts",
                    "oracles",
                    "metrics",
                },
                "normalized requirement",
            )
            key = (row["acceptance_item_id"], row["matrix_cell_id"])
            require(key not in actual, "normalization: duplicate requirement")
            policy = (
                pointer(self.acceptance, row["policy_pointer"])
                if row["kind"] != "diagnostic"
                else oracles[row["acceptance_item_id"].removeprefix("diagnostic:")]
            )
            require(
                digest(canonical(policy)) == row["policy_sha256"],
                "normalization: policy pointer hash mismatch",
            )
            actual[key] = (
                row["kind"],
                row["policy_sha256"],
                set(strings(row["case_ids"], "cases")),
            )
            self.refs(row["input_artifacts"]) if row["input_artifacts"] else None
            require(
                type(row["oracles"]) is dict and row["oracles"],
                "normalization: empty actual oracles",
            )
            # No inferred tolerance or exclusion: normalized forms must appear in
            # the frozen policy (or be bound by its resolved normalization record).
            for field in (
                "allowed_baseline_outcomes",
                "input_artifacts",
                "oracles",
                "metrics",
            ):
                require(
                    policy.get(field) == row[field],
                    f"normalization resolution required: exact {field}",
                )
            require(
                type(row["allowed_baseline_outcomes"]) is dict,
                "baseline outcomes: expected object",
            )
            self.requirements[key] = row
        require(
            actual == expected,
            "normalization: omitted, altered or extra scope/case/cell requirement",
        )

    def command(self, command):
        exact(
            command,
            {"argv", "cwd", "environment", "exit_code", "stdout", "stderr", "reports"},
            "command",
        )
        strings(command["argv"], "command.argv", unique=False)
        require(
            type(command["cwd"]) is str and Path(command["cwd"]).is_absolute(),
            "command: absolute cwd required",
        )
        require(
            type(command["environment"]) is dict
            and all(
                type(k) is str and k and (v is None or type(v) is str)
                for k, v in command["environment"].items()
            ),
            "command: environment must record exact set/unset values",
        )
        require(
            type(command["exit_code"]) is int and command["exit_code"] == 0,
            "command: nonzero/invalid exit",
        )
        self.file(command["stdout"])
        self.file(command["stderr"])
        self.refs(command["reports"])

    def outcomes(self, item, requirement):
        partition = item[
            "passed_failed_skipped_xfailed_deselected_case_ids_and_reasons"
        ]
        exact(partition, OUTCOMES, "case partition")
        flattened = {}
        for outcome, values in partition.items():
            require(
                type(values) is dict, "case partition: outcome must map IDs to reasons"
            )
            for case, reason in values.items():
                require(
                    type(case) is str and case and case not in flattened,
                    "case partition: duplicate/empty ID",
                )
                require(type(reason) is str, "case partition: reason must be text")
                require(
                    (outcome == "passed" and reason == "")
                    or (outcome != "passed" and reason.strip()),
                    "case partition: wrong reason",
                )
                flattened[case] = {"outcome": outcome, "reason": reason}
        collection = set(strings(item["collected_case_ids"], "collected_case_ids"))
        require(
            set(flattened) == collection == set(requirement["case_ids"]),
            "case partition/collection/policy mismatch",
        )
        require(not partition["failed"], "failed/error testcase")
        exclusions = {
            case: record
            for case, record in flattened.items()
            if record["outcome"] != "passed"
        }
        require(
            exclusions == requirement["allowed_baseline_outcomes"],
            "new, missing or changed baseline exclusion",
        )
        reports = item["report_sha256s"]
        require(
            type(reports) is dict
            and set(reports) == {"junit", "collection", "observations"},
            "report roles",
        )
        real = junit(self.file(reports["junit"]))
        require(
            real
            == {
                case: value
                for case, value in flattened.items()
                if value["outcome"] != "deselected"
            },
            "case outcomes disagree with actual JUnit",
        )
        collection_report = self.document(reports["collection"])
        exact(
            collection_report,
            {
                "schema_version",
                "fixture_source_inventory_sha256",
                "collected_case_ids",
                "deselected_case_ids_and_reasons",
                "excluded_roots",
            },
            "collection report",
        )
        require(
            version(collection_report["schema_version"])
            and collection_report["excluded_roots"] == [],
            "collection: migration exclusions/version rejected",
        )
        require(
            collection_report["fixture_source_inventory_sha256"]
            == self.acceptance["full_fixture_contract"][
                "fixture_source_inventory_sha256"
            ],
            "collection: fixture inventory mismatch",
        )
        require(
            set(strings(collection_report["collected_case_ids"], "actual collection"))
            == collection,
            "actual collection differs from receipt",
        )
        require(
            collection_report["deselected_case_ids_and_reasons"]
            == partition["deselected"],
            "actual deselection differs from receipt",
        )
        return self.document(reports["observations"])

    def observations(self, report, requirement, item):
        exact(
            report,
            {
                "schema_version",
                "acceptance_item_id",
                "matrix_cell_id",
                "bindings",
                "checks",
                "distribution",
                "diagnostics",
                "workload",
            },
            "observation report",
        )
        require(
            version(report["schema_version"])
            and report["acceptance_item_id"] == item["acceptance_item_id"]
            and report["matrix_cell_id"] == item["matrix_cell_id"],
            "observation identity mismatch",
        )
        expected = {
            field: item[field]
            for field in (
                "acceptance_record_sha256",
                "source_inventory_sha256",
                "baseline_commit",
                "dependency_lock_sha256",
                "sdist_sha256",
                "wheel_sha256",
                "installed_binary_sha256s",
                "python_implementation_version_abi",
                "qt_binding_version_runtime_version_sdk_version",
                "os_arch_compiler_gpu_driver_display",
            )
        }
        require(
            report["bindings"] == expected,
            "observation artifact/environment binding mismatch",
        )
        checks = report["checks"]
        require(
            type(checks) is dict and set(checks) == set(requirement["oracles"]),
            "observation oracle coverage",
        )
        covered_cases = set()
        for name, oracle in requirement["oracles"].items():
            check = exact(
                checks[name], {"case_ids", "expected", "observed"}, "observation check"
            )
            require(
                set(strings(check["case_ids"], "check cases"))
                <= set(requirement["case_ids"]),
                "unknown check case",
            )
            require(
                set(check["case_ids"]) == set(requirement["case_ids"]),
                "observation required case coverage incomplete for oracle: " + name,
            )
            covered_cases.update(check["case_ids"])
            require(
                canonical(check["expected"]) == canonical(oracle)
                and canonical(check["observed"]) == canonical(oracle),
                f"observation oracle failed: {name}",
            )
        require(
            covered_cases == set(requirement["case_ids"]),
            "observation required case coverage incomplete",
        )
        kind = requirement["kind"]
        for field, active in (
            ("distribution", "distribution"),
            ("diagnostics", "diagnostic"),
            ("workload", "workload"),
        ):
            require(
                (report[field] is not None) == (kind == active),
                "observation payload role mismatch",
            )
        if kind == "distribution":
            self.distribution(report["distribution"], item)
        if kind == "diagnostic":
            exact(report["diagnostics"], {"processes", "oracle"}, "diagnostic report")
            require(
                canonical(report["diagnostics"]["oracle"])
                == canonical(requirement["oracles"]),
                "diagnostic oracle changed",
            )
            processes = report["diagnostics"]["processes"]
            require(
                type(processes) is list and processes,
                "diagnostic: missing actual processes",
            )
            for process in processes:
                exact(process, {"command", "observed"}, "diagnostic process")
                self.command(process["command"])
                require(
                    canonical(process["observed"]) == canonical(requirement["oracles"]),
                    "per-process diagnostic failed",
                )
        if kind == "workload":
            self.workload(report["workload"], requirement, item)

    def item(self, item):
        exact(item, ITEM_FIELDS, "release item")
        key = (item["acceptance_item_id"], item["matrix_cell_id"])
        require(
            key in self.requirements and key not in self.items,
            "unknown or duplicate release item",
        )
        requirement = self.requirements[key]
        cell = self.cells[key[1]]
        require(
            item["acceptance_record_sha256"] == self.index["acceptance"]["sha256"]
            and item["source_inventory_sha256"]
            == self.index["source_inventory"]["sha256"]
            and item["baseline_commit"] == COMMIT,
            "item source/acceptance/reference mismatch",
        )
        require(
            item["dependency_lock_sha256"]
            == cell["dependency_artifact_lock"]["sha256"],
            "item dependency lock mismatch",
        )
        self.file(cell["dependency_artifact_lock"])
        for field in ("sdist_sha256", "wheel_sha256"):
            sha(item[field], field)
        require(
            type(item["installed_binary_sha256s"]) is dict
            and item["installed_binary_sha256s"],
            "missing installed native binaries",
        )
        for value in item["installed_binary_sha256s"].values():
            sha(value, "installed binary")
        python = exact(
            item["python_implementation_version_abi"],
            {"implementation", "version", "abi"},
            "Python identity",
        )
        require(
            python["implementation"] == cell["interpreter"]
            and python["version"] == cell["python_patch"]
            and python["abi"] == cell["python_abi"]
            and type(python["abi"]) is str
            and python["abi"],
            "Python identity mismatch",
        )
        qt = item["qt_binding_version_runtime_version_sdk_version"]
        if cell["binding"] is None:
            require(qt is None, "headless Qt identity must be null")
        else:
            exact(
                qt,
                {
                    "binding",
                    "binding_version",
                    "runtime_version",
                    "sdk_version",
                    "observed_qversion",
                },
                "Qt identity",
            )
            require(
                qt
                == {
                    "binding": cell["binding"],
                    "binding_version": cell["binding_package_version_from_constraints"],
                    "runtime_version": cell[
                        "qt_runtime_package_version_from_constraints"
                    ],
                    "sdk_version": cell["native_qt_sdk_version"],
                    "observed_qversion": cell["native_qt_sdk_version"],
                },
                "Qt runtime/binding/SDK mismatch",
            )
        host = exact(
            item["os_arch_compiler_gpu_driver_display"],
            {
                "os",
                "architecture",
                "compiler_lock_sha256",
                "host_lock_sha256",
                "gpu_driver_display_lock_sha256",
            },
            "host identity",
        )
        require(
            host["os"] == cell["os_version"]
            and host["architecture"] == cell["architecture"]
            and host["compiler_lock_sha256"]
            == cell["compiler_toolchain_lock"]["sha256"],
            "host/compiler mismatch",
        )
        host_ref = cell.get("host_fingerprint") or cell.get("runner_image_digest")
        require(
            host["host_lock_sha256"]
            == (host_ref["sha256"] if type(host_ref) is dict else host_ref),
            "runner identity mismatch",
        )
        require(
            host["gpu_driver_display_lock_sha256"]
            == (
                cell["gpu_driver_lock"]["sha256"]
                if cell["binding"] is not None
                else None
            ),
            "GPU/display mismatch",
        )
        commands = item["commands_cwd_env"]
        require(type(commands) is list and commands, "item actual commands missing")
        for command in commands:
            self.command(command)
        require(
            item["exit_codes"] == [command["exit_code"] for command in commands],
            "item exit-code mismatch",
        )
        require(
            item["stdout_stderr_sha256s"]
            == [
                {"stdout": c["stdout"]["sha256"], "stderr": c["stderr"]["sha256"]}
                for c in commands
            ],
            "item output SHA mismatch",
        )
        report_refs = list(item["report_sha256s"].values())
        require(
            all(
                ref in [value for c in commands for value in c["reports"]]
                for ref in report_refs
            ),
            "item reports not bound to actual commands",
        )
        observation = self.outcomes(item, requirement)
        original = self.document(item["original_reference_evidence"])
        exact(
            original,
            {
                "schema_version",
                "baseline_commit",
                "policy_sha256",
                "input_artifacts",
                "commands",
                "junit",
                "collection",
                "observations",
            },
            "original reference",
        )
        require(
            version(original["schema_version"])
            and original["baseline_commit"] == COMMIT
            and original["policy_sha256"] == requirement["policy_sha256"]
            and original["input_artifacts"] == requirement["input_artifacts"],
            "original reference policy/input mismatch",
        )
        require(
            type(original["commands"]) is list and original["commands"],
            "original actual commands missing",
        )
        for command in original["commands"]:
            self.command(command)
        require(
            all(
                original[field]
                in [
                    ref
                    for command in original["commands"]
                    for ref in command["reports"]
                ]
                for field in ("junit", "collection", "observations")
            ),
            "original reports not bound to actual commands",
        )
        originals = junit(self.file(original["junit"]))
        require(
            set(originals)
            == set(requirement["case_ids"])
            - set(
                item["passed_failed_skipped_xfailed_deselected_case_ids_and_reasons"][
                    "deselected"
                ]
            ),
            "original testcase coverage differs",
        )
        require(
            all(value["outcome"] != "failed" for value in originals.values()),
            "original reference failed",
        )
        for case, outcome in originals.items():
            if outcome["outcome"] != "passed":
                require(
                    requirement["allowed_baseline_outcomes"].get(case) == outcome,
                    "unqualified original exclusion",
                )
        baseline_collection = self.document(original["collection"])
        require(
            baseline_collection == self.document(item["report_sha256s"]["collection"]),
            "original/candidate collection differs",
        )
        baseline_observations = self.document(original["observations"])
        require(
            baseline_observations.get("policy_sha256") == requirement["policy_sha256"]
            and canonical(baseline_observations.get("checks"))
            == canonical(requirement["oracles"]),
            "original reference oracle mismatch",
        )
        if observation["workload"] is not None:
            reference = baseline_observations.get("workload")
            candidate = observation["workload"]
            require(type(reference) is dict, "actual original workload report missing")
            for field in ("input_artifacts", "protocol", "reference_commit"):
                require(
                    reference.get(field) == candidate[field],
                    "original/candidate workload inputs or protocol differ",
                )
            require(
                type(reference.get("metrics")) is dict
                and set(reference["metrics"]) == set(candidate["metrics"]),
                "actual original workload metric coverage differs",
            )
            for name, metric in candidate["metrics"].items():
                require(
                    reference["metrics"][name] == metric["baseline"],
                    "candidate receipt substituted original workload measurements",
                )
        self.observations(observation, requirement, item)
        require(
            item["measured_workload_inputs_protocol_results"]
            == observation["workload"],
            "workload receipt/report mismatch",
        )
        self.items[key] = item

    def workload(self, report, requirement, item):
        exact(
            report,
            {"input_artifacts", "protocol", "reference_commit", "metrics"},
            "workload report",
        )
        require(
            report["input_artifacts"] == requirement["input_artifacts"]
            and report["reference_commit"] == COMMIT,
            "workload input/reference mismatch",
        )
        self.refs(report["input_artifacts"])
        require(
            report["protocol"]
            == self.acceptance["workload_contract"]["frozen_measurement_protocol"],
            "workload protocol differs",
        )
        metrics = report["metrics"]
        require(
            type(metrics) is dict
            and set(metrics) == set(requirement["metrics"])
            and metrics,
            "workload metric coverage",
        )
        for name, policy in requirement["metrics"].items():
            exact(
                policy,
                {
                    "aggregation",
                    "direction",
                    "absolute_tolerance",
                    "relative_tolerance",
                    "minimum_samples",
                },
                "metric policy",
            )
            row = exact(
                metrics[name],
                {"baseline", "candidate", "policy"},
                "metric measurements",
            )
            require(
                row["policy"] == policy, "metric tolerance changed after measurement"
            )
            values = []
            for series in (row["baseline"], row["candidate"]):
                require(
                    type(series) is list
                    and len(series)
                    >= integer(policy["minimum_samples"], "minimum samples", minimum=1),
                    "insufficient samples",
                )
                require(
                    all(type(v) in {int, float} and math.isfinite(v) for v in series),
                    "non-finite/invalid measurement",
                )
                require(
                    policy["aggregation"] in {"mean", "median", "max", "min"},
                    "unknown aggregation",
                )
                values.append(
                    {
                        "mean": statistics.mean,
                        "median": statistics.median,
                        "max": max,
                        "min": min,
                    }[policy["aggregation"]](series)
                )
            baseline, candidate = values
            for tolerance in (
                policy["absolute_tolerance"],
                policy["relative_tolerance"],
            ):
                require(
                    type(tolerance) in {int, float}
                    and math.isfinite(tolerance)
                    and tolerance >= 0,
                    "invalid tolerance",
                )
            allowed = (
                policy["absolute_tolerance"]
                + abs(baseline) * policy["relative_tolerance"]
            )
            direction = policy["direction"]
            require(
                direction in {"equal", "lower", "higher"}, "unknown metric direction"
            )
            require(
                (direction == "equal" and abs(candidate - baseline) <= allowed)
                or (direction == "lower" and candidate <= baseline + allowed)
                or (direction == "higher" and candidate >= baseline - allowed),
                f"workload tolerance failed: {name}",
            )

    def distribution(self, report, item):
        exact(
            report,
            {
                "sdist",
                "wheels",
                "wheel_build_commands",
                "sdist_source_files",
                "wheel_members",
                "environment_root",
                "site_packages",
                "executable",
                "install_command",
                "probe_command",
                "cwd",
                "sys_path",
                "module_origins",
                "installed_binaries",
                "native_qt_probe_case_ids",
            },
            "distribution report",
        )
        require(
            report["sdist"]["sha256"] == item["sdist_sha256"], "sdist identity mismatch"
        )
        self.file(report["sdist"])
        require(
            type(report["wheels"]) is list and len(report["wheels"]) == 2,
            "two reproducible wheel builds required",
        )
        require(
            all(row["sha256"] == item["wheel_sha256"] for row in report["wheels"]),
            "wheel builds are not reproducible",
        )
        self.refs(report["wheels"])
        require(
            type(report["wheel_build_commands"]) is list
            and len(report["wheel_build_commands"]) == 2,
            "wheel build receipts required",
        )
        for command, wheel_ref in zip(
            report["wheel_build_commands"], report["wheels"], strict=True
        ):
            self.command(command)
            require(
                wheel_ref in command["reports"],
                "wheel output not bound to build command",
            )
            require(
                any(
                    (
                        Path(token)
                        if Path(token).is_absolute()
                        else Path(command["cwd"]) / token
                    ).resolve()
                    == (self.root / report["sdist"]["path"]).resolve()
                    for token in command["argv"]
                ),
                "wheel not built from retained sdist",
            )
        with tarfile.open(self.root / report["sdist"]["path"], "r:*") as archive:
            members = archive.getmembers()
            names = [row.name for row in members]
            require(len(names) == len(set(names)), "duplicate sdist member")
            require(
                all(
                    not row.issym()
                    and not row.islnk()
                    and not PurePosixPath(row.name).is_absolute()
                    and ".." not in PurePosixPath(row.name).parts
                    for row in members
                ),
                "unsafe sdist member",
            )
            files = {
                row.name: archive.extractfile(row).read()
                for row in members
                if row.isfile()
            }
        source_files = report["sdist_source_files"]
        inventory = self.document(self.index["source_inventory"])
        require(
            type(source_files) is dict
            and set(source_files) == {row["path"] for row in inventory["files"]},
            "sdist source coverage",
        )
        for reference in inventory["files"]:
            require(
                source_files[reference["path"]] in files
                and digest(files[source_files[reference["path"]]])
                == reference["sha256"],
                "sdist source mismatch",
            )
        with zipfile.ZipFile(self.root / report["wheels"][0]["path"]) as archive:
            names = archive.namelist()
            require(
                len(names) == len(set(names))
                and all(
                    not PurePosixPath(name).is_absolute()
                    and ".." not in PurePosixPath(name).parts
                    and "\\" not in name
                    for name in names
                ),
                "unsafe/duplicate wheel member",
            )
            wheel = {
                name: archive.read(name) for name in names if not name.endswith("/")
            }
        require(
            report["wheel_members"]
            == {name: digest(data) for name, data in wheel.items()},
            "wheel member hash mismatch",
        )
        environment = absolute_path(report["environment_root"], "environment root")
        require(
            environment.is_absolute()
            and not environment.is_relative_to(self.root)
            and not self.root.is_relative_to(environment),
            "fresh environment must be outside checkout",
        )
        site = absolute_path(report["site_packages"], "site packages")
        executable = absolute_path(report["executable"], "executable")
        require(
            site.is_relative_to(environment) and executable.is_relative_to(environment),
            "environment provenance mismatch",
        )
        require(
            not absolute_path(report["cwd"], "probe cwd").is_relative_to(self.root),
            "fresh probe cwd inside checkout",
        )
        require(
            type(report["sys_path"]) is list
            and all(
                not absolute_path(path, "sys.path").is_relative_to(self.root)
                for path in report["sys_path"]
            ),
            "checkout import path in fresh environment",
        )
        for command in (report["install_command"], report["probe_command"]):
            self.command(command)
            require(
                Path(command["argv"][0]).resolve() == executable
                and not Path(command["cwd"]).resolve().is_relative_to(self.root),
                "fresh install/probe executable or cwd mismatch",
            )
            require(
                command["environment"].get("PYTHONPATH") in {None, ""},
                "fresh install/probe PYTHONPATH contamination",
            )
        require(
            any(
                Path(token).name == Path(report["wheels"][0]["path"]).name
                for token in report["install_command"]["argv"]
            ),
            "install command does not bind wheel",
        )
        origins = report["module_origins"]
        require(
            type(origins) is dict
            and {"napari", "naparixx", "naparixx._native"} <= set(origins),
            "missing actual module origins",
        )
        require(
            all(
                absolute_path(path, "module origin").is_relative_to(site)
                for path in origins.values()
            ),
            "application fallback/origin outside site-packages",
        )
        for origin in origins.values():
            path = Path(origin).resolve()
            member = path.relative_to(site).as_posix()
            require(
                member in wheel and path.read_bytes() == wheel[member],
                "fresh module origin does not match installed wheel bytes",
            )
        binaries = report["installed_binaries"]
        require(type(binaries) is list and binaries, "installed binaries missing")
        actual = {}
        for row in binaries:
            exact(
                row,
                {"module", "installed_path", "wheel_member", "sha256"},
                "installed binary",
            )
            path = Path(row["installed_path"]).resolve()
            require(
                path.is_relative_to(site) and origins.get(row["module"]) == str(path),
                "installed binary origin mismatch",
            )
            data = path.read_bytes()
            require(
                data.startswith(
                    (
                        b"MZ",
                        b"\x7fELF",
                        b"\xcf\xfa\xed\xfe",
                        b"\xfe\xed\xfa\xcf",
                        b"\xca\xfe\xba\xbe",
                    )
                ),
                "native payload missing binary header",
            )
            require(
                row["wheel_member"] in wheel
                and data == wheel[row["wheel_member"]]
                and digest(data) == sha(row["sha256"], "binary.sha256"),
                "installed/wheel binary mismatch",
            )
            require(row["module"] not in actual, "duplicate installed native binary")
            actual[row["module"]] = row["sha256"]
        require(
            actual == item["installed_binary_sha256s"] and "naparixx._native" in actual,
            "installed binary set mismatch",
        )
        if self.cells[item["matrix_cell_id"]]["binding"] is not None:
            require(
                any(name.startswith("naparixx._qt") for name in actual),
                "actual native Qt bridge missing",
            )
            require(
                set(
                    strings(report["native_qt_probe_case_ids"], "native Qt probe cases")
                )
                <= set(item["collected_case_ids"]),
                "native Qt probes not executed",
            )
        else:
            require(
                report["native_qt_probe_case_ids"] == [],
                "headless cell claims GUI probe",
            )

    def final_gates(self):
        require(
            set(self.items) == set(self.requirements),
            "required release items incomplete",
        )
        # Each cell may have a distinct wheel/lock; all its evidence shares them.
        for cell in self.cells:
            items = [
                value for (_, identity), value in self.items.items() if identity == cell
            ]
            distribution = self.items[("distribution", cell)]
            for item in items:
                for field in (
                    "sdist_sha256",
                    "wheel_sha256",
                    "installed_binary_sha256s",
                    "dependency_lock_sha256",
                    "python_implementation_version_abi",
                    "qt_binding_version_runtime_version_sdk_version",
                    "os_arch_compiler_gpu_driver_display",
                ):
                    require(
                        item[field] == distribution[field],
                        "same-cell source/artifact/environment drift",
                    )
        milestones = self.index["milestones"]
        require(
            type(milestones) is dict and set(milestones) == MILESTONES,
            "M0-M9 receipt coverage",
        )
        assigned = set()
        for name, reference in milestones.items():
            report = self.document(reference)
            exact(
                report,
                {
                    "schema_version",
                    "milestone",
                    "exit_contract",
                    "responsibilities",
                    "item_keys",
                    "review",
                },
                "milestone receipt",
            )
            require(
                version(report["schema_version"]) and report["milestone"] == name,
                "milestone identity",
            )
            contract = report["exit_contract"]
            exact(contract, {"plan", "exact_text", "sha256"}, "milestone exit")
            plan = self.file(contract["plan"]).decode("utf-8")
            require(
                contract["plan"]["path"] == "docs/full-conversion-plan.md"
                and digest(contract["exact_text"].encode()) == contract["sha256"],
                "milestone exit contract mismatch",
            )
            rows = [
                line
                for line in plan.splitlines()
                if line.startswith("| " + name + " |")
            ]
            require(
                len(rows) == 1 and rows[0] == contract["exact_text"],
                "milestone exit text is not actual plan exit",
            )
            responsibilities = strings(
                report["responsibilities"], "milestone responsibilities", empty=True
            )
            require(
                not assigned.intersection(responsibilities)
                and set(responsibilities) <= self.responsibilities,
                "milestone responsibility collision/unknown",
            )
            assigned.update(responsibilities)
            keys = report["item_keys"]
            require(
                type(keys) is list
                and keys
                and all(
                    type(k) is list and len(k) == 2 and tuple(k) in self.items
                    for k in keys
                ),
                "milestone actual item coverage",
            )
            review = self.document(report["review"])
            require(
                review.get("verdict") == "PASS"
                and review.get("findings") == []
                and review.get("exit_contract_sha256") == contract["sha256"]
                and review.get("item_keys") == keys,
                "milestone exit independent review rejected/stale",
            )
        require(
            assigned == self.responsibilities,
            "milestones omit current responsibilities",
        )
        gates = self.index["gates"]
        require(
            type(gates) is dict and set(gates) == GATES, "final gate receipt coverage"
        )
        for name, reference in gates.items():
            report = self.document(reference)
            exact(
                report,
                {
                    "schema_version",
                    "gate_id",
                    "acceptance_record_sha256",
                    "source_inventory_sha256",
                    "item_keys",
                    "milestone_receipts",
                    "source_audit",
                },
                "gate receipt",
            )
            require(
                version(report["schema_version"])
                and report["gate_id"] == name
                and report["acceptance_record_sha256"]
                == self.index["acceptance"]["sha256"]
                and report["source_inventory_sha256"]
                == self.index["source_inventory"]["sha256"],
                "gate bindings mismatch",
            )
            require(
                report["milestone_receipts"] == milestones
                and report["source_audit"] == self.index["source_audit"],
                "gate source/milestone scope mismatch",
            )
            keys = report["item_keys"]
            require(
                type(keys) is list
                and len(keys) == len(self.items)
                and {tuple(k) for k in keys} == set(self.items),
                "gate item scope incomplete/duplicate",
            )
        review = self.document(self.index["independent_review"])
        exact(
            review,
            {
                "schema_version",
                "verdict",
                "findings",
                "reviewed_by",
                "index_payload_sha256",
                "gate_refs",
            },
            "independent release review",
        )
        payload = {
            key: value
            for key, value in self.index.items()
            if key != "independent_review"
        }
        require(
            version(review["schema_version"])
            and review["verdict"] == "PASS"
            and review["findings"] == []
            and type(review["reviewed_by"]) is str
            and review["reviewed_by"].strip()
            and review["index_payload_sha256"] == digest(canonical(payload))
            and review["gate_refs"] == gates,
            "independent all-gates release review stale/rejected",
        )

    def run(self, document):
        self.index = document
        if (
            self.check("index", lambda: exact(document, INDEX_FIELDS, "release index"))
            is not None
        ):
            self.check(
                "index.status",
                lambda: require(
                    type(document["schema_version"]) is int
                    and document["schema_version"] == 1
                    and document["status"] == "final",
                    "draft/unknown release index",
                ),
            )
            for label, callback in (
                ("acceptance", self.prerequisites),
                ("source_inventory", self.inventory),
                ("source_audit", self.source_audit),
                ("normalization", self.normalization),
            ):
                self.check(label, callback)
            if not self.errors:
                if type(document["items"]) is not list or not document["items"]:
                    self.errors.append(
                        {"location": "items", "message": "actual items missing"}
                    )
                else:
                    for number, item in enumerate(document["items"]):
                        self.check(
                            f"items[{number}]", lambda item=item: self.item(item)
                        )
                    if not self.errors:
                        self.check("final_gates", self.final_gates)
        return {
            "schema_version": 1,
            "valid": not self.errors,
            "release_evidence_verified": not self.errors,
            "completion_flags_mutated": False,
            "errors": sorted(
                self.errors, key=lambda row: (row["location"], row["message"])
            ),
            "checked_artifact_count": len(self.checked),
            "verified_item_count": len(self.items),
        }


def validate_document(document, root):
    return Validator(root).run(document)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument(
        "--evidence", type=Path, default=Path("docs/conversion-release-evidence.json")
    )
    args = parser.parse_args()
    try:
        path = (
            args.evidence if args.evidence.is_absolute() else args.root / args.evidence
        )
        result = validate_document(json_bytes(path.read_bytes()), args.root)
    except (OSError, Invalid, ValueError) as error:
        result = {
            "schema_version": 1,
            "valid": False,
            "release_evidence_verified": False,
            "completion_flags_mutated": False,
            "errors": [{"location": "index", "message": str(error)}],
            "checked_artifact_count": 0,
            "verified_item_count": 0,
        }
    print(json.dumps(result, sort_keys=True, allow_nan=False))
    return 0 if result["valid"] else 1


if __name__ == "__main__":
    sys.exit(main())
