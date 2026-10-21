"""Validate persisted source reviews; never infer conversion from native calls.

This gate checks the consistency and freshness of reviewed decisions. It cannot
replace review of C++ control flow, dynamic Python behavior or release parity.
"""

import builtins
import hashlib
import json
from datetime import datetime
from pathlib import Path, PurePosixPath
from xml.etree import ElementTree

CLASSIFICATIONS = {
    "native",
    "facade",
    "adapter",
    "dependency",
    "tooling",
    "partial",
    "unconverted",
}
COMPLETE_CLASSIFICATIONS = CLASSIFICATIONS - {"partial", "unconverted"}
MODULE_COVERAGE = {
    "module_execution",
    "state",
    "inheritance",
    "exports",
    "callbacks",
    "fallbacks",
    "calls",
}
DEFINITION_COVERAGE = {"implementation", "decorators", "calls"}
APPLICATION_PREFIXES = ("napari.", "napari_builtins.", "naparixx.")
NATIVE_PREFIXES = (
    "naparixx._native.",
    "naparixx.native.",
    "naparixx._qt_native.",
    "naparixx._qt5_native.",
)
CPP_SUFFIXES = {".cpp", ".cc", ".cxx", ".hpp", ".h", ".hxx"}


def nonempty(value):
    return isinstance(value, str) and bool(value.strip())


class ReviewValidator:
    def __init__(self, root, modules, commit, baseline=()):
        self.root = Path(root).resolve()
        self.modules = modules
        self.commit = commit
        self.baseline = {row["file"]: row for row in baseline}
        self.issues = []
        self.entries = {}
        self.owners = {}
        self.global_targets = {}
        self.fingerprints = {}
        self.operations = {}
        self.records = {}
        self.config_valid = True
        for module in modules:
            for entry in (module, *module["definitions"]):
                key = entry["review_key"]
                if key in self.entries:
                    self.issue(
                        key, "duplicate_responsibility", "Inventory key collision"
                    )
                self.entries[key] = entry
                self.owners[key] = module
                entry["review_status"] = "unreviewed"
                if entry is not module:
                    name = module["file"].removesuffix(".py").replace("/", ".")
                    name = name.removesuffix(".__init__") + "." + entry["name"]
                    self.global_targets.setdefault(name, set()).add(key)

    def issue(self, key, code, detail):
        self.issues.append({"responsibility": key, "code": code, "detail": detail})

    def load(self, path, collection, optional=False):
        key = str(path)
        try:
            document = json.loads(Path(path).read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            self.issue(key, "review_file", str(error))
            self.config_valid = False
            return {}
        if not isinstance(document, dict):
            self.issue(key, "review_schema", "Expected a JSON object")
            self.config_valid = False
            return {}
        if type(document.get("schema_version")) is not int or (
            document["schema_version"] != 1
        ):
            self.issue(key, "review_schema", "Expected schema_version 1")
            self.config_valid = False
        if document.get("upstream_commit") != self.commit:
            self.issue(key, "baseline_changed", "Reviews belong to another baseline")
            self.config_valid = False
        values = document.get(collection, {} if optional else None)
        if not isinstance(values, dict):
            self.issue(key, "review_schema", f"Expected an object for {collection}")
            self.config_valid = False
            return {}
        return values

    def path(self, value):
        if not nonempty(value):
            raise ValueError("Evidence path is missing")
        relative = PurePosixPath(value)
        if (
            "\\" in value
            or ":" in value
            or relative.is_absolute()
            or (".." in relative.parts)
        ):
            raise ValueError("Use a relative POSIX path within the workspace")
        result = (self.root / Path(*relative.parts)).resolve()
        if not result.is_relative_to(self.root):
            raise ValueError("Evidence resolves outside the workspace")
        return result

    def fingerprint(self, key, record, code):
        if not isinstance(record, dict):
            self.issue(key, code, "Expected path and sha256")
            return None
        try:
            path = self.path(record.get("path"))
            if path not in self.fingerprints:
                self.fingerprints[path] = hashlib.sha256(path.read_bytes()).hexdigest()
            actual = self.fingerprints[path]
        except (OSError, ValueError) as error:
            self.issue(key, code, str(error))
            return None
        if record.get("sha256") != actual:
            self.issue(key, code, f"Missing or stale hash for {record.get('path')}")
            return None
        return path

    def sources(self, key, values, native=False):
        if not isinstance(values, list):
            self.issue(key, "implementation_sources", "Expected implementation sources")
            return
        if native and not values:
            self.issue(key, "native_implementation", "No current C++ implementation")
            return
        native_found = False
        for value in values:
            path = self.fingerprint(key, value, "implementation_source")
            if path is not None and path.suffix in CPP_SUFFIXES:
                native_found = True
                if not nonempty(value.get("symbol")):
                    self.issue(key, "implementation_symbol", "Name the C++ symbol")
        if native and not native_found:
            self.issue(key, "native_implementation", "No current C++ implementation")

    def evidence(self, key, values):
        if not isinstance(values, list) or not values:
            self.issue(key, "test_evidence", "Provide a source-hashed JUnit report")
            return
        for value in values:
            path = self.fingerprint(key, value, "test_evidence")
            if path is None:
                continue
            if value.get("format") != "junit":
                self.issue(key, "test_evidence", "Only JUnit evidence is supported")
                continue
            try:
                tree = ElementTree.parse(path)
                root = tree.getroot()
                if root.tag not in {"testsuite", "testsuites"}:
                    raise ValueError("Expected a JUnit testsuite or testsuites")
                cases = list(root.iter("testcase"))
                if not cases:
                    raise ValueError("JUnit evidence has no test cases")
                for suite in root.iter("testsuite"):
                    for attribute in ("failures", "errors"):
                        if int(suite.get(attribute, "0")) != 0:
                            raise ValueError("JUnit suite reports failures or errors")
                if list(root.iter("failure")) or list(root.iter("error")):
                    raise ValueError("JUnit evidence contains failures or errors")
                if all(case.find("skipped") is not None for case in cases):
                    raise ValueError("JUnit evidence has only skipped tests")
                selected = value.get("cases")
                if (
                    not isinstance(selected, list)
                    or not selected
                    or not all(nonempty(item) for item in selected)
                ):
                    raise ValueError(
                        "Name the JUnit cases supporting this responsibility"
                    )
                indexed = {}
                for case in cases:
                    identifier = f"{case.get('classname', '')}::{case.get('name', '')}"
                    indexed.setdefault(identifier, []).append(case)
                for identifier in selected:
                    matching = indexed.get(identifier, [])
                    if not matching or any(
                        case.find("skipped") is not None for case in matching
                    ):
                        raise ValueError(
                            f"Missing or skipped evidence case: {identifier}"
                        )
            except (OSError, ValueError, ElementTree.ParseError) as error:
                self.issue(key, "test_result", str(error))

    def dependency(self, identifier, operation):
        key = f"dependency:{identifier}"
        if not isinstance(operation, dict):
            self.issue(key, "dependency_schema", "Expected an operation object")
            return False
        before = len(self.issues)
        target = operation.get("callable")
        if (
            not nonempty(target)
            or "*" in target
            or target.startswith(APPLICATION_PREFIXES)
        ):
            self.issue(key, "dependency_callable", "Name one external callable")
        for field in ("owner", "purpose"):
            if not nonempty(operation.get(field)):
                self.issue(key, "dependency_schema", f"Missing {field}")
        self.fingerprint(key, operation.get("adapter"), "dependency_adapter")
        self.evidence(key, operation.get("evidence"))
        return len(self.issues) == before

    def review(self, key, record):
        entry = self.entries[key]
        module = self.owners[key]
        before = len(self.issues)
        if not isinstance(record, dict):
            self.issue(key, "review_schema", "Expected a review object")
            entry["review_status"] = "invalid"
            return
        classification = record.get("classification")
        if not nonempty(classification) or classification not in CLASSIFICATIONS:
            self.issue(key, "classification", "Unknown responsibility classification")
            classification = None
        else:
            entry["review_classification"] = classification
        if record.get("source_sha256") != module["source_sha256"]:
            self.issue(key, "source_changed", "Module source hash is missing or stale")
        if (
            "body_sha256" in entry
            and record.get("body_sha256") != (entry["body_sha256"])
        ):
            self.issue(key, "body_changed", "Definition body hash is missing or stale")
        for field in ("reason", "reviewed_by"):
            if not nonempty(record.get(field)):
                self.issue(key, "review_schema", f"Missing {field}")
        try:
            timestamp = datetime.fromisoformat(record.get("reviewed_at", ""))
            if timestamp.tzinfo is None:
                raise ValueError("Review timestamp must include its timezone")
        except (TypeError, ValueError) as error:
            self.issue(key, "review_timestamp", str(error))
        required = MODULE_COVERAGE if entry is module else DEFINITION_COVERAGE
        coverage = record.get("coverage")
        if not isinstance(coverage, dict) or any(
            not nonempty(coverage.get(field)) for field in required
        ):
            self.issue(key, "review_coverage", f"Explain each of {sorted(required)}")
        if classification == "tooling" and module["scope"] != "tooling":
            self.issue(
                key, "runtime_tooling", "Runtime code cannot be excluded as tooling"
            )
        complete = classification in COMPLETE_CLASSIFICATIONS
        self.sources(key, record.get("implementation", []), classification == "native")
        if complete:
            self.evidence(key, record.get("evidence"))
        elif record.get("evidence"):
            self.evidence(key, record["evidence"])
        dependency_ids = record.get("dependency_ids", [])
        if not isinstance(dependency_ids, list) or not all(
            nonempty(item) for item in dependency_ids
        ):
            self.issue(key, "dependency_scope", "Expected a list of dependency IDs")
            dependency_ids = []
        if classification == "dependency" and not dependency_ids:
            self.issue(
                key, "dependency_scope", "Identify the retained dependency operations"
            )
        for identifier in dependency_ids:
            if identifier not in self.operations:
                self.issue(
                    key, "dependency_scope", f"Unapproved dependency: {identifier}"
                )
        calls = record.get("calls")
        if not isinstance(calls, list):
            self.issue(
                key, "call_review", "Declare all observed and retained C++/Python calls"
            )
        else:
            declared = set()
            observed = {item["target"] for item in entry["call_evidence"]}
            for call in calls:
                if not isinstance(call, dict) or not nonempty(call.get("target")):
                    self.issue(key, "call_review", "Each call needs its target")
                    continue
                target = call["target"]
                declared.add(target)
                if not nonempty(call.get("purpose")):
                    self.issue(key, "call_review", f"Explain call to {target}")
                self.call(key, call)
                if target not in observed and call.get("source") is None:
                    self.issue(
                        key, "call_source", f"Fingerprint the retained call to {target}"
                    )
            for target in sorted(observed - declared):
                self.issue(key, "undeclared_call", target)
        entry["review_status"] = "reviewed" if len(self.issues) == before else "invalid"

    def local_targets(self, key, target):
        if target in self.global_targets:
            return self.global_targets[target]
        module = self.owners[key]
        definitions = module["definitions"]
        module_name = module["file"].removesuffix(".py").replace("/", ".")
        module_name = module_name.removesuffix(".__init__")
        if target.startswith(module_name + "."):
            name = target[len(module_name) + 1 :]
        elif "." not in target:
            scope = self.entries[key].get("name", "").split(".")
            class_names = {
                item["name"] for item in definitions if item["kind"] == "class"
            }
            for depth in range(len(scope), -1, -1):
                parent = ".".join(scope[:depth])
                if (
                    parent in class_names
                    and self.entries[key].get("kind") == "function"
                ):
                    continue
                name = f"{parent}.{target}" if parent else target
                matches = {
                    item["review_key"] for item in definitions if item["name"] == name
                }
                if matches:
                    return matches
            return set()
        elif target.startswith(("self.", "cls.")):
            owner = self.entries[key].get("name", "")
            classes = [
                item["name"]
                for item in definitions
                if item["kind"] == "class" and owner.startswith(item["name"] + ".")
            ]
            if not classes:
                return set()
            name = max(classes, key=len) + "." + target.partition(".")[2]
        else:
            return set()
        return {item["review_key"] for item in definitions if item["name"] == name}

    def call(self, key, call):
        target = call["target"]
        kind = call.get("kind")
        native = target.startswith(NATIVE_PREFIXES)
        candidates = self.local_targets(key, target)
        internal = target.startswith(APPLICATION_PREFIXES) and not native
        if (internal or candidates) and kind != "application":
            self.issue(
                key, "application_call", f"{target} needs an application review target"
            )
        if kind == "application":
            responsibility = call.get("responsibility")
            if not nonempty(responsibility) or responsibility not in self.entries:
                self.issue(
                    key, "application_call", f"Unknown responsibility for {target}"
                )
            elif candidates and responsibility not in candidates:
                self.issue(
                    key, "application_call", f"Wrong local responsibility for {target}"
                )
        elif kind == "dependency":
            identifier = call.get("dependency_id")
            operation = (
                self.operations.get(identifier) if nonempty(identifier) else None
            )
            if operation is None or operation["callable"] != target:
                self.issue(key, "dependency_call", f"Unapproved operation: {target}")
        elif kind == "native":
            if not native:
                self.issue(
                    key, "native_call", f"{target} is not a declared native entry"
                )
        elif kind == "builtin":
            if target not in vars(builtins) and not (
                target.startswith("builtins.") and target[9:] in vars(builtins)
            ):
                self.issue(key, "builtin_call", f"{target} is not a Python builtin")
        elif kind == "protocol":
            if target.startswith(APPLICATION_PREFIXES) or native or candidates:
                self.issue(
                    key,
                    "protocol_call",
                    f"{target} cannot be treated as a provider protocol",
                )
            if any(
                item["target"] == target and item.get("imported")
                for item in self.entries[key]["call_evidence"]
            ):
                self.issue(
                    key, "protocol_call", f"Allowlist imported operation {target}"
                )
            if not nonempty(call.get("receiver")):
                self.issue(key, "protocol_call", f"Identify the receiver of {target}")
        else:
            self.issue(key, "call_review", f"Unknown call kind for {target}")
        if call.get("source") is not None:
            self.fingerprint(key, call["source"], "call_source")

    def graph(self):
        for key, record in self.records.items():
            if key not in self.entries or not isinstance(record, dict):
                continue
            if self.entries[key]["review_status"] != "reviewed":
                continue
            if record["classification"] not in COMPLETE_CLASSIFICATIONS:
                self.issue(key, "implementation_incomplete", record["classification"])
            for call in record["calls"]:
                if call["kind"] != "application":
                    continue
                target = self.entries[call["responsibility"]]
                if (
                    target["review_status"] != "reviewed"
                    or target.get("review_classification")
                    not in COMPLETE_CLASSIFICATIONS
                ):
                    self.issue(key, "python_application_delegation", call["target"])

    def retirements(self, values):
        present = {row["file"] for row in self.modules}
        removed = set(self.baseline) - present
        for file in sorted(removed - set(values)):
            self.issue(
                file,
                "removed_baseline_module",
                "Review its replacement or tooling retirement",
            )
        for file, record in values.items():
            if file not in removed:
                self.issue(
                    file,
                    "retirement_target",
                    "Module is present or absent from the baseline",
                )
                continue
            if not isinstance(record, dict):
                self.issue(file, "retirement_schema", "Expected a retirement review")
                continue
            original = self.baseline[file]
            if record.get("baseline_sha256") != original["source_sha256"]:
                self.issue(
                    file, "retirement_source", "Missing or stale baseline source hash"
                )
            if not nonempty(record.get("reason")):
                self.issue(
                    file, "retirement_schema", "Explain the preserved public behavior"
                )
            replacements = record.get("replacements")
            if not isinstance(replacements, list) or not all(
                nonempty(item) for item in replacements
            ):
                self.issue(
                    file,
                    "retirement_schema",
                    "Expected replacement responsibility keys",
                )
                continue
            if not replacements and original["scope"] != "tooling":
                self.issue(
                    file,
                    "retirement_runtime",
                    "Runtime responsibilities need reviewed replacements",
                )
            for key in replacements:
                entry = self.entries.get(key)
                if (
                    entry is None
                    or entry["review_status"] != "reviewed"
                    or (
                        entry.get("review_classification")
                        not in COMPLETE_CLASSIFICATIONS
                    )
                ):
                    self.issue(
                        file, "retirement_replacement", f"Incomplete replacement: {key}"
                    )
            self.evidence(file, record.get("evidence"))

    def run(self, reviews_path, dependencies_path):
        operations = self.load(dependencies_path, "operations")
        for identifier, operation in operations.items():
            if self.dependency(identifier, operation):
                self.operations[identifier] = operation
        self.records = self.load(reviews_path, "records")
        for key, record in self.records.items():
            if key not in self.entries:
                self.issue(
                    key, "removed_responsibility", "Review target no longer exists"
                )
                continue
            self.review(key, record)
        if not self.config_valid:
            for entry in self.entries.values():
                if entry["review_status"] == "reviewed":
                    entry["review_status"] = "invalid"
        self.graph()
        self.retirements(self.load(reviews_path, "retired_modules", optional=True))
        statuses = [entry["review_status"] for entry in self.entries.values()]
        unreviewed = sorted(
            key
            for key, entry in self.entries.items()
            if entry["review_status"] == "unreviewed"
        )
        return {
            "schema_version": 1,
            "reviews_file": str(reviews_path),
            "dependencies_file": str(dependencies_path),
            "reviewed": statuses.count("reviewed"),
            "unreviewed": len(unreviewed),
            "invalid": statuses.count("invalid"),
            "unreviewed_responsibilities": unreviewed,
            "issues": self.issues,
            "complete": bool(self.entries) and not self.issues and not unreviewed,
            "semantic_and_release_parity_proven": False,
        }


def apply_reviews(root, modules, commit, reviews_path, dependencies_path, baseline=()):
    return ReviewValidator(root, modules, commit, baseline).run(
        reviews_path, dependencies_path
    )
