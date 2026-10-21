"""Fault injection for tooling only; all passing receipts here are synthetic."""

import copy
import hashlib
import io
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

from tools import check_conversion_release as release


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        workspace = release.check_conversion_acceptance.ROOT.resolve()
        stage = (workspace / ".artifacts").resolve()
        self.assertTrue(stage.is_relative_to(workspace))
        stage.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="synthetic-release-", dir=stage)
        parent = Path(self.temp.name).resolve()
        self.assertTrue(parent.is_relative_to(stage.resolve()))
        self.addCleanup(self.temp.cleanup)
        self.root = parent / "checkout"
        self.root.mkdir()
        self.environment = parent / "outside-environment"
        self.environment.mkdir()
        self.count = 0
        self.validator = release.Validator(self.root)
        self.log = self.put(b"synthetic tooling output\n", ".log")

    def put(self, data, suffix=".json"):
        self.count += 1
        path = self.root / f"receipt-{self.count}{suffix}"
        path.write_bytes(data)
        return {
            "path": path.relative_to(self.root).as_posix(),
            "sha256": hashlib.sha256(data).hexdigest(),
            "bytes": len(data),
        }

    def document(self, value):
        return self.put(release.canonical(value))

    def command(self, reports, argv=None, cwd=None):
        return {
            "argv": argv or ["synthetic", "run"],
            "cwd": str(cwd or self.root),
            "environment": {"PYTHONPATH": None},
            "exit_code": 0,
            "stdout": self.log,
            "stderr": self.log,
            "reports": reports,
        }

    def rejected(self, callback, text):
        with self.assertRaisesRegex(release.Invalid, text):
            callback()

    def case_fixture(self):
        case = "original.fixture::test_real_case"
        xml = self.put(
            b'<testsuite tests="1" failures="0" errors="0" skipped="0">'
            b'<testcase classname="original.fixture" name="test_real_case"/>'
            b"</testsuite>",
            ".xml",
        )
        collection = self.document(
            {
                "schema_version": 1,
                "fixture_source_inventory_sha256": "a" * 64,
                "collected_case_ids": [case],
                "deselected_case_ids_and_reasons": {},
                "excluded_roots": [],
            }
        )
        observations = self.document({"synthetic": "actual observation bytes"})
        item = {
            "collected_case_ids": [case],
            "passed_failed_skipped_xfailed_deselected_case_ids_and_reasons": {
                name: {case: ""} if name == "passed" else {}
                for name in release.OUTCOMES
            },
            "report_sha256s": {
                "junit": xml,
                "collection": collection,
                "observations": observations,
            },
        }
        requirement = {"case_ids": [case], "allowed_baseline_outcomes": {}}
        self.validator.acceptance = {
            "full_fixture_contract": {"fixture_source_inventory_sha256": "a" * 64},
        }
        return item, requirement

    def test_actual_junit_and_collection_positive(self):
        item, requirement = self.case_fixture()
        self.assertEqual(
            self.validator.outcomes(item, requirement),
            {"synthetic": "actual observation bytes"},
        )

    def test_pass_claim_cannot_override_actual_failure(self):
        item, requirement = self.case_fixture()
        data = (
            self.validator.file(item["report_sha256s"]["junit"])
            .replace(b'failures="0"', b'failures="1"')
            .replace(b"/>", b'><failure message="bad"/></testcase>')
        )
        item["report_sha256s"]["junit"] = self.put(data, ".xml")
        self.rejected(
            lambda: self.validator.outcomes(item, requirement), "actual JUnit"
        )

    def test_actual_error_and_malformed_suite_counter_rejected(self):
        for data in (
            b'<testsuite tests="2"><testcase classname="a" name="b"/></testsuite>',
            b'<testsuite><testcase classname="a" name="b"><error/></testcase>'
            b'<testcase classname="a" name="b"/></testsuite>',
            b'<!DOCTYPE x><testsuite><testcase classname="a" name="b"/></testsuite>',
        ):
            with self.subTest(data=data), self.assertRaises(release.Invalid):
                release.junit(data)

    def test_missing_unknown_and_duplicate_case_ids(self):
        item, requirement = self.case_fixture()
        original = copy.deepcopy(item)
        for cases in ([], ["unknown::test"], item["collected_case_ids"] * 2):
            item = copy.deepcopy(original)
            item["collected_case_ids"] = cases
            self.rejected(
                lambda item=item: self.validator.outcomes(item, requirement),
                "empty|mismatch|duplicate",
            )

    def test_new_skip_or_deselection_is_rejected(self):
        item, requirement = self.case_fixture()
        case = item["collected_case_ids"][0]
        for outcome in ("skipped", "xfailed", "deselected"):
            current = copy.deepcopy(item)
            partition = current[
                "passed_failed_skipped_xfailed_deselected_case_ids_and_reasons"
            ]
            partition["passed"].clear()
            partition[outcome][case] = "migration exclusion"
            self.rejected(
                lambda current=current: self.validator.outcomes(current, requirement),
                "baseline exclusion",
            )

    def test_root_exclusion_cannot_hide_missing_fixtures(self):
        item, requirement = self.case_fixture()
        reference = item["report_sha256s"]["collection"]
        record = self.validator.document(reference)
        record["excluded_roots"] = ["napari/_tests"]
        item["report_sha256s"]["collection"] = self.document(record)
        self.rejected(
            lambda: self.validator.outcomes(item, requirement), "migration exclusions"
        )

    def test_duplicate_json_unknown_fields_bool_and_nonfinite(self):
        for raw in (b'{"id":1,"id":2}', b'{"id":NaN}', b'{"id":Infinity}'):
            with self.subTest(raw=raw), self.assertRaises(release.Invalid):
                release.json_bytes(raw)
        self.assertFalse(release.version(True))
        with self.assertRaises(release.Invalid):
            release.integer(True, "counter")
        with self.assertRaises(release.Invalid):
            release.exact({"x": 1, "extra": 2}, {"x"}, "record")

    def test_hash_path_bytes_and_symlink_escape(self):
        references = [
            {**self.log, "sha256": "0" * 64},
            {**self.log, "bytes": True},
            {**self.log, "path": "../outside-environment/file"},
            {**self.log, "path": "a\\b"},
            {**self.log, "path": "C:/foreign/file"},
            {**self.log, "path": "./file"},
        ]
        for reference in references:
            with self.subTest(reference=reference), self.assertRaises(release.Invalid):
                self.validator.file(reference)

    def test_outside_environment_paths_cannot_be_relative_or_traversal(self):
        for path in (
            "relative/environment",
            "",
            str(self.environment / ".." / "elsewhere"),
        ):
            with self.subTest(path=path), self.assertRaises(release.Invalid):
                release.absolute_path(path, "environment")
        self.assertEqual(
            release.absolute_path(str(self.environment), "environment"),
            self.environment.resolve(),
        )

    def test_zero_exit_commands_and_retained_reports_required(self):
        command = self.command([self.log], ["python", "run.py", "a", "a"])
        self.validator.command(command)
        for key, value in (("exit_code", 1), ("exit_code", False), ("reports", [])):
            current = copy.deepcopy(command)
            current[key] = value
            self.rejected(
                lambda current=current: self.validator.command(current),
                "nonzero|artifact list",
            )

    def distribution_fixture(self):
        source = self.put(b"synthetic source declaration", ".py")
        inventory = self.document({"schema_version": 1, "files": [source]})
        self.validator.index = {"source_inventory": inventory}
        stream = io.BytesIO()
        with tarfile.open(fileobj=stream, mode="w") as archive:
            info = tarfile.TarInfo("package/source.py")
            data = self.validator.file(source)
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))
        sdist = self.put(stream.getvalue(), ".tar")
        files = {
            "napari/__init__.py": b"# synthetic declaration\n",
            "naparixx/__init__.py": b"# synthetic declaration\n",
            "naparixx/_native.pyd": b"MZsynthetic binary test bytes",
        }
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, "w") as archive:
            for name, data in files.items():
                archive.writestr(name, data)
        wheels = [self.put(stream.getvalue(), ".whl") for _ in range(2)]
        site = self.environment / "site-packages"
        origins = {}
        for name, data in files.items():
            path = site / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            module = name.split("/")[0]
            if name.endswith("_native.pyd"):
                module = "naparixx._native"
            origins[module] = str(path.resolve())
        executable = self.environment / "python.exe"
        executable.write_bytes(b"synthetic executable identity")
        report = {
            "sdist": sdist,
            "wheels": wheels,
            "wheel_build_commands": [
                self.command([wheel], ["synthetic-wheel-builder", sdist["path"]])
                for wheel in wheels
            ],
            "sdist_source_files": {source["path"]: "package/source.py"},
            "wheel_members": {
                name: release.digest(data) for name, data in files.items()
            },
            "environment_root": str(self.environment.resolve()),
            "site_packages": str(site.resolve()),
            "executable": str(executable.resolve()),
            "cwd": str(self.environment.resolve()),
            "sys_path": [str(site.resolve())],
            "install_command": self.command(
                [wheels[0]],
                [str(executable.resolve()), "-m", "pip", "install", wheels[0]["path"]],
                self.environment,
            ),
            "probe_command": self.command(
                [self.log],
                [str(executable.resolve()), "-I", "synthetic_probe.py"],
                self.environment,
            ),
            "module_origins": origins,
            "installed_binaries": [
                {
                    "module": "naparixx._native",
                    "installed_path": origins["naparixx._native"],
                    "wheel_member": "naparixx/_native.pyd",
                    "sha256": release.digest(files["naparixx/_native.pyd"]),
                }
            ],
            "native_qt_probe_case_ids": [],
        }
        item = {
            "sdist_sha256": sdist["sha256"],
            "wheel_sha256": wheels[0]["sha256"],
            "installed_binary_sha256s": {
                "naparixx._native": release.digest(files["naparixx/_native.pyd"]),
            },
            "matrix_cell_id": "cell",
            "collected_case_ids": ["synthetic::install"],
        }
        self.validator.cells = {"cell": {"binding": None}}
        return report, item

    def test_retained_archives_fresh_origins_and_installed_bytes_positive(self):
        report, item = self.distribution_fixture()
        self.validator.distribution(report, item)

    def test_claimed_fresh_install_rejects_checkout_origin_and_payload_mismatch(self):
        report, item = self.distribution_fixture()
        for change, expected in (
            (
                lambda row: row.update(environment_root=str(self.root)),
                "outside checkout",
            ),
            (lambda row: row.update(cwd=str(self.root)), "cwd inside checkout"),
            (
                lambda row: row.update(sys_path=[str(self.root / "src")]),
                "checkout import",
            ),
            (
                lambda row: row["module_origins"].update(
                    napari=str(self.root / "src/napari/__init__.py")
                ),
                "fallback/origin",
            ),
        ):
            current = copy.deepcopy(report)
            change(current)
            self.rejected(
                lambda current=current: self.validator.distribution(current, item),
                expected,
            )
        path = Path(report["module_origins"]["naparixx._native"])
        path.write_bytes(b"MZdifferent installed bytes")
        self.rejected(
            lambda: self.validator.distribution(report, item), "installed wheel bytes"
        )

    def test_two_reproducible_wheels_and_complete_sdist_source_required(self):
        report, item = self.distribution_fixture()
        for change, expected in (
            (lambda row: row.update(wheels=row["wheels"][:1]), "two reproducible"),
            (lambda row: row["wheels"][1].update(sha256="0" * 64), "not reproducible"),
            (lambda row: row.update(sdist_source_files={}), "sdist source coverage"),
        ):
            current = copy.deepcopy(report)
            change(current)
            self.rejected(
                lambda current=current: self.validator.distribution(current, item),
                expected,
            )

    def test_gui_cell_requires_actual_native_qt_payload(self):
        report, item = self.distribution_fixture()
        self.validator.cells["cell"]["binding"] = "PyQt6"
        self.rejected(
            lambda: self.validator.distribution(report, item),
            "native Qt bridge missing",
        )

    def audit_fixture(self):
        path = self.root / "src/napari/metadata.py"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"VALUE = 1\n")
        modules = release.audit_conversion.collect(self.root / "src", {})
        case = self.put(
            b'<testsuite tests="1">'
            b'<testcase classname="tool" name="check"/></testsuite>',
            ".xml",
        )
        record = {
            "classification": "facade",
            "source_sha256": modules[0]["source_sha256"],
            "reason": "Synthetic declaration metadata fixture",
            "reviewed_by": "synthetic tooling test",
            "reviewed_at": "2026-10-14T00:00:00+00:00",
            "coverage": dict.fromkeys(
                release.ReviewValidator.__init__.__globals__["MODULE_COVERAGE"],
                "synthetic explicitly reviewed source",
            ),
            "implementation": [
                {
                    "path": "src/napari/metadata.py",
                    "sha256": modules[0]["source_sha256"],
                }
            ],
            "calls": [],
            "evidence": [
                {
                    "path": case["path"],
                    "sha256": case["sha256"],
                    "format": "junit",
                    "cases": ["tool::check"],
                }
            ],
        }
        reviews = self.document(
            {
                "schema_version": 1,
                "upstream_commit": release.COMMIT,
                "records": {"napari/metadata.py:module": record},
                "retired_modules": {},
            }
        )
        dependencies = self.document(
            {
                "schema_version": 1,
                "upstream_commit": release.COMMIT,
                "operations": {},
            }
        )
        result = release.ReviewValidator(self.root, modules, release.COMMIT).run(
            self.root / reviews["path"], self.root / dependencies["path"]
        )
        self.assertTrue(result["complete"], result)
        report = self.document(
            {
                "review_validation": result,
                "modules": modules,
                "upstream": {"commit": release.COMMIT},
            }
        )
        command = self.command(
            [report],
            [
                "python",
                "tools/audit_conversion.py",
                "--check-complete",
                "--reviews",
                reviews["path"],
                "--dependencies",
                dependencies["path"],
                "--output",
                report["path"],
            ],
        )
        source_inventory = self.document({"schema_version": 1, "files": [self.log]})
        audit = self.document(
            {
                "schema_version": 1,
                "command": command,
                "report": report,
                "reviews": reviews,
                "dependencies": dependencies,
                "source_inventory_sha256": source_inventory["sha256"],
            }
        )
        self.validator.index = {
            "source_audit": audit,
            "reviews": reviews,
            "dependencies": dependencies,
            "source_inventory": source_inventory,
        }
        return reviews, dependencies, audit

    def test_real_discovery_and_review_validator_positive(self):
        self.audit_fixture()
        self.validator.source_audit()
        self.assertEqual(self.validator.responsibilities, {"napari/metadata.py:module"})

    def test_source_incomplete_or_stale_recorded_audit_fails(self):
        reviews, _, audit = self.audit_fixture()
        record = self.validator.document(reviews)
        record["records"]["napari/metadata.py:module"]["classification"] = "partial"
        current = self.document(record)
        receipt = self.validator.document(audit)
        receipt["reviews"] = current
        self.validator.index["reviews"] = current
        receipt["command"]["argv"][4] = current["path"]
        self.validator.index["source_audit"] = self.document(receipt)
        self.rejected(self.validator.source_audit, "current source reviews incomplete")

    def test_audit_command_must_be_actual_check_complete_and_bound_output(self):
        _, _, audit = self.audit_fixture()
        receipt = self.validator.document(audit)
        receipt["command"]["argv"].remove("--check-complete")
        self.validator.index["source_audit"] = self.document(receipt)
        self.rejected(self.validator.source_audit, "actual --check-complete")

    def workload_fixture(self):
        protocol = {"warmup": 2, "samples": 3, "synthetic": "fixed hardware"}
        policy = {
            "aggregation": "median",
            "direction": "lower",
            "absolute_tolerance": 0.5,
            "relative_tolerance": 0.1,
            "minimum_samples": 3,
        }
        requirement = {
            "input_artifacts": [self.log],
            "metrics": {"time": policy},
        }
        report = {
            "input_artifacts": [self.log],
            "protocol": protocol,
            "reference_commit": release.COMMIT,
            "metrics": {
                "time": {
                    "baseline": [10, 10, 11],
                    "candidate": [10, 11, 11],
                    "policy": policy,
                }
            },
        }
        self.validator.acceptance = {
            "workload_contract": {"frozen_measurement_protocol": protocol},
        }
        return report, requirement

    def test_actual_workload_numbers_and_predeclared_tolerance(self):
        report, requirement = self.workload_fixture()
        self.validator.workload(report, requirement, {})
        report["metrics"]["time"]["candidate"] = [20, 20, 20]
        self.rejected(
            lambda: self.validator.workload(report, requirement, {}),
            "tolerance failed",
        )

    def test_protocol_insufficient_samples_nonfinite_and_changed_metric_policy(self):
        report, requirement = self.workload_fixture()
        for change, expected in (
            (lambda row: row.update(protocol={"different": "hardware"}), "protocol"),
            (lambda row: row["metrics"]["time"].update(candidate=[1]), "samples"),
            (
                lambda row: row["metrics"]["time"].update(candidate=[float("nan")] * 3),
                "non-finite",
            ),
            (lambda row: row["metrics"]["time"].update(policy={}), "changed"),
        ):
            current = copy.deepcopy(report)
            change(current)
            self.rejected(
                lambda current=current: self.validator.workload(
                    current, requirement, {}
                ),
                expected,
            )

    def test_direction_does_not_assume_universal_speedup(self):
        report, requirement = self.workload_fixture()
        requirement["metrics"]["time"]["direction"] = "equal"
        report["metrics"]["time"]["policy"]["direction"] = "equal"
        report["metrics"]["time"]["candidate"] = [8, 8, 8]
        self.rejected(
            lambda: self.validator.workload(report, requirement, {}), "tolerance"
        )

    def test_diagnostics_are_per_process_and_observation_labels_are_not_enough(self):
        requirement = {
            "kind": "diagnostic",
            "case_ids": ["x::test"],
            "oracles": {"remaining_owned_threads": 0},
        }
        item = {
            key: "synthetic"
            for key in (
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
        report = {
            "schema_version": 1,
            "acceptance_item_id": "diagnostic:thread-gil",
            "matrix_cell_id": "cell",
            "bindings": copy.deepcopy(item),
            "checks": {
                "remaining_owned_threads": {
                    "case_ids": ["x::test"],
                    "expected": 0,
                    "observed": 0,
                }
            },
            "distribution": None,
            "workload": None,
            "diagnostics": {
                "oracle": requirement["oracles"],
                "processes": [
                    {
                        "command": self.command([self.log]),
                        "observed": requirement["oracles"],
                    }
                ],
            },
        }
        item.update(acceptance_item_id="diagnostic:thread-gil", matrix_cell_id="cell")
        self.validator.observations(report, requirement, item)
        report["diagnostics"]["processes"][0]["observed"] = {
            "remaining_owned_threads": 1,
        }
        self.rejected(
            lambda: self.validator.observations(report, requirement, item),
            "per-process diagnostic failed",
        )

    def test_real_current_acceptance_draft_fails_frozen_gate(self):
        root = release.check_conversion_acceptance.ROOT
        data = (root / "docs/conversion-acceptance.json").read_bytes()
        validator = release.Validator(root)
        validator.index = {
            "acceptance": {
                "path": "docs/conversion-acceptance.json",
                "sha256": release.digest(data),
                "bytes": len(data),
            }
        }
        self.rejected(validator.prerequisites, "frozen prerequisite validation")

    def test_schema_failure_is_deterministic_and_does_not_mutate_document(self):
        document = {"schema_version": True, "status": "draft", "unknown": 1}
        saved = copy.deepcopy(document)
        one = release.validate_document(document, self.root)
        two = release.validate_document(document, self.root)
        self.assertEqual(one, two)
        self.assertEqual(document, saved)
        self.assertFalse(one["valid"])
        self.assertFalse(one["completion_flags_mutated"])

    def test_missing_and_duplicate_required_items_or_milestones_cannot_close_gate(self):
        self.validator.requirements = {("required", "cell"): {}}
        self.validator.items = {}
        self.rejected(self.validator.final_gates, "items incomplete")
        self.validator.items = {("required", "cell"): {}}
        self.validator.cells = {}
        self.validator.index = {"milestones": {"M0": {"claimed": "passed"}}}
        self.rejected(self.validator.final_gates, "M0-M9 receipt coverage")

    def test_same_source_and_artifacts_per_cell_are_required(self):
        distribution = {
            key: "same"
            for key in (
                "sdist_sha256",
                "wheel_sha256",
                "installed_binary_sha256s",
                "dependency_lock_sha256",
                "python_implementation_version_abi",
                "qt_binding_version_runtime_version_sdk_version",
                "os_arch_compiler_gpu_driver_display",
            )
        }
        different = {**distribution, "wheel_sha256": "different"}
        self.validator.items = {
            ("distribution", "cell"): distribution,
            ("workflow", "cell"): different,
        }
        self.validator.requirements = dict.fromkeys(self.validator.items)
        self.validator.cells = {"cell": {}}
        self.rejected(self.validator.final_gates, "artifact/environment drift")

    def composite_fixture(self):
        self.audit_fixture()
        self.validator.source_audit()
        source_audit = self.validator.index["source_audit"]
        plan = self.root / "docs/full-conversion-plan.md"
        plan.parent.mkdir(exist_ok=True)
        lines = [f"| M{n} | synthetic title | synthetic exit {n} |" for n in range(10)]
        plan.write_bytes(("\n".join(lines) + "\n").encode())
        plan_ref = {
            "path": "docs/full-conversion-plan.md",
            "sha256": release.digest(plan.read_bytes()),
            "bytes": plan.stat().st_size,
        }
        values = dict.fromkeys(
            (
                "sdist_sha256",
                "wheel_sha256",
                "installed_binary_sha256s",
                "dependency_lock_sha256",
                "python_implementation_version_abi",
                "qt_binding_version_runtime_version_sdk_version",
                "os_arch_compiler_gpu_driver_display",
            ),
            "same synthetic bindings",
        )
        self.validator.items = {
            ("distribution", "cell"): values,
            ("workflow", "cell"): values,
        }
        self.validator.requirements = dict.fromkeys(self.validator.items)
        self.validator.cells = {"cell": {}}
        keys = [list(key) for key in self.validator.items]
        index = {
            "source_audit": source_audit,
            "acceptance": {"sha256": "a" * 64},
            "source_inventory": {"sha256": "b" * 64},
            "milestones": {},
            "gates": {},
        }
        for n, text in enumerate(lines):
            contract = release.digest(text.encode())
            review = self.document(
                {
                    "verdict": "PASS",
                    "findings": [],
                    "exit_contract_sha256": contract,
                    "item_keys": keys,
                }
            )
            index["milestones"][f"M{n}"] = self.document(
                {
                    "schema_version": 1,
                    "milestone": f"M{n}",
                    "exit_contract": {
                        "plan": plan_ref,
                        "exact_text": text,
                        "sha256": contract,
                    },
                    "responsibilities": ["napari/metadata.py:module"] if n == 8 else [],
                    "item_keys": keys,
                    "review": review,
                }
            )
        for gate in release.GATES:
            index["gates"][gate] = self.document(
                {
                    "schema_version": 1,
                    "gate_id": gate,
                    "acceptance_record_sha256": "a" * 64,
                    "source_inventory_sha256": "b" * 64,
                    "item_keys": keys,
                    "milestone_receipts": index["milestones"],
                    "source_audit": source_audit,
                }
            )
        index["independent_review"] = self.document(
            {
                "schema_version": 1,
                "verdict": "PASS",
                "findings": [],
                "reviewed_by": "synthetic test reviewer",
                "index_payload_sha256": release.digest(release.canonical(index)),
                "gate_refs": index["gates"],
            }
        )
        self.validator.index = index
        return index

    def test_composite_same_artifacts_all_milestones_six_gates_and_review_positive(
        self,
    ):
        self.composite_fixture()
        self.validator.final_gates()

    def test_final_review_binds_current_entire_payload_not_pass_label(self):
        index = self.composite_fixture()
        index["source_inventory"]["sha256"] = "c" * 64
        self.rejected(self.validator.final_gates, "gate bindings mismatch")
        index["source_inventory"]["sha256"] = "b" * 64
        index["extra_mutation"] = "different current payload"
        self.rejected(self.validator.final_gates, "release review stale")

    def test_all_gate_and_milestone_current_responsibility_coverage_required(self):
        index = self.composite_fixture()
        index["gates"].pop("fresh-native-distributions")
        self.rejected(self.validator.final_gates, "gate receipt coverage")
        index = self.composite_fixture()
        self.validator.responsibilities.add("actual_unassigned:module")
        self.rejected(self.validator.final_gates, "omit current responsibilities")

    def test_acceptance_checker_is_called_in_frozen_mode_without_flag_updates(self):
        acceptance = {
            "complete_application_port": False,
            "milestone_status": dict.fromkeys(release.MILESTONES, "open"),
            "release_receipt_contract": {
                "required_per_item_fields": list(release.ITEM_FIELDS)
            },
            "final_evidence_gates": [{"id": gate} for gate in release.GATES],
        }
        self.validator.index = {"acceptance": self.document(acceptance)}
        with patch.object(
            release.check_conversion_acceptance,
            "validate_document",
            return_value={"valid": True, "frozen_verified": True, "exit_code": 0},
        ) as prerequisite:
            self.validator.prerequisites()
        prerequisite.assert_called_once_with(acceptance, self.root, check_frozen=True)
        self.assertEqual(self.validator.acceptance, acceptance)

    def workflow_item_fixture(self):
        tester = self
        item, requirement = tester.case_fixture()
        case = item["collected_case_ids"][0]
        lock = tester.put(b"synthetic locked input\n", ".lock")
        acceptance = tester.document({"synthetic": "acceptance reference"})
        inventory = tester.document({"synthetic": "inventory reference"})
        tester.validator.index = {
            "acceptance": acceptance,
            "source_inventory": inventory,
        }
        cell = {
            "dependency_artifact_lock": lock,
            "interpreter": "CPython",
            "python_patch": "3.12.8",
            "python_abi": "cp312",
            "binding": None,
            "os_version": "synthetic OS",
            "architecture": "synthetic architecture",
            "compiler_toolchain_lock": lock,
            "host_fingerprint": lock,
        }
        tester.validator.cells = {"synthetic-cell": cell}
        requirement.update(
            kind="workflow",
            policy_sha256="c" * 64,
            input_artifacts=[lock],
            oracles={"count": {"value": 1}},
            metrics={},
        )
        tester.validator.requirements = {
            ("workflow:synthetic", "synthetic-cell"): requirement
        }
        item.update(
            acceptance_record_sha256=acceptance["sha256"],
            acceptance_item_id="workflow:synthetic",
            matrix_cell_id="synthetic-cell",
            source_inventory_sha256=inventory["sha256"],
            baseline_commit=release.COMMIT,
            dependency_lock_sha256=lock["sha256"],
            sdist_sha256="d" * 64,
            wheel_sha256="e" * 64,
            installed_binary_sha256s={"naparixx._native": "f" * 64},
            python_implementation_version_abi={
                "implementation": "CPython",
                "version": "3.12.8",
                "abi": "cp312",
            },
            qt_binding_version_runtime_version_sdk_version=None,
            os_arch_compiler_gpu_driver_display={
                "os": cell["os_version"],
                "architecture": cell["architecture"],
                "compiler_lock_sha256": lock["sha256"],
                "host_lock_sha256": lock["sha256"],
                "gpu_driver_display_lock_sha256": None,
            },
            measured_workload_inputs_protocol_results=None,
        )
        binding_fields = (
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
        report = {
            "schema_version": 1,
            "acceptance_item_id": item["acceptance_item_id"],
            "matrix_cell_id": item["matrix_cell_id"],
            "bindings": {name: item[name] for name in binding_fields},
            "checks": {
                "count": {
                    "case_ids": [case],
                    "expected": {"value": 1},
                    "observed": {"value": 1},
                }
            },
            "distribution": None,
            "diagnostics": None,
            "workload": None,
        }
        item["report_sha256s"]["observations"] = tester.document(report)
        command = tester.command(list(item["report_sha256s"].values()))
        item.update(
            commands_cwd_env=[command],
            exit_codes=[0],
            stdout_stderr_sha256s=[
                {"stdout": tester.log["sha256"], "stderr": tester.log["sha256"]}
            ],
        )
        baseline = {
            "schema_version": 1,
            "baseline_commit": release.COMMIT,
            "policy_sha256": requirement["policy_sha256"],
            "input_artifacts": [lock],
            "junit": item["report_sha256s"]["junit"],
            "collection": item["report_sha256s"]["collection"],
            "observations": tester.document(
                {
                    "policy_sha256": requirement["policy_sha256"],
                    "checks": requirement["oracles"],
                }
            ),
        }
        baseline["commands"] = [
            tester.command(
                [baseline[k] for k in ("junit", "collection", "observations")]
            )
        ]
        item["original_reference_evidence"] = tester.document(baseline)
        return item, requirement, report, baseline

    def test_original_reports_must_be_bound_to_original_commands(self):
        item, _, _, baseline = self.workflow_item_fixture()
        self.validator.item(item)
        for omitted in ("junit", "collection", "observations"):
            self.validator.items.clear()
            changed = copy.deepcopy(baseline)
            changed["commands"][0]["reports"] = [
                baseline[field]
                for field in ("junit", "collection", "observations")
                if field != omitted
            ]
            item["original_reference_evidence"] = self.document(changed)
            self.rejected(
                lambda: self.validator.item(item),
                "original reports not bound to actual commands",
            )

    def test_oracles_cover_every_required_case(self):
        item, requirement, report, _ = self.workflow_item_fixture()
        requirement["case_ids"].append("original.fixture::test_second_case")
        self.rejected(
            lambda: self.validator.observations(report, requirement, item),
            "required case coverage incomplete",
        )
        report["checks"]["count"]["case_ids"] = list(requirement["case_ids"])
        self.validator.observations(report, requirement, item)

    def test_boolean_and_float_do_not_replace_frozen_integer_oracle(self):
        item, requirement, report, _ = self.workflow_item_fixture()
        for field in ("expected", "observed"):
            for value in (True, 1.0):
                changed = copy.deepcopy(report)
                changed["checks"]["count"][field] = {"value": value}
                self.rejected(
                    lambda changed=changed: self.validator.observations(
                        changed, requirement, item
                    ),
                    "observation oracle failed",
                )
        self.validator.observations(report, requirement, item)

    def test_duplicate_sdist_member_names_are_rejected(self):
        report, item = self.distribution_fixture()
        self.validator.distribution(report, item)
        raw = self.validator.file(report["sdist"])
        with tarfile.open(fileobj=io.BytesIO(raw), mode="r:*") as archive:
            members = [
                (member, archive.extractfile(member).read())
                for member in archive.getmembers()
                if member.isfile()
            ]
        stream = io.BytesIO()
        with tarfile.open(fileobj=stream, mode="w") as archive:
            for member, data in members + members:
                archive.addfile(member, io.BytesIO(data))
        report["sdist"] = self.put(stream.getvalue(), ".tar")
        item["sdist_sha256"] = report["sdist"]["sha256"]
        for command in report["wheel_build_commands"]:
            command["argv"][-1] = report["sdist"]["path"]
        self.rejected(
            lambda: self.validator.distribution(report, item), "duplicate sdist member"
        )

    def test_each_retained_wheel_is_bound_to_its_build_command(self):
        report, item = self.distribution_fixture()
        self.validator.distribution(report, item)
        for position in range(2):
            changed = copy.deepcopy(report)
            changed["wheel_build_commands"][position]["reports"] = [self.log]
            self.rejected(
                lambda changed=changed: self.validator.distribution(changed, item),
                "wheel output not bound to build command",
            )

    def test_baseline_boolean_cannot_replace_frozen_integer_oracle(self):
        item, requirement, _, baseline = self.workflow_item_fixture()
        changed = copy.deepcopy(baseline)
        changed["observations"] = self.document(
            {
                "policy_sha256": requirement["policy_sha256"],
                "checks": {"count": {"value": True}},
            }
        )
        changed["commands"] = [
            self.command([changed[k] for k in ("junit", "collection", "observations")])
        ]
        item["original_reference_evidence"] = self.document(changed)
        self.rejected(
            lambda: self.validator.item(item), "original reference oracle mismatch"
        )

    def test_sdist_input_identity_resolves_against_each_actual_command_cwd(self):
        report, item = self.distribution_fixture()
        self.validator.distribution(report, item)
        unrelated = self.root / "unrelated"
        unrelated.mkdir()
        basename = Path(report["sdist"]["path"]).name
        (unrelated / basename).write_bytes(b"actual distinct same-basename archive")
        wrong = copy.deepcopy(report)
        wrong["wheel_build_commands"][0]["cwd"] = str(unrelated)
        wrong["wheel_build_commands"][0]["argv"] = [
            "synthetic-wheel-builder",
            basename,
        ]
        self.rejected(
            lambda: self.validator.distribution(wrong, item),
            "wheel not built from retained sdist",
        )
        correct = copy.deepcopy(report)
        correct["wheel_build_commands"][0]["cwd"] = str(unrelated)
        correct["wheel_build_commands"][0]["argv"] = [
            "synthetic-wheel-builder",
            "../" + report["sdist"]["path"],
        ]
        self.validator.distribution(correct, item)
        correct["wheel_build_commands"][0]["argv"][-1] = str(
            (self.root / report["sdist"]["path"]).resolve()
        )
        self.validator.distribution(correct, item)

    def test_each_process_diagnostic_observation_keeps_frozen_json_types(self):
        item, requirement, report, _ = self.workflow_item_fixture()
        requirement["kind"] = "diagnostic"
        requirement["oracles"] = {"remaining_owned_threads": 0}
        report["checks"] = {
            "remaining_owned_threads": {
                "case_ids": list(requirement["case_ids"]),
                "expected": 0,
                "observed": 0,
            },
        }
        report["diagnostics"] = {
            "oracle": requirement["oracles"],
            "processes": [
                {
                    "command": self.command([self.log]),
                    "observed": requirement["oracles"],
                }
            ],
        }
        self.validator.observations(report, requirement, item)
        for substitute in (False, 0.0):
            changed = copy.deepcopy(report)
            changed["diagnostics"]["processes"][0]["observed"] = {
                "remaining_owned_threads": substitute,
            }
            self.rejected(
                lambda changed=changed: self.validator.observations(
                    changed, requirement, item
                ),
                "per-process diagnostic failed",
            )
        self.validator.observations(report, requirement, item)

    def test_each_oracle_requires_full_frozen_cases_despite_complete_swapped_union(
        self,
    ):
        item, requirement, report, _ = self.workflow_item_fixture()
        first, second = (
            requirement["case_ids"][0],
            "original.fixture::test_second_case",
        )
        requirement["case_ids"] = [first, second]
        requirement["oracles"] = {
            "first-output": {"value": 1},
            "second-output": {"value": 2},
        }
        report["checks"] = {
            name: {
                "case_ids": [first, second],
                "expected": oracle,
                "observed": oracle,
            }
            for name, oracle in requirement["oracles"].items()
        }
        self.validator.observations(report, requirement, item)
        for assignments in (([first], [second]), ([second], [first])):
            changed = copy.deepcopy(report)
            changed["checks"]["first-output"]["case_ids"] = assignments[0]
            changed["checks"]["second-output"]["case_ids"] = assignments[1]
            self.assertEqual(
                set(assignments[0]) | set(assignments[1]),
                set(requirement["case_ids"]),
            )
            self.rejected(
                lambda changed=changed: self.validator.observations(
                    changed, requirement, item
                ),
                "required case coverage incomplete for oracle",
            )
        self.validator.observations(report, requirement, item)


if __name__ == "__main__":
    unittest.main()
