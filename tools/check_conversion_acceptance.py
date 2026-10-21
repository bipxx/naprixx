"""Check M0 acceptance prerequisites without certifying runtime or release parity.

Usage: python tools/check_conversion_acceptance.py [--check-frozen]
       [--root WORKSPACE] [--acceptance docs/conversion-acceptance.json]

The read-only CLI always emits one deterministic JSON object, with sorted issue
lists and no timestamps. Exit 0 means the draft is structurally consistent and
its source fingerprints are current. --check-frozen additionally requires a
frozen record with no prerequisite blockers. Exit 1 reports a rejected record
or an unmet freeze gate. Neither mode verifies runtime reports, closes a
milestone, changes the record, or certifies final release evidence.

The configured-cell reference below is bound to the fixed napari 0.9.2 commit.
It preserves original CI scopes, rather than treating each configured row as a
full-suite pass. Applicability of additional platform combinations remains a
decision for the acceptance record, not a test requirement inferred by this tool.
"""

import argparse
import hashlib
import itertools
import json
import math
import re
import sys
import tomllib
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "595639295a153c84bcb127a9e07d801355440a35"
PYTHONS = {"3.11", "3.12", "3.13", "3.14"}
SYSTEMS = {"Windows", "Linux", "macOS"}
BINDINGS = {"PyQt5", "PyQt6", "PySide6"}
WORKFLOWS = {
    "api-state-contracts",
    "viewer-layer-state",
    "image-labels-roundtrip",
    "all-seven-layers",
    "services-io-settings-cli",
    "real-plugin-interop",
    "actual-qt-input-window",
    "rendering-and-gpu-cleanup",
    "lifetime-and-interpreter-exit",
}
CHOICES = {
    "python-support-boundary",
    "os-and-architecture-applicability",
    "qt-version-and-sdk-locks",
    "minimum-and-latest-dependencies",
    "full-fixture-coverage",
    "provider-protocol-profiles",
    "representative-real-plugins",
    "gpu-image-environment",
    "workflow-inputs-and-oracles",
    "performance-measurement-policy",
    "native-distribution-policy",
    "acceptance-validator-and-evidence-index",
}
GATES = {
    "m0-freeze",
    "m0-m9-exits",
    "source-audit",
    "full-upstream-providers-plugins-workflows",
    "fresh-native-distributions",
    "lifetime-gpu-and-workloads",
}
BENCHMARKS = {
    "benchmark_evented_model",
    "benchmark_image_layer",
    "benchmark_import",
    "benchmark_labels_layer",
    "benchmark_points_layer",
    "benchmark_python_layer",
    "benchmark_qt_slicing",
    "benchmark_qt_viewer",
    "benchmark_qt_viewer_image",
    "benchmark_qt_viewer_labels",
    "benchmark_qt_viewer_vectors",
    "benchmark_shapes_layer",
    "benchmark_surface_layer",
    "benchmark_text_manager",
    "benchmark_tracks_layer",
    "benchmark_vectors_layer",
}
CRITICAL_PACKAGES = set(
    "app-model numpy scipy pandas dask fsspec zarr xarray tensorstore torch numba "
    "pint psygnal pydantic pydantic-core pydantic-settings qtpy vispy pyopengl "
    "magicgui npe2 napari-plugin-engine pyqt5 pyqt5-qt5 pyqt5-sip pyqt6 pyqt6-qt6 "
    "pyqt6-sip pyside6 pyside6-addons pyside6-essentials shiboken6 pytest pytest-qt "
    "asv asv-runner pympler virtualenv".split()
)
SECTIONS = {
    "baseline",
    "evidence_semantics",
    "supported_contract_facts",
    "candidate_matrix",
    "fork_configured_matrix_snapshot",
    "provider_contract",
    "plugin_contract",
    "workload_contract",
    "full_fixture_contract",
    "release_receipt_contract",
    "draft_schema_contract",
    "milestone_status",
}
PLACEHOLDERS = {"unresolved", "unverified", "pending", "tbd", "unknown", "draft"}


def expected_cells():
    """Return the 32 configured recipes from the pinned workflow sources."""
    rows = {}

    def add(
        workflow,
        job,
        ordinal,
        py,
        runner,
        backend,
        *,
        minimum=False,
        extras="",
        scope="upstream pytest profile using original fixtures",
    ):
        profile = "constraints-py" + py + ("_min_req" if minimum else "")
        if job == "test_examples":
            profile += "_examples"
            scope = "src/napari/_tests/test_examples.py only"
        rows[f"{workflow}-{job}-{ordinal}-{backend}"] = {
            "source": f".upstream/napari/.github/workflows/{workflow}.yml",
            "job": job,
            "python_minor_requested": py,
            "runner_label": runner,
            "backend_factor": backend,
            "constraint_profile_id": profile,
            "minimum_requirements": minimum,
            "tox_extras": extras,
            "configured_test_scope": scope,
            "os_family": "Windows"
            if runner.startswith("windows")
            else ("macOS" if runner.startswith("macos") else "Linux"),
            "binding": {"pyqt5": "PyQt5", "pyqt6": "PyQt6", "pyside6": "PySide6"}.get(
                backend.removesuffix("_no_numba")
            ),
        }

    pr = "test_pull_requests"
    add(pr, "test_initial", 1, "3.11", "ubuntu-latest", "pyqt5")
    add(pr, "test_initial", 2, "3.14", "ubuntu-latest", "pyqt6")
    add(pr, "test", 1, "3.14", "ubuntu-latest", "pyqt5")
    add(pr, "test", 1, "3.14", "ubuntu-latest", "pyqt6", scope="qapp-fixture subset")
    includes = [
        ("3.11", "windows-latest", "pyqt5", False, ""),
        ("3.14", "windows-latest", "pyqt6", False, ""),
        ("3.14", "macos-15-intel", "pyqt5_no_numba", False, ""),
        ("3.14", "macos-15", "pyqt6", False, ""),
        ("3.11", "ubuntu-22.04", "pyqt5", True, ""),
        ("3.13", "ubuntu-22.04", "pyqt6", False, "all_optional"),
        ("3.12", "ubuntu-22.04", "headless", False, ""),
        ("3.11", "ubuntu-latest", "pyqt6", False, "testing_extra"),
        ("3.14", "ubuntu-latest", "pyqt6_no_numba", False, ""),
        ("3.14", "ubuntu-latest", "pyside6", False, ""),
        ("3.14", "ubuntu-24.04-arm", "pyqt6", False, ""),
    ]
    for ordinal, (py, runner, backend, minimum, extras) in enumerate(includes, 2):
        scope = (
            "headless with pinned GUI/rendering exclusions"
            if (backend == "headless")
            else "upstream pytest profile using original fixtures"
        )
        add(
            pr,
            "test",
            ordinal,
            py,
            runner,
            backend,
            minimum=minimum,
            extras=extras,
            scope=scope,
        )
    comprehensive = "test_comprehensive"
    for ordinal, (runner, py) in enumerate(
        itertools.product(["ubuntu-latest", "windows-latest"], sorted(PYTHONS)), 1
    ):
        add(comprehensive, "test", ordinal, py, runner, "pyqt5")
    includes = [
        ("3.11", "macos-15", "pyqt5", False, ""),
        ("3.11", "ubuntu-22.04", "pyqt5", True, ""),
        ("3.11", "ubuntu-22.04", "headless", False, ""),
        ("3.14", "ubuntu-latest", "pyqt6", False, "testing_extra"),
        ("3.12", "ubuntu-latest", "pyside6", False, "testing_extra"),
        ("3.14", "ubuntu-24.04-arm", "pyqt6", False, ""),
    ]
    for ordinal, (py, runner, backend, minimum, extras) in enumerate(includes, 9):
        scope = (
            "headless with pinned GUI/rendering exclusions"
            if (backend == "headless")
            else "upstream pytest profile using original fixtures"
        )
        add(
            comprehensive,
            "test",
            ordinal,
            py,
            runner,
            backend,
            minimum=minimum,
            extras=extras,
            scope=scope,
        )
    for workflow in [pr, comprehensive]:
        add(workflow, "test_examples", 1, "3.12", "ubuntu-latest", "pyqt6")
    add(
        "reusable_pip_test",
        "test_pip_install",
        1,
        "3.12",
        "ubuntu-latest",
        "pyqt6",
        extras="testing",
    )
    pip = rows["reusable_pip_test-test_pip_install-1-pyqt6"]
    pip["backend_factor"] = "pyqt extra alias; no tox backend factor"
    pip["configured_test_scope"] = (
        "Installed napari and napari_builtins --pyargs; numba uninstalled; "
        "sdist built before wheel"
    )
    return rows


class Validator:
    def __init__(self, document, root):
        self.root = Path(root).resolve()
        self.document = document
        self.errors = []
        self.blockers = []
        self.sources = {}
        self.files = {}

    def error(self, path, code, detail):
        self.errors.append({"path": path, "code": code, "detail": detail})

    def block(self, path, code, detail):
        self.blockers.append({"path": path, "code": code, "detail": detail})

    def obj(self, value, path):
        if not isinstance(value, dict):
            self.error(path, "schema", "Expected an object")
            return {}
        return value

    def rows(self, value, path, required=()):
        if not isinstance(value, list):
            self.error(path, "schema", "Expected an array")
            return {}
        result = {}
        for index, item in enumerate(value):
            location = f"{path}[{index}]"
            item = self.obj(item, location)
            identity = item.get("id")
            if not isinstance(identity, str) or not identity.strip():
                self.error(location, "schema", "Expected a nonempty id")
            elif identity in result:
                self.error(location, "duplicate_id", identity)
            else:
                result[identity] = item
        for identity in sorted(set(required) - result.keys()):
            self.error(path, "missing_accounting", identity)
        return result

    def need(self, value, path):
        if (
            value is None
            or value == ""
            or value == []
            or value == {}
            or (isinstance(value, str) and value.strip().lower() in PLACEHOLDERS)
        ):
            self.block(path, "unresolved_prerequisite", "Explicit resolution required")
            return False
        if isinstance(value, dict) and value.get("applicable") is False:
            if not isinstance(value.get("reason"), str) or not value["reason"].strip():
                self.error(
                    path, "applicability_reason", "Nonapplicability needs a reason"
                )
            self.artifact(value.get("review"), path + ".review")
        elif isinstance(value, dict):
            for key, child in sorted(value.items()):
                self.need(child, path + "." + str(key))
        elif isinstance(value, list):
            for index, child in enumerate(value):
                self.need(child, f"{path}[{index}]")
        elif isinstance(value, float) and not math.isfinite(value):
            self.error(path, "schema", "Nonfinite numbers are forbidden")
        return True

    def fields(self, item, path, names):
        for name in names:
            if name not in item:
                self.error(path + "." + name, "schema", "Missing declared field")

    def collection(self, value, path, kind, *, strings=False):
        """Draft nulls block freezing; booleans are never collection records."""
        if value is None:
            self.need(value, path)
            return None
        if not isinstance(value, kind):
            self.error(path, "schema", "Expected a declared collection or null")
            return None
        self.need(value, path)
        if strings and any(not isinstance(v, str) or not v.strip() for v in value):
            self.error(path, "schema", "Expected nonempty string entries")
        return value

    def artifact_collection(self, value, path):
        values = self.collection(value, path, (list, dict))
        if values is None:
            return
        entries = values.items() if isinstance(values, dict) else enumerate(values)
        for identity, item in entries:
            if isinstance(item, str):
                self.read_file(str(identity), item, path + "." + str(identity))
            else:
                self.artifact(item, path + "." + str(identity))

    def applicability(self, value, path):
        if value is None or (
            isinstance(value, str) and value.strip().lower().startswith("unresolved")
        ):
            self.block(
                path,
                "unresolved_applicability",
                "Explicit decision and reason required",
            )
            return
        item = self.obj(value, path)
        if type(item.get("applicable")) is not bool:
            self.error(path, "schema", "Applicability needs a boolean decision")
        self.exact_string(item.get("reason"), path + ".reason")
        self.artifact(item.get("review"), path + ".review")

    def exact_string(self, value, path, pattern=None):
        """Null is a draft blocker; primitives cannot masquerade as exact locks."""
        if value is None:
            self.need(value, path)
            return False
        if not isinstance(value, str) or not value.strip():
            self.error(path, "schema", "Expected a nonempty exact string or null")
            return False
        if not self.need(value, path):
            return False
        if pattern is not None and re.fullmatch(pattern, value) is None:
            self.error(path, "lock_format", "Exact lock has an invalid format")
            return False
        return True

    def policy(self, value, path):
        """Validate typed criteria, without choosing a method or numeric threshold."""
        if value is None:
            self.need(value, path)
            return
        if not isinstance(value, dict):
            self.error(path, "schema", "Expected a structured policy or null")
            return
        self.need(value, path)
        numeric_names = {
            "atol",
            "rtol",
            "absolute_tolerance",
            "relative_tolerance",
            "max_error",
            "max_ratio",
            "warmup",
            "warmup_count",
            "repetitions",
            "samples",
            "sample_count",
            "iterations",
            "confidence_level",
        }

        def visit(node, location):
            if isinstance(node, dict):
                for key, child in node.items():
                    child_path = location + "." + key
                    if (
                        key in numeric_names
                        and child is not None
                        and (type(child) not in (int, float))
                    ):
                        self.error(child_path, "numeric_criterion", "Expected a number")
                    elif isinstance(child, bool) and key != "applicable":
                        self.error(
                            child_path,
                            "numeric_criterion",
                            "Boolean is not a criterion",
                        )
                    else:
                        visit(child, child_path)
            elif isinstance(node, list):
                for index, child in enumerate(node):
                    visit(child, f"{location}[{index}]")
            elif isinstance(node, bool):
                self.error(location, "numeric_criterion", "Boolean is not a criterion")
            elif type(node) in (int, float) and (
                (isinstance(node, float) and not math.isfinite(node)) or node < 0
            ):
                self.error(
                    location,
                    "numeric_criterion",
                    "Expected finite nonnegative criteria",
                )

        visit(value, path)

    def read_file(self, path, sha, location):
        if not isinstance(path, str) or not path or "\\" in path or ":" in path:
            self.error(
                location, "source_path", "Expected a workspace-relative POSIX path"
            )
            return None
        relative = PurePosixPath(path)
        absolute = (self.root / path).resolve()
        if (
            relative.is_absolute()
            or ".." in relative.parts
            or not (absolute.is_relative_to(self.root))
        ):
            self.error(location, "source_path", "Path escapes the workspace")
            return None
        if not isinstance(sha, str) or not re.fullmatch(r"[0-9a-f]{64}", sha):
            self.error(location, "source_digest", "Expected lowercase SHA256")
            return None
        try:
            if path not in self.files:
                self.files[path] = absolute.read_bytes()
            content = self.files[path]
        except OSError:
            self.error(location, "source_missing", path)
            return None
        if hashlib.sha256(content).hexdigest() != sha:
            self.error(location, "source_hash_mismatch", path)
        return content

    def artifact(self, value, path):
        if not self.need(value, path):
            return
        if isinstance(value, dict) and value.get("applicable") is False:
            return
        item = self.obj(value, path)
        self.read_file(item.get("path"), item.get("sha256"), path)

    def references(self, values, allowed, path):
        if not self.need(values, path):
            return
        if not isinstance(values, list) or not all(isinstance(v, str) for v in values):
            self.error(path, "schema", "Expected an array of reference ids")
            return
        for value in values:
            if value not in allowed:
                self.error(path, "unknown_reference", value)

    def runtime_claim(self, item, path):
        if item.get("evidence_status", "unverified") != "unverified":
            self.error(
                path, "runtime_claim", "This prerequisite checker cannot attest passes"
            )

    def sources_and_metadata(self, sections):
        values = self.document.get("source_evidence")
        if not isinstance(values, list) or not values:
            self.error("source_evidence", "schema", "Expected nonempty source evidence")
            values = []
        for index, value in enumerate(values):
            location = f"source_evidence[{index}]"
            value = self.obj(value, location)
            path = value.get("path")
            if isinstance(path, str):
                if path in self.sources:
                    self.error(location, "duplicate_source", path)
                self.sources[path] = value.get("sha256_raw")
            self.read_file(path, value.get("sha256_raw"), location)
            if isinstance(path, str) and path.startswith(".upstream/napari/"):
                if value.get("commit") != COMMIT:
                    self.error(location, "baseline", "Source belongs to another commit")
        metadata = {}
        for path in [".upstream/napari/pyproject.toml", "pyproject.toml"]:
            if path not in self.sources:
                self.error("source_evidence", "missing_source", path)
                continue
            try:
                metadata[path] = tomllib.loads(self.files[path].decode("utf-8"))
            except (KeyError, UnicodeError, tomllib.TOMLDecodeError):
                self.error("source_evidence", "metadata", "Cannot parse " + path)
        pinned = metadata.get(".upstream/napari/pyproject.toml", {}).get("project", {})
        fork = metadata.get("pyproject.toml", {}).get("project", {})
        facts = sections["supported_contract_facts"]
        providers = sections["provider_contract"]
        for prefix, project in [("pinned", pinned), ("fork", fork)]:
            if project and facts.get(prefix + "_requires_python") != project.get(
                "requires-python"
            ):
                self.error("supported_contract_facts", "metadata_mismatch", prefix)
            extras = project.get("optional-dependencies", {})
            declared = facts.get(prefix + "_qt_extras_as_written")
            expected = {
                key: extras.get(key)
                for key in ["pyqt5", "pyqt6", "pyside6", "pyqt", "pyside", "qt"]
            }
            if project and declared != expected:
                self.error(
                    "supported_contract_facts", "metadata_mismatch", prefix + " Qt"
                )
        profiles = providers.get("pinned_requirement_profiles_as_written", {})
        if isinstance(profiles, dict) and pinned:
            expected = {"runtime": pinned.get("dependencies")}
            expected.update(
                {
                    key: pinned.get("optional-dependencies", {}).get(key)
                    for key in [
                        "testing",
                        "testing_extra",
                        "optional-base",
                        "optional-numba",
                        "partsegcore",
                        "triangle",
                    ]
                }
            )
            if profiles != expected:
                self.error(
                    "provider_contract", "metadata_mismatch", "Pinned requirements"
                )
        else:
            self.error(
                "provider_contract", "schema", "Missing pinned requirement profiles"
            )
        if fork and providers.get("fork_runtime_requirements_as_written") != fork.get(
            "dependencies"
        ):
            self.error("provider_contract", "metadata_mismatch", "Fork requirements")

    def configurations(self, sections):
        axes = self.obj(
            sections["supported_contract_facts"].get("required_goal_axes"),
            "supported_contract_facts.required_goal_axes",
        )
        for name, required in [
            ("python_minors", PYTHONS),
            ("os_families", SYSTEMS),
            ("qt_bindings", BINDINGS),
        ]:
            values = axes.get(name)
            if not isinstance(values, list) or not all(
                isinstance(v, str) for v in values
            ):
                self.error(
                    "required_goal_axes." + name, "schema", "Expected string array"
                )
            elif not required.issubset(values) or len(values) != len(set(values)):
                self.error(
                    "required_goal_axes." + name,
                    "missing_accounting",
                    "Required supported axis removed or duplicated",
                )
        architectures = self.collection(
            axes.get("architectures"),
            "required_goal_axes.architectures",
            list,
            strings=True,
        )
        if isinstance(architectures, list) and all(
            isinstance(a, str) for a in architectures
        ):
            if len(architectures) != len(set(architectures)):
                self.error(
                    "required_goal_axes.architectures",
                    "schema",
                    "Architecture entries must be unique",
                )
            for index, value in enumerate(architectures):
                self.exact_string(value, f"required_goal_axes.architectures[{index}]")
        cross_product = axes.get("cross_product_applicability")
        if cross_product is not None and not isinstance(cross_product, (str, dict)):
            self.error(
                "required_goal_axes.cross_product_applicability",
                "schema",
                "Expected applicability policy text/object or null",
            )
        self.need(cross_product, "required_goal_axes.cross_product_applicability")
        # The draft begins with 'unresolved' rather than a bare placeholder token.
        if str(axes.get("cross_product_applicability", "")).startswith("unresolved"):
            self.block(
                "required_goal_axes.cross_product_applicability",
                "unresolved_applicability",
                "Declare applicable and excluded combinations",
            )
        profiles = self.rows(
            self.document.get("dependency_constraint_profiles"),
            "dependency_constraint_profiles",
            {
                "constraints-py" + v
                for v in [
                    "3.11",
                    "3.12",
                    "3.13",
                    "3.14",
                    "3.11_min_req",
                    "3.12_examples",
                ]
            }
            | {"constraints-benchmark"},
        )
        for identity, item in profiles.items():
            location = "dependency_constraint_profiles." + identity
            self.fields(
                item,
                location,
                [
                    "path",
                    "sha256_raw",
                    "selected_exact_pins",
                    "complete_resolved_platform_lock",
                ],
            )
            content = self.read_file(item.get("path"), item.get("sha256_raw"), location)
            if not isinstance(item.get("path"), str) or (
                self.sources.get(item["path"]) != item.get("sha256_raw")
            ):
                self.error(
                    location,
                    "source_reference",
                    "Profile needs matching source evidence",
                )
            if item.get("constraint_is_execution_evidence") is not False:
                self.error(
                    location, "runtime_claim", "Constraints are not execution evidence"
                )
            if content is not None:
                try:
                    pins = {}
                    for line in content.decode("utf-8").splitlines():
                        match = re.fullmatch(r"([a-zA-Z0-9_.-]+)==(\S+)", line)
                        if match:
                            pins[match[1]] = match[2]
                    selected = {
                        k.lower(): v
                        for k, v in pins.items()
                        if identity == "constraints-benchmark"
                        or k.lower() in CRITICAL_PACKAGES
                    }
                    if item.get("selected_exact_pins") != selected:
                        self.error(
                            location, "constraint_pins", "Pins differ from source bytes"
                        )
                except UnicodeError:
                    self.error(location, "constraint_pins", "Constraints are not UTF-8")
            self.artifact(
                item.get("complete_resolved_platform_lock"),
                location + ".complete_resolved_platform_lock",
            )
        reference = expected_cells()
        cells = self.rows(
            sections["candidate_matrix"].get("cells"),
            "candidate_matrix.cells",
            reference,
        )
        accounted = set()
        for identity, item in cells.items():
            location = "candidate_matrix.cells." + identity
            if identity in reference:
                for key, value in reference[identity].items():
                    if item.get(key) != value:
                        self.error(
                            location + "." + key,
                            "configured_cell_mismatch",
                            "Changed pinned configuration or original test scope",
                        )
            if (
                not isinstance(item.get("source"), str)
                or item["source"] not in self.sources
            ):
                self.error(
                    location, "missing_source", "Configured source needs a fingerprint"
                )
            if not isinstance(item.get("constraint_profile_id"), str) or (
                item["constraint_profile_id"] not in profiles
            ):
                self.error(location, "unknown_reference", "Missing constraint profile")
            binding = item.get("binding")
            if binding is not None and (
                not isinstance(binding, str) or binding not in BINDINGS
            ):
                self.error(location + ".binding", "schema", "Unsupported binding name")
                binding = None
            self.runtime_claim(item, location)
            for key in ["interpreter", "os_version", "architecture"]:
                self.exact_string(item.get(key), location + "." + key)
            patch = item.get("python_patch")
            if self.exact_string(
                patch, location + ".python_patch", r"3\.\d+\.\d+(?:(?:a|b|rc)\d+)?"
            ):
                if ".".join(patch.split(".")[:2]) != item.get("python_minor_requested"):
                    self.error(
                        location + ".python_patch",
                        "python_patch_minor",
                        "Exact patch does not match requested Python minor",
                    )
            if item.get("interpreter") not in (None, "CPython"):
                self.error(location + ".interpreter", "schema", "Expected CPython")
            self.applicability(item.get("applicability"), location + ".applicability")
            self.need(
                item.get("required_for_release"), location + ".required_for_release"
            )
            if (
                item.get("required_for_release") is not None
                and type(item["required_for_release"]) is not bool
            ):
                self.error(
                    location, "schema", "required_for_release must be boolean or null"
                )
            if item.get("runner_image_digest") is None:
                self.artifact(
                    item.get("host_fingerprint"), location + ".host_fingerprint"
                )
            elif not re.fullmatch(r"[0-9a-f]{64}", str(item["runner_image_digest"])):
                self.error(location, "source_digest", "Runner image needs exact SHA256")
            for key in ["compiler_toolchain_lock", "gpu_driver_lock"]:
                if key == "gpu_driver_lock" and binding is None:
                    continue
                self.artifact(item.get(key), location + "." + key)
            if binding:
                if (
                    item.get("native_qt_bridge_required_for_gui_release_gate")
                    is not True
                ):
                    self.error(
                        location,
                        "native_qt_bridge",
                        "GUI release requires a native bridge",
                    )
                for key in [
                    "binding_package_version_from_constraints",
                    "qt_runtime_package_version_from_constraints",
                    "native_qt_sdk_version",
                ]:
                    self.exact_string(
                        item.get(key), location + "." + key, r"\d+\.\d+\.\d+"
                    )
                sdk = item.get("native_qt_sdk_version")
                runtime = item.get("qt_runtime_package_version_from_constraints")
                if isinstance(sdk, str) and isinstance(runtime, str) and sdk != runtime:
                    self.error(
                        location, "qt_sdk_runtime", "SDK and declared Qt runtime differ"
                    )
            if self.need(item.get("gate_ids"), location + ".gate_ids"):
                self.references(item["gate_ids"], GATES, location + ".gate_ids")
            if (
                isinstance(item.get("architecture"), str)
                and isinstance(item.get("python_minor_requested"), str)
                and isinstance(item.get("os_family"), str)
            ):
                accounted.add(
                    (
                        item.get("python_minor_requested"),
                        item.get("os_family"),
                        binding,
                        item["architecture"],
                    )
                )
        architectures = axes.get("architectures")
        if isinstance(architectures, list) and all(
            isinstance(a, str) for a in architectures
        ):
            for py, system, binding, arch in itertools.product(
                sorted(PYTHONS), sorted(SYSTEMS), sorted(BINDINGS), architectures
            ):
                if (py, system, binding, arch) not in accounted:
                    self.block(
                        "candidate_matrix",
                        "unaccounted_applicability",
                        f"Decide applicability: {py}/{system}/{binding}/{arch}",
                    )
        return cells

    def contracts(self, sections, cells):
        choices = self.rows(
            self.document.get("unresolved_choices"), "unresolved_choices", CHOICES
        )
        for identity, item in choices.items():
            location = "unresolved_choices." + identity
            if item.get("status") != "resolved":
                self.block(
                    location, "open_choice", "Acceptance choice remains unresolved"
                )
            else:
                self.exact_string(item.get("resolution"), location + ".resolution")
                self.artifact(
                    item.get("resolution_evidence"), location + ".resolution_evidence"
                )
        workflows = self.rows(
            self.document.get("required_workflows"), "required_workflows", WORKFLOWS
        )
        for identity, item in workflows.items():
            location = "required_workflows." + identity
            self.runtime_claim(item, location)
            self.collection(
                item.get("exact_case_ids"),
                location + ".exact_case_ids",
                list,
                strings=True,
            )
            self.artifact_collection(
                item.get("input_artifact_hashes"), location + ".input_artifact_hashes"
            )
            steps = self.collection(
                item.get("ordered_steps"), location + ".ordered_steps", list
            )
            if steps is not None and any(
                not isinstance(step, (str, dict)) for step in steps
            ):
                self.error(
                    location + ".ordered_steps",
                    "schema",
                    "Expected step text or objects",
                )
            self.collection(
                item.get("reference_output_and_event_oracles"),
                location + ".reference_output_and_event_oracles",
                dict,
            )
            for key in ["numerical_tolerances", "image_tolerances"]:
                self.policy(item.get(key), location + "." + key)
            self.references(
                item.get("applicable_matrix_cell_ids"),
                cells,
                location + ".applicable_matrix_cell_ids",
            )
            paths = item.get("pinned_fixture_sources")
            if not isinstance(paths, list):
                self.error(location, "schema", "Expected pinned fixture paths")
                paths = []
            for path in paths:
                if not isinstance(path, str) or not (self.root / path).exists():
                    self.error(location, "missing_source", "Fixture source missing")
                elif (self.root / path).is_file() and path not in self.sources:
                    self.error(
                        location, "missing_source", "Fixture file needs a fingerprint"
                    )
        provider = sections["provider_contract"]
        self.runtime_claim(provider, "provider_contract")
        case_map = self.collection(
            provider.get("exact_profile_case_map"),
            "provider_contract.exact_profile_case_map",
            dict,
        )
        if case_map is not None:
            for name, cases in case_map.items():
                location = "provider_contract.exact_profile_case_map." + name
                entries = self.collection(cases, location, (list, dict))
                if isinstance(entries, list):
                    self.collection(entries, location, list, strings=True)
                elif isinstance(entries, dict):
                    for identity, case in entries.items():
                        if not isinstance(case, (str, list, dict)):
                            self.error(
                                location + "." + identity,
                                "schema",
                                "Expected case identifiers or structured cases",
                            )
        self.artifact_collection(
            provider.get("platform_resolved_artifact_locks"),
            "provider_contract.platform_resolved_artifact_locks",
        )
        plugin = sections["plugin_contract"]
        self.runtime_claim(plugin, "plugin_contract")
        classes = plugin.get("required_contribution_classes")
        if (
            not isinstance(classes, list)
            or not all(isinstance(c, str) for c in classes)
            or set(classes) != {"reader", "writer", "command", "dock-widget"}
        ):
            self.error(
                "plugin_contract", "missing_accounting", "Plugin contributions removed"
            )
        self.applicability(
            plugin.get("legacy_engine_applicability"),
            "plugin_contract.legacy_engine_applicability",
        )
        representatives = self.collection(
            plugin.get("representative_plugin_artifacts"),
            "plugin_contract.representative_plugin_artifacts",
            list,
        )
        for index, item in enumerate(representatives or []):
            location = f"plugin_contract.representative_plugin_artifacts[{index}]"
            item = self.obj(item, location)
            for key in [
                "distribution",
                "version",
                "source_revision",
                "wheel_sha256",
                "dependency_lock_sha256",
            ]:
                self.exact_string(
                    item.get(key),
                    location + "." + key,
                    r"[0-9a-f]{64}" if key.endswith("sha256") else None,
                )
            for key in [
                "manifest_or_hook_ids",
                "public_and_private_api_use",
                "workflow_ids",
                "matrix_cell_ids",
            ]:
                self.collection(item.get(key), location + "." + key, list, strings=True)
        fixture = sections["full_fixture_contract"]
        self.exact_string(
            fixture.get("fixture_source_inventory_sha256"),
            "full_fixture_contract.fixture_source_inventory_sha256",
            r"[0-9a-f]{64}",
        )
        expected = self.collection(
            fixture.get("expected_collection_per_cell"),
            "full_fixture_contract.expected_collection_per_cell",
            dict,
        )
        for identity, value in (expected or {}).items():
            if not (type(value) is int and value >= 0) and not isinstance(value, dict):
                self.error(
                    "full_fixture_contract.expected_collection_per_cell." + identity,
                    "schema",
                    "Expected a nonnegative count or collection record",
                )
        self.artifact_collection(
            fixture.get("baseline_skip_xfail_and_deselection_receipts"),
            "full_fixture_contract.baseline_skip_xfail_and_deselection_receipts",
        )
        command = fixture.get("full_suite_runner_command")
        if isinstance(command, list):
            self.collection(
                command,
                "full_fixture_contract.full_suite_runner_command",
                list,
                strings=True,
            )
        else:
            self.exact_string(
                command, "full_fixture_contract.full_suite_runner_command"
            )
        if fixture.get("additional_migration_exclusions_allowed") is not False:
            self.error(
                "full_fixture_contract",
                "exclusion_policy",
                "Migration exclusions forbidden",
            )
        workload = sections["workload_contract"]
        if workload.get("fixed_reference_commit") != COMMIT:
            self.error("workload_contract", "baseline", "Workloads need fixed baseline")
        self.collection(
            workload.get("end_to_end_workload_ids"),
            "workload_contract.end_to_end_workload_ids",
            list,
            strings=True,
        )
        for key in [
            "numerical_tolerances",
            "image_tolerances",
            "performance_tolerances",
        ]:
            self.policy(workload.get(key), "workload_contract." + key)
        protocol = self.obj(
            workload.get("frozen_measurement_protocol"),
            "workload_contract.frozen_measurement_protocol",
        )
        for key in [
            "hardware_and_os_lock",
            "python_qt_provider_build_lock",
            "gpu_display_and_synchronization",
            "warmup_repetitions_and_statistics",
            "time_memory_and_resource_measurement_methods",
            "workload_parameter_and_environment_skip_policy",
        ]:
            self.policy(
                protocol.get(key),
                "workload_contract.frozen_measurement_protocol." + key,
            )
        external = self.obj(
            workload.get("external_benchmark_input"),
            "workload_contract.external_benchmark_input",
        )
        self.exact_string(
            external.get("sha256"),
            "workload_contract.external_benchmark_input.sha256",
            r"[0-9a-f]{64}",
        )
        suites = self.rows(
            workload.get("pinned_suite_inventory"),
            "workload_contract.pinned_suite_inventory",
            BENCHMARKS,
        )
        for identity, item in suites.items():
            location = "workload_contract.pinned_suite_inventory." + identity
            self.runtime_claim(item, location)
            self.read_file(item.get("source"), item.get("sha256_raw"), location)
            if not isinstance(item.get("source"), str) or (
                self.sources.get(item["source"]) != item.get("sha256_raw")
            ):
                self.error(
                    location, "source_reference", "Benchmark needs matching fingerprint"
                )
            cases = self.collection(
                item.get("selected_acceptance_cases"),
                location + ".selected_acceptance_cases",
                list,
            )
            if cases is not None and any(
                not isinstance(case, (str, dict)) for case in cases
            ):
                self.error(
                    location,
                    "schema",
                    "Expected selected case ids or parameter objects",
                )
            self.artifact_collection(
                item.get("materialized_input_hashes"),
                location + ".materialized_input_hashes",
            )
            self.policy(
                item.get("acceptance_tolerances"), location + ".acceptance_tolerances"
            )
        for item in self.rows(
            self.document.get("final_evidence_gates"), "final_evidence_gates", GATES
        ).values():
            self.runtime_claim(item, "final_evidence_gates." + item["id"])

    def run(self, check_frozen=False):
        self.document = self.obj(self.document, "$")
        d = self.document
        if type(d.get("schema_version")) is not int or d["schema_version"] != 1:
            self.error("schema_version", "schema", "Expected schema_version 1")
        if (
            not isinstance(d.get("status"), str)
            or d["status"] not in {"draft", "frozen"}
            or type(d.get("frozen")) is not bool
        ):
            self.error(
                "status", "schema", "Expected draft/frozen status and boolean frozen"
            )
        elif d["frozen"] != (d["status"] == "frozen"):
            self.error("frozen", "claim_conflict", "Status and frozen flag disagree")
        if d.get("complete_application_port") is not False:
            self.error(
                "complete_application_port",
                "runtime_claim",
                "Release proof is out of scope",
            )
        sections = {key: self.obj(d.get(key), key) for key in sorted(SECTIONS)}
        if sections["baseline"].get("commit") != COMMIT or (
            sections["baseline"].get("version") != "0.9.2"
        ):
            self.error("baseline", "baseline", "Expected fixed napari 0.9.2 commit")
        if sections["milestone_status"] != {f"M{i}": "open" for i in range(10)}:
            self.error(
                "milestone_status",
                "runtime_claim",
                "This record cannot certify milestone exits",
            )
        semantics = sections["evidence_semantics"]
        for key in [
            "configured_ci_is_not_runtime_evidence",
            "no_inferred_passes_from_prior_packet_counts",
            "failed_required_items_cannot_be_removed_to_obtain_acceptance",
        ]:
            if semantics.get(key) is not True:
                self.error(
                    "evidence_semantics." + key,
                    "runtime_claim",
                    "Required evidence boundary",
                )
        if semantics.get("runtime_reports_accepted_by_this_record") != [] or (
            sections["workload_contract"].get("runtime_evidence") != []
        ):
            self.error(
                "evidence_semantics",
                "runtime_claim",
                "Runtime receipt validation is separate",
            )
        self.sources_and_metadata(sections)
        cells = self.configurations(sections)
        self.contracts(sections, cells)
        if (d.get("frozen") is True or d.get("status") == "frozen") and self.blockers:
            self.error(
                "frozen",
                "unproved_frozen_claim",
                "Freeze prerequisites remain unresolved",
            )
        if check_frozen and (
            d.get("status") != "frozen" or d.get("frozen") is not True
        ):
            self.block(
                "frozen", "record_not_frozen", "--check-frozen requires a frozen record"
            )

        def key(item):
            return item["path"], item["code"], item["detail"]

        self.errors.sort(key=key)
        self.blockers.sort(key=key)
        valid = not self.errors
        ready = valid and not self.blockers
        return {
            "schema_version": 1,
            "scope": "M0 acceptance prerequisites only",
            "mode": "check-frozen" if check_frozen else "validate-draft",
            "record_status": d.get("status"),
            "valid": valid,
            "freeze_prerequisites_met": ready,
            "frozen_verified": ready and d.get("frozen") is True,
            "runtime_parity_proven": False,
            "release_parity_proven": False,
            "errors": self.errors,
            "blockers": self.blockers,
            "counts": {
                "errors": len(self.errors),
                "blockers": len(self.blockers),
                "sources": len(self.sources),
                "configured_cells": len(cells),
            },
            "exit_code": 0 if valid and (not check_frozen or ready) else 1,
        }


def validate_document(document, root=ROOT, *, check_frozen=False):
    """Return a deterministic read-only prerequisite report for parsed JSON."""
    return Validator(document, root).run(check_frozen)


def reject_constant(value):
    raise ValueError("Nonfinite JSON number: " + value)


def unique_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError("Duplicate JSON key: " + key)
        value[key] = item
    return value


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument(
        "--acceptance", type=Path, default=Path("docs/conversion-acceptance.json")
    )
    parser.add_argument("--check-frozen", action="store_true")
    args = parser.parse_args(argv)
    path = (
        args.acceptance
        if args.acceptance.is_absolute()
        else args.root / args.acceptance
    )
    try:
        document = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=unique_object,
            parse_constant=reject_constant,
        )
        report = validate_document(document, args.root, check_frozen=args.check_frozen)
    except (OSError, ValueError) as error:
        report = {
            "schema_version": 1,
            "scope": "M0 acceptance prerequisites only",
            "valid": False,
            "freeze_prerequisites_met": False,
            "frozen_verified": False,
            "runtime_parity_proven": False,
            "release_parity_proven": False,
            "exit_code": 1,
            "errors": [{"path": "$", "code": "input", "detail": str(error)}],
            "blockers": [],
        }
    print(json.dumps(report, indent=2, sort_keys=True, allow_nan=False))
    return report["exit_code"]


if __name__ == "__main__":
    sys.exit(main())
