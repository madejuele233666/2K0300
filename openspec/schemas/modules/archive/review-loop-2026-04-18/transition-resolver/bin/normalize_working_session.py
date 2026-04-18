#!/usr/bin/env python3
"""
Normalize caller-observed working-session reuse evidence into the shared
transition-resolver session shape.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

SCRIPT = Path(__file__).resolve()
RESOLVER_ROOT = SCRIPT.parents[1]
CONTRACT_PATH = RESOLVER_ROOT / "contracts" / "working-session-normalization-v1.json"

ALLOWED_PROBE_RESULTS = {
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


def as_bool(value: Any) -> bool | None:
    return value if isinstance(value, bool) else None


def as_str(value: Any) -> str | None:
    return value if isinstance(value, str) else None


def normalize(data: dict[str, Any]) -> tuple[dict[str, Any] | None, list[str]]:
    errors: list[str] = []
    source = data
    if isinstance(data.get("session"), dict) and "active_working_agent_id" not in data:
        source = data["session"]

    active_working_agent_id = as_str(source.get("active_working_agent_id")) or ""
    active_challenger_agent_id = as_str(source.get("active_challenger_agent_id")) or ""
    send_input_ready = as_bool(source.get("send_input_ready"))
    resume_probe_attempted = as_bool(source.get("resume_probe_attempted"))
    resume_probe_result = as_str(source.get("resume_probe_result"))

    if send_input_ready is None:
        errors.append("Expected boolean send_input_ready")
    if resume_probe_attempted is None:
        errors.append("Expected boolean resume_probe_attempted")
    if resume_probe_result not in ALLOWED_PROBE_RESULTS:
        allowed = ",".join(sorted(ALLOWED_PROBE_RESULTS))
        errors.append(f"Expected resume_probe_result in {{{allowed}}}")
    if errors:
        return None, errors

    if not active_working_agent_id:
        if send_input_ready is True:
            errors.append("send_input_ready=true requires active_working_agent_id")
        if resume_probe_attempted is True:
            errors.append("resume_probe_attempted=true requires active_working_agent_id")
        if resume_probe_result != "not_attempted":
            errors.append("resume_probe_result must be not_attempted when active_working_agent_id is empty")
        if errors:
            return None, errors
        return {
            "contract": "working-session-normalization-v1",
            "active_working_agent_id": "",
            "active_challenger_agent_id": active_challenger_agent_id,
            "session_open": False,
            "resumable": False,
            "exception_reason": "",
            "send_input_ready": False,
            "resume_probe_attempted": False,
            "resume_probe_result": "not_attempted",
        }, []

    if send_input_ready is True:
        if resume_probe_attempted is True and resume_probe_result not in {"not_attempted", "resumable"}:
            errors.append("send_input_ready=true is incompatible with failed resume probe results")
        if errors:
            return None, errors
        return {
            "contract": "working-session-normalization-v1",
            "active_working_agent_id": active_working_agent_id,
            "active_challenger_agent_id": active_challenger_agent_id,
            "session_open": True,
            "resumable": False,
            "exception_reason": "",
            "send_input_ready": True,
            "resume_probe_attempted": bool(resume_probe_attempted),
            "resume_probe_result": resume_probe_result or "not_attempted",
        }, []

    if resume_probe_attempted is not True:
        if resume_probe_result != "not_attempted":
            errors.append("resume_probe_attempted=false requires resume_probe_result=not_attempted")
        if errors:
            return None, errors
        return {
            "contract": "working-session-normalization-v1",
            "active_working_agent_id": active_working_agent_id,
            "active_challenger_agent_id": active_challenger_agent_id,
            "session_open": False,
            "resumable": False,
            "exception_reason": "",
            "send_input_ready": False,
            "resume_probe_attempted": False,
            "resume_probe_result": "not_attempted",
        }, []

    exception_reason = {
        "unresumable": "session_completed_unresumable",
        "agent_not_found": "agent_not_found",
        "tooling_error": "tooling_recovery",
    }.get(resume_probe_result, "")
    resumable = resume_probe_result == "resumable"

    return {
        "contract": "working-session-normalization-v1",
        "active_working_agent_id": active_working_agent_id,
        "active_challenger_agent_id": active_challenger_agent_id,
        "session_open": False,
        "resumable": resumable,
        "exception_reason": exception_reason,
        "send_input_ready": False,
        "resume_probe_attempted": True,
        "resume_probe_result": resume_probe_result,
    }, []


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, help="Path to caller-observed working session evidence JSON")
    parser.add_argument(
        "--print-contract-path",
        action="store_true",
        help="Print the authoritative working-session normalization contract path before normalization",
    )
    args = parser.parse_args()

    if args.print_contract_path:
        print(CONTRACT_PATH)

    data, errors = load_json_object(Path(args.input).resolve())
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    assert data is not None

    normalized, errors = normalize(data)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print(json.dumps(normalized, ensure_ascii=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
