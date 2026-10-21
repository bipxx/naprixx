"""Fault-injection checks for the migration review and completion gate."""

import copy
import hashlib
import json
import sys
from types import SimpleNamespace

import pytest

from tools import audit_conversion as audit
from tools.conversion_review import (
    DEFINITION_COVERAGE,
    MODULE_COVERAGE,
    apply_reviews,
)

COMMIT = "0" * 40


def fingerprint(path, root, **metadata):
    return {
        "path": path.relative_to(root).as_posix(),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        **metadata,
    }


@pytest.fixture
def harness(tmp_path):
    source = tmp_path / "src"
    module = source / "napari/model.py"
    module.parent.mkdir(parents=True)
    module.write_text(
        "from naparixx import _native\n\ndef answer():\n    return _native.answer()\n",
        encoding="utf-8",
    )
    implementation = tmp_path / "cpp/model.cpp"
    implementation.parent.mkdir()
    implementation.write_text("int answer() { return 42; }\n", encoding="utf-8")
    evidence = tmp_path / ".artifacts/contracts.xml"
    evidence.parent.mkdir()
    evidence.write_text(
        '<testsuites><testsuite tests="1" failures="0" errors="0">'
        '<testcase classname="contracts" name="answer" />'
        "</testsuite></testsuites>",
        encoding="utf-8",
    )
    docs = tmp_path / "docs"
    docs.mkdir()
    reviews = docs / "conversion-reviews.json"
    dependencies = docs / "conversion-dependencies.json"
    state = SimpleNamespace(
        root=tmp_path,
        source=source,
        module=module,
        implementation=implementation,
        evidence=evidence,
        reviews=reviews,
        dependencies=dependencies,
        records={},
        operations={},
        retirements={},
    )
    state.collect = lambda: audit.collect(source, {})
    state.baseline = copy.deepcopy(state.collect())
    state.evidence_record = lambda: fingerprint(
        evidence, tmp_path, format="junit", cases=["contracts::answer"]
    )

    def populate():
        rows = state.collect()
        state.records = {}
        for row in rows:
            for entry in (row, *row["definitions"]):
                classification = "facade" if entry is row else "native"
                coverage = MODULE_COVERAGE if entry is row else DEFINITION_COVERAGE
                record = {
                    "classification": classification,
                    "source_sha256": row["source_sha256"],
                    "reason": "Reviewed fixture contract for the completion gate.",
                    "reviewed_by": "gate test",
                    "reviewed_at": "2026-10-10T00:00:00+00:00",
                    "coverage": {field: "Explicitly reviewed." for field in coverage},
                    "implementation": [
                        fingerprint(implementation, tmp_path, symbol="answer")
                    ]
                    if classification == "native"
                    else [],
                    "evidence": [state.evidence_record()],
                    "calls": [
                        {
                            "target": item["target"],
                            "kind": "native",
                            "purpose": "Forward the fixture operation to C++.",
                        }
                        for item in entry["call_evidence"]
                    ],
                }
                if "body_sha256" in entry:
                    record["body_sha256"] = entry["body_sha256"]
                state.records[entry["review_key"]] = record
        return rows

    def write(commit=COMMIT):
        reviews.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "upstream_commit": commit,
                    "records": state.records,
                    "retired_modules": state.retirements,
                }
            ),
            encoding="utf-8",
        )
        dependencies.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "upstream_commit": COMMIT,
                    "operations": state.operations,
                }
            ),
            encoding="utf-8",
        )

    def run(commit=COMMIT):
        write(commit)
        return apply_reviews(
            tmp_path,
            state.collect(),
            COMMIT,
            reviews,
            dependencies,
            baseline=state.baseline,
        )

    state.populate = populate
    state.write = write
    state.run = run
    populate()
    return state


def codes(result):
    return {issue["code"] for issue in result["issues"]}


def function_record(harness, name="answer"):
    return harness.records[f"napari/model.py:function:{name}:1"]


def test_persisted_reviews_are_reloaded_and_do_not_claim_semantic_parity(harness):
    first = harness.run()
    second = apply_reviews(
        harness.root,
        harness.collect(),
        COMMIT,
        harness.reviews,
        harness.dependencies,
        baseline=harness.baseline,
    )
    assert first["complete"] and second["complete"]
    assert first["reviewed"] == 2
    assert not first["semantic_and_release_parity_proven"]


@pytest.mark.parametrize(
    "mutation, expected",
    [
        ("python_body", "source_changed"),
        ("python_import", "source_changed"),
        ("class_fields", "source_changed"),
        ("cpp_body", "implementation_source"),
        ("report_bytes", "test_evidence"),
        ("body_hash", "body_changed"),
        ("source_hash", "source_changed"),
        ("missing_review", None),
        ("missing_cpp", "native_implementation"),
        ("missing_case", "test_result"),
        ("missing_calls", "undeclared_call"),
        ("partial", "implementation_incomplete"),
        ("unconverted", "implementation_incomplete"),
        ("tooling", "runtime_tooling"),
        ("unreviewed_fields", "review_coverage"),
        ("removed_definition", "removed_responsibility"),
        ("wrong_baseline", "baseline_changed"),
    ],
)
def test_completion_rejects_incomplete_or_stale_evidence(harness, mutation, expected):
    record = function_record(harness)
    if mutation in {"python_body", "python_import", "class_fields"}:
        extra = {
            "python_body": "\nanswer = lambda: 0\n",
            "python_import": "\nfrom napari.legacy import answer\n",
            "class_fields": "\nclass HiddenState:\n    value = []\n",
        }[mutation]
        harness.module.write_text(
            harness.module.read_text(encoding="utf-8") + extra, encoding="utf-8"
        )
    elif mutation == "cpp_body":
        harness.implementation.write_text(
            "int answer() { return 0; }", encoding="utf-8"
        )
    elif mutation == "report_bytes":
        harness.evidence.write_text("changed evidence", encoding="utf-8")
    elif mutation in {"body_hash", "source_hash"}:
        record[mutation.replace("_hash", "_sha256")] = "stale"
    elif mutation == "missing_review":
        harness.records.pop("napari/model.py:module")
    elif mutation == "missing_cpp":
        record["implementation"] = []
    elif mutation == "missing_case":
        record["evidence"][0]["cases"] = ["unrelated::missing"]
    elif mutation == "missing_calls":
        record["calls"] = []
    elif mutation in {"partial", "unconverted", "tooling"}:
        record["classification"] = mutation
    elif mutation == "unreviewed_fields":
        del harness.records["napari/model.py:module"]["coverage"]["state"]
    elif mutation == "removed_definition":
        harness.module.write_text("# implementation removed\n", encoding="utf-8")
    result = harness.run("1" * 40 if mutation == "wrong_baseline" else COMMIT)
    assert not result["complete"]
    if expected:
        assert expected in codes(result)


@pytest.mark.parametrize(
    "case_body, suite_attributes",
    [
        ('<failure message="wrong result" />', 'failures="0" errors="0"'),
        ('<error message="worker failed" />', 'failures="0" errors="0"'),
        ("<skipped />", 'failures="0" errors="0"'),
        ("", 'failures="1" errors="0"'),
        ("", 'failures="0" errors="1"'),
    ],
)
def test_a_fresh_hash_does_not_make_failed_or_skipped_tests_pass(
    harness, case_body, suite_attributes
):
    harness.evidence.write_text(
        f'<testsuite tests="1" {suite_attributes}>'
        f'<testcase classname="contracts" name="answer">{case_body}</testcase>'
        "</testsuite>",
        encoding="utf-8",
    )
    for record in harness.records.values():
        record["evidence"] = [harness.evidence_record()]
    result = harness.run()
    assert not result["complete"]
    assert "test_result" in codes(result)


@pytest.mark.parametrize("path", ["../outside.cpp", "C:/outside.cpp", "/outside.cpp"])
def test_evidence_must_resolve_inside_the_workspace(harness, path):
    function_record(harness)["implementation"][0]["path"] = path
    result = harness.run()
    assert not result["complete"]
    assert "implementation_source" in codes(result)


def test_application_calls_cannot_be_allowlisted_as_external_dependencies(harness):
    harness.operations["legacy"] = {
        "callable": "napari.layers.Labels.paint",
        "owner": "napari",
        "purpose": "Attempted application delegation",
        "adapter": fingerprint(harness.module, harness.root),
        "evidence": [harness.evidence_record()],
    }
    result = harness.run()
    assert not result["complete"]
    assert "dependency_callable" in codes(result)


def test_complete_caller_cannot_delegate_to_an_unconverted_python_algorithm(harness):
    harness.module.write_text(
        "def answer():\n    return fallback()\n\ndef fallback():\n    return 42\n",
        encoding="utf-8",
    )
    harness.populate()
    fallback = function_record(harness, "fallback")
    fallback["classification"] = "unconverted"
    caller = function_record(harness)
    caller["calls"][0].update(
        kind="application", responsibility="napari/model.py:function:fallback:1"
    )
    result = harness.run()
    assert not result["complete"]
    assert "python_application_delegation" in codes(result)


def test_nested_application_callback_cannot_be_hidden_as_a_provider(harness):
    harness.module.write_text(
        "def answer():\n"
        "    def callback():\n        return 42\n"
        "    return callback()\n",
        encoding="utf-8",
    )
    harness.populate()
    call = function_record(harness)["calls"][0]
    call.update(kind="protocol", receiver="claimed external callback")
    assert "application_call" in codes(harness.run())


def test_qualified_application_call_cannot_point_to_an_unrelated_native_review(harness):
    harness.module.write_text(
        "from napari.model import fallback as legacy\n\n"
        "def answer():\n    return legacy()\n\n"
        "def fallback():\n    return 42\n",
        encoding="utf-8",
    )
    harness.populate()
    call = function_record(harness)["calls"][0]
    call.update(kind="application", responsibility="napari/model.py:function:answer:1")
    assert "application_call" in codes(harness.run())


def test_retained_cpp_calls_need_fresh_source_and_reviewed_callees(harness):
    harness.module.write_text(
        "from naparixx import _native\n\n"
        "def answer():\n    return _native.answer()\n\n"
        "def legacy():\n    return 42\n",
        encoding="utf-8",
    )
    harness.populate()
    function_record(harness, "legacy")["classification"] = "unconverted"
    call = {
        "target": "napari.model.legacy",
        "kind": "application",
        "responsibility": "napari/model.py:function:legacy:1",
        "purpose": "C++ binding delegates to the old application implementation.",
    }
    function_record(harness)["calls"].append(call)
    assert "call_source" in codes(harness.run())
    call["source"] = fingerprint(harness.implementation, harness.root)
    assert "python_application_delegation" in codes(harness.run())


def test_imported_dependency_needs_an_exact_approved_callable(harness):
    harness.module.write_text(
        "import numpy as np\n\ndef answer():\n    return np.asarray(42)\n",
        encoding="utf-8",
    )
    harness.populate()
    caller = function_record(harness)
    caller["classification"] = "adapter"
    caller["calls"][0].update(kind="dependency", dependency_id="numpy.asarray")
    assert "dependency_call" in codes(harness.run())
    harness.operations["numpy.asarray"] = {
        "callable": "numpy.asarray",
        "owner": "NumPy",
        "purpose": "Construct the external array at the provider boundary.",
        "adapter": fingerprint(harness.module, harness.root),
        "evidence": [harness.evidence_record()],
    }
    assert harness.run()["complete"]
    caller["calls"][0].update(kind="protocol", receiver="claimed external provider")
    assert "protocol_call" in codes(harness.run())


def test_a_skipped_selected_case_cannot_borrow_an_unrelated_pass(harness):
    harness.evidence.write_text(
        '<testsuite tests="2" failures="0" errors="0">'
        '<testcase classname="contracts" name="answer"><skipped /></testcase>'
        '<testcase classname="other" name="unrelated" />'
        "</testsuite>",
        encoding="utf-8",
    )
    for record in harness.records.values():
        record["evidence"] = [harness.evidence_record()]
    assert "test_result" in codes(harness.run())


def test_changed_dependency_adapter_invalidates_its_callers(harness):
    harness.module.write_text(
        "import numpy as np\n\ndef answer():\n    return np.asarray(42)\n",
        encoding="utf-8",
    )
    harness.populate()
    caller = function_record(harness)
    caller["classification"] = "adapter"
    caller["calls"][0].update(kind="dependency", dependency_id="numpy.asarray")
    harness.operations["numpy.asarray"] = {
        "callable": "numpy.asarray",
        "owner": "NumPy",
        "purpose": "Use an external primitive at the array boundary.",
        "adapter": fingerprint(harness.implementation, harness.root),
        "evidence": [harness.evidence_record()],
    }
    assert harness.run()["complete"]
    harness.implementation.write_text("// changed binding\n", encoding="utf-8")
    result = harness.run()
    assert "dependency_adapter" in codes(result)
    assert "dependency_call" in codes(result)


def test_module_execution_and_dynamic_calls_have_separate_review_records(tmp_path):
    path = tmp_path / "napari/sample.py"
    path.parent.mkdir()
    path.write_text(
        "register_callback(make_handler())\n\n"
        "def outer():\n"
        "    def callback():\n        return factory()()\n"
        "    return callback()\n",
        encoding="utf-8",
    )
    row = audit.module_record(path, tmp_path, {})
    assert {call["target"] for call in row["call_evidence"]} == {
        "register_callback",
        "make_handler",
    }
    definitions = {entry["name"]: entry for entry in row["definitions"]}
    assert {call["target"] for call in definitions["outer"]["call_evidence"]} == {
        "callback"
    }
    assert {
        call["target"] for call in definitions["outer.callback"]["call_evidence"]
    } == {"factory", "expression:factory()"}


def test_review_keys_distinguish_property_accessors_and_survive_line_moves(tmp_path):
    path = tmp_path / "napari/model.py"
    path.parent.mkdir()
    code = (
        "class Model:\n    @property\n    def value(self):\n        return 1\n"
        "    @value.setter\n    def value(self, value):\n        pass\n"
    )
    path.write_text(code, encoding="utf-8")
    before = audit.module_record(path, tmp_path, {})["definitions"]
    path.write_text("\n\n" + code, encoding="utf-8")
    after = audit.module_record(path, tmp_path, {})["definitions"]
    assert len({entry["review_key"] for entry in before}) == 3
    assert [entry["review_key"] for entry in before] == [
        entry["review_key"] for entry in after
    ]
    assert before[0]["id"] != after[0]["id"]


def test_deleted_baseline_code_requires_a_reviewed_replacement(harness):
    original = copy.deepcopy(harness.baseline[0])
    original["file"] = "napari/deleted.py"
    harness.baseline.append(original)
    assert "removed_baseline_module" in codes(harness.run())
    harness.retirements[original["file"]] = {
        "baseline_sha256": original["source_sha256"],
        "reason": "Fixture behavior moved to the reviewed answer implementation.",
        "replacements": ["napari/model.py:function:answer:1"],
        "evidence": [harness.evidence_record()],
    }
    assert harness.run()["complete"]
    harness.retirements[original["file"]]["replacements"] = []
    assert "retirement_runtime" in codes(harness.run())


@pytest.mark.parametrize(
    "record",
    [
        None,
        [],
        {"classification": []},
        {
            "classification": "facade",
            "dependency_ids": 0,
            "calls": [None],
        },
    ],
)
def test_malformed_review_records_fail_closed_without_crashing(harness, record):
    harness.records["napari/model.py:module"] = record
    assert not harness.run()["complete"]


def test_invalid_json_fails_closed(harness):
    harness.write()
    harness.reviews.write_text("not JSON", encoding="utf-8")
    result = apply_reviews(
        harness.root,
        harness.collect(),
        COMMIT,
        harness.reviews,
        harness.dependencies,
    )
    assert not result["complete"]
    assert "review_file" in codes(result)


@pytest.mark.parametrize("explicit, declared_complete", [(True, False), (False, True)])
def test_cli_rejects_incomplete_reviews_and_premature_completion_claims(
    harness, monkeypatch, explicit, declared_complete
):
    harness.records.clear()
    harness.write()
    (harness.root / "upstream.json").write_text(
        json.dumps({"commit": COMMIT}), encoding="utf-8"
    )
    (harness.root / "migration.json").write_text(
        json.dumps(
            {
                "distribution": "fixture",
                "native_version": "0",
                "application_components": [],
                "complete_application_port": declared_complete,
            }
        ),
        encoding="utf-8",
    )
    monkeypatch.setattr(audit, "ROOT", harness.root)
    monkeypatch.setattr(audit, "pinned_source", lambda upstream: harness.source)
    monkeypatch.setattr(
        sys,
        "argv",
        ["audit_conversion.py"] + (["--check-complete"] if explicit else []),
    )
    with pytest.raises(SystemExit) as error:
        audit.main()
    assert error.value.code == 1
    report = json.loads(
        (harness.root / ".artifacts/responsibility-register.json").read_text()
    )
    assert report["review_validation"]["unreviewed"] == 2


def test_pinned_reference_rejects_tracked_source_changes(harness, monkeypatch):
    checkout = harness.root / ".upstream/napari"
    checkout.mkdir(parents=True)
    monkeypatch.setattr(audit, "ROOT", harness.root)
    responses = iter((COMMIT + "\n", " M src/napari/model.py\n"))
    monkeypatch.setattr(
        audit.subprocess,
        "run",
        lambda *args, **kwargs: SimpleNamespace(stdout=next(responses)),
    )
    with pytest.raises(SystemExit, match="tracked modifications"):
        audit.pinned_source({"commit": COMMIT})
