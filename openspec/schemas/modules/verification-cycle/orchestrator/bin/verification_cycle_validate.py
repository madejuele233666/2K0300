#!/usr/bin/env python3
"""Validate verification-cycle orchestrator decision payloads."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

ALLOWED_DECISIONS = {
    "resume_active",
    "spawn_active",
    "enter_repair",
    "mark_non_active",
    "terminate",
    "wait_for_user",
    "blocked_invalid_state",
}
ALLOWED_SPAWN_REASON_CODES = {
    "no_usable_active_agent",
    "active_agent_missing",
    "active_agent_not_resumable",
}


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate a verification-cycle decision JSON file.")
    parser.add_argument("--input", required=True, help="Path to a decision JSON file.")
    args = parser.parse_args()
    data = json.loads(Path(args.input).read_text())
    errors: list[str] = []
    if data.get("contract") != "verification-cycle-orchestrator-decision-v1":
        errors.append("Expected contract=verification-cycle-orchestrator-decision-v1")
    if data.get("decision") not in ALLOWED_DECISIONS:
        errors.append("Unknown decision value")
    if not isinstance(data.get("reason"), str) or not data["reason"].strip():
        errors.append("Decision reason must be a non-empty string")
    spawn_reason_code = data.get("spawn_reason_code")
    if data.get("decision") == "spawn_active":
        if spawn_reason_code not in ALLOWED_SPAWN_REASON_CODES:
            errors.append("spawn_active decisions must include a valid spawn_reason_code")
    elif "spawn_reason_code" in data:
        errors.append("Only spawn_active decisions may include spawn_reason_code")
    if errors:
        print("verification-cycle validation failed:")
        for error in errors:
            print(f"- {error}")
        return 1
    print("verification-cycle validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
