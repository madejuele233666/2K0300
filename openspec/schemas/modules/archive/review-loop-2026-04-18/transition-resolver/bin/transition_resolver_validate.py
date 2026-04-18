#!/usr/bin/env python3
"""
Validate transition-resolver decision payloads against the shared v1 contract.

The validator is intentionally mechanical:
- it validates shape and enums
- it validates decision-specific cross-field constraints
- it does not decide whether reviewer findings are correct
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve()
MODULE_ROOT = SCRIPT.parents[1]
CONTRACTS_DIR = MODULE_ROOT / "contracts"
ROUTING_PATH = CONTRACTS_DIR / "transition-resolver-routing-v1.json"
DECISION_SCHEMA_PATH = CONTRACTS_DIR / "transition-resolver-decision-v1.json"


def load_json_object(path: Path) -> tuple[dict | None, list[str]]:
    try:
        data = json.loads(path.read_text())
    except Exception as exc:
        return None, [f"JSON parse failed: {path}: {exc}"]
    if not isinstance(data, dict):
        return None, [f"Expected JSON object in {path}"]
    return data, []


def require_string(data: dict, key: str, errors: list[str]) -> None:
    value = data.get(key)
    if not isinstance(value, str) or not value:
        errors.append(f"Expected non-empty string field {key!r}")


def require_bool(data: dict, key: str, errors: list[str]) -> None:
    if not isinstance(data.get(key), bool):
        errors.append(f"Expected boolean field {key!r}")


def require_string_list(data: dict, key: str, errors: list[str]) -> None:
    value = data.get(key)
    if not isinstance(value, list):
        errors.append(f"Expected array field {key!r}")
        return
    if any(not isinstance(item, str) or not item for item in value):
        errors.append(f"Expected {key!r} to contain non-empty strings only")
    if len(set(value)) != len(value):
        errors.append(f"Expected {key!r} to contain unique values only")


def validate_shape(data: dict, routing: dict) -> list[str]:
    errors: list[str] = []
    required_fields = [
        "contract",
        "decision",
        "decision_class",
        "reason_code",
        "required_session_mode",
        "required_invocation_mode",
        "reroute_destination",
        "reroute_class",
        "required_evidence_checks",
        "required_path_checks",
        "required_output_artifacts",
        "authorized",
        "blocking",
        "required_mechanical_gate",
        "mechanical_gate_status",
    ]
    for field in required_fields:
        if field not in data:
            errors.append(f"Missing required field {field!r}")

    allowed_fields = set(required_fields + ["denial_reason"])
    extra = sorted(set(data) - allowed_fields)
    if extra:
        errors.append(f"Unexpected fields present: {', '.join(extra)}")

    if data.get("contract") != "transition-resolver-decision-v1":
        errors.append("Expected contract=transition-resolver-decision-v1")

    require_string(data, "reason_code", errors)
    require_bool(data, "authorized", errors)
    require_bool(data, "blocking", errors)
    require_string_list(data, "required_evidence_checks", errors)
    require_string_list(data, "required_path_checks", errors)
    require_string_list(data, "required_output_artifacts", errors)

    enum_fields = {
        "decision": "decision_enum",
        "decision_class": "decision_classes",
        "required_session_mode": "session_modes",
        "required_invocation_mode": "invocation_modes",
        "reroute_destination": "reroute_destinations",
        "reroute_class": "reroute_classes",
        "required_mechanical_gate": "mechanical_gates",
        "mechanical_gate_status": "mechanical_gate_statuses",
    }
    for field, routing_key in enum_fields.items():
        value = data.get(field)
        allowed = routing.get(routing_key, [])
        if value not in allowed:
            errors.append(f"Unexpected {field}={value!r}; allowed={allowed!r}")

    output_artifacts = data.get("required_output_artifacts")
    allowed_artifacts = set(routing.get("output_artifact_names", []))
    if isinstance(output_artifacts, list):
        for item in output_artifacts:
            if item not in allowed_artifacts:
                errors.append(
                    f"Unexpected required_output_artifacts entry {item!r}; allowed={sorted(allowed_artifacts)!r}"
                )

    return errors


def validate_decision_rules(data: dict) -> list[str]:
    errors: list[str] = []
    decision = data.get("decision")
    outputs = set(data.get("required_output_artifacts", [])) if isinstance(
        data.get("required_output_artifacts"), list
    ) else set()

    expected = {
        "spawn_initial_working": {
            "decision_class": "allow",
            "required_session_mode": "initial_working",
            "required_invocation_mode": "built_in_subagent_spawn",
            "authorized": True,
            "required_outputs": {"spawn-decision.json"},
        },
        "send_input_same_working": {
            "decision_class": "allow",
            "required_session_mode": "reuse_working_same_session",
            "required_invocation_mode": "built_in_session_message",
            "required_outputs": set(),
        },
        "resume_same_working": {
            "decision_class": "allow",
            "required_session_mode": "reuse_working_same_session",
            "required_invocation_mode": "built_in_session_resume",
            "required_outputs": set(),
        },
        "spawn_fresh_challenger": {
            "decision_class": "allow",
            "required_session_mode": "fresh_challenger",
            "required_invocation_mode": "built_in_subagent_spawn",
            "authorized": True,
            "required_outputs": {"spawn-decision.json"},
        },
        "record_challenger_reopen": {
            "decision_class": "allow",
            "required_session_mode": "promoted_challenger_to_working",
            "required_invocation_mode": "no_invocation",
            "required_outputs": {"reopen-record.json"},
        },
        "spawn_exception_working": {
            "decision_class": "allow",
            "required_session_mode": "fresh_working_exception",
            "required_invocation_mode": "built_in_subagent_spawn",
            "authorized": True,
            "required_outputs": {"spawn-decision.json"},
        },
        "enter_repair": {
            "decision_class": "allow",
            "required_session_mode": "no_session_change",
            "required_invocation_mode": "no_invocation",
        },
        "return_caller_flow": {
            "decision_class": "allow",
            "required_session_mode": "no_session_change",
            "required_invocation_mode": "no_invocation",
            "reroute_destination": "return_caller_flow",
        },
        "wait_for_user": {
            "decision_class": "allow",
            "required_session_mode": "no_session_change",
            "required_invocation_mode": "no_invocation",
        },
        "allow_close": {
            "decision_class": "allow",
            "required_session_mode": "no_session_change",
            "required_invocation_mode": "no_invocation",
            "reroute_destination": "none",
            "required_mechanical_gate": "review_loop_guard_run_dir",
            "mechanical_gate_status": "passed",
            "blocking": False,
            "authorized": True,
        },
        "deny_close": {
            "decision_class": "deny",
            "required_session_mode": "no_session_change",
            "required_invocation_mode": "no_invocation",
            "reroute_destination": "none",
        },
        "blocked": {
            "decision_class": "deny",
            "required_session_mode": "no_session_change",
            "required_invocation_mode": "no_invocation",
        },
    }

    if decision not in expected:
        return [f"Unsupported decision {decision!r}"]

    rules = expected[decision]
    for field, expected_value in rules.items():
        if field == "required_outputs":
            if outputs != expected_value:
                errors.append(
                    f"Expected required_output_artifacts={sorted(expected_value)!r} for decision={decision}"
                )
            continue
        if data.get(field) != expected_value:
            errors.append(
                f"Expected {field}={expected_value!r} for decision={decision}; got {data.get(field)!r}"
            )

    if decision == "enter_repair":
        if data.get("reroute_destination") not in {
            "openspec-repair-change",
            "docs_artifact_repair",
            "source_repair",
        }:
            errors.append(
                "enter_repair must reroute to openspec-repair-change, docs_artifact_repair, or source_repair"
            )
        if data.get("blocking") is not True:
            errors.append("enter_repair must set blocking=true")

    if decision == "wait_for_user" and data.get("reroute_class") != "caller_pause":
        errors.append("wait_for_user must set reroute_class=caller_pause")

    if decision == "blocked":
        denial_reason = data.get("denial_reason")
        if not isinstance(denial_reason, str) or not denial_reason:
            errors.append("blocked decisions must include denial_reason")

    if outputs and decision in {
        "send_input_same_working",
        "resume_same_working",
        "enter_repair",
        "return_caller_flow",
        "wait_for_user",
        "allow_close",
        "deny_close",
        "blocked",
    }:
        errors.append(f"{decision} must not require output artifacts")

    if data.get("mechanical_gate_status") == "passed" and data.get(
        "required_mechanical_gate"
    ) == "none":
        errors.append("mechanical_gate_status=passed requires a non-none required_mechanical_gate")

    return errors


def validate_file(path: Path) -> list[str]:
    routing, routing_errors = load_json_object(ROUTING_PATH)
    if routing_errors:
        return routing_errors
    decision, decision_errors = load_json_object(path)
    if decision_errors:
        return decision_errors
    assert routing is not None
    assert decision is not None
    return validate_shape(decision, routing) + validate_decision_rules(decision)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, help="Path to a transition-resolver decision JSON file")
    parser.add_argument(
        "--print-contract-paths",
        action="store_true",
        help="Print the authoritative contract paths before validation",
    )
    args = parser.parse_args()

    if args.print_contract_paths:
        print(DECISION_SCHEMA_PATH)
        print(ROUTING_PATH)

    errors = validate_file(Path(args.input).resolve())
    if errors:
        print("transition-resolver validation failed:")
        for error in errors:
            print(f"- {error}")
        return 1

    print("transition-resolver validation passed.")
    print(f"input={Path(args.input).resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
