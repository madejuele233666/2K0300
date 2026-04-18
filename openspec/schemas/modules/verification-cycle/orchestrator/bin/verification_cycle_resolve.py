#!/usr/bin/env python3
"""Resolve verification-cycle state into a deterministic orchestrator decision."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def valid_pass(agent: dict[str, Any]) -> bool:
    return (
        agent.get("last_verdict") == "pass"
        and agent.get("coverage_status") == "complete"
        and agent.get("exhaustive") is True
    )


def valid_pass_result(result: dict[str, Any]) -> bool:
    return (
        result.get("verdict") == "pass"
        and result.get("coverage_status") == "complete"
        and result.get("exhaustive") is True
    )


def find_active_agents(agents: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [agent for agent in agents if agent.get("status") == "active"]


def decide(data: dict[str, Any]) -> dict[str, Any]:
    if data.get("manual_pause") is True:
        return {
            "contract": "verification-cycle-orchestrator-decision-v1",
            "decision": "wait_for_user",
            "reason": "manual_pause requested",
        }

    table = data.get("agent_table") or {}
    agents = table.get("agents") or []
    active_agents = find_active_agents(agents)

    if len(active_agents) > 1:
        return {
            "contract": "verification-cycle-orchestrator-decision-v1",
            "decision": "blocked_invalid_state",
            "reason": "multiple active agents are not allowed",
        }

    if not active_agents:
        return {
            "contract": "verification-cycle-orchestrator-decision-v1",
            "decision": "spawn_active",
            "reason": "no active agent exists",
            "spawn_reason_code": "no_usable_active_agent",
        }

    active = active_agents[0]
    agent_id = active.get("agent_id", "")
    latest_result = data.get("latest_result") or {}
    continuation_probe = data.get("continuation_probe") or {}
    if continuation_probe:
        if continuation_probe.get("agent_id") != agent_id:
            return {
                "contract": "verification-cycle-orchestrator-decision-v1",
                "decision": "blocked_invalid_state",
                "reason": "continuation probe agent_id must match the active agent",
                "agent_id": agent_id,
            }
        if continuation_probe.get("send_input_result") == "agent_not_found":
            resume_result = continuation_probe.get("resume_result")
            if resume_result == "not_attempted":
                return {
                    "contract": "verification-cycle-orchestrator-decision-v1",
                    "decision": "resume_active",
                    "reason": "send_input reported agent_not_found; resume the same active agent next",
                    "agent_id": agent_id,
                }
            if resume_result == "agent_not_found":
                return {
                    "contract": "verification-cycle-orchestrator-decision-v1",
                    "decision": "spawn_active",
                    "reason": "active agent is missing after required send_input/resume recovery",
                    "spawn_reason_code": "active_agent_missing",
                    "agent_id": agent_id,
                }
            if resume_result == "not_resumable":
                return {
                    "contract": "verification-cycle-orchestrator-decision-v1",
                    "decision": "spawn_active",
                    "reason": "active agent could not be resumed and must be replaced",
                    "spawn_reason_code": "active_agent_not_resumable",
                    "agent_id": agent_id,
                }

    if latest_result:
        if latest_result.get("agent_id") != agent_id:
            return {
                "contract": "verification-cycle-orchestrator-decision-v1",
                "decision": "blocked_invalid_state",
                "reason": "latest_result agent_id must match the active agent",
                "agent_id": agent_id,
            }
        if latest_result.get("verdict") == "block":
            return {
                "contract": "verification-cycle-orchestrator-decision-v1",
                "decision": "enter_repair",
                "reason": "latest_result is blocking for the active agent",
                "agent_id": agent_id,
            }
        if valid_pass_result(latest_result):
            if active.get("last_verdict") == "block":
                return {
                    "contract": "verification-cycle-orchestrator-decision-v1",
                    "decision": "mark_non_active",
                    "reason": "active agent transitioned from block to valid pass",
                    "agent_id": agent_id,
                }
            return {
                "contract": "verification-cycle-orchestrator-decision-v1",
                "decision": "terminate",
                    "reason": "latest_result is a valid pass for the active agent",
                    "agent_id": agent_id,
                }
        if latest_result.get("verdict") == "pass":
            if latest_result.get("coverage_status") == "partial" and not str(latest_result.get("scope", "")).strip():
                return {
                    "contract": "verification-cycle-orchestrator-decision-v1",
                    "decision": "blocked_invalid_state",
                    "reason": "partial latest_result must declare scope for the active agent",
                    "agent_id": agent_id,
                }
            return {
                "contract": "verification-cycle-orchestrator-decision-v1",
                "decision": "resume_active",
                "reason": "latest_result pass is non-terminal and the active agent must continue",
                "agent_id": agent_id,
            }

    if active.get("resumable") is True:
        if active.get("last_verdict") == "block":
            return {
                "contract": "verification-cycle-orchestrator-decision-v1",
                "decision": "enter_repair",
                "reason": "active agent is blocking",
                "agent_id": agent_id,
            }
        if valid_pass(active):
            return {
                "contract": "verification-cycle-orchestrator-decision-v1",
                "decision": "terminate",
                "reason": "active agent has a valid pass",
                "agent_id": agent_id,
            }
        return {
            "contract": "verification-cycle-orchestrator-decision-v1",
            "decision": "resume_active",
            "reason": "active agent is resumable and requires continuation",
            "agent_id": agent_id,
        }

    if active.get("last_verdict") == "pass":
        return {
            "contract": "verification-cycle-orchestrator-decision-v1",
            "decision": "blocked_invalid_state",
            "reason": "active agent with pass must be resumable or terminated",
            "agent_id": agent_id,
        }

    return {
        "contract": "verification-cycle-orchestrator-decision-v1",
        "decision": "spawn_active",
        "reason": "active agent is unusable and must be replaced",
        "spawn_reason_code": "active_agent_not_resumable",
        "agent_id": agent_id,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Resolve verification-cycle state.")
    parser.add_argument("--input", required=True, help="Path to orchestrator input JSON.")
    args = parser.parse_args()
    data = load_json(Path(args.input))
    print(json.dumps(decide(data), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
