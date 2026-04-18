#!/usr/bin/env python3
"""
Reusable validator and autofix entrypoint for the review-loop module.

This script can be used by:
- OpenSpec schema integrations
- direct rollout maintenance
- standalone review-flow docs in an external repository

It validates that the repository references the shared review-loop module
instead of stale local contracts and can rewrite a small set of known stale
references automatically.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime
from pathlib import Path

SCRIPT = Path(__file__).resolve()
MODULE_ROOT = SCRIPT.parents[1]
OPENSPEC_ROOT = SCRIPT.parents[4]

if OPENSPEC_ROOT.name == "openspec":
    REPO_ROOT = OPENSPEC_ROOT.parent
    local_codex_home = REPO_ROOT / ".codex"
    if (local_codex_home / "skills").exists():
        CODEX_HOME = local_codex_home
    else:
        CODEX_HOME = Path.home() / ".codex"
else:
    CODEX_HOME = Path.home() / ".codex"

CORE_CONTRACT_PATH = MODULE_ROOT / "contracts/review-loop-core-v1.json"
REOPEN_RECORD_CONTRACT_PATH = MODULE_ROOT / "contracts/review-loop-reopen-record-v1.json"
STANDALONE_ADAPTER_PATH = MODULE_ROOT / "contracts/review-loop-standalone-adapter-v1.json"
STANDALONE_SPAWN_CONTRACT_PATH = MODULE_ROOT / "contracts/review-loop-standalone-spawn-decision-v1.json"
OPENSPEC_SPAWN_SCHEMA_PATH = OPENSPEC_ROOT / "schemas/ai-enforced-workflow/agent-spawn-decision-v1.schema.json"

KNOWN_REPLACEMENTS = [
    (
        "openspec/schemas/ai-enforced-workflow/contracts/review-contract-v1.json",
        "openspec/schemas/modules/review-loop/contracts/review-loop-core-v1.json",
    ),
    (
        "docs/review-flow/contracts/standalone-review-contract-v1.json",
        "openspec/schemas/modules/review-loop/contracts/review-loop-core-v1.json",
    ),
    (
        "docs/review-flow/contracts/standalone-spawn-decision-v1.json",
        "openspec/schemas/modules/review-loop/contracts/review-loop-standalone-spawn-decision-v1.json",
    ),
    (
        "review-contract-v1.json",
        "review-loop-core-v1.json",
    ),
    (
        "the current verifier pass must not self-authorize challenger entry or closure",
        "the main process must not substitute its own judgment for the prior working sub-agent output when deciding challenger entry or closure",
    ),
    (
        "the verifier pass must not self-authorize either transition",
        "the main process must not substitute its own judgment for the prior working sub-agent output",
    ),
]

CONTRACT_CACHE: dict[Path, dict] = {}


def detect_repo_root(cli_repo_root: str | None) -> Path | None:
    if cli_repo_root:
        return Path(cli_repo_root).resolve()
    cwd = Path.cwd().resolve()
    for candidate in [cwd, *cwd.parents]:
        if (candidate / "docs").exists():
            return candidate
    return None


def load_json(path: Path) -> list[str]:
    try:
        json.loads(path.read_text())
    except Exception as exc:
        return [f"JSON parse failed: {path}: {exc}"]
    return []


def read_json_object(path: Path) -> tuple[dict | None, list[str]]:
    try:
        data = json.loads(path.read_text())
    except Exception as exc:
        return None, [f"JSON parse failed: {path}: {exc}"]
    if not isinstance(data, dict):
        return None, [f"Expected JSON object in {path}"]
    return data, []


def read_contract(path: Path) -> dict:
    if path in CONTRACT_CACHE:
        return CONTRACT_CACHE[path]
    data, _ = read_json_object(path)
    CONTRACT_CACHE[path] = data or {}
    return CONTRACT_CACHE[path]


def require_tokens(path: Path, patterns: list[str | tuple[str, ...]]) -> list[str]:
    text = path.read_text()
    errors: list[str] = []
    for pattern in patterns:
        if isinstance(pattern, tuple):
            if not any(option in text for option in pattern):
                errors.append(f"Missing required token alternatives {pattern!r} in {path}")
            continue
        if pattern not in text:
            errors.append(f"Missing required token {pattern!r} in {path}")
    return errors


def forbid_tokens(path: Path, patterns: list[str]) -> list[str]:
    text = path.read_text()
    errors: list[str] = []
    for pattern in patterns:
        if re.fullmatch(r"[A-Za-z0-9_-]+", pattern):
            if re.search(rf"(?<![A-Za-z0-9_-]){re.escape(pattern)}(?![A-Za-z0-9_-])", text):
                errors.append(f"Forbidden token {pattern!r} present in {path}")
            continue
        if pattern in text:
            errors.append(f"Forbidden token {pattern!r} present in {path}")
    return errors


def parse_datetime(value: object) -> datetime | None:
    if not isinstance(value, str) or not value:
        return None
    normalized = value.replace("Z", "+00:00")
    try:
        return datetime.fromisoformat(normalized)
    except ValueError:
        return None


def resolve_run_path(run_dir: Path, raw_path: str) -> Path:
    path = Path(raw_path)
    if path.is_absolute():
        return path
    return (run_dir / path).resolve()


def run_dir_from_attempt_artifact(path: Path) -> Path:
    if path.parent.name.startswith("attempt-"):
        return path.parents[2]
    return path.parent


def attempt_sort_key(path: Path) -> tuple[int, str]:
    name = path.name
    if name.startswith("attempt-"):
        suffix = name[len("attempt-") :]
        if suffix.isdigit():
            return (int(suffix), name)
    return (sys.maxsize, name)


def attempt_dirs(root: Path) -> list[Path]:
    if not root.exists():
        return []
    return sorted((path for path in root.iterdir() if path.is_dir()), key=attempt_sort_key)


def module_scope() -> dict[Path, list[str]]:
    return {
        MODULE_ROOT / "README.md": [
            "implementation is correct",
            "bootstrap_defaults",
            "substitute its own judgment",
            "review-loop-core-v1.json",
            "review-loop-standalone-adapter-v1.json",
            "VERIFY-IMPLEMENTATION.md",
        ],
        MODULE_ROOT / "VERIFY-IMPLEMENTATION.md": [
            "Bootstrap Rules For Context-Free AI",
            "implementation_correctness",
            "shared-findings-v1",
            "Do not spawn challenger",
        ],
        CORE_CONTRACT_PATH: [
            "bootstrap_defaults",
            "bootstrap_rules",
            "working_session_rule",
            "challenger_entry",
            "authority_rule",
        ],
        REOPEN_RECORD_CONTRACT_PATH: [
            "\"review-loop-reopen-record-v1\"",
            "\"challenger_reopen\"",
            "\"promoted_working_agent_id\"",
        ],
        MODULE_ROOT / "contracts/review-loop-openspec-adapter-v1.json": [
            "subject_key",
            "\"change\"",
            "spawn_decision_contract",
        ],
        MODULE_ROOT / "transition-resolver/contracts/working-session-normalization-v1.json": [
            "\"working-session-normalization-v1\"",
            "resume_probe_results",
            "session_completed_unresumable",
            "ordinary working_rerun requires an active working baseline",
        ],
        MODULE_ROOT / "transition-resolver/contracts/transition-resolver-input-v1.json": [
            "\"transition-resolver-input-v1\"",
            "\"reopen\"",
            "\"resume_probe_attempted\"",
            "\"resume_probe_result\"",
        ],
        MODULE_ROOT / "transition-resolver/CALLER-INTEGRATION.md": [
            "working-session-normalization-v1.json",
            "fresh working spawn is legal only after a recorded",
            "reopen.promoted_working_agent_id",
        ],
        STANDALONE_ADAPTER_PATH: [
            "subject_key",
            "\"target_ref\"",
            "bootstrap_target_ref_templates",
            "review-loop-standalone-spawn-decision-v1.json",
        ],
        STANDALONE_SPAWN_CONTRACT_PATH: [
            "\"review-loop-standalone-spawn-decision-v1\"",
            "\"next_session_role\"",
            "\"reason_code\"",
        ],
    }


def openspec_scope() -> dict[Path, list[str]]:
    return {
        OPENSPEC_ROOT / "schemas/ai-enforced-workflow/verification-sequence.md": [
            "review-loop-core-v1.json",
            "review-loop-openspec-adapter-v1.json",
            "source_review",
            "substitute its own judgment",
            "working-session-normalization-v1.json",
            "normalize_working_session.py",
        ],
        OPENSPEC_ROOT / "schemas/ai-enforced-workflow/schema.yaml": [
            "review-loop-core-v1.json",
            "review-loop-openspec-adapter-v1.json",
            "challenger_entry.source_review_required",
        ],
        OPENSPEC_ROOT / "schemas/ai-enforced-workflow/templates/design.md": [
            "review-loop-core-v1.json",
            "review-loop-openspec-adapter-v1.json",
            "review_phase",
        ],
        OPENSPEC_ROOT / "schemas/ai-enforced-workflow/templates/tasks.md": [
            "review-loop-core-v1.json",
            "review-loop-openspec-adapter-v1.json",
            "challenger_entry.source_review_required",
        ],
        CODEX_HOME / "skills/openspec-artifact-verify/SKILL.md": [
            "verify-sequence/default",
            "cache_mode=bypassed",
            "return to the active caller flow",
        ],
        CODEX_HOME / "skills/openspec-propose/SKILL.md": [
            "openspec-artifact-verify",
            "artifact-completion docs-first gate",
            "Do not hand off artifact repair to `openspec-apply-change`",
        ],
        CODEX_HOME / "skills/openspec-ff-change/SKILL.md": [
            "openspec-artifact-verify",
            "artifact-completion docs-first gate",
            "Do not hand off artifact repair to `openspec-apply-change`",
        ],
        CODEX_HOME / "skills/openspec-continue-change/SKILL.md": [
            "openspec-artifact-verify",
            "artifact-completion docs-first gate",
            "do not stop at \"all artifacts exist\"",
        ],
        CODEX_HOME / "skills/openspec-apply-change/SKILL.md": [
            "review-loop-core-v1.json",
            "review-loop-openspec-adapter-v1.json",
            "CALLER-INTEGRATION.md",
            "transition_resolver_resolve.py",
            "Do not re-run docs-first artifact verification as an apply-stage",
        ],
        CODEX_HOME / "skills/openspec-verify-change/SKILL.md": [
            "review-loop-core-v1.json",
            "review-loop-openspec-adapter-v1.json",
            "CALLER-INTEGRATION.md",
            "transition_resolver_resolve.py",
        ],
        CODEX_HOME / "skills/openspec-repair-change/SKILL.md": [
            "review-loop-core-v1.json",
            "CALLER-INTEGRATION.md",
            "transition_resolver_resolve.py",
        ],
        OPENSPEC_ROOT / "specs/verify-subagent-orchestration/spec.md": [
            "review-loop-core-v1.json",
            "review-loop-openspec-adapter-v1.json",
            "substitute its own judgment",
        ],
    }


def standalone_scope(repo_root: Path | None) -> dict[Path, list[str]]:
    if repo_root is None:
        return {}
    return {
        repo_root / "docs/review-flow/README.md": [
            "review-loop-core-v1.json",
            "review-loop-standalone-adapter-v1.json",
            "review-loop-standalone-spawn-decision-v1.json",
            "target_ref",
            "review_scope",
            "review_coverage",
            "substitute its own judgment",
        ],
        repo_root / "docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/03-layer-1-core-review-loop.md": [
            "review_phase",
            "closure_authority",
            "working_evidence_only",
            "challenger_confirmed",
        ],
        repo_root / "docs/reusable-review-flow.md": [
            "compatibility entrypoint",
            "docs/review-flow/README.md",
            "implementation_correctness",
            "requested_agent_type=verify-reviewer",
            "next_session_role=working|challenger",
            "reason_code=initial_working_session|challenger_pass|active_session_unavailable|session_unresumable|tooling_recovery",
        ],
    }


def standalone_spawn_forbidden_literals() -> list[str]:
    standalone_contract = read_contract(STANDALONE_SPAWN_CONTRACT_PATH)
    openspec_contract = read_contract(OPENSPEC_SPAWN_SCHEMA_PATH)

    standalone_reason_codes = set(
        standalone_contract.get("properties", {}).get("reason_code", {}).get("enum", [])
    )
    openspec_reason_codes = set(
        openspec_contract.get("properties", {}).get("reason_code", {}).get("enum", [])
    )

    standalone_agent_types = {
        standalone_contract.get("properties", {}).get("requested_agent_type", {}).get("const")
    }
    openspec_agent_types = set(
        openspec_contract.get("properties", {}).get("requested_agent_type", {}).get("enum", [])
    )

    standalone_session_roles = set(
        standalone_contract.get("properties", {})
        .get("prior_session", {})
        .get("properties", {})
        .get("session_role", {})
        .get("enum", [])
    )
    openspec_session_roles = set(
        openspec_contract.get("properties", {})
        .get("prior_session", {})
        .get("properties", {})
        .get("session_role", {})
        .get("enum", [])
    )

    derived = (
        (openspec_reason_codes - standalone_reason_codes)
        | (openspec_agent_types - standalone_agent_types)
        | (openspec_session_roles - standalone_session_roles)
    )
    return sorted(token for token in derived if isinstance(token, str) and token)


def standalone_forbidden_tokens(repo_root: Path | None) -> dict[Path, list[str]]:
    if repo_root is None:
        return {}
    return {
        repo_root / "docs/reusable-review-flow.md": [
            "cache-maintainer",
            "cache refresh required",
            *standalone_spawn_forbidden_literals(),
        ],
    }


def selected_scopes(scope: str, repo_root: Path | None) -> dict[Path, list[str]]:
    scopes: dict[Path, list[str]] = {}
    if scope in {"module", "all"}:
        scopes.update(module_scope())
    if scope in {"openspec", "all"}:
        scopes.update(openspec_scope())
    if scope in {"standalone", "all"}:
        scopes.update(standalone_scope(repo_root))
    return scopes


def autofix(paths: list[Path]) -> list[str]:
    changed: list[str] = []
    for path in paths:
        if not path.exists() or not path.is_file():
            continue
        text = path.read_text()
        updated = text
        for old, new in KNOWN_REPLACEMENTS:
            updated = updated.replace(old, new)
        if updated != text:
            path.write_text(updated)
            changed.append(str(path))
    return changed


def validate_findings_payload(path: Path) -> tuple[dict | None, list[str]]:
    data, errors = read_json_object(path)
    if errors:
        return None, errors
    subject_keys = [
        key
        for key in ("change", "target_ref")
        if isinstance(data.get(key), str) and data.get(key)
    ]
    if len(subject_keys) != 1:
        errors.append(
            f"Findings payload must include exactly one top-level subject field (change or target_ref) in {path}"
        )
    findings = data.get("findings")
    if not isinstance(findings, list):
        errors.append(f"Missing findings array in {path}")
        return data, errors
    final_assessment = data.get("final_assessment")
    if not isinstance(final_assessment, str) or not final_assessment:
        errors.append(f"Missing final_assessment in {path}")
    elif final_assessment not in {"pass", "pass_with_warnings", "blocked"}:
        errors.append(f"Unsupported final_assessment in {path}")

    required_finding_fields = [
        "id",
        "severity",
        "dimension",
        "problem",
        "evidence",
        "recommendation",
        "blocking",
        "auto_fixable",
    ]
    allowed_severity = {"CRITICAL", "WARNING", "SUGGESTION"}
    allowed_dimension = {"Completeness", "Correctness", "Coherence"}
    finding_ids: set[str] = set()
    for index, finding in enumerate(findings):
        if not isinstance(finding, dict):
            errors.append(f"Finding {index} is not an object in {path}")
            continue
        for field in required_finding_fields:
            if field not in finding:
                errors.append(f"Missing finding field {field!r} in {path}")
        finding_id = finding.get("id")
        if isinstance(finding_id, str):
            if finding_id in finding_ids:
                errors.append(f"Duplicate finding id {finding_id!r} in {path}")
            finding_ids.add(finding_id)
        else:
            errors.append(f"Finding {index} must have string id in {path}")
        if finding.get("severity") not in allowed_severity:
            errors.append(f"Invalid finding severity in {path}")
        if finding.get("dimension") not in allowed_dimension:
            errors.append(f"Invalid finding dimension in {path}")
        for field in ["problem", "evidence", "recommendation"]:
            if not isinstance(finding.get(field), str) or not finding.get(field):
                errors.append(f"Finding field {field!r} must be a non-empty string in {path}")
        for field in ["blocking", "auto_fixable"]:
            if not isinstance(finding.get(field), bool):
                errors.append(f"Finding field {field!r} must be boolean in {path}")
        severity = finding.get("severity")
        if severity == "SUGGESTION" and finding.get("blocking") is not False:
            errors.append(f"SUGGESTION findings must set blocking=false in {path}")
        if finding.get("auto_fixable") is True and finding.get("blocking") is not True:
            errors.append(f"auto_fixable=true requires blocking=true in {path}")
    return data, errors


def validate_evidence_payload(path: Path, findings_path: Path, expected_pass_type: str) -> tuple[dict | None, list[str]]:
    data, errors = read_json_object(path)
    if errors:
        return None, errors

    core_contract = read_contract(CORE_CONTRACT_PATH)
    required_fields = list(core_contract.get("verifier_evidence_required", [])) + list(
        core_contract.get("implementation_evidence_required", [])
    )

    list_fields = {"reviewed_paths", "skipped_paths", "reviewed_axes", "unreviewed_axes"}
    dict_fields = {"review_scope", "review_coverage"}
    allowed_review_phases = {"docs_first", "source_first"}
    subject_keys = [
        key
        for key in ("change", "target_ref")
        if isinstance(data.get(key), str) and data.get(key)
    ]
    if len(subject_keys) != 1:
        errors.append(
            f"Verifier evidence must include exactly one top-level subject field (change or target_ref) in {path}"
        )

    for field in required_fields:
        if field not in data:
            errors.append(f"Missing {field!r} in {path}")
            continue
        value = data[field]
        if field in list_fields and not isinstance(value, list):
            errors.append(f"Expected list for {field!r} in {path}")
        elif field in dict_fields and not isinstance(value, dict):
            errors.append(f"Expected object for {field!r} in {path}")
        elif field not in list_fields and field not in dict_fields and not isinstance(value, str):
            errors.append(f"Expected string for {field!r} in {path}")

    review_phase = data.get("review_phase")
    if isinstance(review_phase, str) and review_phase not in allowed_review_phases:
        allowed = ",".join(sorted(allowed_review_phases))
        errors.append(f"Expected review_phase in {{{allowed}}} in {path}")
    cache_mode = data.get("cache_mode")
    if isinstance(cache_mode, str) and cache_mode not in {
        "used",
        "missed",
        "stale_but_ignored",
        "refreshed",
        "bypassed",
    }:
        errors.append(f"Unexpected cache_mode in {path}")
    closure_authority = data.get("closure_authority")
    if isinstance(closure_authority, str) and closure_authority not in {
        "working_evidence_only",
        "working_convergence_only",
        "challenger_reopen_required",
        "challenger_confirmed",
    }:
        errors.append(f"Unexpected closure_authority in {path}")

    review_scope = data.get("review_scope")
    if isinstance(review_scope, dict) and not review_scope:
        errors.append(f"review_scope must not be empty in {path}")
    review_coverage = data.get("review_coverage")
    inline_coverage_status: str | None = None
    if isinstance(review_coverage, dict):
        if not review_coverage:
            errors.append(f"review_coverage must not be empty in {path}")
        if review_phase == "source_first":
            required_scope_fields = {
                "changed_code_paths",
                "changed_test_paths",
                "impacted_interfaces",
                "mandatory_deep_scan_paths",
                "cache_inputs",
            }
            if isinstance(review_scope, dict):
                for field in required_scope_fields:
                    value = review_scope.get(field)
                    if not isinstance(value, list):
                        errors.append(f"Expected review_scope.{field} to be a list in {path}")
            required_coverage_fields = {
                "reviewed_paths",
                "deep_scanned_paths",
                "skipped_paths",
                "coverage_status",
            }
            for field in required_coverage_fields:
                if field not in review_coverage:
                    errors.append(f"Missing review_coverage.{field} in {path}")
            for field in {"reviewed_paths", "deep_scanned_paths", "skipped_paths"} & set(review_coverage):
                if not isinstance(review_coverage.get(field), list):
                    errors.append(f"Expected review_coverage.{field} to be a list in {path}")
            coverage_value = review_coverage.get("coverage_status")
            if coverage_value not in {"complete", "partial"}:
                errors.append(f"Expected review_coverage.coverage_status in {{complete,partial}} in {path}")
            inline_coverage_status = coverage_value if coverage_value in {"complete", "partial"} else None
        elif review_phase == "docs_first":
            if "coverage_status" in review_coverage:
                coverage_value = review_coverage.get("coverage_status")
                if coverage_value not in {"complete", "partial"}:
                    errors.append(f"Expected review_coverage.coverage_status in {{complete,partial}} in {path}")
                inline_coverage_status = coverage_value if coverage_value in {"complete", "partial"} else None
            elif "covered" in review_coverage:
                covered = review_coverage.get("covered")
                if not isinstance(covered, bool):
                    errors.append(f"Expected review_coverage.covered to be boolean in {path}")
                inline_coverage_status = "complete" if covered is True else "partial"
            else:
                errors.append(f"Expected review_coverage.coverage_status or review_coverage.covered in {path}")
        if "reviewed_paths" in review_coverage and review_coverage.get("reviewed_paths") != data.get("reviewed_paths"):
            errors.append(f"review_coverage.reviewed_paths must match top-level reviewed_paths in {path}")
        if "skipped_paths" in review_coverage and review_coverage.get("skipped_paths") != data.get("skipped_paths"):
            errors.append(f"review_coverage.skipped_paths must match top-level skipped_paths in {path}")
    top_level_coverage = data.get("coverage_status")
    if inline_coverage_status is not None and top_level_coverage in {"complete", "partial"}:
        if inline_coverage_status != top_level_coverage:
            errors.append(
                f"Inline review_coverage.coverage_status={inline_coverage_status} must match top-level coverage_status={top_level_coverage} in {path}"
            )

    if data.get("review_goal") != core_contract.get("invocation_common_constants", {}).get("review_goal"):
        errors.append(f"Expected review_goal=implementation_correctness in {path}")
    if data.get("findings_contract") != core_contract.get("bootstrap_defaults", {}).get("findings_contract"):
        errors.append(f"Expected findings_contract=shared-findings-v1 in {path}")
    if data.get("review_pass_type") != expected_pass_type:
        errors.append(f"Expected review_pass_type={expected_pass_type} in {path}")

    verifier_output_path = data.get("verifier_output_path")
    if isinstance(verifier_output_path, str) and verifier_output_path:
        resolved_output = resolve_run_path(run_dir_from_attempt_artifact(path), verifier_output_path)
        if resolved_output != findings_path.resolve():
            errors.append(f"verifier_output_path does not match findings.json in {path}")

    if parse_datetime(data.get("start_at")) is None:
        errors.append(f"Invalid or missing ISO start_at in {path}")
    if parse_datetime(data.get("end_at")) is None:
        errors.append(f"Invalid or missing ISO end_at in {path}")
    skipped_paths = data.get("skipped_paths")
    if isinstance(skipped_paths, list) and skipped_paths:
        skip_reasons = data.get("skip_reasons")
        if not isinstance(skip_reasons, list) or not skip_reasons:
            errors.append(f"skip_reasons is required when skipped_paths is non-empty in {path}")
    if expected_pass_type == "working" and data.get("closure_authority") == "challenger_confirmed":
        errors.append(f"working passes must not claim closure_authority=challenger_confirmed in {path}")

    return data, errors


def validate_standalone_spawn_decision(
    path: Path,
    expected_session_role: str,
    allowed_reason_codes: set[str],
    expected_prior_agent_id: str | None,
    expected_source_review: dict | None,
) -> tuple[dict | None, list[str]]:
    data, errors = read_json_object(path)
    if errors:
        return None, errors

    required = [
        "contract",
        "target_ref",
        "next_session_role",
        "requested_agent_type",
        "reason_code",
        "decision",
        "recorded_at",
        "prior_session",
        "evidence",
    ]
    for key in required:
        if key not in data:
            errors.append(f"Missing {key!r} in {path}")

    if data.get("contract") != "review-loop-standalone-spawn-decision-v1":
        errors.append(f"Unexpected contract in {path}")
    if data.get("next_session_role") != expected_session_role:
        errors.append(f"Expected next_session_role={expected_session_role} in {path}")
    if data.get("requested_agent_type") != "verify-reviewer":
        errors.append(f"Expected requested_agent_type=verify-reviewer in {path}")
    if data.get("reason_code") not in allowed_reason_codes:
        allowed = ",".join(sorted(allowed_reason_codes))
        errors.append(f"Expected reason_code in {{{allowed}}} in {path}")
    if data.get("decision") != "allow":
        errors.append(f"Expected decision=allow in {path}")
    if parse_datetime(data.get("recorded_at")) is None:
        errors.append(f"Invalid or missing ISO recorded_at in {path}")

    prior_session = data.get("prior_session")
    if not isinstance(prior_session, dict):
        errors.append(f"Missing prior_session object in {path}")
    else:
        if expected_prior_agent_id is None:
            if prior_session.get("agent_id", "__sentinel__") is not None:
                errors.append(f"Expected prior_session.agent_id=null in {path}")
            if prior_session.get("session_role") != "none":
                errors.append(f"Expected prior_session.session_role=none in {path}")
            if prior_session.get("reuse_blocker") != "not_applicable":
                errors.append(f"Expected prior_session.reuse_blocker=not_applicable in {path}")
        else:
            if prior_session.get("agent_id") != expected_prior_agent_id:
                errors.append(f"Expected prior_session.agent_id={expected_prior_agent_id} in {path}")
            expected_session = "working"
            if prior_session.get("session_role") != expected_session:
                errors.append(f"Expected prior_session.session_role={expected_session} in {path}")
            allowed_blockers = {
                "challenger_pass": {"policy_requires_challenger"},
                "active_session_unavailable": {"agent_not_found"},
                "session_unresumable": {"session_completed_unresumable"},
                "tooling_recovery": {"tooling_recovery"},
            }
            reason_code = data.get("reason_code")
            blockers = allowed_blockers.get(reason_code, set())
            if blockers and prior_session.get("reuse_blocker") not in blockers:
                allowed = ",".join(sorted(blockers))
                errors.append(f"Expected prior_session.reuse_blocker in {{{allowed}}} in {path}")

    evidence = data.get("evidence")
    if not isinstance(evidence, dict):
        errors.append(f"Missing evidence object in {path}")
    elif expected_source_review is not None:
        source_review = evidence.get("source_review")
        if not isinstance(source_review, dict):
            errors.append(f"Missing evidence.source_review in {path}")
        else:
            if source_review.get("agent_id") != expected_source_review["agent_id"]:
                errors.append(f"Expected source_review.agent_id={expected_source_review['agent_id']} in {path}")
            if resolve_run_path(run_dir_from_attempt_artifact(path), source_review.get("findings_path", "")) != expected_source_review["findings_path"]:
                errors.append(f"source_review.findings_path does not reference the expected working findings in {path}")
            if resolve_run_path(run_dir_from_attempt_artifact(path), source_review.get("verifier_evidence_path", "")) != expected_source_review["evidence_path"]:
                errors.append(
                    f"source_review.verifier_evidence_path does not reference the expected working evidence in {path}"
                )

    return data, errors


def validate_openspec_spawn_decision(
    path: Path,
    allowed_reason_codes: set[str],
    expected_prior_agent_id: str | None,
    expected_source_review: dict | None,
) -> tuple[dict | None, list[str]]:
    data, errors = read_json_object(path)
    if errors:
        return None, errors

    required = [
        "contract",
        "change",
        "phase",
        "requested_agent_type",
        "reason_code",
        "decision",
        "recorded_at",
        "prior_session",
        "evidence",
    ]
    for key in required:
        if key not in data:
            errors.append(f"Missing {key!r} in {path}")

    if data.get("contract") != "agent-spawn-decision-v1":
        errors.append(f"Unexpected contract in {path}")
    if data.get("requested_agent_type") != "verify-reviewer":
        errors.append(f"Expected requested_agent_type=verify-reviewer in {path}")
    allowed_phases = set(
        read_contract(OPENSPEC_SPAWN_SCHEMA_PATH).get("properties", {}).get("phase", {}).get("enum", [])
    )
    if data.get("phase") not in allowed_phases:
        allowed = ",".join(sorted(allowed_phases))
        errors.append(f"Expected phase in {{{allowed}}} in {path}")
    reason_code = data.get("reason_code")
    if reason_code not in allowed_reason_codes:
        allowed = ",".join(sorted(allowed_reason_codes))
        errors.append(f"Expected reason_code in {{{allowed}}} in {path}")
    if reason_code in {"challenger_pass"} and data.get("phase") not in {
        "artifact_gate",
        "implementation_verify",
    }:
        errors.append(f"Expected phase in {{artifact_gate,implementation_verify}} in {path}")
    if reason_code in {"challenger_pass"} and expected_source_review is not None:
        expected_phase = (
            "artifact_gate"
            if expected_source_review.get("evidence", {}).get("review_phase") == "docs_first"
            else "implementation_verify"
        )
        if data.get("phase") != expected_phase:
            errors.append(f"Expected phase={expected_phase} in {path}")
    if data.get("decision") != "allow":
        errors.append(f"Expected decision=allow in {path}")
    if parse_datetime(data.get("recorded_at")) is None:
        errors.append(f"Invalid or missing ISO recorded_at in {path}")

    prior_session = data.get("prior_session")
    if not isinstance(prior_session, dict):
        errors.append(f"Missing prior_session object in {path}")
    else:
        if expected_prior_agent_id is None:
            if prior_session.get("agent_id", "__sentinel__") is not None:
                errors.append(f"Expected prior_session.agent_id=null in {path}")
            if prior_session.get("session_role") != "none":
                errors.append(f"Expected prior_session.session_role=none in {path}")
            if data.get("reason_code") == "initial_working_session" and prior_session.get("reuse_blocker") != "not_applicable":
                errors.append(f"Expected prior_session.reuse_blocker=not_applicable in {path}")
        else:
            if prior_session.get("agent_id") != expected_prior_agent_id:
                errors.append(f"Expected prior_session.agent_id={expected_prior_agent_id} in {path}")
            expected_session = "working"
            if prior_session.get("session_role") != expected_session:
                errors.append(f"Expected prior_session.session_role={expected_session} in {path}")
            if reason_code == "challenger_pass" and prior_session.get("reuse_blocker") != "policy_requires_challenger":
                errors.append(f"Expected prior_session.reuse_blocker=policy_requires_challenger in {path}")
            allowed_recovery_blockers = {
                "active_session_unavailable": "agent_not_found",
                "session_unresumable": "session_completed_unresumable",
                "tooling_recovery": "tooling_recovery",
            }
            expected_recovery_blocker = allowed_recovery_blockers.get(reason_code)
            if expected_recovery_blocker and prior_session.get("reuse_blocker") != expected_recovery_blocker:
                errors.append(
                    f"Expected prior_session.reuse_blocker={expected_recovery_blocker} in {path}"
                )

    evidence = data.get("evidence")
    if not isinstance(evidence, dict):
        errors.append(f"Missing evidence object in {path}")
    elif expected_source_review is not None:
        source_review = evidence.get("source_review")
        if not isinstance(source_review, dict):
            errors.append(f"Missing evidence.source_review in {path}")
        else:
            if source_review.get("agent_id") != expected_source_review["agent_id"]:
                errors.append(f"Expected source_review.agent_id={expected_source_review['agent_id']} in {path}")
            if resolve_run_path(run_dir_from_attempt_artifact(path), source_review.get("findings_path", "")) != expected_source_review["findings_path"]:
                errors.append(f"source_review.findings_path does not reference the expected working findings in {path}")
            if resolve_run_path(run_dir_from_attempt_artifact(path), source_review.get("verifier_evidence_path", "")) != expected_source_review["evidence_path"]:
                errors.append(
                    f"source_review.verifier_evidence_path does not reference the expected working evidence in {path}"
                )

    return data, errors


def validate_spawn_decision(
    path: Path,
    expected_session_role: str,
    allowed_reason_codes: set[str],
    expected_prior_agent_id: str | None,
    expected_source_review: dict | None,
) -> tuple[dict | None, list[str]]:
    data, errors = read_json_object(path)
    if errors:
        return None, errors
    contract = data.get("contract")
    if contract == "review-loop-standalone-spawn-decision-v1":
        return validate_standalone_spawn_decision(
            path,
            expected_session_role=expected_session_role,
            allowed_reason_codes=allowed_reason_codes,
            expected_prior_agent_id=expected_prior_agent_id,
            expected_source_review=expected_source_review,
        )
    if contract == "agent-spawn-decision-v1":
        return validate_openspec_spawn_decision(
            path,
            allowed_reason_codes=allowed_reason_codes,
            expected_prior_agent_id=expected_prior_agent_id,
            expected_source_review=expected_source_review,
        )
    return None, [f"Unsupported spawn-decision contract in {path}"]


def subject_binding_from_spawn(spawn_data: dict) -> tuple[str | None, str | None]:
    contract = spawn_data.get("contract")
    if contract == "review-loop-standalone-spawn-decision-v1":
        return "target_ref", spawn_data.get("target_ref")
    if contract == "agent-spawn-decision-v1":
        return "change", spawn_data.get("change")
    return None, None


def validate_reopen_record(
    path: Path,
    expected_subject_key: str | None,
    expected_subject_value: str | None,
    expected_promoted_working_agent_id: str,
    expected_failed_challenger: dict,
) -> tuple[dict | None, list[str]]:
    data, errors = read_json_object(path)
    if errors:
        return None, errors

    required = [
        "contract",
        "reason_code",
        "recorded_at",
        "promoted_working_agent_id",
        "prior_challenger",
    ]
    for key in required:
        if key not in data:
            errors.append(f"Missing {key!r} in {path}")

    subject_keys = [
        key
        for key in ("change", "target_ref")
        if isinstance(data.get(key), str) and data.get(key)
    ]
    if len(subject_keys) != 1:
        errors.append(
            f"Reopen record must include exactly one top-level subject field (change or target_ref) in {path}"
        )
    elif expected_subject_key is not None and expected_subject_value is not None:
        subject_key = subject_keys[0]
        if subject_key != expected_subject_key or data.get(subject_key) != expected_subject_value:
            errors.append(f"Expected {expected_subject_key}={expected_subject_value} in {path}")

    if data.get("contract") != "review-loop-reopen-record-v1":
        errors.append(f"Unexpected contract in {path}")
    if data.get("reason_code") != "challenger_reopen":
        errors.append(f"Expected reason_code=challenger_reopen in {path}")
    if parse_datetime(data.get("recorded_at")) is None:
        errors.append(f"Invalid or missing ISO recorded_at in {path}")
    if data.get("promoted_working_agent_id") != expected_promoted_working_agent_id:
        errors.append(
            f"Expected promoted_working_agent_id={expected_promoted_working_agent_id} in {path}"
        )

    prior_challenger = data.get("prior_challenger")
    if not isinstance(prior_challenger, dict):
        errors.append(f"Missing prior_challenger object in {path}")
        return data, errors

    if prior_challenger.get("agent_id") != expected_failed_challenger["agent_id"]:
        errors.append(
            f"Expected prior_challenger.agent_id={expected_failed_challenger['agent_id']} in {path}"
        )
    if (
        resolve_run_path(run_dir_from_attempt_artifact(path), prior_challenger.get("findings_path", ""))
        != expected_failed_challenger["findings_path"]
    ):
        errors.append(f"prior_challenger.findings_path does not reference the expected challenger findings in {path}")
    if (
        resolve_run_path(run_dir_from_attempt_artifact(path), prior_challenger.get("verifier_evidence_path", ""))
        != expected_failed_challenger["evidence_path"]
    ):
        errors.append(
            f"prior_challenger.verifier_evidence_path does not reference the expected challenger evidence in {path}"
        )
    return data, errors


def validate_subject_binding(evidence: dict, evidence_path: Path, subject_key: str, subject_value: str) -> list[str]:
    errors: list[str] = []
    if evidence.get(subject_key) != subject_value:
        errors.append(f"Expected {subject_key}={subject_value} in {evidence_path}")
    return errors


def validate_spawn_subject_binding(spawn_data: dict, spawn_path: Path, subject_key: str, subject_value: str) -> list[str]:
    errors: list[str] = []
    spawn_subject_key, spawn_subject_value = subject_binding_from_spawn(spawn_data)
    if spawn_subject_key != subject_key or spawn_subject_value != subject_value:
        errors.append(f"Expected {subject_key}={subject_value} in {spawn_path}")
    return errors


def validate_task_template_ids(path: Path) -> list[str]:
    errors: list[str] = []
    task_line_pattern = re.compile(r"^- \[[ xX]\] (\d+\.\d+) ", re.MULTILINE)
    checklist_line_pattern = re.compile(r"^- \[[ xX]\] ", re.MULTILINE)
    text = path.read_text()
    ids = task_line_pattern.findall(text)
    if len(checklist_line_pattern.findall(text)) != len(ids):
        errors.append(f"Checklist items in {path} must use unique X.Y task ids")
    seen: set[str] = set()
    for task_id in ids:
        if task_id in seen:
            errors.append(f"Duplicate task id {task_id!r} in {path}")
        seen.add(task_id)
    return errors


def validate_attempt(run_dir: Path, attempt_dir: Path, expected_pass_type: str) -> tuple[dict | None, list[str]]:
    errors: list[str] = []
    findings_path = attempt_dir / "findings.json"
    evidence_path = attempt_dir / "verifier-evidence.json"
    spawn_path = attempt_dir / "spawn-decision.json"

    for path in [findings_path, evidence_path]:
        if not path.exists():
            errors.append(f"Missing required run artifact: {path}")
    if errors:
        return None, errors

    findings_data, finding_errors = validate_findings_payload(findings_path)
    evidence_data, evidence_errors = validate_evidence_payload(
        evidence_path,
        findings_path=findings_path,
        expected_pass_type=expected_pass_type,
    )
    errors += finding_errors + evidence_errors
    if findings_data is not None and evidence_data is not None:
        evidence_subject_keys = [
            key
            for key in ("change", "target_ref")
            if isinstance(evidence_data.get(key), str) and evidence_data.get(key)
        ]
        if len(evidence_subject_keys) == 1:
            subject_key = evidence_subject_keys[0]
            if findings_data.get(subject_key) != evidence_data.get(subject_key):
                errors.append(
                    f"Findings payload must record {subject_key}={evidence_data.get(subject_key)} in {findings_path}"
                )
        if evidence_data.get("review_phase") == "docs_first":
            for finding in findings_data.get("findings", []):
                if finding.get("auto_fixable") is True:
                    errors.append(
                        f"docs_first passes must not mark findings auto_fixable=true in {findings_path}"
                    )
        if (
            expected_pass_type == "challenger"
            and evidence_data.get("closure_authority") == "challenger_confirmed"
            and (
                findings_data.get("final_assessment") != "pass"
                or findings_data.get("findings") != []
            )
        ):
            errors.append(
                f"closure_authority=challenger_confirmed is only valid on zero-findings challenger passes in {evidence_path}"
            )
    if errors or findings_data is None or evidence_data is None:
        return None, errors

    return {
        "attempt_dir": attempt_dir,
        "findings_path": findings_path.resolve(),
        "evidence_path": evidence_path.resolve(),
        "spawn_path": spawn_path.resolve(),
        "findings": findings_data,
        "evidence": evidence_data,
        "agent_id": evidence_data["agent_id"],
        "start_at": parse_datetime(evidence_data["start_at"]),
        "end_at": parse_datetime(evidence_data["end_at"]),
    }, errors


def collect_attempts(run_dir: Path, pass_type: str) -> tuple[list[dict], list[str]]:
    errors: list[str] = []
    metadata: list[dict] = []
    for attempt_dir in attempt_dirs(run_dir / pass_type):
        attempt_metadata, attempt_errors = validate_attempt(run_dir, attempt_dir, expected_pass_type=pass_type)
        errors += attempt_errors
        if attempt_metadata is None:
            continue
        attempt_metadata["pass_type"] = pass_type
        metadata.append(attempt_metadata)
    return metadata, errors


def attempt_sequence_sort_key(attempt: dict) -> tuple[datetime, datetime, str]:
    start_at = attempt.get("start_at") or datetime.min
    end_at = attempt.get("end_at") or datetime.min
    return (start_at, end_at, str(attempt["attempt_dir"]))


def validate_challenger_entry_requirements(working_attempt: dict) -> list[str]:
    errors: list[str] = []
    findings = working_attempt["findings"].get("findings")
    if working_attempt["findings"].get("final_assessment") != "pass":
        errors.append(
            f"Challenger gate requires final_assessment=pass in {working_attempt['findings_path']}"
        )
    if findings != []:
        errors.append(f"Challenger gate requires empty findings in {working_attempt['findings_path']}")
    if working_attempt["evidence"].get("coverage_status") != "complete":
        errors.append(
            f"Challenger gate requires coverage_status=complete in {working_attempt['evidence_path']}"
        )
    if working_attempt["evidence"].get("saturation_status") != "exhaustive":
        errors.append(
            f"Challenger gate requires saturation_status=exhaustive in {working_attempt['evidence_path']}"
        )
    return errors


def validate_final_challenger_closure(final_attempt: dict) -> list[str]:
    errors: list[str] = []
    if final_attempt["pass_type"] != "challenger":
        errors.append("Final validated run must end on a challenger attempt")
        return errors
    if final_attempt["findings"].get("final_assessment") != "pass":
        errors.append(
            f"Final challenger must record final_assessment=pass in {final_attempt['findings_path']}"
        )
    if final_attempt["findings"].get("findings") != []:
        errors.append(f"Final challenger must have empty findings in {final_attempt['findings_path']}")
    if final_attempt["evidence"].get("closure_authority") != "challenger_confirmed":
        errors.append(
            f"Final challenger must record closure_authority=challenger_confirmed in {final_attempt['evidence_path']}"
        )
    if final_attempt["evidence"].get("coverage_status") != "complete":
        errors.append(
            f"Final challenger must record coverage_status=complete in {final_attempt['evidence_path']}"
        )
    if final_attempt["evidence"].get("saturation_status") != "exhaustive":
        errors.append(
            f"Final challenger must record saturation_status=exhaustive in {final_attempt['evidence_path']}"
        )
    return errors


def validate_run_dir(run_dir: Path) -> list[str]:
    run_dir = run_dir.resolve()
    if not run_dir.exists():
        return [f"Run directory does not exist: {run_dir}"]

    errors: list[str] = []
    working_attempts, working_errors = collect_attempts(run_dir, "working")
    challenger_attempts, challenger_errors = collect_attempts(run_dir, "challenger")
    errors += working_errors + challenger_errors

    if not working_attempts:
        return errors + [f"No working attempts found under {run_dir / 'working'}"]

    sequence = sorted([*working_attempts, *challenger_attempts], key=attempt_sequence_sort_key)
    first_attempt = sequence[0]
    first_spawn_path = first_attempt["attempt_dir"] / "spawn-decision.json"
    spawn_data: dict | None = None
    if first_attempt["pass_type"] != "working":
        errors.append("Review sequence must start with a working attempt")
        subject_binding: tuple[str | None, str | None] = (None, None)
    elif not first_spawn_path.exists():
        errors.append(f"Missing required initial working spawn-decision.json: {first_spawn_path}")
        subject_binding = (None, None)
    else:
        spawn_data, spawn_errors = validate_spawn_decision(
            first_spawn_path,
            expected_session_role="working",
            allowed_reason_codes={"initial_working_session"},
            expected_prior_agent_id=None,
            expected_source_review=None,
        )
        errors += spawn_errors
        subject_binding = subject_binding_from_spawn(spawn_data or {})

    subject_key, subject_value = subject_binding
    if subject_key is None or not isinstance(subject_value, str):
        errors.append("Could not resolve subject binding from the initial spawn decision")
    elif spawn_data is not None:
        errors += validate_spawn_subject_binding(spawn_data, first_spawn_path, subject_key, subject_value)

    challenger_count = 0
    for index, attempt in enumerate(sequence):
        if subject_key and isinstance(subject_value, str):
            errors += validate_subject_binding(
                attempt["evidence"],
                attempt["evidence_path"],
                subject_key,
                subject_value,
            )
        if index == 0:
            continue

        previous = sequence[index - 1]
        spawn_path = attempt["attempt_dir"] / "spawn-decision.json"
        reopen_path = attempt["attempt_dir"] / "reopen-record.json"

        if attempt["pass_type"] == "challenger":
            challenger_count += 1
            if previous["pass_type"] != "working":
                errors.append(f"Challenger attempt must directly follow a working attempt in {attempt['attempt_dir']}")
                continue
            if not spawn_path.exists():
                errors.append(f"Missing required challenger spawn-decision.json: {spawn_path}")
                continue
            if attempt["agent_id"] == previous["agent_id"]:
                errors.append(
                    f"Challenger session must be fresh; agent_id {attempt['agent_id']} was reused in {attempt['attempt_dir']}"
                )
            if attempt.get("start_at") and previous.get("end_at") and attempt["start_at"] <= previous["end_at"]:
                errors.append(
                    f"Challenger attempt starts before the preceding working attempt finished in {attempt['attempt_dir']}"
                )
            errors += validate_challenger_entry_requirements(previous)
            spawn_data, spawn_errors = validate_spawn_decision(
                spawn_path,
                expected_session_role="challenger",
                allowed_reason_codes={"challenger_pass"},
                expected_prior_agent_id=previous["agent_id"],
                expected_source_review=previous,
            )
            errors += spawn_errors
            if subject_key and isinstance(subject_value, str) and spawn_data is not None:
                errors += validate_spawn_subject_binding(spawn_data, spawn_path, subject_key, subject_value)
            continue

        if previous["pass_type"] == "working":
            if attempt["agent_id"] == previous["agent_id"]:
                if spawn_path.exists():
                    errors.append(
                        f"Unexpected spawn-decision.json for same-session working rerun in {spawn_path}"
                    )
            else:
                if not spawn_path.exists():
                    errors.append(
                        f"Working agent_id changed from {previous['agent_id']} to {attempt['agent_id']} without recovery spawn-decision in {attempt['attempt_dir']}"
                    )
                else:
                    spawn_data, spawn_errors = validate_spawn_decision(
                        spawn_path,
                        expected_session_role="working",
                        allowed_reason_codes={
                            "active_session_unavailable",
                            "session_unresumable",
                            "tooling_recovery",
                        },
                        expected_prior_agent_id=previous["agent_id"],
                        expected_source_review=None,
                    )
                    errors += spawn_errors
                    if subject_key and isinstance(subject_value, str) and spawn_data is not None:
                        errors += validate_spawn_subject_binding(spawn_data, spawn_path, subject_key, subject_value)
            continue

        previous_challenger_passed = (
            previous["findings"].get("final_assessment") == "pass"
            and previous["findings"].get("findings") == []
            and previous["evidence"].get("closure_authority") == "challenger_confirmed"
        )
        if previous_challenger_passed:
            errors.append(
                f"Working attempt {attempt['attempt_dir']} cannot follow a closing challenger pass"
            )
            continue
        if index < 2 or sequence[index - 2]["pass_type"] != "working":
            errors.append(
                f"Reopened working attempt must follow a challenger that directly followed a working attempt in {attempt['attempt_dir']}"
            )
            continue
        if spawn_path.exists():
            errors.append(
                f"Unexpected spawn-decision.json for same-session challenger reopen in {spawn_path}"
            )
        if not reopen_path.exists():
            errors.append(f"Missing required challenger_reopen reopen-record.json: {reopen_path}")
            continue
        if attempt["agent_id"] != previous["agent_id"]:
            errors.append(
                f"Reopened working attempt must promote the failed challenger session into the next working session in {attempt['attempt_dir']}"
            )
        reopen_data, reopen_errors = validate_reopen_record(
            reopen_path,
            expected_subject_key=subject_key,
            expected_subject_value=subject_value if isinstance(subject_value, str) else None,
            expected_promoted_working_agent_id=previous["agent_id"],
            expected_failed_challenger=previous,
        )
        errors += reopen_errors

    if challenger_count == 0 and challenger_attempts:
        challenger_count = len(challenger_attempts)
    if challenger_count == 0:
        errors.append("Final validated run requires at least one challenger attempt")
    errors += validate_final_challenger_closure(sequence[-1])
    return errors


def validate(scope: str, repo_root: Path | None) -> list[str]:
    errors: list[str] = []
    for path in [
        CORE_CONTRACT_PATH,
        MODULE_ROOT / "contracts/review-loop-openspec-adapter-v1.json",
        STANDALONE_ADAPTER_PATH,
        STANDALONE_SPAWN_CONTRACT_PATH,
        OPENSPEC_SPAWN_SCHEMA_PATH,
    ]:
        errors += load_json(path)

    scopes = selected_scopes(scope, repo_root)
    for path, patterns in scopes.items():
        if not path.exists():
            errors.append(f"Missing required file: {path}")
            continue
        errors += require_tokens(path, patterns)

    forbidden = [
        "review-contract-v1.json",
        "standalone-review-contract-v1.json",
        "docs/review-flow/contracts/standalone-spawn-decision-v1.json",
    ]
    for path in scopes:
        if path.exists():
            errors += forbid_tokens(path, forbidden)

    if scope in {"standalone", "all"}:
        for path, patterns in standalone_forbidden_tokens(repo_root).items():
            if path.exists():
                errors += forbid_tokens(path, patterns)

    tasks_template = OPENSPEC_ROOT / "schemas/ai-enforced-workflow/templates/tasks.md"
    if tasks_template.exists():
        errors += validate_task_template_ids(tasks_template)

    return errors


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate or autofix review-loop module integration.")
    parser.add_argument(
        "--scope",
        choices=["module", "openspec", "standalone", "all"],
        default="all",
        help="Validation scope",
    )
    parser.add_argument(
        "--repo-root",
        help="Repository root for standalone docs validation",
    )
    parser.add_argument(
        "--fix",
        action="store_true",
        help="Apply known textual fixes before validating",
    )
    parser.add_argument(
        "--run-dir",
        help="Validate a concrete review run directory that follows the module workflow",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    repo_root = detect_repo_root(args.repo_root)
    scopes = list(selected_scopes(args.scope, repo_root).keys())

    if args.fix:
        changed = autofix(scopes)
        if changed:
            print("review-loop autofix updated:")
            for path in changed:
                print(f"- {path}")

    errors = validate(args.scope, repo_root)
    if args.run_dir:
        errors += validate_run_dir(Path(args.run_dir))
    if errors:
        print("review-loop validation failed:")
        for error in errors:
            print(f"- {error}")
        return 1

    print("review-loop validation passed.")
    print(f"scope={args.scope}")
    if repo_root:
        print(f"repo_root={repo_root}")
    if args.run_dir:
        print(f"run_dir={Path(args.run_dir).resolve()}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
