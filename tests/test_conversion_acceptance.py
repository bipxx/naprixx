"""Fault injection for the M0 prerequisite gate, using synthetic evidence only."""

import copy
import hashlib
import json

import pytest

from tools import check_conversion_acceptance as gate


@pytest.fixture(scope="module")
def source_snapshot(tmp_path_factory):
    """Isolate a current source snapshot; never refresh the repository record."""
    tmp_path = tmp_path_factory.mktemp("acceptance-snapshot")
    document = json.loads(
        (gate.ROOT / "docs/conversion-acceptance.json").read_text(encoding="utf-8")
    )
    fingerprints = {}
    for item in document["source_evidence"]:
        content = (gate.ROOT / item["path"]).read_bytes()
        path = tmp_path / item["path"]
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        item["sha256_raw"] = hashlib.sha256(content).hexdigest()
        fingerprints[item["path"]] = item["sha256_raw"]
    for item in document["dependency_constraint_profiles"]:
        item["sha256_raw"] = fingerprints[item["path"]]
    for item in document["workload_contract"]["pinned_suite_inventory"]:
        item["sha256_raw"] = fingerprints[item["source"]]
    for workflow in document["required_workflows"]:
        for path in workflow["pinned_fixture_sources"]:
            if (gate.ROOT / path).is_dir():
                (tmp_path / path).mkdir(parents=True, exist_ok=True)
    path = tmp_path / "docs/conversion-acceptance.json"
    path.write_text(json.dumps(document), encoding="utf-8")
    return document, tmp_path


@pytest.fixture
def draft(source_snapshot):
    document, root = source_snapshot
    return copy.deepcopy(document), root


def validate(draft, *, frozen=False):
    document, root = draft
    return gate.validate_document(document, root, check_frozen=frozen)


def codes(report):
    return {issue["code"] for issue in report["errors"]}


def test_draft_is_valid_without_certifying_passes(draft):
    report = validate(draft)
    assert report["valid"] and report["exit_code"] == 0
    assert report["counts"]["configured_cells"] == 32
    assert report["blockers"]
    assert not report["freeze_prerequisites_met"]
    assert not report["frozen_verified"]
    assert not report["runtime_parity_proven"]
    assert not report["release_parity_proven"]


def test_check_frozen_rejects_valid_draft(draft):
    report = validate(draft, frozen=True)
    assert report["valid"] and report["exit_code"] == 1
    assert any(issue["code"] == "record_not_frozen" for issue in report["blockers"])


def test_frozen_claim_cannot_bypass_explicit_nulls(draft):
    document, _ = draft
    document.update(status="frozen", frozen=True)
    report = validate(draft)
    assert report["exit_code"] == 1
    assert "unproved_frozen_claim" in codes(report)
    assert any("native_qt_sdk_version" in b["path"] for b in report["blockers"])
    assert any(
        "reference_output_and_event_oracles" in b["path"] for b in report["blockers"]
    )
    assert any("performance_tolerances" in b["path"] for b in report["blockers"])


def test_renaming_all_choices_resolved_does_not_freeze_locks(draft):
    document, root = draft
    receipt = root / "synthetic-decision.json"
    receipt.write_text('{"test_only": true}', encoding="utf-8")
    evidence = {
        "path": receipt.relative_to(root).as_posix(),
        "sha256": hashlib.sha256(receipt.read_bytes()).hexdigest(),
    }
    for choice in document["unresolved_choices"]:
        choice.update(
            status="resolved",
            resolution="Synthetic test decision",
            resolution_evidence=evidence,
        )
    report = validate(draft)
    assert report["valid"]
    assert not any(b["code"] == "open_choice" for b in report["blockers"])
    assert any("exact_profile_case_map" in b["path"] for b in report["blockers"])
    assert not report["freeze_prerequisites_met"]


def test_open_choice_blocks_even_when_blocks_freeze_is_tampered(draft):
    document, _ = draft
    for choice in document["unresolved_choices"]:
        choice["blocks_freeze"] = False
    assert sum(b["code"] == "open_choice" for b in validate(draft)["blockers"]) == 12


@pytest.mark.parametrize(
    "axis,removed",
    [
        ("python_minors", "3.14"),
        ("os_families", "macOS"),
        ("qt_bindings", "PySide6"),
    ],
)
def test_required_support_axes_cannot_be_narrowed(draft, axis, removed):
    document, _ = draft
    document["supported_contract_facts"]["required_goal_axes"][axis].remove(removed)
    assert "missing_accounting" in codes(validate(draft))


def test_one_missing_configured_recipe_is_detected(draft):
    document, _ = draft
    del document["candidate_matrix"]["cells"][0]
    assert "missing_accounting" in codes(validate(draft))


def test_secondary_binding_cannot_be_relabelled_as_full_suite(draft):
    document, _ = draft
    cell = next(
        c
        for c in document["candidate_matrix"]["cells"]
        if c["configured_test_scope"] == "qapp-fixture subset"
    )
    cell["configured_test_scope"] = "upstream pytest profile using original fixtures"
    assert "configured_cell_mismatch" in codes(validate(draft))


@pytest.mark.parametrize(
    "collection", ["required_workflows", "unresolved_choices", "final_evidence_gates"]
)
def test_required_contract_items_cannot_be_deleted(draft, collection):
    document, _ = draft
    del document[collection][0]
    assert "missing_accounting" in codes(validate(draft))


def test_architecture_coverage_requires_applicability_decisions(draft):
    document, _ = draft
    document["supported_contract_facts"]["required_goal_axes"].update(
        architectures=["synthetic-architecture"],
        cross_product_applicability="Synthetic candidate decision, no runtime pass",
    )
    report = validate(draft)
    missing = [
        b for b in report["blockers"] if b["code"] == "unaccounted_applicability"
    ]
    assert len(missing) == 4 * 3 * 3
    assert all(b["detail"].startswith("Decide applicability:") for b in missing)


@pytest.mark.parametrize(
    "value", [True, False, "x86_64", {}, [True], ["x86_64", "x86_64"]]
)
def test_architecture_axis_rejects_fake_collections_and_duplicates(draft, value):
    document, _ = draft
    document["supported_contract_facts"]["required_goal_axes"]["architectures"] = value
    report = validate(draft)
    assert any(
        e["path"].startswith("required_goal_axes.architectures")
        and e["code"] == "schema"
        for e in report["errors"]
    )


@pytest.mark.parametrize("value", [True, False, [], 1])
def test_cross_product_policy_rejects_fake_types(draft, value):
    document, _ = draft
    document["supported_contract_facts"]["required_goal_axes"][
        "cross_product_applicability"
    ] = value
    assert any(
        e["path"] == "required_goal_axes.cross_product_applicability"
        and e["code"] == "schema"
        for e in validate(draft)["errors"]
    )


@pytest.mark.parametrize("value", [True, False])
@pytest.mark.parametrize(
    "section,field",
    [
        ("provider_contract", "exact_profile_case_map"),
        ("provider_contract", "platform_resolved_artifact_locks"),
        ("plugin_contract", "representative_plugin_artifacts"),
        ("plugin_contract", "legacy_engine_applicability"),
        ("full_fixture_contract", "expected_collection_per_cell"),
        ("full_fixture_contract", "baseline_skip_xfail_and_deselection_receipts"),
        ("full_fixture_contract", "full_suite_runner_command"),
        ("workload_contract", "end_to_end_workload_ids"),
    ],
)
def test_contract_collections_reject_bare_booleans(draft, section, field, value):
    document, _ = draft
    document[section][field] = value
    assert any(
        e["path"] == section + "." + field and e["code"] == "schema"
        for e in validate(draft)["errors"]
    )


@pytest.mark.parametrize("value", [True, False])
@pytest.mark.parametrize(
    "field",
    [
        "exact_case_ids",
        "input_artifact_hashes",
        "ordered_steps",
        "reference_output_and_event_oracles",
        "applicable_matrix_cell_ids",
    ],
)
def test_workflow_collections_reject_bare_booleans(draft, field, value):
    document, _ = draft
    workflow = document["required_workflows"][0]
    workflow[field] = value
    assert any(
        e["path"] == "required_workflows." + workflow["id"] + "." + field
        and e["code"] == "schema"
        for e in validate(draft)["errors"]
    )


@pytest.mark.parametrize("value", [True, False])
@pytest.mark.parametrize(
    "field", ["selected_acceptance_cases", "materialized_input_hashes"]
)
def test_benchmark_collections_reject_bare_booleans(draft, field, value):
    document, _ = draft
    benchmark = document["workload_contract"]["pinned_suite_inventory"][0]
    benchmark[field] = value
    assert any(
        e["path"]
        == "workload_contract.pinned_suite_inventory." + benchmark["id"] + "." + field
        and e["code"] == "schema"
        for e in validate(draft)["errors"]
    )


@pytest.mark.parametrize(
    "tamper",
    [
        "provider-entry",
        "provider-case",
        "provider-named-case",
        "plugin-entry",
        "workflow-case",
        "workflow-step",
        "workflow-artifact",
        "fixture-count",
        "fixture-receipt",
        "workload-id",
        "benchmark-case",
        "benchmark-artifact",
    ],
)
def test_collection_entries_reject_boolean_placeholders(draft, tamper):
    document, _ = draft
    workflow = document["required_workflows"][0]
    benchmark = document["workload_contract"]["pinned_suite_inventory"][0]
    if tamper == "provider-entry":
        document["provider_contract"]["exact_profile_case_map"] = {"NumPy": True}
    elif tamper == "provider-case":
        document["provider_contract"]["exact_profile_case_map"] = {"NumPy": [True]}
    elif tamper == "provider-named-case":
        document["provider_contract"]["exact_profile_case_map"] = {
            "NumPy": {"case": True}
        }
    elif tamper == "plugin-entry":
        document["plugin_contract"]["representative_plugin_artifacts"] = [True]
    elif tamper == "workflow-case":
        workflow["exact_case_ids"] = [True]
    elif tamper == "workflow-step":
        workflow["ordered_steps"] = [True]
    elif tamper == "workflow-artifact":
        workflow["input_artifact_hashes"] = {"input": True}
    elif tamper == "fixture-count":
        document["full_fixture_contract"]["expected_collection_per_cell"] = {
            "cell": True
        }
    elif tamper == "fixture-receipt":
        document["full_fixture_contract"][
            "baseline_skip_xfail_and_deselection_receipts"
        ] = [True]
    elif tamper == "workload-id":
        document["workload_contract"]["end_to_end_workload_ids"] = [True]
    elif tamper == "benchmark-case":
        benchmark["selected_acceptance_cases"] = [True]
    else:
        benchmark["materialized_input_hashes"] = [True]
    assert "schema" in codes(validate(draft))


@pytest.mark.parametrize("field", ["applicability", "required_for_release"])
def test_cell_decision_records_reject_wrong_types(draft, field):
    document, _ = draft
    document["candidate_matrix"]["cells"][0][field] = (
        True if field == "applicability" else "yes"
    )
    assert "schema" in codes(validate(draft))


@pytest.mark.parametrize(
    "section,field",
    [
        ("full_fixture_contract", "fixture_source_inventory_sha256"),
        ("workload_contract", "external_benchmark_input"),
    ],
)
def test_artifact_digests_reject_boolean_placeholders(draft, section, field):
    document, _ = draft
    if field == "external_benchmark_input":
        document[section][field]["sha256"] = True
    else:
        document[section][field] = True
    assert "schema" in codes(validate(draft))


def test_source_digest_uses_raw_bytes_including_newlines(draft):
    document, root = draft
    item = next(s for s in document["source_evidence"] if s["path"] == "pyproject.toml")
    path = root / item["path"]
    original = path.read_bytes()
    altered = (
        original.replace(b"\r\n", b"\n")
        if b"\r\n" in original
        else (original.replace(b"\n", b"\r\n"))
    )
    assert original != altered
    try:
        path.write_bytes(altered)
        assert "source_hash_mismatch" in codes(validate(draft))
    finally:
        path.write_bytes(original)


@pytest.mark.parametrize(
    "path", ["../outside.py", "/absolute.py", "C:/outside.py", "folder\\outside.py"]
)
def test_source_paths_cannot_escape_root(draft, path):
    document, _ = draft
    document["source_evidence"][0]["path"] = path
    assert "source_path" in codes(validate(draft))


def test_removed_source_fingerprint_is_not_trusted(draft):
    document, _ = draft
    source = document["dependency_constraint_profiles"][0]["path"]
    document["source_evidence"] = [
        s for s in document["source_evidence"] if s["path"] != source
    ]
    assert "source_reference" in codes(validate(draft))


def test_package_pins_cannot_be_fabricated_without_source_change(draft):
    document, _ = draft
    document["dependency_constraint_profiles"][0]["selected_exact_pins"]["numpy"] = (
        "0.0"
    )
    assert "constraint_pins" in codes(validate(draft))


def test_lock_needs_an_existing_matching_artifact(draft):
    document, _ = draft
    document["candidate_matrix"]["cells"][0]["compiler_toolchain_lock"] = {
        "path": "missing-lock.json",
        "sha256": "0" * 64,
    }
    assert "source_missing" in codes(validate(draft))


def test_nested_null_provider_choice_remains_a_blocker(draft):
    document, _ = draft
    document["provider_contract"]["exact_profile_case_map"] = {"NumPy": None}
    assert any(
        b["path"] == "provider_contract.exact_profile_case_map.NumPy"
        for b in validate(draft)["blockers"]
    )


def test_qt_sdk_must_equal_declared_runtime(draft):
    document, _ = draft
    cell = next(
        c for c in document["candidate_matrix"]["cells"] if c["binding"] == "PyQt6"
    )
    cell["native_qt_sdk_version"] = "6.5.0"
    assert "qt_sdk_runtime" in codes(validate(draft))


@pytest.mark.parametrize(
    "field",
    [
        "python_patch",
        "interpreter",
        "os_version",
        "architecture",
        "native_qt_sdk_version",
        "binding_package_version_from_constraints",
        "qt_runtime_package_version_from_constraints",
    ],
)
@pytest.mark.parametrize("value", [True, False, ["exact"], {"exact": "value"}])
def test_exact_lock_fields_reject_fake_primitive_types(draft, field, value):
    document, _ = draft
    cell = next(
        c for c in document["candidate_matrix"]["cells"] if c["binding"] == "PyQt6"
    )
    cell[field] = value
    report = validate(draft)
    assert "schema" in codes(report)
    assert any(e["path"].endswith("." + field) for e in report["errors"])


@pytest.mark.parametrize(
    "patch,code",
    [
        ("3.14", "lock_format"),
        ("3.13.8", "python_patch_minor"),
        ("3.14.fake", "lock_format"),
    ],
)
def test_python_patch_is_exact_and_matches_minor(draft, patch, code):
    document, _ = draft
    cell = next(
        c
        for c in document["candidate_matrix"]["cells"]
        if c["python_minor_requested"] == "3.14"
    )
    cell["python_patch"] = patch
    assert code in codes(validate(draft))


def test_bool_sdk_runtime_pair_cannot_evade_version_check(draft):
    document, _ = draft
    cell = next(
        c for c in document["candidate_matrix"]["cells"] if c["binding"] == "PyQt6"
    )
    cell.update(
        native_qt_sdk_version=True, qt_runtime_package_version_from_constraints=True
    )
    report = validate(draft)
    assert (
        sum(e["code"] == "schema" and "version" in e["path"] for e in report["errors"])
        == 2
    )


@pytest.mark.parametrize("value", [True, False, [0], 0, "resolved"])
def test_tolerance_policy_is_structured(draft, value):
    document, _ = draft
    document["workload_contract"]["performance_tolerances"] = value
    assert "schema" in codes(validate(draft))


@pytest.mark.parametrize("value", [True, "0", -1, float("inf")])
def test_numeric_tolerances_reject_bool_text_negative_and_nonfinite(draft, value):
    document, _ = draft
    document["workload_contract"]["numerical_tolerances"] = {"atol": value}
    assert "numeric_criterion" in codes(validate(draft))


def test_numeric_protocol_repetitions_do_not_accept_boolean(draft):
    document, _ = draft
    protocol = document["workload_contract"]["frozen_measurement_protocol"]
    protocol["warmup_repetitions_and_statistics"] = {"repetitions": True}
    assert "numeric_criterion" in codes(validate(draft))


@pytest.mark.parametrize(
    "tamper",
    ["completion", "milestone", "cell-pass", "gate-pass", "reports", "constraint-pass"],
)
def test_runtime_pass_and_completion_claims_are_not_inferred(draft, tamper):
    document, _ = draft
    if tamper == "completion":
        document["complete_application_port"] = True
    elif tamper == "milestone":
        document["milestone_status"]["M0"] = "complete"
    elif tamper == "cell-pass":
        document["candidate_matrix"]["cells"][0]["evidence_status"] = "passed"
    elif tamper == "gate-pass":
        document["final_evidence_gates"][0]["evidence_status"] = "verified"
    elif tamper == "reports":
        document["evidence_semantics"]["runtime_reports_accepted_by_this_record"] = [
            {"passed": 2398}
        ]
    else:
        document["dependency_constraint_profiles"][0][
            "constraint_is_execution_evidence"
        ] = True
    report = validate(draft)
    assert "runtime_claim" in codes(report)
    assert not report["runtime_parity_proven"] and not report["release_parity_proven"]


@pytest.mark.parametrize(
    "tamper",
    [
        "boolean-schema",
        "list-status",
        "list-binding",
        "bad-plugin-list",
        "null-fixture-paths",
    ],
)
def test_malformed_field_types_return_issues_without_crashing(draft, tamper):
    document, _ = draft
    if tamper == "boolean-schema":
        document["schema_version"] = True
    elif tamper == "list-status":
        document["status"] = []
    elif tamper == "list-binding":
        document["candidate_matrix"]["cells"][0]["binding"] = []
    elif tamper == "bad-plugin-list":
        document["plugin_contract"]["required_contribution_classes"] = [{}]
    else:
        document["required_workflows"][0]["pinned_fixture_sources"] = None
    assert validate(draft)["exit_code"] == 1


def test_duplicate_ids_are_rejected(draft):
    document, _ = draft
    document["candidate_matrix"]["cells"].append(
        copy.deepcopy(document["candidate_matrix"]["cells"][0])
    )
    assert "duplicate_id" in codes(validate(draft))


def test_zero_tolerance_is_not_an_unresolved_value(draft):
    document, _ = draft
    document["workload_contract"]["numerical_tolerances"] = {"atol": 0, "rtol": 0}
    report = validate(draft)
    assert not any(
        b["path"].startswith("workload_contract.numerical_tolerances")
        for b in report["blockers"]
    )


def test_cli_output_is_deterministic_json_and_distinguishes_freeze(draft, capsys):
    _, root = draft
    args = ["--root", str(root)]
    assert gate.main(args) == 0
    first = capsys.readouterr().out
    assert gate.main(args) == 0
    assert capsys.readouterr().out == first
    assert gate.main([*args, "--check-frozen"]) == 1
    frozen = json.loads(capsys.readouterr().out)
    assert frozen["mode"] == "check-frozen" and frozen["valid"]


@pytest.mark.parametrize(
    "text",
    [
        '{"schema_version":1,"schema_version":1}',
        '{"schema_version": NaN}',
        "[]",
        "{bad json",
    ],
)
def test_cli_rejects_invalid_json_objects(tmp_path, capsys, text):
    path = tmp_path / "input.json"
    path.write_text(text, encoding="utf-8")
    assert gate.main(["--root", str(tmp_path), "--acceptance", str(path)]) == 1
    assert json.loads(capsys.readouterr().out)["valid"] is False


def test_validator_never_mutates_acceptance_record(draft):
    document, _ = draft
    before = copy.deepcopy(document)
    validate(draft)
    assert document == before
