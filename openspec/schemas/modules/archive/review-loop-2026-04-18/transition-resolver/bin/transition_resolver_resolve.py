#!/usr/bin/env python3
"""
Resolve normalized review-loop orchestration state into a deterministic
transition-resolver decision payload.
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

SCRIPT = Path(__file__).resolve()
MODULE_ROOT = SCRIPT.parents[1]
CONTRACTS_DIR = MODULE_ROOT / "contracts"
INPUT_CONTRACT_PATH = CONTRACTS_DIR / "transition-resolver-input-v1.json"
ROUTING_PATH = CONTRACTS_DIR / "transition-resolver-routing-v1.json"
WORKING_SESSION_CONTRACT_PATH = CONTRACTS_DIR / "working-session-normalization-v1.json"

ALLOWED_EXCEPTION_REASONS = {
    "",
    "agent_not_found",
    "session_completed_unresumable",
    "tooling_recovery",
}
ALLOWED_RESUME_PROBE_RESULTS = {
    "not_attempted",
    "resumable",
    "unresumable",
    "agent_not_found",
    "tooling_error",
}


def load_json_object(path: Path) -> tuple[dict[str, Any] | None, list[str]]:
    try:
        data = json.loads(path.read_text())
    except Exception as exc:
        return None, [f"JSON parse failed: {path}: {exc}"]
    if not isinstance(data, dict):
        return None, [f"Expected JSON object in {path}"]
    return data, []


def iso_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def unique_strings(values: list[str]) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for value in values:
        if value not in seen:
            seen.add(value)
            result.append(value)
    return result


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def as_bool(value: Any) -> bool | None:
    return value if isinstance(value, bool) else None


def as_int(value: Any) -> int | None:
    return value if isinstance(value, int) and not isinstance(value, bool) else None


def as_str(value: Any) -> str | None:
    return value if isinstance(value, str) and value else None


def as_raw_str(value: Any) -> str | None:
    return value if isinstance(value, str) else None


def normalized_path(path: str | None) -> str:
    return (path or "").replace("\\", "/")


def path_has_role(path: str | None, role: str) -> bool:
    normalized = normalized_path(path)
    return f"/{role}/" in normalized or normalized.startswith(f"{role}/")


def paths_match_role(paths: list[str | None], role: str) -> bool:
    return all(path_has_role(path, role) for path in paths if path)


def validate_input_shape(data: dict[str, Any]) -> list[str]:
    contract_data, contract_errors = load_json_object(INPUT_CONTRACT_PATH)
    if contract_errors:
        return contract_errors
    assert contract_data is not None

    errors: list[str] = []
    if data.get("contract") != "transition-resolver-input-v1":
        errors.append("Expected contract=transition-resolver-input-v1")

    intent = data.get("intent")
    allowed_intents = {
        "working_entry",
        "working_rerun",
        "post_working",
        "challenger_entry",
        "post_challenger",
        "close",
    }
    if intent not in allowed_intents:
        errors.append(f"Unexpected intent={intent!r}; allowed={sorted(allowed_intents)!r}")

    required_objects = [
        "subject",
        "session",
        "review_result",
        "predecessor",
        "reopen",
        "caller",
        "authorization",
        "invocation",
        "paths",
        "mechanical_gate",
    ]
    for key in required_objects:
        if not isinstance(data.get(key), dict):
            errors.append(f"Expected object field {key!r}")

    def validate_required_fields(value: Any, contract_node: Any, prefix: str) -> None:
        if not isinstance(value, dict) or not isinstance(contract_node, dict):
            return
        for required_key in contract_node.get("required", []):
            if required_key not in value:
                errors.append(f"Missing required field {prefix}{required_key!r}")
        for key, child_contract in contract_node.items():
            if key == "required":
                continue
            if isinstance(child_contract, dict):
                child_value = value.get(key)
                if isinstance(child_value, dict):
                    validate_required_fields(child_value, child_contract, f"{prefix}{key}.")

    for key in required_objects:
        validate_required_fields(data.get(key), contract_data.get(key), f"{key}.")
    return errors


def working_session_normalization_error(
    *,
    intent: str,
    active_working_agent_id: str | None,
    session_open: bool | None,
    resumable: bool | None,
    exception_reason: str | None,
    send_input_ready: bool | None,
    resume_probe_attempted: bool | None,
    resume_probe_result: str | None,
) -> str | None:
    if exception_reason not in ALLOWED_EXCEPTION_REASONS:
        allowed = ",".join(sorted(reason or '""' for reason in ALLOWED_EXCEPTION_REASONS))
        return f"exception_reason must be one of {{{allowed}}}"
    if resume_probe_result not in ALLOWED_RESUME_PROBE_RESULTS:
        allowed = ",".join(sorted(ALLOWED_RESUME_PROBE_RESULTS))
        return f"resume_probe_result must be one of {{{allowed}}}"
    if session_open is None:
        return "session_open must be boolean"
    if resumable is None:
        return "resumable must be boolean"
    if send_input_ready is None:
        return "send_input_ready must be boolean"
    if resume_probe_attempted is None:
        return "resume_probe_attempted must be boolean"
    if active_working_agent_id:
        if send_input_ready is True and session_open is not True:
            return "send_input_ready=true requires session_open=true"
        if session_open is True:
            if send_input_ready is not True:
                return "session_open=true requires send_input_ready=true"
            if resumable is True:
                return "session_open=true forbids resumable=true"
            if exception_reason:
                return "session_open=true forbids exception_reason"
        if resumable is True:
            if resume_probe_attempted is not True or resume_probe_result != "resumable":
                return "resumable=true requires resume_probe_attempted=true and resume_probe_result=resumable"
            if exception_reason:
                return "resumable=true forbids exception_reason"
        if exception_reason == "session_completed_unresumable":
            if resume_probe_attempted is not True or resume_probe_result != "unresumable":
                return "session_completed_unresumable requires resume_probe_attempted=true and resume_probe_result=unresumable"
        if exception_reason == "agent_not_found":
            if resume_probe_attempted is not True or resume_probe_result != "agent_not_found":
                return "agent_not_found requires resume_probe_attempted=true and resume_probe_result=agent_not_found"
        if exception_reason == "tooling_recovery":
            if resume_probe_attempted is not True or resume_probe_result != "tooling_error":
                return "tooling_recovery requires resume_probe_attempted=true and resume_probe_result=tooling_error"
        if not exception_reason and resume_probe_attempted is True and resume_probe_result in {
            "unresumable",
            "agent_not_found",
            "tooling_error",
        }:
            return "failed resume probe requires a matching exception_reason"
        if resume_probe_attempted is False and resume_probe_result != "not_attempted":
            return "resume_probe_attempted=false requires resume_probe_result=not_attempted"
        return None

    if send_input_ready is True:
        return "send_input_ready=true requires active_working_agent_id"
    if resume_probe_attempted is True:
        return "resume_probe_attempted=true requires active_working_agent_id"
    if resumable is True:
        return "resumable=true requires active_working_agent_id"
    if exception_reason:
        return "exception_reason requires active_working_agent_id"
    if resume_probe_result != "not_attempted":
        return "resume_probe_result must be not_attempted when active_working_agent_id is empty"
    return None


def make_decision(
    *,
    decision: str,
    reason_code: str,
    required_session_mode: str,
    required_invocation_mode: str,
    reroute_destination: str = "none",
    reroute_class: str = "none",
    required_evidence_checks: list[str] | None = None,
    required_path_checks: list[str] | None = None,
    required_output_artifacts: list[str] | None = None,
    authorized: bool = True,
    blocking: bool = False,
    required_mechanical_gate: str = "none",
    mechanical_gate_status: str = "not_required",
    denial_reason: str | None = None,
) -> dict[str, Any]:
    decision_class = "deny" if decision in {"deny_close", "blocked"} else "allow"
    payload: dict[str, Any] = {
        "contract": "transition-resolver-decision-v1",
        "decision": decision,
        "decision_class": decision_class,
        "reason_code": reason_code,
        "required_session_mode": required_session_mode,
        "required_invocation_mode": required_invocation_mode,
        "reroute_destination": reroute_destination,
        "reroute_class": reroute_class,
        "required_evidence_checks": required_evidence_checks or [],
        "required_path_checks": required_path_checks or [],
        "required_output_artifacts": required_output_artifacts or [],
        "authorized": authorized,
        "blocking": blocking,
        "required_mechanical_gate": required_mechanical_gate,
        "mechanical_gate_status": mechanical_gate_status,
    }
    if denial_reason:
        payload["denial_reason"] = denial_reason
    return payload


def resolve(data: dict[str, Any]) -> dict[str, Any]:
    subject = as_dict(data.get("subject"))
    session = as_dict(data.get("session"))
    review_result = as_dict(data.get("review_result"))
    predecessor = as_dict(data.get("predecessor"))
    reopen = as_dict(data.get("reopen"))
    caller = as_dict(data.get("caller"))
    authorization = as_dict(data.get("authorization"))
    invocation = as_dict(data.get("invocation"))
    paths = as_dict(data.get("paths"))
    mechanical_gate = as_dict(data.get("mechanical_gate"))

    intent = data["intent"]
    review_goal = as_str(subject.get("review_goal"))
    review_phase = as_str(subject.get("review_phase"))
    active_working_agent_id = as_str(session.get("active_working_agent_id"))
    session_open = as_bool(session.get("session_open"))
    resumable = as_bool(session.get("resumable"))
    exception_reason = as_raw_str(session.get("exception_reason"))
    send_input_ready = as_bool(session.get("send_input_ready"))
    resume_probe_attempted = as_bool(session.get("resume_probe_attempted"))
    resume_probe_result = as_raw_str(session.get("resume_probe_result"))

    reviewer_required = as_bool(authorization.get("reviewer_required"))
    module_root_invocation = as_bool(authorization.get("module_root_invocation"))
    minimal_bundle_present = as_bool(authorization.get("minimal_bundle_present"))
    bundle_field_validation = as_bool(authorization.get("bundle_field_validation"))
    built_in_subagent_available = as_bool(invocation.get("built_in_subagent_available"))
    fork_context_requested = as_bool(invocation.get("fork_context_requested"))
    provided_context_scope = as_str(invocation.get("provided_context_scope")) or ""

    findings_count = as_int(review_result.get("findings_count")) or 0
    has_blocking_findings = as_bool(review_result.get("has_blocking_findings")) is True
    has_auto_fixable_findings = as_bool(review_result.get("has_auto_fixable_findings")) is True
    final_assessment = as_str(review_result.get("final_assessment"))
    review_pass_type = as_str(review_result.get("review_pass_type"))
    coverage_status = as_str(review_result.get("coverage_status"))
    saturation_status = as_str(review_result.get("saturation_status"))
    closure_authority = as_str(review_result.get("closure_authority"))

    source_review = as_dict(predecessor.get("source_review"))
    source_review_agent_id = as_str(source_review.get("agent_id"))
    source_review_findings_path = as_str(source_review.get("findings_path"))
    source_review_evidence_path = as_str(source_review.get("verifier_evidence_path"))
    predecessor_final_assessment = as_str(predecessor.get("findings_final_assessment"))
    predecessor_evidence = as_dict(predecessor.get("evidence"))
    predecessor_evidence_agent_id = as_str(predecessor_evidence.get("agent_id"))
    predecessor_evidence_goal = as_str(predecessor_evidence.get("review_goal"))
    predecessor_evidence_pass_type = as_str(predecessor_evidence.get("review_pass_type"))
    predecessor_evidence_coverage = as_str(predecessor_evidence.get("coverage_status"))
    predecessor_evidence_saturation = as_str(predecessor_evidence.get("saturation_status"))

    failed_challenger = as_dict(reopen.get("failed_challenger"))
    failed_challenger_agent_id = as_str(failed_challenger.get("agent_id"))
    failed_challenger_findings_path = as_str(failed_challenger.get("findings_path"))
    failed_challenger_evidence_path = as_str(failed_challenger.get("verifier_evidence_path"))
    promoted_working_agent_id = as_str(reopen.get("promoted_working_agent_id"))
    reopen_record_path = as_str(reopen.get("reopen_record_path"))

    review_run_dir = as_str(paths.get("review_run_dir"))
    spawn_decision_path = as_str(paths.get("spawn_decision_path"))
    findings_path = as_str(paths.get("findings_path"))
    verifier_evidence_path = as_str(paths.get("verifier_evidence_path"))
    verifier_output_path = as_str(paths.get("verifier_output_path"))

    dry_run = as_bool(caller.get("dry_run")) is True
    manual_pause = as_bool(caller.get("manual_pause")) is True
    pause_requested = dry_run or manual_pause

    mechanical_gate_status = as_str(mechanical_gate.get("mechanical_gate_status")) or "not_required"

    shared_required_checks = [
        "review_goal=implementation_correctness",
        "minimal_bundle_present",
        "bundle_field_validation",
        "fork_context=false",
        "built_in_subagent_available",
    ]
    shared_path_checks = [
        "review_run_dir",
        "findings_path",
        "verifier_evidence_path",
        "verifier_output_path",
    ]

    session_normalization_error = working_session_normalization_error(
        intent=intent,
        active_working_agent_id=active_working_agent_id,
        session_open=session_open,
        resumable=resumable,
        exception_reason=exception_reason,
        send_input_ready=send_input_ready,
        resume_probe_attempted=resume_probe_attempted,
        resume_probe_result=resume_probe_result,
    )
    if session_normalization_error:
        return make_decision(
            decision="blocked",
            reason_code="invalid_working_session_normalization",
            required_session_mode="no_session_change",
            required_invocation_mode="no_invocation",
            authorized=False,
            blocking=True,
            denial_reason=(
                "session normalization violated working-session-normalization-v1: "
                f"{session_normalization_error}"
            ),
        )

    if reviewer_required is not True or module_root_invocation is not True:
        return make_decision(
            decision="blocked",
            reason_code="reviewer_not_authorized",
            required_session_mode="no_session_change",
            required_invocation_mode="no_invocation",
            authorized=False,
            blocking=True,
            denial_reason="reviewer authorization is missing for this resolver step",
        )

    if minimal_bundle_present is not True or bundle_field_validation is not True:
        return make_decision(
            decision="blocked",
            reason_code="bundle_invalid",
            required_session_mode="no_session_change",
            required_invocation_mode="no_invocation",
            authorized=False,
            blocking=True,
            denial_reason="minimal verification bundle is missing or failed field validation",
        )

    if built_in_subagent_available is not True and intent in {
        "working_entry",
        "working_rerun",
        "challenger_entry",
    }:
        return make_decision(
            decision="blocked",
            reason_code="built_in_subagent_required",
            required_session_mode="no_session_change",
            required_invocation_mode="no_invocation",
            authorized=False,
            blocking=True,
            denial_reason="built-in subagent invocation is required for reviewer sessions",
        )

    if fork_context_requested is True or provided_context_scope == "full_parent_context":
        return make_decision(
            decision="blocked",
            reason_code="context_scope_too_broad",
            required_session_mode="no_session_change",
            required_invocation_mode="no_invocation",
            authorized=False,
            blocking=True,
            denial_reason="resolver requires minimal bundle invocation with fork_context=false",
        )

    if intent in {"working_entry", "working_rerun", "post_working"} and not paths_match_role(
        [findings_path, verifier_evidence_path, verifier_output_path], "working"
    ):
        return make_decision(
            decision="blocked",
            reason_code="working_path_role_mismatch",
            required_session_mode="no_session_change",
            required_invocation_mode="no_invocation",
            authorized=False,
            blocking=True,
            denial_reason="working intents require findings/evidence/output paths under working/",
        )

    if intent in {"working_entry", "working_rerun"} and spawn_decision_path and not path_has_role(
        spawn_decision_path, "working"
    ):
        return make_decision(
            decision="blocked",
            reason_code="working_spawn_path_role_mismatch",
            required_session_mode="no_session_change",
            required_invocation_mode="no_invocation",
            authorized=False,
            blocking=True,
            denial_reason="working spawn_decision_path must be under working/",
        )

    if intent == "challenger_entry" and spawn_decision_path and not path_has_role(
        spawn_decision_path, "challenger"
    ):
        return make_decision(
            decision="blocked",
            reason_code="challenger_spawn_path_role_mismatch",
            required_session_mode="no_session_change",
            required_invocation_mode="no_invocation",
            authorized=False,
            blocking=True,
            denial_reason="challenger spawn_decision_path must be under challenger/",
        )

    if intent in {"post_challenger", "close"} and not paths_match_role(
        [findings_path, verifier_evidence_path, verifier_output_path], "challenger"
    ):
        return make_decision(
            decision="blocked",
            reason_code="challenger_path_role_mismatch",
            required_session_mode="no_session_change",
            required_invocation_mode="no_invocation",
            authorized=False,
            blocking=True,
            denial_reason="challenger intents require findings/evidence/output paths under challenger/",
        )

    if intent in {"working_entry", "working_rerun"} and predecessor_evidence_pass_type == "challenger":
        if not all(
            [
                promoted_working_agent_id,
                failed_challenger_agent_id,
                failed_challenger_findings_path,
                failed_challenger_evidence_path,
                reopen_record_path,
            ]
        ):
            return make_decision(
                decision="blocked",
                reason_code="missing_reopen_continuation_binding",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason=(
                    "working continuation after challenger findings requires reopen metadata: "
                    "promoted_working_agent_id, failed_challenger bindings, and reopen_record_path"
                ),
            )
        if predecessor_evidence_agent_id and failed_challenger_agent_id != predecessor_evidence_agent_id:
            return make_decision(
                decision="blocked",
                reason_code="reopen_predecessor_challenger_mismatch",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason=(
                    "working continuation after challenger findings requires "
                    "failed_challenger.agent_id to match predecessor.evidence.agent_id"
                ),
            )
    if promoted_working_agent_id and intent in {"working_entry", "working_rerun"}:
        if active_working_agent_id != promoted_working_agent_id:
            return make_decision(
                decision="blocked",
                reason_code="reopen_promoted_working_baseline_mismatch",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason=(
                    "reopen metadata already established a promoted working baseline; "
                    "session.active_working_agent_id must match reopen.promoted_working_agent_id"
                ),
            )
        if failed_challenger_agent_id and failed_challenger_agent_id != promoted_working_agent_id:
            return make_decision(
                decision="blocked",
                reason_code="reopen_failed_challenger_agent_mismatch",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason=(
                    "reopen metadata is inconsistent; failed_challenger.agent_id must match "
                    "reopen.promoted_working_agent_id"
                ),
            )

    if intent == "working_entry":
        if active_working_agent_id:
            if session_open is True:
                return make_decision(
                    decision="send_input_same_working",
                    reason_code="active_working_session_open",
                    required_session_mode="reuse_working_same_session",
                    required_invocation_mode="built_in_session_message",
                    required_evidence_checks=shared_required_checks,
                    required_path_checks=shared_path_checks,
                )
            if resumable is True:
                return make_decision(
                    decision="resume_same_working",
                    reason_code="active_working_session_resumable",
                    required_session_mode="reuse_working_same_session",
                    required_invocation_mode="built_in_session_resume",
                    required_evidence_checks=shared_required_checks,
                    required_path_checks=shared_path_checks,
                )
            if exception_reason in {
                "agent_not_found",
                "session_completed_unresumable",
                "tooling_recovery",
            }:
                return make_decision(
                    decision="spawn_exception_working",
                    reason_code=exception_reason,
                    required_session_mode="fresh_working_exception",
                    required_invocation_mode="built_in_subagent_spawn",
                    required_evidence_checks=shared_required_checks,
                    required_path_checks=shared_path_checks + ["spawn_decision_path"],
                    required_output_artifacts=["spawn-decision.json"],
                )
            return make_decision(
                decision="blocked",
                reason_code="working_session_unavailable_without_exception",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason="working session is unavailable and no explicit exception reason was recorded",
            )

        return make_decision(
            decision="spawn_initial_working",
            reason_code="initial_working_session",
            required_session_mode="initial_working",
            required_invocation_mode="built_in_subagent_spawn",
            required_evidence_checks=shared_required_checks,
            required_path_checks=shared_path_checks + ["spawn_decision_path"],
            required_output_artifacts=["spawn-decision.json"],
        )

    if intent == "working_rerun":
        if not active_working_agent_id:
            return make_decision(
                decision="blocked",
                reason_code="working_rerun_requires_active_working_agent",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason=(
                    "ordinary working rerun requires an active working baseline; "
                    "use working_entry only for loop start or explicit recovery"
                ),
            )
        if session_open is True:
            return make_decision(
                decision="send_input_same_working",
                reason_code="ordinary_rerun_same_session",
                required_session_mode="reuse_working_same_session",
                required_invocation_mode="built_in_session_message",
                required_evidence_checks=shared_required_checks,
                required_path_checks=shared_path_checks,
            )
        if resumable is True:
            return make_decision(
                decision="resume_same_working",
                reason_code="ordinary_rerun_resumable_session",
                required_session_mode="reuse_working_same_session",
                required_invocation_mode="built_in_session_resume",
                required_evidence_checks=shared_required_checks,
                required_path_checks=shared_path_checks,
            )
        if exception_reason in {
            "agent_not_found",
            "session_completed_unresumable",
            "tooling_recovery",
        }:
            return make_decision(
                decision="spawn_exception_working",
                reason_code=exception_reason,
                required_session_mode="fresh_working_exception",
                required_invocation_mode="built_in_subagent_spawn",
                required_evidence_checks=shared_required_checks,
                required_path_checks=shared_path_checks + ["spawn_decision_path"],
                required_output_artifacts=["spawn-decision.json"],
            )
        return make_decision(
            decision="blocked",
            reason_code="ordinary_rerun_without_reusable_session",
            required_session_mode="no_session_change",
            required_invocation_mode="no_invocation",
            authorized=False,
            blocking=True,
            denial_reason="ordinary working rerun requires send_input/resume or an explicit exception path",
        )

    if intent == "post_working":
        if review_pass_type != "working":
            return make_decision(
                decision="blocked",
                reason_code="post_working_requires_working_pass",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason="post_working intent requires current review_pass_type=working",
            )
        if has_blocking_findings:
            reroute_destination = "docs_artifact_repair" if review_phase == "docs_first" else "source_repair"
            reroute_class = (
                "docs_blocking_findings"
                if review_phase == "docs_first"
                else "implementation_auto_fix"
                if has_auto_fixable_findings
                else "source_blocking_findings"
            )
            return make_decision(
                decision="enter_repair",
                reason_code="blocking_findings_require_repair",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                reroute_destination=reroute_destination,
                reroute_class=reroute_class,
                required_evidence_checks=[
                    "review_pass_type=working",
                    "has_blocking_findings=true",
                    "reviewer-derived reroute_class",
                ],
                required_path_checks=shared_path_checks,
                blocking=True,
            )
        if final_assessment == "pass" and findings_count == 0 and coverage_status == "complete" and saturation_status == "exhaustive":
            if pause_requested:
                return make_decision(
                    decision="wait_for_user",
                    reason_code="caller_pause_requested",
                    required_session_mode="no_session_change",
                    required_invocation_mode="no_invocation",
                    reroute_destination="none",
                    reroute_class="caller_pause",
                    required_evidence_checks=[
                        "review_pass_type=working",
                        "final_assessment=pass",
                        "zero_findings",
                        "coverage_status=complete",
                        "saturation_status=exhaustive",
                    ],
                    required_path_checks=shared_path_checks,
                )
            return make_decision(
                decision="return_caller_flow",
                reason_code="working_converged_ready_for_challenger_entry",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                reroute_destination="return_caller_flow",
                reroute_class="none",
                required_evidence_checks=[
                    "review_pass_type=working",
                    "final_assessment=pass",
                    "zero_findings",
                    "coverage_status=complete",
                    "saturation_status=exhaustive",
                ],
                required_path_checks=shared_path_checks,
            )
        return make_decision(
            decision="blocked",
            reason_code="working_result_not_converged",
            required_session_mode="no_session_change",
            required_invocation_mode="no_invocation",
            authorized=False,
            blocking=True,
            denial_reason="working result is neither repair-routable nor converged for challenger entry",
        )

    if intent == "challenger_entry":
        predecessor_checks = [
            "source_review.agent_id",
            "source_review.findings_path",
            "source_review.verifier_evidence_path",
            "final_assessment=pass",
            "findings must be empty",
            "review_goal=implementation_correctness",
            "review_pass_type=working",
            "coverage_status=complete",
            "saturation_status=exhaustive",
            "agent_id matches source_review.agent_id",
        ]
        if pause_requested:
            return make_decision(
                decision="wait_for_user",
                reason_code="caller_pause_requested",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                reroute_destination="none",
                reroute_class="caller_pause",
                required_evidence_checks=predecessor_checks,
                required_path_checks=shared_path_checks,
            )
        if review_goal != "implementation_correctness":
            return make_decision(
                decision="blocked",
                reason_code="review_goal_mismatch",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason="challenger entry requires review_goal=implementation_correctness",
            )
        if not all([source_review_agent_id, source_review_findings_path, source_review_evidence_path]):
            return make_decision(
                decision="blocked",
                reason_code="missing_source_review_binding",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason="challenger entry requires source_review agent_id/findings/evidence binding",
            )
        if not paths_match_role([source_review_findings_path, source_review_evidence_path], "working"):
            return make_decision(
                decision="blocked",
                reason_code="source_review_path_role_mismatch",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason="challenger entry requires source_review findings/evidence paths under working/",
            )
        if predecessor_final_assessment != "pass":
            return make_decision(
                decision="blocked",
                reason_code="predecessor_not_pass",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason="challenger entry requires previous working final_assessment=pass",
            )
        if findings_count != 0:
            return make_decision(
                decision="blocked",
                reason_code="predecessor_findings_nonempty",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason="challenger entry requires zero findings on the previous working pass",
            )
        if predecessor_evidence_goal != "implementation_correctness":
            return make_decision(
                decision="blocked",
                reason_code="predecessor_review_goal_mismatch",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason="challenger entry requires predecessor evidence review_goal=implementation_correctness",
            )
        if predecessor_evidence_pass_type != "working":
            return make_decision(
                decision="blocked",
                reason_code="predecessor_not_working_pass",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason="challenger entry requires predecessor evidence review_pass_type=working",
            )
        if predecessor_evidence_coverage != "complete" or predecessor_evidence_saturation != "exhaustive":
            return make_decision(
                decision="blocked",
                reason_code="predecessor_coverage_incomplete",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason="challenger entry requires complete coverage and exhaustive saturation",
            )
        if predecessor_evidence_agent_id != source_review_agent_id:
            return make_decision(
                decision="blocked",
                reason_code="predecessor_agent_mismatch",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason="challenger entry requires predecessor evidence agent_id to match source_review.agent_id",
            )
        return make_decision(
            decision="spawn_fresh_challenger",
            reason_code="challenger_pass",
            required_session_mode="fresh_challenger",
            required_invocation_mode="built_in_subagent_spawn",
            required_evidence_checks=predecessor_checks,
            required_path_checks=shared_path_checks + ["spawn_decision_path"],
            required_output_artifacts=["spawn-decision.json"],
        )

    if intent == "post_challenger":
        if review_pass_type != "challenger":
            return make_decision(
                decision="blocked",
                reason_code="post_challenger_requires_challenger_pass",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason="post_challenger intent requires current review_pass_type=challenger",
            )
        if final_assessment == "pass" and findings_count == 0:
            if pause_requested:
                return make_decision(
                    decision="wait_for_user",
                    reason_code="caller_pause_requested",
                    required_session_mode="no_session_change",
                    required_invocation_mode="no_invocation",
                    reroute_destination="none",
                    reroute_class="caller_pause",
                    required_evidence_checks=[
                        "final_assessment=pass",
                        "zero_findings",
                        "closure_authority=challenger_confirmed",
                    ],
                    required_path_checks=unique_strings(shared_path_checks + ["review_run_dir"]),
                )
            if closure_authority == "challenger_confirmed" and coverage_status == "complete" and saturation_status == "exhaustive" and mechanical_gate_status == "passed":
                return make_decision(
                    decision="allow_close",
                    reason_code="challenger_confirmed_zero_findings",
                    required_session_mode="no_session_change",
                    required_invocation_mode="no_invocation",
                    required_evidence_checks=[
                        "final_assessment=pass",
                        "zero_findings",
                        "closure_authority=challenger_confirmed",
                        "coverage_status=complete",
                        "saturation_status=exhaustive",
                    ],
                    required_path_checks=unique_strings(shared_path_checks + ["review_run_dir"]),
                    required_mechanical_gate="review_loop_guard_run_dir",
                    mechanical_gate_status="passed",
                )
            return make_decision(
                decision="deny_close",
                reason_code="challenger_pass_requires_closure_gate",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                required_evidence_checks=[
                    "final_assessment=pass",
                    "zero_findings",
                    "closure_authority=challenger_confirmed",
                    "coverage_status=complete",
                    "saturation_status=exhaustive",
                ],
                required_path_checks=unique_strings(shared_path_checks + ["review_run_dir"]),
                required_mechanical_gate="review_loop_guard_run_dir",
                mechanical_gate_status=mechanical_gate_status,
                blocking=True,
            )
        if not all(
            [
                failed_challenger_agent_id,
                failed_challenger_findings_path,
                failed_challenger_evidence_path,
                promoted_working_agent_id,
                reopen_record_path,
            ]
        ):
            return make_decision(
                decision="blocked",
                reason_code="missing_challenger_reopen_binding",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason="challenger findings require reopen metadata and reopen_record_path",
            )
        if not paths_match_role([failed_challenger_findings_path, failed_challenger_evidence_path], "challenger"):
            return make_decision(
                decision="blocked",
                reason_code="failed_challenger_path_role_mismatch",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason="failed challenger findings/evidence paths must be under challenger/",
            )
        if not path_has_role(reopen_record_path, "working"):
            return make_decision(
                decision="blocked",
                reason_code="reopen_record_path_role_mismatch",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason="challenger reopen_record_path must be under working/",
            )
        if promoted_working_agent_id != failed_challenger_agent_id:
            return make_decision(
                decision="blocked",
                reason_code="promoted_working_agent_mismatch",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                authorized=False,
                blocking=True,
                denial_reason="promoted_working_agent_id must equal the failed challenger agent_id",
            )
        return make_decision(
            decision="record_challenger_reopen",
            reason_code="challenger_found_new_findings",
            required_session_mode="promoted_challenger_to_working",
            required_invocation_mode="no_invocation",
            required_evidence_checks=[
                "failed_challenger.agent_id",
                "failed_challenger.findings_path",
                "failed_challenger.verifier_evidence_path",
            ],
            required_path_checks=unique_strings(shared_path_checks + ["reopen_record_path"]),
            required_output_artifacts=["reopen-record.json"],
            blocking=True,
        )

    if intent == "close":
        required_checks = [
            "final pass is challenger",
            "final_assessment=pass",
            "zero_findings",
            "closure_authority=challenger_confirmed",
            "coverage_status=complete",
            "saturation_status=exhaustive",
        ]
        if review_pass_type == "challenger" and final_assessment == "pass" and findings_count == 0 and closure_authority == "challenger_confirmed" and coverage_status == "complete" and saturation_status == "exhaustive" and review_run_dir and mechanical_gate_status == "passed":
            return make_decision(
                decision="allow_close",
                reason_code="challenger_confirmed_zero_findings",
                required_session_mode="no_session_change",
                required_invocation_mode="no_invocation",
                required_evidence_checks=required_checks,
                required_path_checks=unique_strings(shared_path_checks + ["review_run_dir"]),
                required_mechanical_gate="review_loop_guard_run_dir",
                mechanical_gate_status="passed",
            )
        return make_decision(
            decision="deny_close",
            reason_code="closure_prerequisites_unsatisfied",
            required_session_mode="no_session_change",
            required_invocation_mode="no_invocation",
            required_evidence_checks=required_checks,
            required_path_checks=unique_strings(shared_path_checks + ["review_run_dir"]),
            authorized=True,
            blocking=True,
            required_mechanical_gate="review_loop_guard_run_dir",
            mechanical_gate_status=mechanical_gate_status,
        )

    raise ValueError(f"Unsupported intent: {intent}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, help="Path to a transition-resolver input JSON file")
    parser.add_argument("--output", help="Optional output path for the resolved decision JSON")
    parser.add_argument(
        "--print-contract-paths",
        action="store_true",
        help="Print the authoritative input, working-session, and routing contract paths before resolving",
    )
    args = parser.parse_args()

    if args.print_contract_paths:
        print(INPUT_CONTRACT_PATH)
        print(WORKING_SESSION_CONTRACT_PATH)
        print(ROUTING_PATH)

    data, errors = load_json_object(Path(args.input).resolve())
    if errors:
        print("transition-resolver resolve failed:")
        for error in errors:
            print(f"- {error}")
        return 1
    assert data is not None

    shape_errors = validate_input_shape(data)
    if shape_errors:
        print("transition-resolver resolve failed:")
        for error in shape_errors:
            print(f"- {error}")
        return 1

    decision = resolve(data)
    rendered = json.dumps(decision, ensure_ascii=True, indent=2) + "\n"
    if args.output:
        Path(args.output).resolve().write_text(rendered)
    else:
        sys.stdout.write(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
