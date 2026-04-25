#!/usr/bin/env python3
"""
Mechanical validation entrypoint for the direct Stage A rollout.

This intentionally does not depend on `openspec/changes/*`.
It validates the explicit workflow files that were edited for the no-change
rollout and exits non-zero on contract drift.
"""

from __future__ import annotations

import json
import py_compile
import re
import shutil
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path

try:
    import jsonschema
except ModuleNotFoundError as exc:  # pragma: no cover - environment-specific
    raise SystemExit("jsonschema is required to validate Stage A contracts") from exc

try:
    import yaml
except ModuleNotFoundError as exc:  # pragma: no cover - environment-specific
    raise SystemExit("PyYAML is required to validate schema.yaml") from exc

SCRIPT = Path(__file__).resolve()
OPENSPEC_ROOT = SCRIPT.parents[1]

if not (OPENSPEC_ROOT / "schemas").exists():
    raise SystemExit(f"Cannot locate openspec/global-openspec root from {SCRIPT}")

if OPENSPEC_ROOT.name == "openspec":
    REPO_ROOT = OPENSPEC_ROOT.parent
    DOCS_ROOT = REPO_ROOT / "docs"
    AGENTS_ROOT = REPO_ROOT / ".codex" / "agents"
    CODEX_HOME = Path.home() / ".codex"
elif OPENSPEC_ROOT.name == "_global-openspec":
    REPO_ROOT = None
    DOCS_ROOT = OPENSPEC_ROOT / "docs"
    CODEX_HOME = OPENSPEC_ROOT.parents[1]
    AGENTS_ROOT = CODEX_HOME / "agents"
else:
    raise SystemExit(f"Unsupported openspec root layout: {OPENSPEC_ROOT}")

MODULE_ROOT = OPENSPEC_ROOT / "schemas/modules/verification-cycle"
ORCHESTRATOR_ROOT = MODULE_ROOT / "orchestrator"
VERIFICATION_CYCLE_GUARD = MODULE_ROOT / "bin/verification_cycle_guard.py"
REFERENCE_INDEX_PATH = MODULE_ROOT / "REFERENCE-INDEX.md"

JSON_FILES = [
    OPENSPEC_ROOT / "schemas/ai-enforced-workflow/agent-spawn-decision-v1.schema.json",
    MODULE_ROOT / "contracts/verification-cycle-core-v1.json",
    MODULE_ROOT / "contracts/verification-cycle-agent-table-v1.json",
    MODULE_ROOT / "contracts/verification-cycle-openspec-adapter-v1.json",
    MODULE_ROOT / "contracts/verification-cycle-standalone-adapter-v1.json",
    ORCHESTRATOR_ROOT / "contracts/orchestrator-input-v1.json",
    ORCHESTRATOR_ROOT / "contracts/orchestrator-decision-v1.json",
]

YAML_FILES = [
    OPENSPEC_ROOT / "schemas/ai-enforced-workflow/schema.yaml",
]

TOML_FILES = [
    AGENTS_ROOT / "verify-reviewer.toml",
]

PYTHON_FILES = [
    SCRIPT,
    VERIFICATION_CYCLE_GUARD,
    ORCHESTRATOR_ROOT / "bin/verification_cycle_resolve.py",
    ORCHESTRATOR_ROOT / "bin/verification_cycle_validate.py",
]

RUN_DIR_FIXTURES = [
    MODULE_ROOT / "fixtures/review-runs/standalone-resume-repair-close",
    MODULE_ROOT / "fixtures/review-runs/standalone-partial-scope-repair-close",
]

COMBINED_OUTPUT_FIXTURE = MODULE_ROOT / "fixtures/verifier-output/combined-envelope-pass.json"

SCOPE_PATTERNS = {
    DOCS_ROOT / "review-flow/README.md": [
        "active `verification-cycle` model",
        "resume active",
        "terminate on valid active pass",
    ],
    DOCS_ROOT / "reusable-review-flow.md": [
        "active reusable flow is `verification-cycle`",
        "resume usable `active` first",
        "partial review must declare `scope`",
    ],
    OPENSPEC_ROOT / "specs/verify-subagent-orchestration/spec.md": [
        "verify-reviewer-inline-v3",
        "verification-cycle-core-v1.json",
        "verification-cycle-openspec-adapter-v1.json",
        "verification-cycle-agent-table-v1.json",
        "`active`",
        "`non_active`",
        "`send_input`",
        "`agent not found`",
        "`no_usable_active_agent` means only that `agent-table.json`",
        "`continuation_probe`",
        "`block` to a valid `pass`",
        "`review_coverage.coverage_status=complete`",
        "`review_coverage.exhaustive=true`",
    ],
    OPENSPEC_ROOT / "schemas/ai-enforced-workflow/verification-sequence.md": [
        "continue active verifier",
        "prefer send_input while still open",
        "if send_input says agent not found, resume that same active agent",
        "use continuation_probe to distinguish resume from recovery spawn",
        "`spawn_reason_code`",
        "normalized JSON only; malformed output blocks the step",
        "verify-reviewer-inline-v3",
        "verification-cycle-core-v1.json",
        "verification-cycle-agent-table-v1.json",
        "mark_non_active",
        "terminate on valid active pass",
        "current-state-only",
    ],
    OPENSPEC_ROOT / "schemas/ai-enforced-workflow/schema.yaml": [
        "verification-cycle-core-v1.json",
        "verification-cycle-openspec-adapter-v1.json",
        "verification-cycle-agent-table-v1.json",
        "verify-reviewer-inline-v3",
        "cycle_rules",
        "agent-table.json",
    ],
    OPENSPEC_ROOT / "schemas/ai-enforced-workflow/templates/design.md": [
        "verification-cycle-core-v1.json",
        "verification-cycle-openspec-adapter-v1.json",
        "verification-cycle-agent-table-v1.json",
        "`active/non_active` verification cycle",
        "`verify-reviewer-inline-v3`",
        "verification continues a usable `active` agent first",
        "prefer `send_input` while that same `active` agent is still open",
        "use `continuation_probe` to distinguish resume from recovery spawn",
        "only `block -> pass` marks `non_active`",
    ],
    OPENSPEC_ROOT / "schemas/ai-enforced-workflow/templates/tasks.md": [
        "verification-cycle-core-v1.json",
        "verification-cycle-openspec-adapter-v1.json",
        "verification-cycle-agent-table-v1.json",
        "agent-table.json",
        "prefer `send_input` while the same active agent is still open",
        "`send_input` returning `agent not found` still routes to `resume`, not",
        "current-state-only `agent-table.json`",
        "`no_usable_active_agent` means only that `agent-table.json` literally",
        "only `block -> pass` marks an agent `non_active`",
        "valid active pass",
    ],
    AGENTS_ROOT / "verify-reviewer.toml": [
        'sandbox_mode = "read-only"',
        "`fork_context=false`",
        "Accept the minimal verification bundle fields:",
        "findings_path",
        "verifier_evidence_path",
        "agent_table_path",
        "Do not require or rely on full parent conversation transcript.",
        "Treat full parent conversation inheritance as invalid verifier input.",
        "`verdict` MUST be `block|pass`",
        "`template_id` MUST be `verify-reviewer-inline-v3`.",
        "Do not return markdown-only summaries as the final result.",
        "If the pass is partial, explicitly describe scope in verifier evidence.",
        "A pass is valid only when coverage is complete and exhaustive.",
        "fast_pass_evidence_required",
    ],
    MODULE_ROOT / "fixtures/README.md": [
        "attempt-*",
        "each `attempt-*` directory contains `findings.json`",
        "each `attempt-*` directory contains `verifier-evidence.json`",
        "`fixtures/verifier-output/`",
    ],
    CODEX_HOME / "skills/openspec-artifact-verify/SKILL.md": [
        "verification-cycle/orchestrator/CALLER-INTEGRATION.md",
        "verify-reviewer-inline-v3",
        "agent-table.json",
    ],
    CODEX_HOME / "skills/openspec-verify-change/SKILL.md": [
        "verification-cycle-core-v1.json",
        "verification-cycle/orchestrator/CALLER-INTEGRATION.md",
        "verify-reviewer-inline-v3",
        "agent-table.json",
    ],
    CODEX_HOME / "skills/openspec-repair-change/SKILL.md": [
        "CALLER-INTEGRATION.md",
        "current `agent-table.json`",
        "cycle_rules",
    ],
    CODEX_HOME / "skills/openspec-apply-change/SKILL.md": [
        "CALLER-INTEGRATION.md",
        "verification-cycle-core-v1.json",
        "verification-cycle-openspec-adapter-v1.json",
        "verification-cycle-agent-table-v1.json",
        "verify-reviewer-inline-v3",
        "cycle_rules",
        "agent-table.json",
    ],
    CODEX_HOME / "skills/openspec-continue-change/SKILL.md": [
        "CALLER-INTEGRATION.md",
        "cycle_rules",
        "agent-table.json",
    ],
    CODEX_HOME / "skills/openspec-propose/SKILL.md": [
        "CALLER-INTEGRATION.md",
        "cycle_rules",
        "agent-table.json",
    ],
    OPENSPEC_ROOT / "schemas/ai-enforced-workflow/agent-spawn-decision-v1.schema.json": [
        "\"deprecated\": true",
        "\"legacy_status\"",
        "no_usable_active_agent",
        "active_agent_missing",
        "active_agent_not_resumable",
        "\"status\"",
        "\"active\"",
        "\"agent_table_path\"",
    ],
    MODULE_ROOT / "README.md": [
        "current-state agent baseline",
        "eight rules",
        "prefer `send_input`",
        "agent-table.json",
    ],
    MODULE_ROOT / "VERIFY-IMPLEMENTATION.md": [
        "Valid pass rule:",
        "Execution metadata constraints:",
        "If `review_coverage.coverage_status=partial`, `review_scope.scope` must be",
        "prefer `send_input`",
        "If `send_input` returns `agent not found`",
        "`no_usable_active_agent` has only that literal",
        "`continuation_probe`",
        "Do not invent archived dual-session phases.",
    ],
    MODULE_ROOT / "REFERENCE-INDEX.md": [
        "contracts/verification-cycle-core-v1.json",
        "contracts/verification-cycle-agent-table-v1.json",
        "orchestrator/contracts/orchestrator-input-v1.json",
        "bin/verification_cycle_guard.py",
        "standalone-resume-repair-close",
        "standalone-partial-scope-repair-close",
    ],
    ORCHESTRATOR_ROOT / "CALLER-INTEGRATION.md": [
        "execute the returned decision exactly",
        "`resume_active`",
        "prefer `send_input`",
        "`latest_result`",
        "`continuation_probe` is the only recovery input",
        "`spawn_reason_code`",
        "`active_agent_missing`",
        "`active_agent_not_resumable`",
        "`mark_non_active`",
        "`blocked_invalid_state`",
    ],
}

LEGACY_FORBIDDEN = [
    "verify-reviewer-inline-v2",
    "transition_resolver_resolve.py",
    "review_loop_guard.py",
    "closure_authority",
    "review_pass_type",
    "saturation_status",
    "challenger_entry",
    "reopen-record",
    "initial_working_session",
    "challenger_pass",
    "policy_requires_challenger",
]

LEGACY_FORBID_PATHS = [
    OPENSPEC_ROOT / "specs/verify-subagent-orchestration/spec.md",
    OPENSPEC_ROOT / "schemas/ai-enforced-workflow/verification-sequence.md",
    OPENSPEC_ROOT / "schemas/ai-enforced-workflow/templates/design.md",
    OPENSPEC_ROOT / "schemas/ai-enforced-workflow/templates/tasks.md",
    AGENTS_ROOT / "verify-reviewer.toml",
    CODEX_HOME / "skills/openspec-apply-change/SKILL.md",
    CODEX_HOME / "skills/openspec-continue-change/SKILL.md",
    CODEX_HOME / "skills/openspec-propose/SKILL.md",
    OPENSPEC_ROOT / "schemas/ai-enforced-workflow/agent-spawn-decision-v1.schema.json",
]


def load_syntax() -> list[str]:
    errors: list[str] = []
    for path in JSON_FILES:
        try:
            json.loads(path.read_text())
        except Exception as exc:  # pragma: no cover - defensive
            errors.append(f"JSON parse failed: {path}: {exc}")
    for path in YAML_FILES:
        try:
            yaml.safe_load(path.read_text())
        except Exception as exc:  # pragma: no cover - defensive
            errors.append(f"YAML parse failed: {path}: {exc}")
    for path in TOML_FILES:
        try:
            tomllib.loads(path.read_text())
        except Exception as exc:  # pragma: no cover - defensive
            errors.append(f"TOML parse failed: {path}: {exc}")
    for path in PYTHON_FILES:
        try:
            py_compile.compile(str(path), doraise=True)
        except Exception as exc:  # pragma: no cover - defensive
            errors.append(f"Python compile failed: {path}: {exc}")
    return errors


def require(path: Path, patterns: list[str]) -> list[str]:
    text = path.read_text()
    errors: list[str] = []
    for pattern in patterns:
        if pattern not in text:
            errors.append(f"Missing required token {pattern!r} in {path}")
    return errors


def forbid(path: Path, patterns: list[str]) -> list[str]:
    text = path.read_text()
    errors: list[str] = []
    for pattern in patterns:
        if pattern in text:
            errors.append(f"Forbidden token {pattern!r} present in {path}")
    return errors


def require_all_in_same_line(path: Path, tokens: list[str]) -> list[str]:
    errors: list[str] = []
    for line in path.read_text().splitlines():
        if all(token in line for token in tokens):
            return errors
    errors.append(
        f"Missing line containing all required tokens {tokens!r} in {path}"
    )
    return errors


def require_tokens_between(path: Path, start_token: str, end_token: str, tokens: list[str]) -> list[str]:
    text = path.read_text()
    start = text.find(start_token)
    if start == -1:
        return [f"Missing start token {start_token!r} in {path}"]
    end = text.find(end_token, start)
    if end == -1:
        return [f"Missing end token {end_token!r} after {start_token!r} in {path}"]
    section = " ".join(text[start:end].split())
    errors: list[str] = []
    for token in tokens:
        if " ".join(token.split()) not in section:
            errors.append(f"Missing required token {token!r} between {start_token!r} and {end_token!r} in {path}")
    return errors


def validate_reference_targets() -> list[str]:
    errors: list[str] = []
    for path in [
        MODULE_ROOT / "README.md",
        MODULE_ROOT / "VERIFY-IMPLEMENTATION.md",
        REFERENCE_INDEX_PATH,
        ORCHESTRATOR_ROOT / "CALLER-INTEGRATION.md",
    ]:
        if not path.exists():
            errors.append(f"Missing verification-cycle reference file: {path}")
    return errors


def validate_verify_implementation_structure() -> list[str]:
    return require(
        MODULE_ROOT / "VERIFY-IMPLEMENTATION.md",
        [
            "OpenSpec usage:",
            "Standalone usage:",
            "Execution metadata constraints:",
            "Valid pass rule:",
            "If `review_coverage.coverage_status=partial`, `review_scope.scope` must be",
            "If `send_input` returns `agent not found`",
            "Terminate only when the current `active` agent returns a valid `pass`",
        ],
    )


def validate_reference_index_entries() -> list[str]:
    return require(
        REFERENCE_INDEX_PATH,
        [
            "## Authoritative Runtime Surface",
            "`contracts/verification-cycle-core-v1.json`",
            "`contracts/verification-cycle-agent-table-v1.json`",
            "`contracts/verification-cycle-openspec-adapter-v1.json`",
            "`contracts/verification-cycle-standalone-adapter-v1.json`",
            "`orchestrator/contracts/orchestrator-input-v1.json`",
            "`orchestrator/contracts/orchestrator-decision-v1.json`",
            "`orchestrator/bin/verification_cycle_resolve.py`",
            "`orchestrator/bin/verification_cycle_validate.py`",
            "`bin/verification_cycle_guard.py`",
            "## Active Reference Runs",
            "standalone-resume-repair-close",
            "standalone-partial-scope-repair-close",
        ],
    )


def validate_verification_cycle_fixtures() -> list[str]:
    errors: list[str] = []
    for run_dir in RUN_DIR_FIXTURES:
        if not run_dir.exists():
            errors.append(f"Missing verification-cycle fixture run dir: {run_dir}")
            continue
        result = subprocess.run(
            [sys.executable, str(VERIFICATION_CYCLE_GUARD), "--run-dir", str(run_dir)],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            detail = (result.stdout + result.stderr).strip()
            errors.append(f"Fixture run-dir validation failed for {run_dir}: {detail}")
    return errors


def validate_negative_guard_cases() -> list[str]:
    source_fixture = RUN_DIR_FIXTURES[1]
    core_contract = json.loads(
        (MODULE_ROOT / "contracts/verification-cycle-core-v1.json").read_text()
    )
    errors: list[str] = []

    def run_mutated_case(case_name: str, mutate: callable, expected_markers: list[str]) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            temp_run = Path(tmpdir) / case_name
            shutil.copytree(source_fixture, temp_run)
            mutate(temp_run)
            result = subprocess.run(
                [sys.executable, str(VERIFICATION_CYCLE_GUARD), "--run-dir", str(temp_run)],
                capture_output=True,
                text=True,
                check=False,
            )
            if result.returncode == 0:
                errors.append(f"Negative guard case unexpectedly passed: {case_name}")
                return
            detail = result.stdout + result.stderr
            for marker in expected_markers:
                if marker not in detail:
                    errors.append(
                        f"Negative guard case {case_name} failed without expected marker {marker!r}"
                    )

    def mutate_missing_scope(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        payload = json.loads(evidence_path.read_text())
        payload["review_scope"]["scope"] = ""
        evidence_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_bad_non_active(temp_run: Path) -> None:
        table_path = temp_run / "agent-table.json"
        payload = json.loads(table_path.read_text())
        payload["agents"][0]["status"] = "non_active"
        payload["agents"][0]["last_verdict"] = "block"
        table_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_invalid_terminal_pass(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-3/verifier-evidence.json"
        table_path = temp_run / "agent-table.json"
        table_payload = json.loads(table_path.read_text())
        evidence_payload = json.loads(evidence_path.read_text())
        table_payload["agents"][0]["coverage_status"] = "partial"
        table_payload["agents"][0]["exhaustive"] = False
        table_payload["agents"][0]["scope"] = ""
        evidence_payload["review_coverage"]["coverage_status"] = "partial"
        evidence_payload["review_coverage"]["exhaustive"] = False
        evidence_payload["review_scope"]["scope"] = ""
        table_path.write_text(json.dumps(table_payload, indent=2) + "\n")
        evidence_path.write_text(json.dumps(evidence_payload, indent=2) + "\n")

    def mutate_subject_mismatch(temp_run: Path) -> None:
        findings_path = temp_run / "attempt-1/findings.json"
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        findings_payload = json.loads(findings_path.read_text())
        evidence_payload = json.loads(evidence_path.read_text())
        findings_payload["target_ref"] = "workflow:alpha"
        evidence_payload["target_ref"] = "workflow:beta"
        findings_path.write_text(json.dumps(findings_payload, indent=2) + "\n")
        evidence_path.write_text(json.dumps(evidence_payload, indent=2) + "\n")

    def mutate_duplicate_finding_ids(temp_run: Path) -> None:
        findings_path = temp_run / "attempt-1/findings.json"
        payload = json.loads(findings_path.read_text())
        payload["findings"].append(dict(payload["findings"][0]))
        findings_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_scalar_agent_table(temp_run: Path) -> None:
        table_path = temp_run / "agent-table.json"
        table_path.write_text('"active"\n')

    def mutate_agent_table_missing_subject(temp_run: Path) -> None:
        table_path = temp_run / "agent-table.json"
        payload = json.loads(table_path.read_text())
        payload.pop("change", None)
        payload.pop("target_ref", None)
        table_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_agent_table_missing_updated_at(temp_run: Path) -> None:
        table_path = temp_run / "agent-table.json"
        payload = json.loads(table_path.read_text())
        payload["agents"][0].pop("updated_at", None)
        table_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_agent_table_invalid_path_type(temp_run: Path) -> None:
        table_path = temp_run / "agent-table.json"
        payload = json.loads(table_path.read_text())
        payload["agents"][0]["verifier_evidence_path"] = 123
        table_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_agent_table_subject_mismatch(temp_run: Path) -> None:
        table_path = temp_run / "agent-table.json"
        payload = json.loads(table_path.read_text())
        payload["target_ref"] = "workflow:mismatch"
        table_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_agent_table_invalid_updated_at_format(temp_run: Path) -> None:
        table_path = temp_run / "agent-table.json"
        payload = json.loads(table_path.read_text())
        payload["agents"][0]["updated_at"] = "not-a-datetime"
        table_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_agent_table_unexpected_property(temp_run: Path) -> None:
        table_path = temp_run / "agent-table.json"
        payload = json.loads(table_path.read_text())
        payload["agents"][0]["unexpected"] = "x"
        table_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_agent_table_broken_paths(temp_run: Path) -> None:
        table_path = temp_run / "agent-table.json"
        payload = json.loads(table_path.read_text())
        payload["agents"][0]["findings_path"] = "missing/nowhere.json"
        payload["agents"][0]["verifier_evidence_path"] = "missing/nowhere-evidence.json"
        table_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_agent_table_directory_paths(temp_run: Path) -> None:
        table_path = temp_run / "agent-table.json"
        payload = json.loads(table_path.read_text())
        payload["agents"][0]["findings_path"] = "attempt-1"
        payload["agents"][0]["verifier_evidence_path"] = "attempt-1"
        table_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_agent_table_wrong_existing_file(temp_run: Path) -> None:
        table_path = temp_run / "agent-table.json"
        payload = json.loads(table_path.read_text())
        payload["agents"][0]["findings_path"] = "agent-table.json"
        payload["agents"][0]["verifier_evidence_path"] = "agent-table.json"
        table_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_agent_row_points_to_blocking_attempt(temp_run: Path) -> None:
        table_path = temp_run / "agent-table.json"
        payload = json.loads(table_path.read_text())
        payload["agents"][0]["findings_path"] = "attempt-1/findings.json"
        payload["agents"][0]["verifier_evidence_path"] = "attempt-1/verifier-evidence.json"
        table_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_terminal_scalar_findings_payload(temp_run: Path) -> None:
        findings_path = temp_run / "attempt-3/findings.json"
        findings_path.write_text('"pass"\n')

    def mutate_terminal_scalar_evidence_payload(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-3/verifier-evidence.json"
        evidence_path.write_text('"pass"\n')

    def mutate_agent_row_points_to_blocking_attempt(temp_run: Path) -> None:
        table_path = temp_run / "agent-table.json"
        payload = json.loads(table_path.read_text())
        payload["agents"][0]["findings_path"] = "attempt-1/findings.json"
        payload["agents"][0]["verifier_evidence_path"] = "attempt-1/verifier-evidence.json"
        table_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_pass_with_blocking_finding(temp_run: Path) -> None:
        findings_path = temp_run / "attempt-1/findings.json"
        payload = json.loads(findings_path.read_text())
        payload["verdict"] = "pass"
        payload["findings"][0]["blocking"] = True
        payload["findings"][0]["auto_fixable"] = False
        findings_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_invalid_template_id(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        payload = json.loads(evidence_path.read_text())
        payload["template_id"] = "wrong-template"
        evidence_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_invalid_final_state(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        payload = json.loads(evidence_path.read_text())
        payload["final_state"] = "unfinished"
        evidence_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_invalid_start_at(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        payload = json.loads(evidence_path.read_text())
        payload["start_at"] = "not-a-datetime"
        evidence_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_reversed_timestamps(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        payload = json.loads(evidence_path.read_text())
        payload["start_at"] = "2026-04-18T10:05:00Z"
        payload["end_at"] = "2026-04-18T10:04:59Z"
        evidence_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_fast_pass_wrong_final_state(temp_run: Path) -> None:
        findings_path = temp_run / "attempt-3/findings.json"
        evidence_path = temp_run / "attempt-3/verifier-evidence.json"
        findings_payload = json.loads(findings_path.read_text())
        evidence_payload = json.loads(evidence_path.read_text())
        findings_payload["verdict"] = "pass"
        findings_payload["findings"] = []
        for field in core_contract["verifier_evidence_required"]:
            if field in set(core_contract.get("fast_pass_evidence_required", [])):
                continue
            evidence_payload.pop(field, None)
        evidence_payload["final_state"] = "completed"
        findings_path.write_text(json.dumps(findings_payload, indent=2) + "\n")
        evidence_path.write_text(json.dumps(evidence_payload, indent=2) + "\n")

    def mutate_exhaustive_pass_with_unreviewed_axes(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        findings_path = temp_run / "attempt-1/findings.json"
        evidence_payload = json.loads(evidence_path.read_text())
        findings_payload = json.loads(findings_path.read_text())
        findings_payload["verdict"] = "pass"
        findings_payload["findings"] = []
        evidence_payload["review_coverage"]["coverage_status"] = "complete"
        evidence_payload["review_coverage"]["exhaustive"] = True
        evidence_payload["reviewed_axes"] = []
        evidence_payload["unreviewed_axes"] = ["Completeness", "Correctness", "Coherence"]
        findings_path.write_text(json.dumps(findings_payload, indent=2) + "\n")
        evidence_path.write_text(json.dumps(evidence_payload, indent=2) + "\n")

    def mutate_exhaustive_pass_with_unreviewed_axes(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        findings_path = temp_run / "attempt-1/findings.json"
        evidence_payload = json.loads(evidence_path.read_text())
        findings_payload = json.loads(findings_path.read_text())
        findings_payload["verdict"] = "pass"
        findings_payload["findings"] = []
        evidence_payload["review_coverage"]["coverage_status"] = "complete"
        evidence_payload["review_coverage"]["exhaustive"] = True
        evidence_payload["reviewed_axes"] = []
        evidence_payload["unreviewed_axes"] = ["Completeness", "Correctness", "Coherence"]
        findings_path.write_text(json.dumps(findings_payload, indent=2) + "\n")
        evidence_path.write_text(json.dumps(evidence_payload, indent=2) + "\n")

    run_mutated_case(
        "negative-missing-partial-scope",
        mutate_missing_scope,
        ["Partial verification must declare scope"],
    )
    run_mutated_case(
        "negative-non-active-with-block",
        mutate_bad_non_active,
        ["non_active agent must have pass verdict"],
    )
    run_mutated_case(
        "negative-invalid-terminal-pass",
        mutate_invalid_terminal_pass,
        ["Partial verification must declare scope"],
    )
    run_mutated_case(
        "negative-subject-mismatch",
        mutate_subject_mismatch,
        ["Findings/evidence subject value mismatch"],
    )
    run_mutated_case(
        "negative-duplicate-finding-ids",
        mutate_duplicate_finding_ids,
        ["duplicate finding id"],
    )
    run_mutated_case(
        "negative-scalar-agent-table",
        mutate_scalar_agent_table,
        ["agent-table.json must be a JSON object"],
    )
    run_mutated_case(
        "negative-agent-table-missing-subject",
        mutate_agent_table_missing_subject,
        ["Payload must include one subject key"],
    )
    run_mutated_case(
        "negative-agent-table-missing-updated-at",
        mutate_agent_table_missing_updated_at,
        ["missing required fields: ['updated_at']"],
    )
    run_mutated_case(
        "negative-agent-table-invalid-path-type",
        mutate_agent_table_invalid_path_type,
        ["field 'verifier_evidence_path' must be a non-empty string"],
    )
    run_mutated_case(
        "negative-agent-table-subject-mismatch",
        mutate_agent_table_subject_mismatch,
        ["agent-table/findings subject value mismatch"],
    )
    run_mutated_case(
        "negative-agent-table-invalid-updated-at-format",
        mutate_agent_table_invalid_updated_at_format,
        ["field 'updated_at' must be a valid date-time string"],
    )
    run_mutated_case(
        "negative-agent-table-unexpected-property",
        mutate_agent_table_unexpected_property,
        ["agent-table schema violation"],
    )
    run_mutated_case(
        "negative-agent-table-broken-paths",
        mutate_agent_table_broken_paths,
        ["must point to an existing file"],
    )
    run_mutated_case(
        "negative-agent-table-directory-paths",
        mutate_agent_table_directory_paths,
        ["must point to a file"],
    )
    run_mutated_case(
        "negative-agent-table-wrong-existing-file",
        mutate_agent_table_wrong_existing_file,
        ["must point to 'findings.json'"],
    )
    run_mutated_case(
        "negative-agent-row-points-to-blocking-attempt",
        mutate_agent_row_points_to_blocking_attempt,
        ["last_verdict must match referenced findings"],
    )
    run_mutated_case(
        "negative-terminal-scalar-findings-payload",
        mutate_terminal_scalar_findings_payload,
        ["active findings.json must be a JSON object"],
    )
    run_mutated_case(
        "negative-terminal-scalar-evidence-payload",
        mutate_terminal_scalar_evidence_payload,
        ["active verifier-evidence.json must be a JSON object"],
    )
    run_mutated_case(
        "negative-agent-row-points-to-blocking-attempt",
        mutate_agent_row_points_to_blocking_attempt,
        ["last_verdict must match referenced findings"],
    )
    run_mutated_case(
        "negative-pass-with-blocking-finding",
        mutate_pass_with_blocking_finding,
        ["pass verdict cannot contain blocking findings"],
    )
    run_mutated_case(
        "negative-invalid-template-id",
        mutate_invalid_template_id,
        ["Verifier evidence template_id must be 'verify-reviewer-inline-v3'"],
    )
    run_mutated_case(
        "negative-invalid-final-state",
        mutate_invalid_final_state,
        ["Verifier evidence final_state must be one of ['completed', 'completed_pass']"],
    )
    run_mutated_case(
        "negative-invalid-start-at",
        mutate_invalid_start_at,
        ["Verifier evidence start_at must be a valid date-time string"],
    )
    run_mutated_case(
        "negative-reversed-timestamps",
        mutate_reversed_timestamps,
        ["Verifier evidence end_at must be greater than or equal to start_at"],
    )
    run_mutated_case(
        "negative-fast-pass-wrong-final-state",
        mutate_fast_pass_wrong_final_state,
        ["fast-pass must use final_state='completed_pass'"],
    )
    return errors


def _require_subject_binding(
    payload: dict[str, object], path: Path, contract: dict[str, object]
) -> tuple[list[str], str | None, object | None]:
    errors: list[str] = []
    allowed = contract["subject_required_any_of"]
    keys = [key for key in allowed if payload.get(key)]
    if not keys:
        errors.append(
            f"{path} must include one subject key from {allowed}"
        )
        return errors, None, None
    if len(keys) > 1:
        errors.append(f"{path} must not bind multiple subject keys: {keys}")
        return errors, None, None
    key = keys[0]
    return errors, key, payload.get(key)


def _require_object_payload(payload: object, label: str) -> list[str]:
    if isinstance(payload, dict):
        return []
    return [f"{label} must be a JSON object"]


def _parse_datetime_value(value: object) -> object:
    if not isinstance(value, str) or not value.strip():
        return None
    try:
        from datetime import datetime

        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None


def validate_findings_attempt_payload(
    findings_path: Path, evidence_path: Path, core_contract: dict[str, object]
) -> list[str]:
    findings_payload = json.loads(findings_path.read_text())
    evidence_payload = json.loads(evidence_path.read_text())
    errors: list[str] = []
    errors.extend(_require_object_payload(findings_payload, str(findings_path)))
    errors.extend(_require_object_payload(evidence_payload, str(evidence_path)))
    if not isinstance(findings_payload, dict) or not isinstance(evidence_payload, dict):
        return errors

    finding_subject_errors, finding_subject_key, finding_subject_value = _require_subject_binding(
        findings_payload, findings_path, core_contract
    )
    evidence_subject_errors, evidence_subject_key, evidence_subject_value = _require_subject_binding(
        evidence_payload, evidence_path, core_contract
    )
    errors.extend(finding_subject_errors)
    errors.extend(evidence_subject_errors)

    if finding_subject_key and evidence_subject_key:
        if finding_subject_key != evidence_subject_key:
            errors.append(
                f"{findings_path} and {evidence_path} must use the same subject key"
            )
        if finding_subject_value != evidence_subject_value:
            errors.append(
                f"{findings_path} and {evidence_path} must bind the same subject value"
            )

    return errors + validate_findings_payloads(
        findings_payload,
        evidence_payload,
        str(findings_path),
        str(evidence_path),
        core_contract,
    )


def validate_fixture_findings_contract() -> list[str]:
    core_contract = json.loads(
        (MODULE_ROOT / "contracts/verification-cycle-core-v1.json").read_text()
    )
    errors: list[str] = []
    for run_dir in RUN_DIR_FIXTURES:
        attempt_dirs = sorted(path for path in run_dir.iterdir() if path.is_dir())
        for attempt_dir in attempt_dirs:
            findings_path = attempt_dir / "findings.json"
            evidence_path = attempt_dir / "verifier-evidence.json"
            if not findings_path.exists() or not evidence_path.exists():
                continue
            errors.extend(
                validate_findings_attempt_payload(findings_path, evidence_path, core_contract)
            )
    return errors


def validate_combined_verifier_output_payload(
    envelope_path: Path, core_contract: dict[str, object]
) -> list[str]:
    try:
        payload = json.loads(envelope_path.read_text())
    except json.JSONDecodeError as exc:
        return [f"{envelope_path} must contain normalized JSON: {exc.msg}"]
    errors: list[str] = []
    errors.extend(_require_object_payload(payload, str(envelope_path)))
    if not isinstance(payload, dict):
        return errors

    subject_errors, _, _ = _require_subject_binding(payload, envelope_path, core_contract)
    errors.extend(subject_errors)

    required = set(core_contract["findings_required"]) | {"verifier_evidence"}
    missing = sorted(field for field in required if field not in payload)
    if missing:
        errors.append(f"{envelope_path} missing combined envelope fields: {missing}")
        return errors

    verifier_evidence = payload["verifier_evidence"]
    if not isinstance(verifier_evidence, dict):
        errors.append(f"{envelope_path} verifier_evidence must be an object")
        return errors

    shadow_findings = {
        key: payload[key]
        for key in [*core_contract["subject_required_any_of"], *core_contract["findings_required"]]
        if key in payload
    }
    errors.extend(
        validate_findings_payloads(
            shadow_findings,
            verifier_evidence,
            str(envelope_path),
            f"{envelope_path}::verifier_evidence",
            core_contract,
        )
    )
    return errors


def validate_findings_payloads(
    findings_payload: dict[str, object],
    evidence_payload: dict[str, object],
    findings_label: str,
    evidence_label: str,
    core_contract: dict[str, object],
) -> list[str]:
    errors: list[str] = []
    errors.extend(_require_object_payload(findings_payload, findings_label))
    errors.extend(_require_object_payload(evidence_payload, evidence_label))
    if not isinstance(findings_payload, dict) or not isinstance(evidence_payload, dict):
        return errors

    finding_subject_errors, finding_subject_key, finding_subject_value = _require_subject_binding(
        findings_payload, Path(findings_label), core_contract
    )
    evidence_subject_contract = {
        "subject_required_any_of": core_contract.get(
            "verifier_evidence_subject_required_any_of",
            core_contract["subject_required_any_of"],
        )
    }
    evidence_subject_errors, evidence_subject_key, evidence_subject_value = _require_subject_binding(
        evidence_payload, Path(evidence_label), evidence_subject_contract
    )
    errors.extend(finding_subject_errors)
    errors.extend(evidence_subject_errors)

    if finding_subject_key and evidence_subject_key:
        if finding_subject_key != evidence_subject_key:
            errors.append(f"{findings_label} and {evidence_label} must use the same subject key")
        if finding_subject_value != evidence_subject_value:
            errors.append(f"{findings_label} and {evidence_label} must bind the same subject value")

    required_findings_fields = set(core_contract["findings_required"])
    missing_top_level = sorted(
        field for field in required_findings_fields if field not in findings_payload
    )
    if missing_top_level:
        errors.append(f"{findings_label} missing findings fields: {missing_top_level}")
        return errors

    verdict = findings_payload["verdict"]
    if verdict not in {"block", "pass"}:
        errors.append(f"{findings_label} has invalid verdict {verdict!r}")

    findings = findings_payload["findings"]
    if not isinstance(findings, list):
        errors.append(f"{findings_label} must contain findings as an array")
        return errors
    full_evidence_fields = core_contract["verifier_evidence_required"]
    has_full_evidence = all(field in evidence_payload for field in full_evidence_fields)
    is_fast_pass = verdict == "pass" and not findings and not has_full_evidence

    required_review_goal = core_contract["invocation_common_constants"]["review_goal"]
    if evidence_payload.get("review_goal") != required_review_goal:
        errors.append(
            f"{evidence_label} review_goal must be {required_review_goal!r}"
        )

    allowed_review_phases = set(core_contract["review_phase_allowed"])
    review_phase = evidence_payload.get("review_phase")
    if review_phase not in allowed_review_phases:
        errors.append(
            f"{evidence_label} review_phase must be one of {sorted(allowed_review_phases)}"
        )

    allowed_findings_contracts = set(core_contract["findings_contract_allowed"])
    findings_contract = evidence_payload.get("findings_contract")
    if findings_contract not in allowed_findings_contracts:
        errors.append(
            f"{evidence_label} findings_contract must be one of {sorted(allowed_findings_contracts)}"
        )

    required_template_id = core_contract.get("verifier_template_id_const")
    if required_template_id and evidence_payload.get("template_id") != required_template_id:
        errors.append(f"{evidence_label} template_id must be {required_template_id!r}")

    allowed_final_states = set(core_contract.get("verifier_final_state_allowed", []))
    final_state = evidence_payload.get("final_state")
    if allowed_final_states and final_state not in allowed_final_states:
        errors.append(
            f"{evidence_label} final_state must be one of {sorted(allowed_final_states)}"
        )

    required_evidence_fields = (
        core_contract.get("fast_pass_evidence_required", full_evidence_fields)
        if is_fast_pass
        else full_evidence_fields
    )
    for field in required_evidence_fields:
        if field not in evidence_payload:
            errors.append(f"{evidence_label} missing verifier evidence field: {field}")
            continue
        value = evidence_payload[field]
        if isinstance(value, str) and not value.strip():
            errors.append(f"{evidence_label} verifier evidence field {field!r} must be non-empty")
        elif field in {"review_scope", "review_coverage"} and not isinstance(value, dict):
            errors.append(f"{evidence_label} verifier evidence field {field!r} must be an object")
        elif field in {"reviewed_paths", "skipped_paths", "reviewed_axes", "unreviewed_axes"} and not isinstance(value, list):
            errors.append(f"{evidence_label} verifier evidence field {field!r} must be an array")

    parsed_timestamps: dict[str, object] = {}
    for field in core_contract.get("verifier_timestamp_fields", []):
        value = evidence_payload.get(field)
        if value is None:
            if not is_fast_pass:
                errors.append(f"{evidence_label} {field} must be a valid date-time string")
            continue
        parsed = _parse_datetime_value(value)
        if parsed is None:
            errors.append(f"{evidence_label} {field} must be a valid date-time string")
            continue
        parsed_timestamps[field] = parsed
    if {"start_at", "end_at"} <= parsed_timestamps.keys():
        if parsed_timestamps["end_at"] < parsed_timestamps["start_at"]:
            errors.append(f"{evidence_label} end_at must be greater than or equal to start_at")

    coverage = (evidence_payload.get("review_coverage") or {}).get("coverage_status")
    exhaustive = (evidence_payload.get("review_coverage") or {}).get("exhaustive")
    scope = (evidence_payload.get("review_scope") or {}).get("scope", "")
    if verdict == "pass" and (coverage != "complete" or exhaustive is not True):
        errors.append(f"{findings_label} invalid: pass requires complete+exhaustive review")
    if coverage == "partial" and not str(scope).strip():
        errors.append(f"{evidence_label} invalid: partial coverage must declare scope")
    if is_fast_pass and evidence_payload.get("final_state") != "completed_pass":
        errors.append(f"{evidence_label} invalid: fast-pass must use final_state='completed_pass'")

    allowed_axes = set(core_contract["review_axes_allowed"])
    reviewed_axes = evidence_payload.get("reviewed_axes")
    unreviewed_axes = evidence_payload.get("unreviewed_axes")
    if isinstance(reviewed_axes, list):
        invalid = sorted(axis for axis in reviewed_axes if axis not in allowed_axes)
        if invalid:
            errors.append(f"{evidence_label} reviewed_axes contains invalid values {invalid!r}")
    if isinstance(unreviewed_axes, list):
        invalid = sorted(axis for axis in unreviewed_axes if axis not in allowed_axes)
        if invalid:
            errors.append(f"{evidence_label} unreviewed_axes contains invalid values {invalid!r}")
    if isinstance(reviewed_axes, list) and isinstance(unreviewed_axes, list):
        overlap = sorted(set(reviewed_axes) & set(unreviewed_axes))
        if overlap:
            errors.append(f"{evidence_label} reviewed_axes and unreviewed_axes must not overlap {overlap!r}")
        if verdict == "pass" and exhaustive is True:
            if unreviewed_axes:
                errors.append(f"{evidence_label} invalid: exhaustive pass must keep unreviewed_axes empty")
            if set(reviewed_axes) != allowed_axes:
                errors.append(f"{evidence_label} invalid: exhaustive pass must cover all review axes")

    required_object_fields = set(core_contract["finding_object_required"])
    seen_ids: set[str] = set()
    has_blocking_finding = False

    for index, finding in enumerate(findings):
        if not isinstance(finding, dict):
            errors.append(f"{findings_label} finding #{index + 1} must be an object")
            continue
        missing = sorted(field for field in required_object_fields if field not in finding)
        if missing:
            errors.append(f"{findings_label} finding #{index + 1} missing fields: {missing}")
            continue

        finding_id = finding["id"]
        if finding_id in seen_ids:
            errors.append(f"{findings_label} contains duplicate finding id {finding_id!r}")
        seen_ids.add(finding_id)
        if finding.get("blocking") is True:
            has_blocking_finding = True

        for field in core_contract["finding_non_empty_string_fields"]:
            value = finding.get(field)
            if not isinstance(value, str) or not value.strip():
                errors.append(
                    f"{findings_label} finding {finding_id!r} field {field!r} must be a non-empty string"
                )

        allowed_severities = set(core_contract["finding_severity_allowed"])
        severity = finding["severity"]
        if severity not in allowed_severities:
            errors.append(
                f"{findings_label} finding {finding_id!r} severity must be one of {sorted(allowed_severities)}"
            )

        allowed_dimensions = set(core_contract["finding_dimension_allowed"])
        dimension = finding["dimension"]
        if dimension not in allowed_dimensions:
            errors.append(
                f"{findings_label} finding {finding_id!r} dimension must be one of {sorted(allowed_dimensions)}"
            )

        for field in core_contract["finding_boolean_fields"]:
            if not isinstance(finding.get(field), bool):
                errors.append(
                    f"{findings_label} finding {finding_id!r} field {field!r} must be boolean"
                )

        blocking = finding["blocking"]
        auto_fixable = finding["auto_fixable"]
        if auto_fixable and not blocking:
            errors.append(
                f"{findings_label} finding {finding_id!r} invalid: auto_fixable=true requires blocking=true"
            )
        if severity == "SUGGESTION" and blocking:
            errors.append(
                f"{findings_label} finding {finding_id!r} invalid: SUGGESTION must keep blocking=false"
            )
        if review_phase == "docs_first" and auto_fixable:
            errors.append(
                f"{findings_label} finding {finding_id!r} invalid: docs_first findings must keep auto_fixable=false"
            )

    if verdict == "pass" and has_blocking_finding:
        errors.append(f"{findings_label} invalid: pass verdict cannot contain blocking findings")

    return errors


def validate_negative_findings_cases() -> list[str]:
    errors: list[str] = []
    core_contract = json.loads(
        (MODULE_ROOT / "contracts/verification-cycle-core-v1.json").read_text()
    )

    def run_mutated_case(
        source_fixture: Path,
        case_name: str,
        mutate: callable,
        expected_markers: list[str],
    ) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            temp_run = Path(tmpdir) / case_name
            shutil.copytree(source_fixture, temp_run)
            mutate(temp_run)
            findings_path = temp_run / "attempt-1/findings.json"
            evidence_path = temp_run / "attempt-1/verifier-evidence.json"
            case_errors = validate_findings_payloads(
                json.loads(findings_path.read_text()),
                json.loads(evidence_path.read_text()),
                str(findings_path),
                str(evidence_path),
                core_contract,
            )
            if not case_errors:
                errors.append(f"Negative findings case unexpectedly passed: {case_name}")
                return
            detail = "\n".join(case_errors)
            for marker in expected_markers:
                if marker not in detail:
                    errors.append(
                        f"Negative findings case {case_name} failed without expected marker {marker!r}"
                    )

    def mutate_missing_subject(temp_run: Path) -> None:
        findings_path = temp_run / "attempt-1/findings.json"
        payload = json.loads(findings_path.read_text())
        payload.pop("target_ref", None)
        findings_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_auto_fixable_without_blocking(temp_run: Path) -> None:
        findings_path = temp_run / "attempt-1/findings.json"
        payload = json.loads(findings_path.read_text())
        payload["findings"][0]["blocking"] = False
        payload["findings"][0]["auto_fixable"] = True
        findings_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_suggestion_blocking(temp_run: Path) -> None:
        findings_path = temp_run / "attempt-1/findings.json"
        payload = json.loads(findings_path.read_text())
        payload["findings"][0]["severity"] = "SUGGESTION"
        payload["findings"][0]["blocking"] = True
        findings_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_docs_first_auto_fixable(temp_run: Path) -> None:
        findings_path = temp_run / "attempt-1/findings.json"
        payload = json.loads(findings_path.read_text())
        payload["findings"][0]["auto_fixable"] = True
        findings_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_invalid_review_goal(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        payload = json.loads(evidence_path.read_text())
        payload["review_goal"] = "something_else"
        evidence_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_invalid_review_phase(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        payload = json.loads(evidence_path.read_text())
        payload["review_phase"] = "nonsense"
        evidence_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_invalid_findings_contract(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        payload = json.loads(evidence_path.read_text())
        payload["findings_contract"] = "not-shared-findings-v1"
        evidence_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_evidence_missing_subject(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        payload = json.loads(evidence_path.read_text())
        payload.pop("target_ref", None)
        payload.pop("change", None)
        evidence_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_invalid_template_id(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        payload = json.loads(evidence_path.read_text())
        payload["template_id"] = "wrong-template"
        evidence_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_invalid_final_state(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        payload = json.loads(evidence_path.read_text())
        payload["final_state"] = "unfinished"
        evidence_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_invalid_start_at(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        payload = json.loads(evidence_path.read_text())
        payload["start_at"] = "not-a-datetime"
        evidence_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_reversed_timestamps(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        payload = json.loads(evidence_path.read_text())
        payload["start_at"] = "2026-04-18T10:05:00Z"
        payload["end_at"] = "2026-04-18T10:04:59Z"
        evidence_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_invalid_finding_severity(temp_run: Path) -> None:
        findings_path = temp_run / "attempt-1/findings.json"
        payload = json.loads(findings_path.read_text())
        payload["findings"][0]["severity"] = "INFO"
        findings_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_invalid_finding_dimension(temp_run: Path) -> None:
        findings_path = temp_run / "attempt-1/findings.json"
        payload = json.loads(findings_path.read_text())
        payload["findings"][0]["dimension"] = "Other"
        findings_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_invalid_finding_boolean(temp_run: Path) -> None:
        findings_path = temp_run / "attempt-1/findings.json"
        payload = json.loads(findings_path.read_text())
        payload["findings"][0]["blocking"] = "yes"
        findings_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_empty_finding_text(temp_run: Path) -> None:
        findings_path = temp_run / "attempt-1/findings.json"
        payload = json.loads(findings_path.read_text())
        payload["findings"][0]["problem"] = "   "
        findings_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_pass_with_blocking_finding(temp_run: Path) -> None:
        findings_path = temp_run / "attempt-1/findings.json"
        payload = json.loads(findings_path.read_text())
        payload["verdict"] = "pass"
        payload["findings"][0]["blocking"] = True
        payload["findings"][0]["auto_fixable"] = False
        findings_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_exhaustive_pass_with_unreviewed_axes(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        findings_path = temp_run / "attempt-1/findings.json"
        evidence_payload = json.loads(evidence_path.read_text())
        findings_payload = json.loads(findings_path.read_text())
        findings_payload["verdict"] = "pass"
        findings_payload["findings"] = []
        evidence_payload["review_coverage"]["coverage_status"] = "complete"
        evidence_payload["review_coverage"]["exhaustive"] = True
        evidence_payload["reviewed_axes"] = []
        evidence_payload["unreviewed_axes"] = ["Completeness", "Correctness", "Coherence"]
        findings_path.write_text(json.dumps(findings_payload, indent=2) + "\n")
        evidence_path.write_text(json.dumps(evidence_payload, indent=2) + "\n")

    def mutate_scalar_findings_payload(temp_run: Path) -> None:
        findings_path = temp_run / "attempt-1/findings.json"
        findings_path.write_text('"pass"\n')

    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-findings-missing-subject",
        mutate_missing_subject,
        ["must include one subject key"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-findings-auto-fixable-without-blocking",
        mutate_auto_fixable_without_blocking,
        ["auto_fixable=true requires blocking=true"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-findings-suggestion-blocking",
        mutate_suggestion_blocking,
        ["SUGGESTION must keep blocking=false"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[1],
        "negative-findings-docs-first-auto-fixable",
        mutate_docs_first_auto_fixable,
        ["docs_first findings must keep auto_fixable=false"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-evidence-invalid-review-goal",
        mutate_invalid_review_goal,
        ["review_goal must be 'implementation_correctness'"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-evidence-invalid-review-phase",
        mutate_invalid_review_phase,
        ["review_phase must be one of ['docs_first', 'source_first']"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-evidence-invalid-findings-contract",
        mutate_invalid_findings_contract,
        ["findings_contract must be one of ['shared-findings-v1']"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-evidence-missing-subject",
        mutate_evidence_missing_subject,
        ["must include one subject key"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-evidence-invalid-template-id",
        mutate_invalid_template_id,
        ["template_id must be 'verify-reviewer-inline-v3'"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-evidence-invalid-final-state",
        mutate_invalid_final_state,
        ["final_state must be one of ['completed', 'completed_pass']"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-evidence-invalid-start-at",
        mutate_invalid_start_at,
        ["start_at must be a valid date-time string"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-evidence-reversed-timestamps",
        mutate_reversed_timestamps,
        ["end_at must be greater than or equal to start_at"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-finding-invalid-severity",
        mutate_invalid_finding_severity,
        ["severity must be one of ['CRITICAL', 'SUGGESTION', 'WARNING']"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-finding-invalid-dimension",
        mutate_invalid_finding_dimension,
        ["dimension must be one of ['Coherence', 'Completeness', 'Correctness']"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-finding-invalid-boolean",
        mutate_invalid_finding_boolean,
        ["field 'blocking' must be boolean"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-finding-empty-problem",
        mutate_empty_finding_text,
        ["field 'problem' must be a non-empty string"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-pass-with-blocking-finding",
        mutate_pass_with_blocking_finding,
        ["pass verdict cannot contain blocking findings"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-exhaustive-pass-with-unreviewed-axes",
        mutate_exhaustive_pass_with_unreviewed_axes,
        ["exhaustive pass must keep unreviewed_axes empty"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-scalar-findings-payload",
        mutate_scalar_findings_payload,
        ["must be a JSON object"],
    )
    def mutate_missing_evidence_field(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        payload = json.loads(evidence_path.read_text())
        payload.pop("template_id", None)
        evidence_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_empty_agent_id(temp_run: Path) -> None:
        evidence_path = temp_run / "attempt-1/verifier-evidence.json"
        payload = json.loads(evidence_path.read_text())
        payload["agent_id"] = "   "
        evidence_path.write_text(json.dumps(payload, indent=2) + "\n")

    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-evidence-missing-template-id",
        mutate_missing_evidence_field,
        ["missing verifier evidence field: template_id"],
    )
    run_mutated_case(
        RUN_DIR_FIXTURES[0],
        "negative-evidence-empty-agent-id",
        mutate_empty_agent_id,
        ["verifier evidence field 'agent_id' must be non-empty"],
    )
    return errors


def validate_combined_output_fixture() -> list[str]:
    core_contract = json.loads(
        (MODULE_ROOT / "contracts/verification-cycle-core-v1.json").read_text()
    )
    return validate_combined_verifier_output_payload(COMBINED_OUTPUT_FIXTURE, core_contract)


def validate_negative_combined_output_cases() -> list[str]:
    core_contract = json.loads(
        (MODULE_ROOT / "contracts/verification-cycle-core-v1.json").read_text()
    )
    errors: list[str] = []

    def validate_case(name: str, mutate: callable, expected_markers: list[str]) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            fixture_path = Path(tmpdir) / "combined-envelope.json"
            fixture_path.write_text(COMBINED_OUTPUT_FIXTURE.read_text())
            mutate(fixture_path)
            case_errors = validate_combined_verifier_output_payload(fixture_path, core_contract)
            if not case_errors:
                errors.append(f"Negative combined-output case unexpectedly passed: {name}")
                return
            detail = "\n".join(case_errors)
            for marker in expected_markers:
                if marker not in detail:
                    errors.append(
                        f"Negative combined-output case {name} failed without expected marker {marker!r}"
                    )

    def validate_positive_case(name: str, mutate: callable) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            fixture_path = Path(tmpdir) / "combined-envelope.json"
            fixture_path.write_text(COMBINED_OUTPUT_FIXTURE.read_text())
            mutate(fixture_path)
            case_errors = validate_combined_verifier_output_payload(fixture_path, core_contract)
            if case_errors:
                errors.append(
                    f"Positive combined-output case unexpectedly failed: {name}: "
                    + "; ".join(case_errors)
                )

    def mutate_missing_verifier_evidence(fixture_path: Path) -> None:
        payload = json.loads(fixture_path.read_text())
        payload.pop("verifier_evidence", None)
        fixture_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_missing_subject(fixture_path: Path) -> None:
        payload = json.loads(fixture_path.read_text())
        payload.pop("change", None)
        fixture_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_missing_nested_evidence_subject(fixture_path: Path) -> None:
        payload = json.loads(fixture_path.read_text())
        payload["verifier_evidence"].pop("change", None)
        payload["verifier_evidence"].pop("target_ref", None)
        fixture_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_markdown_only_output(fixture_path: Path) -> None:
        fixture_path.write_text("## Review Summary\n\n- pass\n")

    def mutate_pass_with_blocking_finding(fixture_path: Path) -> None:
        payload = json.loads(fixture_path.read_text())
        payload["verdict"] = "pass"
        payload["findings"] = [
            {
                "id": "blocking-pass-1",
                "severity": "WARNING",
                "dimension": "Correctness",
                "problem": "blocking issue remains",
                "evidence": "evidence",
                "recommendation": "fix it",
                "blocking": True,
                "auto_fixable": False
            }
        ]
        fixture_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_scalar_json_output(fixture_path: Path) -> None:
        fixture_path.write_text('"pass"\n')

    def mutate_partial_non_exhaustive_pass(fixture_path: Path) -> None:
        payload = json.loads(fixture_path.read_text())
        payload["verdict"] = "pass"
        payload["verifier_evidence"]["review_coverage"]["coverage_status"] = "partial"
        payload["verifier_evidence"]["review_coverage"]["exhaustive"] = False
        payload["verifier_evidence"]["review_scope"]["scope"] = ""
        fixture_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_exhaustive_pass_with_unreviewed_axes(fixture_path: Path) -> None:
        payload = json.loads(fixture_path.read_text())
        payload["verdict"] = "pass"
        payload["verifier_evidence"]["review_coverage"]["coverage_status"] = "complete"
        payload["verifier_evidence"]["review_coverage"]["exhaustive"] = True
        payload["verifier_evidence"]["reviewed_axes"] = []
        payload["verifier_evidence"]["unreviewed_axes"] = ["Completeness", "Correctness", "Coherence"]
        fixture_path.write_text(json.dumps(payload, indent=2) + "\n")

    def mutate_valid_fast_pass(fixture_path: Path) -> None:
        payload = json.loads(fixture_path.read_text())
        payload["verdict"] = "pass"
        payload["findings"] = []
        evidence = payload["verifier_evidence"]
        evidence["final_state"] = "completed_pass"
        for field in ["start_at", "end_at", "review_scope", "reviewed_paths", "skipped_paths"]:
            evidence.pop(field, None)
        fixture_path.write_text(json.dumps(payload, indent=2) + "\n")

    validate_case(
        "missing-verifier-evidence",
        mutate_missing_verifier_evidence,
        ["missing combined envelope fields: ['verifier_evidence']"],
    )
    validate_case(
        "missing-subject",
        mutate_missing_subject,
        ["must include one subject key"],
    )
    validate_case(
        "missing-nested-evidence-subject",
        mutate_missing_nested_evidence_subject,
        ["must include one subject key"],
    )
    validate_case(
        "markdown-only-output",
        mutate_markdown_only_output,
        ["must contain normalized JSON"],
    )
    validate_case(
        "pass-with-blocking-finding",
        mutate_pass_with_blocking_finding,
        ["pass verdict cannot contain blocking findings"],
    )
    validate_case(
        "scalar-json-output",
        mutate_scalar_json_output,
        ["must be a JSON object"],
    )
    validate_case(
        "partial-non-exhaustive-pass",
        mutate_partial_non_exhaustive_pass,
        ["partial coverage must declare scope"],
    )
    validate_case(
        "exhaustive-pass-with-unreviewed-axes",
        mutate_exhaustive_pass_with_unreviewed_axes,
        ["exhaustive pass must keep unreviewed_axes empty"],
    )
    validate_positive_case(
        "valid-fast-pass",
        mutate_valid_fast_pass,
    )
    return errors


def validate_task_template_ids() -> list[str]:
    path = OPENSPEC_ROOT / "schemas/ai-enforced-workflow/templates/tasks.md"
    text = path.read_text()
    task_line_pattern = re.compile(r"^- \[[ xX]\] (\d+\.\d+) ", re.MULTILINE)
    checklist_line_pattern = re.compile(r"^- \[[ xX]\] ", re.MULTILINE)
    ids = task_line_pattern.findall(text)
    errors: list[str] = []
    if len(checklist_line_pattern.findall(text)) != len(ids):
        errors.append(f"Checklist items in {path} must use unique X.Y task ids")
    seen: set[str] = set()
    for task_id in ids:
        if task_id in seen:
            errors.append(f"Duplicate task id {task_id!r} in {path}")
        seen.add(task_id)
    return errors


def validate_spawn_schema_semantics() -> list[str]:
    path = OPENSPEC_ROOT / "schemas/ai-enforced-workflow/agent-spawn-decision-v1.schema.json"
    schema = json.loads(path.read_text())
    errors: list[str] = []
    if schema.get("deprecated") is not True:
        errors.append(f"{path} must declare deprecated=true")
    legacy_status = schema.get("legacy_status")
    if not isinstance(legacy_status, str) or "archive-only" not in legacy_status:
        errors.append(f"{path} must describe archive-only legacy usage")
    reasons = set((schema.get("properties") or {}).get("reason_code", {}).get("enum", []))
    required = {
        "no_usable_active_agent",
        "active_agent_missing",
        "active_agent_not_resumable",
        "tooling_recovery",
    }
    missing = required - reasons
    if missing:
        errors.append(f"Spawn reason_code enum missing required values in {path}: {sorted(missing)}")
    return errors


def validate_send_input_resume_contract() -> list[str]:
    errors: list[str] = []
    tasks_template_path = OPENSPEC_ROOT / "schemas/ai-enforced-workflow/templates/tasks.md"

    errors += require_tokens_between(
        tasks_template_path,
        "- [ ] 2.3 [Checkpoint]",
        "## 3.",
        [
            "verify-sequence/default",
            "verification-cycle-core-v1.json",
            "verification-cycle-openspec-adapter-v1.json",
            "review_goal=implementation_correctness",
            "cycle_rules",
            "verifier_evidence_required",
            "agent-table.json",
        ],
    )
    return errors


def validate_agent_table_ownership_contract() -> list[str]:
    errors: list[str] = []
    tasks_template_path = OPENSPEC_ROOT / "schemas/ai-enforced-workflow/templates/tasks.md"
    schema_path = OPENSPEC_ROOT / "schemas/ai-enforced-workflow/schema.yaml"

    errors += require_tokens_between(
        tasks_template_path,
        "- [ ] 2.3 [Checkpoint]",
        "## 3.",
        [
            "Write authoritative findings JSON and verifier evidence JSON; the caller/orchestrator reconciles and writes `agent-table.json`.",
        ],
    )
    errors += require_tokens_between(
        schema_path,
        "CHECKPOINTS:",
        "SKILL AWARENESS:",
        [
            "Require authoritative findings JSON and verifier",
            "caller/orchestrator-maintained",
            "`agent-table.json`",
        ],
    )
    errors += require_tokens_between(
        schema_path,
        "- maintain `agent-table.json` as authoritative agent-state record",
        "- finding semantics: follow `finding_semantics` and",
        [
            "caller-maintained `agent-table.json`",
        ],
    )
    return errors


def validate_verify_reviewer_contract() -> list[str]:
    path = AGENTS_ROOT / "verify-reviewer.toml"
    payload = tomllib.loads(path.read_text())
    instructions = payload.get("developer_instructions", "")
    errors: list[str] = []

    def require_in_section(start_token: str, end_token: str | None, tokens: list[str]) -> None:
        start = instructions.find(start_token)
        if start == -1:
            errors.append(f"Missing section token {start_token!r} in {path}")
            return
        end = len(instructions) if end_token is None else instructions.find(end_token, start)
        if end_token is not None and end == -1:
            errors.append(f"Missing section token {end_token!r} after {start_token!r} in {path}")
            return
        section = " ".join(instructions[start:end].split())
        for token in tokens:
            if " ".join(token.split()) not in section:
                errors.append(
                    f"Missing required verifier contract token {token!r} between {start_token!r} and {end_token or '<eof>'!r} in {path}"
                )

    require_in_section(
        "Input contract:",
        "Output requirements:",
        [
            "`change` or `target_ref`",
            "`output_paths`",
            "`findings_path`",
            "`verifier_evidence_path`",
            "`agent_table_path`",
        ],
    )
    require_in_section(
        "Output requirements:",
        "Review constraints:",
        [
            "Return normalized JSON using the full envelope:",
            "subject key (`change` or `target_ref`)",
            "`verdict`",
            "`findings`",
            "`verifier_evidence`",
            "`blocking=true` forbids `verdict=pass`",
            "`auto_fixable=true` requires `blocking=true`",
            "`severity=SUGGESTION` MUST set `blocking=false`",
            "For `review_phase=docs_first`, all findings MUST keep `auto_fixable=false`.",
            "`verifier_evidence` MUST carry the same subject key (`change` or `target_ref`) as the findings payload.",
            "`verifier_evidence` field requirements: see `verifier_evidence_required` in",
            "`template_id` MUST be `verify-reviewer-inline-v3`.",
            "`final_state` MUST be one of `completed|completed_pass`.",
            "When timestamps are present, `end_at` >= `start_at`.",
            "Fast-pass template (verdict=pass with zero findings):",
            "Do not return markdown-only summaries as the final result.",
        ],
    )
    return errors


def validate_verification_cycle_core_contract() -> list[str]:
    path = MODULE_ROOT / "contracts/verification-cycle-core-v1.json"
    payload = json.loads(path.read_text())
    errors: list[str] = []
    errors.extend(validate_core_contract_payload(payload, path))
    return errors


def validate_adapter_contracts() -> list[str]:
    errors: list[str] = []

    def validate_schema(
        path: Path,
        subject_key: str,
        forbidden_subject_key: str,
    ) -> None:
        payload = json.loads(path.read_text())
        expected_required = [
            subject_key,
            "review_goal",
            "review_phase",
            "risk_tier",
            "evidence_paths_or_diff_scope",
            "findings_contract",
            "output_paths",
        ]
        if payload.get("type") != "object":
            errors.append(f"{path} must declare type=object")
        if payload.get("additionalProperties") is not False:
            errors.append(f"{path} must declare additionalProperties=false")
        if payload.get("subject_key") != subject_key:
            errors.append(f"{path} subject_key drifted")
        if payload.get("required") != expected_required:
            errors.append(f"{path} required fields drifted")
        if payload.get("required_fields") != expected_required[:-1]:
            errors.append(f"{path} required_fields drifted")

        validator = jsonschema.Draft202012Validator(payload)
        valid_instance = {
            subject_key: f"{subject_key}-value",
            "review_goal": "implementation_correctness",
            "review_phase": "docs_first",
            "risk_tier": "STANDARD",
            "evidence_paths_or_diff_scope": ["path/a"],
            "findings_contract": "shared-findings-v1",
            "output_paths": {
                "findings_path": "verification/findings.json",
                "verifier_evidence_path": "verification/verifier-evidence.json",
                "agent_table_path": "verification/agent-table.json",
            },
        }
        if list(validator.iter_errors(valid_instance)):
            errors.append(f"{path} rejected a valid invocation envelope")

        invalid_instances = [
            (
                "missing-output-path-member",
                {
                    **valid_instance,
                    "output_paths": {
                        "findings_path": "verification/findings.json",
                        "agent_table_path": "verification/agent-table.json",
                    },
                },
            ),
            (
                "extra-envelope-key",
                {
                    **valid_instance,
                    "unexpected": True,
                },
            ),
            (
                "illegal-dual-subject-binding",
                {
                    **valid_instance,
                    forbidden_subject_key: "other-subject",
                },
            ),
            (
                "missing-bundle-field",
                {
                    key: value
                    for key, value in valid_instance.items()
                    if key != "review_phase"
                },
            ),
        ]
        for name, instance in invalid_instances:
            if not list(validator.iter_errors(instance)):
                errors.append(f"{path} unexpectedly accepted invalid invocation envelope: {name}")

    validate_schema(
        MODULE_ROOT / "contracts/verification-cycle-openspec-adapter-v1.json",
        "change",
        "target_ref",
    )
    validate_schema(
        MODULE_ROOT / "contracts/verification-cycle-standalone-adapter-v1.json",
        "target_ref",
        "change",
    )
    return errors


def validate_orchestrator_contracts() -> list[str]:
    errors: list[str] = []
    input_path = ORCHESTRATOR_ROOT / "contracts/orchestrator-input-v1.json"
    decision_path = ORCHESTRATOR_ROOT / "contracts/orchestrator-decision-v1.json"
    resolver_path = ORCHESTRATOR_ROOT / "bin/verification_cycle_resolve.py"
    validate_path = ORCHESTRATOR_ROOT / "bin/verification_cycle_validate.py"

    input_schema = json.loads(input_path.read_text())
    decision_schema = json.loads(decision_path.read_text())

    if input_schema.get("type") != "object":
        errors.append(f"{input_path} must declare type=object")
    if input_schema.get("additionalProperties") is not False:
        errors.append(f"{input_path} must declare additionalProperties=false")
    if input_schema.get("required") != ["contract", "agent_table", "manual_pause"]:
        errors.append(f"{input_path} required fields drifted")
    continuation_probe = (input_schema.get("properties") or {}).get("continuation_probe") or {}
    expected_probe_required = ["agent_id", "send_input_result", "resume_result"]
    if continuation_probe.get("required") != expected_probe_required:
        errors.append(f"{input_path} continuation_probe required fields drifted")

    input_validator = jsonschema.Draft202012Validator(input_schema)
    valid_input = {
        "contract": "verification-cycle-orchestrator-input-v1",
        "target_ref": "workflow:test",
        "manual_pause": False,
        "agent_table": {"agents": []},
        "continuation_probe": {
            "agent_id": "verifier-1",
            "send_input_result": "agent_not_found",
            "resume_result": "not_attempted",
        },
    }
    if list(input_validator.iter_errors(valid_input)):
        errors.append(f"{input_path} rejected a valid continuation_probe payload")
    invalid_input = dict(valid_input)
    invalid_input["continuation_probe"] = {
        "agent_id": "verifier-1",
        "send_input_result": "agent_not_found",
    }
    if not list(input_validator.iter_errors(invalid_input)):
        errors.append(f"{input_path} unexpectedly accepted invalid continuation_probe payload")

    if decision_schema.get("type") != "object":
        errors.append(f"{decision_path} must declare type=object")
    if decision_schema.get("additionalProperties") is not False:
        errors.append(f"{decision_path} must declare additionalProperties=false")
    decision_validator = jsonschema.Draft202012Validator(decision_schema)
    valid_spawn_decision = {
        "contract": "verification-cycle-orchestrator-decision-v1",
        "decision": "spawn_active",
        "reason": "bootstrap spawn",
        "spawn_reason_code": "no_usable_active_agent",
    }
    if list(decision_validator.iter_errors(valid_spawn_decision)):
        errors.append(f"{decision_path} rejected a valid spawn decision payload")
    invalid_spawn_decision = {
        "contract": "verification-cycle-orchestrator-decision-v1",
        "decision": "spawn_active",
        "reason": "bootstrap spawn",
    }
    if not list(decision_validator.iter_errors(invalid_spawn_decision)):
        errors.append(f"{decision_path} unexpectedly accepted spawn_active without spawn_reason_code")
    invalid_non_spawn = {
        "contract": "verification-cycle-orchestrator-decision-v1",
        "decision": "resume_active",
        "reason": "continue active agent",
        "spawn_reason_code": "no_usable_active_agent",
    }
    if not list(decision_validator.iter_errors(invalid_non_spawn)):
        errors.append(f"{decision_path} unexpectedly accepted non-spawn decision with spawn_reason_code")

    def run_resolver_case(name: str, payload: dict[str, object], expected: dict[str, object]) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            input_file = Path(tmpdir) / f"{name}.json"
            decision_file = Path(tmpdir) / f"{name}.decision.json"
            input_file.write_text(json.dumps(payload, indent=2) + "\n")
            result = subprocess.run(
                [sys.executable, str(resolver_path), "--input", str(input_file)],
                capture_output=True,
                text=True,
                check=False,
            )
            if result.returncode != 0:
                errors.append(f"Resolver case {name} failed: {(result.stdout + result.stderr).strip()}")
                return
            try:
                decision_payload = json.loads(result.stdout)
            except json.JSONDecodeError as exc:
                errors.append(f"Resolver case {name} emitted invalid JSON: {exc}")
                return
            decision_file.write_text(json.dumps(decision_payload, indent=2) + "\n")
            validate_result = subprocess.run(
                [sys.executable, str(validate_path), "--input", str(decision_file)],
                capture_output=True,
                text=True,
                check=False,
            )
            if validate_result.returncode != 0:
                errors.append(
                    f"Resolver case {name} produced a decision rejected by validation: {(validate_result.stdout + validate_result.stderr).strip()}"
                )
                return
            for key, value in expected.items():
                if decision_payload.get(key) != value:
                    errors.append(
                        f"Resolver case {name} expected {key}={value!r}, got {decision_payload.get(key)!r}"
                    )

    base = {
        "contract": "verification-cycle-orchestrator-input-v1",
        "target_ref": "workflow:test",
        "manual_pause": False,
    }
    run_resolver_case(
        "bootstrap-spawn",
        {
            **base,
            "agent_table": {"agents": []},
        },
        {
            "decision": "spawn_active",
            "spawn_reason_code": "no_usable_active_agent",
        },
    )
    run_resolver_case(
        "send-input-agent-not-found-resume",
        {
            **base,
            "agent_table": {
                "agents": [
                    {
                        "agent_id": "verifier-1",
                        "status": "active",
                        "last_verdict": "unknown",
                        "coverage_status": "unknown",
                        "exhaustive": False,
                    }
                ]
            },
            "continuation_probe": {
                "agent_id": "verifier-1",
                "send_input_result": "agent_not_found",
                "resume_result": "not_attempted",
            },
        },
        {
            "decision": "resume_active",
        },
    )
    run_resolver_case(
        "active-agent-missing",
        {
            **base,
            "agent_table": {
                "agents": [
                    {
                        "agent_id": "verifier-1",
                        "status": "active",
                        "last_verdict": "unknown",
                        "coverage_status": "unknown",
                        "exhaustive": False,
                    }
                ]
            },
            "continuation_probe": {
                "agent_id": "verifier-1",
                "send_input_result": "agent_not_found",
                "resume_result": "agent_not_found",
            },
        },
        {
            "decision": "spawn_active",
            "spawn_reason_code": "active_agent_missing",
        },
    )
    run_resolver_case(
        "active-agent-not-resumable",
        {
            **base,
            "agent_table": {
                "agents": [
                    {
                        "agent_id": "verifier-1",
                        "status": "active",
                        "last_verdict": "unknown",
                        "coverage_status": "unknown",
                        "exhaustive": False,
                    }
                ]
            },
            "continuation_probe": {
                "agent_id": "verifier-1",
                "send_input_result": "agent_not_found",
                "resume_result": "not_resumable",
            },
        },
        {
            "decision": "spawn_active",
            "spawn_reason_code": "active_agent_not_resumable",
        },
    )
    run_resolver_case(
        "block-to-pass-mark-non-active",
        {
            **base,
            "agent_table": {
                "agents": [
                    {
                        "agent_id": "verifier-1",
                        "status": "active",
                        "last_verdict": "block",
                        "coverage_status": "unknown",
                        "exhaustive": False,
                    }
                ]
            },
            "latest_result": {
                "agent_id": "verifier-1",
                "verdict": "pass",
                "coverage_status": "complete",
                "exhaustive": True,
                "scope": "full",
            },
        },
        {
            "decision": "mark_non_active",
        },
    )
    run_resolver_case(
        "partial-latest-result-continues-cycle",
        {
            **base,
            "agent_table": {
                "agents": [
                    {
                        "agent_id": "verifier-1",
                        "status": "active",
                        "last_verdict": "block",
                        "coverage_status": "unknown",
                        "exhaustive": False,
                    }
                ]
            },
            "latest_result": {
                "agent_id": "verifier-1",
                "verdict": "pass",
                "coverage_status": "partial",
                "exhaustive": False,
                "scope": "docs subset",
            },
        },
        {
            "decision": "resume_active",
        },
    )
    run_resolver_case(
        "active-block-enters-repair",
        {
            **base,
            "agent_table": {
                "agents": [
                    {
                        "agent_id": "verifier-1",
                        "status": "active",
                        "last_verdict": "block",
                        "coverage_status": "complete",
                        "exhaustive": True,
                    }
                ]
            },
        },
        {
            "decision": "enter_repair",
        },
    )
    run_resolver_case(
        "active-valid-pass-terminates",
        {
            **base,
            "agent_table": {
                "agents": [
                    {
                        "agent_id": "verifier-1",
                        "status": "active",
                        "last_verdict": "pass",
                        "coverage_status": "complete",
                        "exhaustive": True,
                    }
                ]
            },
        },
        {
            "decision": "terminate",
        },
    )

    return errors


def validate_core_contract_payload(payload: dict[str, object], path: Path) -> list[str]:
    errors: list[str] = []
    expected_invocation_common_required = [
        "review_goal",
        "review_phase",
        "risk_tier",
        "evidence_paths_or_diff_scope",
        "findings_contract",
    ]
    if payload.get("invocation_common_required") != expected_invocation_common_required:
        errors.append(f"verification-cycle invocation_common_required drifted in {path}")

    expected_invocation_common_constants = {
        "review_goal": "implementation_correctness",
    }
    if payload.get("invocation_common_constants") != expected_invocation_common_constants:
        errors.append(f"verification-cycle invocation_common_constants drifted in {path}")

    expected_bootstrap_defaults = {
        "review_goal": "implementation_correctness",
        "findings_contract": "shared-findings-v1",
        "risk_tier": "STANDARD",
    }
    if payload.get("bootstrap_defaults") != expected_bootstrap_defaults:
        errors.append(f"verification-cycle bootstrap_defaults drifted in {path}")

    expected_review_phase_allowed = [
        "docs_first",
        "source_first",
    ]
    if payload.get("review_phase_allowed") != expected_review_phase_allowed:
        errors.append(f"verification-cycle review_phase_allowed drifted in {path}")

    expected_findings_contract_allowed = [
        "shared-findings-v1",
    ]
    if payload.get("findings_contract_allowed") != expected_findings_contract_allowed:
        errors.append(f"verification-cycle findings_contract_allowed drifted in {path}")

    expected_output_paths_required = [
        "findings_path",
        "verifier_evidence_path",
        "agent_table_path",
    ]
    if payload.get("output_paths_required") != expected_output_paths_required:
        errors.append(f"verification-cycle output_paths_required drifted in {path}")

    expected_verifier_evidence_required = [
        "review_goal",
        "review_phase",
        "template_id",
        "findings_contract",
        "agent_id",
        "start_at",
        "end_at",
        "final_state",
        "review_scope",
        "review_coverage",
        "reviewed_paths",
        "skipped_paths",
        "reviewed_axes",
        "unreviewed_axes",
    ]
    if payload.get("verifier_evidence_required") != expected_verifier_evidence_required:
        errors.append(f"verification-cycle verifier_evidence_required drifted in {path}")

    expected_verifier_evidence_subject_required_any_of = [
        "change",
        "target_ref",
    ]
    if payload.get("verifier_evidence_subject_required_any_of") != expected_verifier_evidence_subject_required_any_of:
        errors.append(f"verification-cycle verifier_evidence_subject_required_any_of drifted in {path}")

    if payload.get("verifier_template_id_const") != "verify-reviewer-inline-v3":
        errors.append(f"verification-cycle verifier_template_id_const drifted in {path}")

    expected_verifier_final_state_allowed = [
        "completed",
        "completed_pass",
    ]
    if payload.get("verifier_final_state_allowed") != expected_verifier_final_state_allowed:
        errors.append(f"verification-cycle verifier_final_state_allowed drifted in {path}")

    expected_verifier_timestamp_fields = [
        "start_at",
        "end_at",
    ]
    if payload.get("verifier_timestamp_fields") != expected_verifier_timestamp_fields:
        errors.append(f"verification-cycle verifier_timestamp_fields drifted in {path}")

    expected_verifier_timestamp_order_rule = "end_at must be greater than or equal to start_at."
    if payload.get("verifier_timestamp_order_rule") != expected_verifier_timestamp_order_rule:
        errors.append(f"verification-cycle verifier_timestamp_order_rule drifted in {path}")

    expected_spawn_reason_semantics = [
        "`no_usable_active_agent` means agent-table.json contains no active agent.",
        "`active_agent_missing` and `active_agent_not_resumable` are distinct recovery spawn cases derived only from continuation_probe.",
    ]
    if payload.get("spawn_reason_semantics") != expected_spawn_reason_semantics:
        errors.append(f"verification-cycle spawn_reason_semantics drifted in {path}")

    expected_cycle_rules = [
        "If a usable active agent exists, continue it first.",
        "Prefer send_input while that same active agent is still open.",
        "If send_input reports agent_not_found, require continuation_probe and try resume for that same active agent next.",
        "Use continuation_probe.resume_result to distinguish resume_active from the recovery spawn cases.",
        "Spawn a new active agent only when agent-table.json contains no active agent or continuation_probe proves a distinct recovery spawn case.",
        "When an active agent reports block, repair until that same agent reports pass.",
        "Only block->pass may mark an agent non_active.",
        "Termination may use only a valid pass from the current active agent.",
    ]
    if payload.get("cycle_rules") != expected_cycle_rules:
        errors.append(f"verification-cycle core cycle_rules drifted in {path}")

    expected_finding_fields = [
        "id",
        "severity",
        "dimension",
        "problem",
        "evidence",
        "recommendation",
        "blocking",
        "auto_fixable",
    ]
    if payload.get("finding_object_required") != expected_finding_fields:
        errors.append(f"verification-cycle finding_object_required drifted in {path}")

    expected_finding_severity_allowed = [
        "CRITICAL",
        "WARNING",
        "SUGGESTION",
    ]
    if payload.get("finding_severity_allowed") != expected_finding_severity_allowed:
        errors.append(f"verification-cycle finding_severity_allowed drifted in {path}")

    expected_finding_dimension_allowed = [
        "Completeness",
        "Correctness",
        "Coherence",
    ]
    if payload.get("finding_dimension_allowed") != expected_finding_dimension_allowed:
        errors.append(f"verification-cycle finding_dimension_allowed drifted in {path}")

    expected_review_axes_allowed = [
        "Completeness",
        "Correctness",
        "Coherence",
    ]
    if payload.get("review_axes_allowed") != expected_review_axes_allowed:
        errors.append(f"verification-cycle review_axes_allowed drifted in {path}")

    expected_non_empty_string_fields = [
        "id",
        "problem",
        "evidence",
        "recommendation",
    ]
    if payload.get("finding_non_empty_string_fields") != expected_non_empty_string_fields:
        errors.append(f"verification-cycle finding_non_empty_string_fields drifted in {path}")

    expected_boolean_fields = [
        "blocking",
        "auto_fixable",
    ]
    if payload.get("finding_boolean_fields") != expected_boolean_fields:
        errors.append(f"verification-cycle finding_boolean_fields drifted in {path}")

    expected_subject_binding = [
        "change",
        "target_ref",
    ]
    if payload.get("subject_required_any_of") != expected_subject_binding:
        errors.append(f"verification-cycle subject_required_any_of drifted in {path}")

    expected_findings_required = [
        "verdict",
        "findings",
    ]
    if payload.get("findings_required") != expected_findings_required:
        errors.append(f"verification-cycle findings_required drifted in {path}")

    expected_finding_semantics = [
        "blocking=true forbids verdict=pass",
        "auto_fixable=true requires blocking=true",
        "severity=SUGGESTION requires blocking=false",
        "review_phase=docs_first requires auto_fixable=false for every finding",
    ]
    if payload.get("finding_semantics") != expected_finding_semantics:
        errors.append(f"verification-cycle finding_semantics drifted in {path}")

    expected_review_axis_semantics = [
        "reviewed_axes and unreviewed_axes must use only review_axes_allowed values",
        "reviewed_axes and unreviewed_axes must not overlap",
        "verdict=pass with review_coverage.exhaustive=true requires unreviewed_axes=[]",
        "verdict=pass with review_coverage.exhaustive=true requires reviewed_axes to cover all review_axes_allowed values",
    ]
    if payload.get("review_axis_semantics") != expected_review_axis_semantics:
        errors.append(f"verification-cycle review_axis_semantics drifted in {path}")

    expected_repair_routing = [
        "automatic repair may run only when blocking=true and auto_fixable=true",
        "blocking findings with auto_fixable=false stop automatic repair routing",
    ]
    if payload.get("repair_routing_rules") != expected_repair_routing:
        errors.append(f"verification-cycle repair_routing_rules drifted in {path}")

    expected_valid_pass_requirements = [
        "review_coverage.coverage_status=complete",
        "review_coverage.exhaustive=true",
    ]
    if payload.get("valid_pass_requirements") != expected_valid_pass_requirements:
        errors.append(f"verification-cycle valid_pass_requirements drifted in {path}")

    expected_partial_scope_rule = (
        "If review_coverage.coverage_status=partial, review_scope.scope must be explicit and non-empty."
    )
    if payload.get("partial_scope_rule") != expected_partial_scope_rule:
        errors.append(f"verification-cycle partial_scope_rule drifted in {path}")

    return errors


def validate_negative_core_contract_cases() -> list[str]:
    path = MODULE_ROOT / "contracts/verification-cycle-core-v1.json"
    payload = json.loads(path.read_text())
    errors: list[str] = []

    def run_case(name: str, mutate: callable, expected_marker: str) -> None:
        mutated = json.loads(json.dumps(payload))
        mutate(mutated)
        case_errors = validate_core_contract_payload(mutated, Path(f"{path}::{name}"))
        if not case_errors:
            errors.append(f"Negative core-contract case unexpectedly passed: {name}")
            return
        detail = "\n".join(case_errors)
        if expected_marker not in detail:
            errors.append(
                f"Negative core-contract case {name} failed without expected marker {expected_marker!r}"
            )

    run_case(
        "missing-template-id",
        lambda mutated: mutated["verifier_evidence_required"].remove("template_id"),
        "verification-cycle verifier_evidence_required drifted",
    )
    run_case(
        "missing-verifier-evidence-subject-binding",
        lambda mutated: mutated.pop("verifier_evidence_subject_required_any_of", None),
        "verification-cycle verifier_evidence_subject_required_any_of drifted",
    )
    run_case(
        "wrong-verifier-template-id-const",
        lambda mutated: mutated.__setitem__("verifier_template_id_const", "wrong-template"),
        "verification-cycle verifier_template_id_const drifted",
    )
    run_case(
        "wrong-verifier-final-state-allowed",
        lambda mutated: mutated.__setitem__("verifier_final_state_allowed", ["completed"]),
        "verification-cycle verifier_final_state_allowed drifted",
    )
    run_case(
        "wrong-verifier-timestamp-fields",
        lambda mutated: mutated.__setitem__("verifier_timestamp_fields", ["start_at"]),
        "verification-cycle verifier_timestamp_fields drifted",
    )
    run_case(
        "wrong-verifier-timestamp-order-rule",
        lambda mutated: mutated.__setitem__("verifier_timestamp_order_rule", "timestamps can float"),
        "verification-cycle verifier_timestamp_order_rule drifted",
    )
    run_case(
        "missing-spawn-reason-semantics",
        lambda mutated: mutated.pop("spawn_reason_semantics", None),
        "verification-cycle spawn_reason_semantics drifted",
    )
    run_case(
        "missing-review-phase",
        lambda mutated: mutated["invocation_common_required"].remove("review_phase"),
        "verification-cycle invocation_common_required drifted",
    )
    run_case(
        "wrong-invocation-common-constants",
        lambda mutated: mutated.__setitem__("invocation_common_constants", {"review_goal": "wrong"}),
        "verification-cycle invocation_common_constants drifted",
    )
    run_case(
        "wrong-bootstrap-defaults",
        lambda mutated: mutated.__setitem__(
            "bootstrap_defaults",
            {
                "review_goal": "implementation_correctness",
                "findings_contract": "wrong",
                "risk_tier": "STANDARD",
            },
        ),
        "verification-cycle bootstrap_defaults drifted",
    )
    run_case(
        "wrong-findings-required",
        lambda mutated: mutated.__setitem__("findings_required", ["verdict"]),
        "verification-cycle findings_required drifted",
    )
    run_case(
        "wrong-partial-scope-rule",
        lambda mutated: mutated.__setitem__("partial_scope_rule", "partial passes are okay"),
        "verification-cycle partial_scope_rule drifted",
    )
    return errors


def validate_findings_routing_contract() -> list[str]:
    errors: list[str] = []
    schema_path = OPENSPEC_ROOT / "schemas/ai-enforced-workflow/schema.yaml"
    spec_path = OPENSPEC_ROOT / "specs/verify-subagent-orchestration/spec.md"

    errors += require_tokens_between(
        schema_path,
        "- finding semantics: follow `finding_semantics` and",
        "- do not substitute user confirmation for verification",
        [
            "`finding_semantics`",
            "`repair_routing_rules`",
            "route blocking non-auto-fixable findings to `openspec-repair-change`",
        ],
    )
    errors += require_tokens_between(
        schema_path,
        "- Verification tasks in `STANDARD` or `STRICT` changes MUST reference",
        "- Review tasks MUST require `agent-table.json`",
        [
            "subject_required_any_of",
            "findings_required / finding_object_required / finding_semantics / repair_routing_rules",
        ],
    )
    errors += require_tokens_between(
        spec_path,
        "### Requirement: Structured Findings Contract",
        "### Requirement: Automatic Repair Loop Boundaries",
        [
            "`blocking`: boolean",
            "`auto_fixable`: boolean",
            "`auto_fixable=true` is valid only when `blocking=true`",
            "`SUGGESTION` findings MUST NOT set `blocking=true`",
        ],
    )
    errors += require_tokens_between(
        spec_path,
        "### Requirement: Automatic Repair Loop Boundaries",
        "### Requirement: Implementation Evidence Includes Inline Scope And Coverage",
        [
            "Blocking findings with `auto_fixable=false` MUST stop the automatic repair loop.",
            "`auto_fixable: false`",
        ],
    )
    return errors


def main() -> int:
    errors = load_syntax()
    errors += validate_reference_targets()
    errors += validate_verify_implementation_structure()
    errors += validate_reference_index_entries()
    errors += validate_verification_cycle_fixtures()
    errors += validate_fixture_findings_contract()
    errors += validate_combined_output_fixture()
    errors += validate_negative_guard_cases()
    errors += validate_negative_findings_cases()
    errors += validate_negative_combined_output_cases()
    errors += validate_task_template_ids()
    errors += validate_spawn_schema_semantics()
    errors += validate_send_input_resume_contract()
    errors += validate_agent_table_ownership_contract()
    errors += validate_verify_reviewer_contract()
    errors += validate_verification_cycle_core_contract()
    errors += validate_adapter_contracts()
    errors += validate_orchestrator_contracts()
    errors += validate_negative_core_contract_cases()
    errors += validate_findings_routing_contract()

    for path, patterns in SCOPE_PATTERNS.items():
        if not path.exists():
            errors.append(f"Missing Stage A scope file: {path}")
            continue
        errors += require(path, patterns)

    for path in LEGACY_FORBID_PATHS:
        if not path.exists():
            errors.append(f"Missing Stage A legacy-check file: {path}")
            continue
        errors += forbid(path, LEGACY_FORBIDDEN)

    if errors:
        print("Stage A validation failed:")
        for error in errors:
            print(f"- {error}")
        return 1

    print("Stage A validation passed.")
    print("Checked syntax, verification-cycle fixtures, and active Stage A contract tokens.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
