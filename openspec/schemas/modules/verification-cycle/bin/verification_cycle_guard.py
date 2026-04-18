#!/usr/bin/env python3
"""Validate verification-cycle run directories."""

from __future__ import annotations

import argparse
from datetime import datetime
import json
from pathlib import Path
from typing import Any

import jsonschema

CORE_CONTRACT_PATH = Path(__file__).resolve().parents[1] / "contracts/verification-cycle-core-v1.json"
AGENT_TABLE_CONTRACT_PATH = Path(__file__).resolve().parents[1] / "contracts/verification-cycle-agent-table-v1.json"


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def load_core_contract() -> dict[str, Any]:
    return load_json(CORE_CONTRACT_PATH)


def load_agent_table_contract() -> dict[str, Any]:
    return load_json(AGENT_TABLE_CONTRACT_PATH)


def _parse_datetime_value(value: Any) -> datetime | None:
    if not isinstance(value, str) or not value.strip():
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None


def valid_pass(agent: dict[str, Any]) -> bool:
    return (
        agent.get("last_verdict") == "pass"
        and agent.get("coverage_status") == "complete"
        and agent.get("exhaustive") is True
    )


def valid_pass_from_payloads(findings: dict[str, Any], evidence: dict[str, Any]) -> bool:
    if not isinstance(findings, dict) or not isinstance(evidence, dict):
        return False
    return (
        findings.get("verdict") == "pass"
        and (evidence.get("review_coverage") or {}).get("coverage_status") == "complete"
        and (evidence.get("review_coverage") or {}).get("exhaustive") is True
    )


def attempt_dirs(root: Path) -> list[Path]:
    return sorted(
        [path for path in root.iterdir() if path.is_dir() and path.name.startswith("attempt-")]
    )


def _resolve_subject_binding(
    payload: dict[str, Any], contract: dict[str, Any]
) -> tuple[list[str], str | None, Any]:
    allowed = contract.get("subject_required_any_of", [])
    keys = [key for key in allowed if payload.get(key)]
    if not keys:
        return ([f"Payload must include one subject key from {allowed}"], None, None)
    if len(keys) > 1:
        return ([f"Payload must not bind multiple subject keys: {keys}"], None, None)
    key = keys[0]
    return ([], key, payload.get(key))


def _require_object_payload(payload: Any, payload_path: Path, label: str) -> list[str]:
    if isinstance(payload, dict):
        return []
    return [f"{label} must be a JSON object in {payload_path}"]


def _validate_agent_table_payload(
    table: dict[str, Any], table_path: Path, contract: dict[str, Any], subject_keys: list[str], run_dir: Path
) -> tuple[list[str], str | None, Any]:
    errors: list[str] = []
    subject_errors, subject_key, subject_value = _resolve_subject_binding(
        table, {"subject_required_any_of": subject_keys}
    )
    for error in subject_errors:
        errors.append(f"{error} in {table_path}")

    validator = jsonschema.Draft202012Validator(
        contract,
        format_checker=jsonschema.Draft202012Validator.FORMAT_CHECKER,
    )
    for schema_error in validator.iter_errors(table):
        errors.append(f"agent-table schema violation in {table_path}: {schema_error.message}")

    agents = table.get("agents")
    if not isinstance(agents, list):
        errors.append(f"'agents' must be an array in {table_path}")
        return errors, subject_key, subject_value

    required_fields = set(
        contract["properties"]["agents"]["items"]["required"]
    )
    allowed_status = {"active", "non_active", "closed"}
    allowed_verdicts = {"block", "pass", "unknown"}
    allowed_coverage = {"complete", "partial", "unknown"}

    for index, agent in enumerate(agents):
        label = f"{table_path} agent #{index + 1}"
        if not isinstance(agent, dict):
            errors.append(f"{label} must be an object")
            continue

        missing = sorted(field for field in required_fields if field not in agent)
        if missing:
            errors.append(f"{label} missing required fields: {missing}")
            continue

        for field in {"agent_id", "findings_path", "verifier_evidence_path", "updated_at"}:
            value = agent.get(field)
            if not isinstance(value, str) or not value.strip():
                errors.append(f"{label} field {field!r} must be a non-empty string")
        updated_at = agent.get("updated_at")
        if isinstance(updated_at, str) and updated_at.strip():
            try:
                datetime.fromisoformat(updated_at.replace("Z", "+00:00"))
            except ValueError:
                errors.append(f"{label} field 'updated_at' must be a valid date-time string")

        if agent.get("status") not in allowed_status:
            errors.append(f"{label} field 'status' must be one of {sorted(allowed_status)}")
        if not isinstance(agent.get("resumable"), bool):
            errors.append(f"{label} field 'resumable' must be boolean")
        if agent.get("last_verdict") not in allowed_verdicts:
            errors.append(f"{label} field 'last_verdict' must be one of {sorted(allowed_verdicts)}")
        if agent.get("coverage_status") not in allowed_coverage:
            errors.append(f"{label} field 'coverage_status' must be one of {sorted(allowed_coverage)}")
        if not isinstance(agent.get("exhaustive"), bool):
            errors.append(f"{label} field 'exhaustive' must be boolean")
        if not isinstance(agent.get("scope"), str):
            errors.append(f"{label} field 'scope' must be a string")

        expected_filenames = {
            "findings_path": "findings.json",
            "verifier_evidence_path": "verifier-evidence.json",
        }
        for field in {"findings_path", "verifier_evidence_path"}:
            value = agent.get(field)
            if isinstance(value, str) and value.strip():
                resolved = (run_dir / value).resolve()
                try:
                    resolved.relative_to(run_dir.resolve())
                except ValueError:
                    errors.append(f"{label} field {field!r} must stay within the run dir")
                    continue
                if not resolved.exists():
                    errors.append(f"{label} field {field!r} must point to an existing file")
                    continue
                if not resolved.is_file():
                    errors.append(f"{label} field {field!r} must point to a file")
                    continue
                if resolved.name != expected_filenames[field]:
                    errors.append(
                        f"{label} field {field!r} must point to {expected_filenames[field]!r}"
                    )

    return errors, subject_key, subject_value


def _validate_required_evidence_fields(
    evidence: dict[str, Any], evidence_path: Path, contract: dict[str, Any]
) -> list[str]:
    errors: list[str] = []
    for field in contract.get("verifier_evidence_required", []):
        if field not in evidence:
            errors.append(f"Missing required verifier evidence field {field!r} in {evidence_path}")
            continue
        value = evidence[field]
        if isinstance(value, str) and not value.strip():
            errors.append(f"Verifier evidence field {field!r} must be non-empty in {evidence_path}")
        elif field in {"review_scope", "review_coverage"} and not isinstance(value, dict):
            errors.append(f"Verifier evidence field {field!r} must be an object in {evidence_path}")
        elif field in {"reviewed_paths", "skipped_paths", "reviewed_axes", "unreviewed_axes"} and not isinstance(value, list):
            errors.append(f"Verifier evidence field {field!r} must be an array in {evidence_path}")
    return errors


def _validate_evidence_semantics(
    evidence: dict[str, Any], evidence_path: Path, contract: dict[str, Any]
) -> list[str]:
    errors: list[str] = []
    required_template_id = contract.get("verifier_template_id_const")
    if required_template_id and evidence.get("template_id") != required_template_id:
        errors.append(
            f"Verifier evidence template_id must be {required_template_id!r} in {evidence_path}"
        )

    allowed_final_states = set(contract.get("verifier_final_state_allowed", []))
    final_state = evidence.get("final_state")
    if allowed_final_states and final_state not in allowed_final_states:
        errors.append(
            f"Verifier evidence final_state must be one of {sorted(allowed_final_states)} in {evidence_path}"
        )

    timestamp_fields = contract.get("verifier_timestamp_fields", [])
    parsed_timestamps: dict[str, datetime] = {}
    for field in timestamp_fields:
        value = evidence.get(field)
        parsed = _parse_datetime_value(value)
        if parsed is None:
            errors.append(
                f"Verifier evidence {field} must be a valid date-time string in {evidence_path}"
            )
            continue
        parsed_timestamps[field] = parsed
    if {"start_at", "end_at"} <= parsed_timestamps.keys():
        if parsed_timestamps["end_at"] < parsed_timestamps["start_at"]:
            errors.append(
                f"Verifier evidence end_at must be greater than or equal to start_at in {evidence_path}"
            )

    required_review_goal = (contract.get("invocation_common_constants") or {}).get("review_goal")
    if required_review_goal and evidence.get("review_goal") != required_review_goal:
        errors.append(
            f"Verifier evidence review_goal must be {required_review_goal!r} in {evidence_path}"
        )

    allowed_review_phases = set(contract.get("review_phase_allowed", []))
    review_phase = evidence.get("review_phase")
    if review_phase not in allowed_review_phases:
        errors.append(
            f"Verifier evidence review_phase must be one of {sorted(allowed_review_phases)} in {evidence_path}"
        )

    allowed_findings_contracts = set(contract.get("findings_contract_allowed", []))
    findings_contract = evidence.get("findings_contract")
    if findings_contract not in allowed_findings_contracts:
        errors.append(
            f"Verifier evidence findings_contract must be one of {sorted(allowed_findings_contracts)} in {evidence_path}"
        )
    allowed_axes = set(contract.get("review_axes_allowed", []))
    reviewed_axes = evidence.get("reviewed_axes")
    unreviewed_axes = evidence.get("unreviewed_axes")
    if isinstance(reviewed_axes, list):
        invalid = sorted(axis for axis in reviewed_axes if axis not in allowed_axes)
        if invalid:
            errors.append(
                f"Verifier evidence reviewed_axes contains invalid values {invalid!r} in {evidence_path}"
            )
    if isinstance(unreviewed_axes, list):
        invalid = sorted(axis for axis in unreviewed_axes if axis not in allowed_axes)
        if invalid:
            errors.append(
                f"Verifier evidence unreviewed_axes contains invalid values {invalid!r} in {evidence_path}"
            )
    if isinstance(reviewed_axes, list) and isinstance(unreviewed_axes, list):
        overlap = sorted(set(reviewed_axes) & set(unreviewed_axes))
        if overlap:
            errors.append(f"Verifier evidence axes must not overlap {overlap!r} in {evidence_path}")
    return errors


def _validate_finding_object(
    finding: dict[str, Any], finding_label: str, contract: dict[str, Any], review_phase: Any
) -> list[str]:
    errors: list[str] = []
    for field in contract.get("finding_non_empty_string_fields", []):
        value = finding.get(field)
        if not isinstance(value, str) or not value.strip():
            errors.append(f"{finding_label} field {field!r} must be a non-empty string")

    allowed_severities = set(contract.get("finding_severity_allowed", []))
    if finding.get("severity") not in allowed_severities:
        errors.append(
            f"{finding_label} severity must be one of {sorted(allowed_severities)}"
        )

    allowed_dimensions = set(contract.get("finding_dimension_allowed", []))
    if finding.get("dimension") not in allowed_dimensions:
        errors.append(
            f"{finding_label} dimension must be one of {sorted(allowed_dimensions)}"
        )

    for field in contract.get("finding_boolean_fields", []):
        if not isinstance(finding.get(field), bool):
            errors.append(f"{finding_label} field {field!r} must be boolean")

    blocking = finding.get("blocking")
    auto_fixable = finding.get("auto_fixable")
    severity = finding.get("severity")
    if auto_fixable is True and blocking is not True:
        errors.append(f"{finding_label} invalid: auto_fixable=true requires blocking=true")
    if severity == "SUGGESTION" and blocking is not False:
        errors.append(f"{finding_label} invalid: SUGGESTION must keep blocking=false")
    if review_phase == "docs_first" and auto_fixable is True:
        errors.append(f"{finding_label} invalid: docs_first findings must keep auto_fixable=false")
    return errors


def validate_run_dir(run_dir: Path) -> list[str]:
    errors: list[str] = []
    core_contract = load_core_contract()
    agent_table_contract = load_agent_table_contract()
    agent_table_path = run_dir / "agent-table.json"
    if not agent_table_path.exists():
        return [f"Missing agent-table.json: {agent_table_path}"]

    table = load_json(agent_table_path)
    errors.extend(_require_object_payload(table, agent_table_path, "agent-table.json"))
    if not isinstance(table, dict):
        return errors
    table_errors, table_subject_key, table_subject_value = _validate_agent_table_payload(
        table,
        agent_table_path,
        agent_table_contract,
        core_contract.get("subject_required_any_of", []),
        run_dir,
    )
    errors.extend(table_errors)
    if table.get("contract") != "verification-cycle-agent-table-v1":
        errors.append("agent-table.json must declare contract=verification-cycle-agent-table-v1")
        return errors

    agents = table.get("agents") or []
    active = [agent for agent in agents if agent.get("status") == "active"]
    if len(active) > 1:
        errors.append("At most one active agent is allowed")

    for agent in agents:
        if agent.get("status") == "non_active" and agent.get("last_verdict") != "pass":
            errors.append(f"non_active agent must have pass verdict: {agent.get('agent_id')}")
        if agent.get("status") == "closed" and agent.get("last_verdict") == "pass":
            errors.append(f"closed agent pass cannot substitute for non_active: {agent.get('agent_id')}")
        if agent.get("coverage_status") == "partial" and not str(agent.get("scope", "")).strip():
            errors.append(f"partial agent must declare scope: {agent.get('agent_id')}")

    attempts = attempt_dirs(run_dir)
    if not attempts:
        errors.append("Run dir must contain at least one attempt-* directory")
        return errors

    for attempt in attempts:
        findings_path = attempt / "findings.json"
        evidence_path = attempt / "verifier-evidence.json"
        if not findings_path.exists():
            errors.append(f"Missing findings.json: {findings_path}")
            continue
        if not evidence_path.exists():
            errors.append(f"Missing verifier-evidence.json: {evidence_path}")
            continue
        findings = load_json(findings_path)
        evidence = load_json(evidence_path)
        errors.extend(_require_object_payload(findings, findings_path, "findings.json"))
        errors.extend(_require_object_payload(evidence, evidence_path, "verifier-evidence.json"))
        if not isinstance(findings, dict) or not isinstance(evidence, dict):
            continue
        verdict = findings.get("verdict")
        coverage = (evidence.get("review_coverage") or {}).get("coverage_status")
        exhaustive = (evidence.get("review_coverage") or {}).get("exhaustive")
        scope = (evidence.get("review_scope") or {}).get("scope", "")
        findings_subject_errors, findings_subject_key, findings_subject_value = _resolve_subject_binding(
            findings, core_contract
        )
        evidence_subject_contract = {
            "subject_required_any_of": core_contract.get(
                "verifier_evidence_subject_required_any_of",
                core_contract.get("subject_required_any_of", []),
            )
        }
        evidence_subject_errors, evidence_subject_key, evidence_subject_value = _resolve_subject_binding(
            evidence, evidence_subject_contract
        )
        for error in findings_subject_errors:
            errors.append(f"{error} in {findings_path}")
        for error in evidence_subject_errors:
            errors.append(f"{error} in {evidence_path}")
        if findings_subject_key and evidence_subject_key:
            if findings_subject_key != evidence_subject_key:
                errors.append(
                    f"Findings/evidence subject key mismatch in {attempt}: {findings_subject_key} != {evidence_subject_key}"
                )
            if findings_subject_value != evidence_subject_value:
                errors.append(
                    f"Findings/evidence subject value mismatch in {attempt}: {findings_subject_value!r} != {evidence_subject_value!r}"
                )
        if table_subject_key and findings_subject_key:
            if table_subject_key != findings_subject_key:
                errors.append(
                    f"agent-table/findings subject key mismatch in {attempt}: {table_subject_key} != {findings_subject_key}"
                )
            if table_subject_value != findings_subject_value:
                errors.append(
                    f"agent-table/findings subject value mismatch in {attempt}: {table_subject_value!r} != {findings_subject_value!r}"
                )
        errors.extend(_validate_required_evidence_fields(evidence, evidence_path, core_contract))
        errors.extend(_validate_evidence_semantics(evidence, evidence_path, core_contract))
        if verdict not in {"block", "pass"}:
            errors.append(f"Invalid verdict in {findings_path}")
        findings_items = findings.get("findings")
        if not isinstance(findings_items, list):
            errors.append(f"findings must be an array in {findings_path}")
            continue
        has_blocking_finding = False
        seen_finding_ids: set[str] = set()
        for index, finding in enumerate(findings_items):
            if not isinstance(finding, dict):
                errors.append(f"finding #{index + 1} must be an object in {findings_path}")
                continue
            finding_id = finding.get("id")
            if isinstance(finding_id, str):
                if finding_id in seen_finding_ids:
                    errors.append(
                        f"duplicate finding id {finding_id!r} in {findings_path}"
                    )
                seen_finding_ids.add(finding_id)
            if finding.get("blocking") is True:
                has_blocking_finding = True
            errors.extend(
                _validate_finding_object(
                    finding,
                    f"{findings_path} finding #{index + 1}",
                    core_contract,
                    evidence.get("review_phase"),
                )
            )
        if verdict == "pass" and has_blocking_finding:
            errors.append(f"pass verdict cannot contain blocking findings in {findings_path}")
        if coverage == "partial" and not str(scope).strip():
            errors.append(f"Partial verification must declare scope in {evidence_path}")
        if verdict == "pass" and (coverage != "complete" or exhaustive is not True):
            errors.append(f"Pass requires complete+exhaustive review in {evidence_path}")
        reviewed_axes = evidence.get("reviewed_axes")
        unreviewed_axes = evidence.get("unreviewed_axes")
        if verdict == "pass" and exhaustive is True and isinstance(reviewed_axes, list) and isinstance(unreviewed_axes, list):
            allowed_axes = set(core_contract.get("review_axes_allowed", []))
            if unreviewed_axes:
                errors.append(f"Exhaustive pass must keep unreviewed_axes empty in {evidence_path}")
            if set(reviewed_axes) != allowed_axes:
                errors.append(f"Exhaustive pass must cover all review axes in {evidence_path}")

    for agent in agents:
        findings_ref = agent.get("findings_path")
        evidence_ref = agent.get("verifier_evidence_path")
        if not isinstance(findings_ref, str) or not findings_ref.strip():
            continue
        if not isinstance(evidence_ref, str) or not evidence_ref.strip():
            continue
        findings_path = (run_dir / findings_ref).resolve()
        evidence_path = (run_dir / evidence_ref).resolve()
        if not findings_path.exists() or not findings_path.is_file():
            continue
        if not evidence_path.exists() or not evidence_path.is_file():
            continue
        findings_payload = load_json(findings_path)
        evidence_payload = load_json(evidence_path)
        if not isinstance(findings_payload, dict) or not isinstance(evidence_payload, dict):
            continue
        row_label = f"agent-table row {agent.get('agent_id')!r}"
        findings_subject_errors, findings_subject_key, findings_subject_value = _resolve_subject_binding(
            findings_payload, core_contract
        )
        evidence_subject_contract = {
            "subject_required_any_of": core_contract.get(
                "verifier_evidence_subject_required_any_of",
                core_contract.get("subject_required_any_of", []),
            )
        }
        evidence_subject_errors, evidence_subject_key, evidence_subject_value = _resolve_subject_binding(
            evidence_payload, evidence_subject_contract
        )
        for error in findings_subject_errors + evidence_subject_errors:
            errors.append(f"{row_label} referenced payload error: {error}")
        if table_subject_key and findings_subject_key and table_subject_key != findings_subject_key:
            errors.append(f"{row_label} subject key must match referenced findings payload")
        if table_subject_value is not None and findings_subject_value != table_subject_value:
            errors.append(f"{row_label} subject value must match referenced findings payload")
        if findings_subject_key and evidence_subject_key and findings_subject_key != evidence_subject_key:
            errors.append(f"{row_label} findings/evidence subject keys must match")
        if findings_subject_value != evidence_subject_value:
            errors.append(f"{row_label} findings/evidence subject values must match")
        if evidence_payload.get("agent_id") != agent.get("agent_id"):
            errors.append(f"{row_label} agent_id must match referenced evidence")
        if findings_payload.get("verdict") != agent.get("last_verdict"):
            errors.append(f"{row_label} last_verdict must match referenced findings")
        row_coverage = (evidence_payload.get("review_coverage") or {}).get("coverage_status")
        row_exhaustive = (evidence_payload.get("review_coverage") or {}).get("exhaustive")
        row_scope = (evidence_payload.get("review_scope") or {}).get("scope", "")
        if row_coverage != agent.get("coverage_status"):
            errors.append(f"{row_label} coverage_status must match referenced evidence")
        if row_exhaustive is not agent.get("exhaustive"):
            errors.append(f"{row_label} exhaustive must match referenced evidence")
        if str(row_scope) != str(agent.get("scope")):
            errors.append(f"{row_label} scope must match referenced evidence")

    if active:
        terminal_agent = active[0]
        findings_path = (run_dir / terminal_agent["findings_path"]).resolve()
        evidence_path = (run_dir / terminal_agent["verifier_evidence_path"]).resolve()
        if not findings_path.exists() or not evidence_path.exists():
            errors.append("Termination requires the active agent to reference existing findings/evidence artifacts")
        else:
            findings_payload = load_json(findings_path)
            evidence_payload = load_json(evidence_path)
            errors.extend(_require_object_payload(findings_payload, findings_path, "terminal findings.json"))
            errors.extend(_require_object_payload(evidence_payload, evidence_path, "terminal verifier-evidence.json"))
            if not valid_pass_from_payloads(findings_payload, evidence_payload):
                errors.append("Termination requires the active agent to hold a valid pass in referenced artifacts")
        if findings_path.exists() and evidence_path.exists() and not valid_pass(terminal_agent):
            errors.append("Termination requires the active agent to hold a valid pass")
    else:
        errors.append("Run dir must end with one active terminal agent")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate verification-cycle run directories.")
    parser.add_argument("--run-dir", required=True, help="Path to a verification-cycle run directory")
    args = parser.parse_args()
    run_dir = Path(args.run_dir)
    errors = validate_run_dir(run_dir)
    if errors:
        print("verification-cycle validation failed:")
        for error in errors:
            print(f"- {error}")
        return 1
    print("verification-cycle validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
