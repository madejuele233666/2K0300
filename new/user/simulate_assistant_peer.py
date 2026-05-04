#!/usr/bin/env python3
"""Local assistant peer simulator for host-workflow verification.

All telemetry emitted by this script is synthetic and is not board/runtime
authority.
"""

from __future__ import annotations

import argparse
import json
import socket
import time
from typing import Any, Dict, Optional


def log(message: str) -> None:
    print(message, flush=True)


def send_json_line(connection: socket.socket, payload: Dict[str, Any]) -> None:
    connection.sendall((json.dumps(payload, separators=(",", ":")) + "\n").encode("ascii"))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Simulate the board-side assistant peer locally")
    parser.add_argument("--host", default="127.0.0.1", help="listener host exposed by tune_speed.py")
    parser.add_argument("--port", type=int, default=8888, help="listener port exposed by tune_speed.py")
    parser.add_argument("--connect-timeout-s", type=float, default=10.0, help="max time to wait for the host listener")
    parser.add_argument("--max-target-speed", type=float, default=100.0, help="accepted target-speed upper bound")
    return parser.parse_args()


def emit_telemetry(connection: socket.socket,
                   motion_phase: str,
                   tuning_mode_enabled: bool,
                   turn_suppressed: bool,
                   override_enabled: bool,
                   override_value: Optional[float],
                   effective_speed_target: float,
                   left_target: float,
                   right_target: float,
                   left_measured: float,
                   right_measured: float,
                   raw_turn: int,
                   applied_turn: int,
                   frames: int = 3) -> None:
    for _ in range(frames):
        send_json_line(
            connection,
            {
                "type": "telemetry",
                "motion_phase": motion_phase,
                "tuning_mode_enabled": tuning_mode_enabled,
                "turn_suppressed": turn_suppressed,
                "target_speed_override_enabled": override_enabled,
                "target_speed_override_value": override_value if override_enabled else None,
                "effective_speed_target": effective_speed_target,
                "left_speed_target": left_target,
                "right_speed_target": right_target,
                "left_measured_speed": left_measured,
                "right_measured_speed": right_measured,
                "left_pwm_command": int(left_target * 100),
                "right_pwm_command": int(right_target * 100),
                "telemetry_source": "simulate_assistant_peer_synthetic",
                "reference": {"mode": "interval_center", "source": "simple_interval_center"},
                "eligibility": {
                    "usable": True,
                    "leading_usable_samples": 4,
                    "leading_min_forward_m": 0.061,
                    "leading_max_forward_m": 0.249,
                    "reason": "ok",
                },
                "lateral_error": {
                    "computed": True,
                    "synthetic": True,
                    "weighted_lateral_error_m": raw_turn / 12000.0,
                    "weighted_sample_count": 4,
                    "weight_sum": 3.75,
                    "reason": "ok",
                },
                "reference_control": {"ready": True, "reason": "ok"},
                "safety_gate": {"veto_active": False, "reason": "none"},
                "degraded": {"active": False, "reason": "none"},
                "yaw_control": {"turn_output_target": 0.0},
                "actuator": {
                    "raw_turn_output": raw_turn,
                    "applied_turn_output": applied_turn,
                },
            },
        )
        time.sleep(0.03)


def main() -> int:
    args = parse_args()
    deadline = time.monotonic() + max(0.1, args.connect_timeout_s)
    connection: Optional[socket.socket] = None

    while time.monotonic() < deadline:
        try:
            connection = socket.create_connection((args.host, args.port), timeout=1.0)
            connection.settimeout(None)
            break
        except OSError:
            time.sleep(0.1)

    if connection is None:
        raise SystemExit("failed to connect to tune_speed.py listener before timeout")

    tuning_mode_enabled = False
    turn_suppressed = False
    override_enabled = False
    override_value: Optional[float] = None
    effective_speed_target = 0.0
    motion_phase = "DISARMED"
    sent_input_rejected = False

    with connection:
        log(f"[peer] connected to {args.host}:{args.port}")
        reader = connection.makefile("r", encoding="ascii", newline="\n")
        for raw_line in reader:
            line = raw_line.strip()
            if not line:
                continue
            frame = json.loads(line)
            seq = int(frame.get("seq", 0))
            cmd = str(frame.get("cmd", ""))
            log(f"[peer] rx seq={seq} cmd={cmd}")

            if cmd == "set_target_speed":
                value = float(frame.get("value", 0.0))
                ttl_ms = int(frame.get("ttl_ms", 0))
                if not tuning_mode_enabled:
                    send_json_line(connection, {"type": "ack", "seq": seq, "outcome": "rejected", "reason": "tuning mode is disabled"})
                    if not sent_input_rejected:
                        sent_input_rejected = True
                        send_json_line(
                            connection,
                            {
                                "type": "state",
                                "event": "input_rejected",
                                "reason": "simulated malformed inbound payload for host verification",
                                "tuning_mode_enabled": tuning_mode_enabled,
                                "turn_suppressed": turn_suppressed,
                                "target_speed_override_enabled": override_enabled,
                                "target_speed_override_value": override_value if override_enabled else None,
                                "effective_speed_target": effective_speed_target,
                            },
                        )
                    emit_telemetry(connection, motion_phase, tuning_mode_enabled, turn_suppressed, override_enabled, override_value, effective_speed_target, 0.0, 0.0, 0.0, 0.0, 0, 0, frames=2)
                    continue
                if value > args.max_target_speed:
                    send_json_line(connection, {"type": "ack", "seq": seq, "outcome": "rejected", "reason": "invalid target speed: value exceeds running_speed_target"})
                    continue
                send_json_line(connection, {"type": "ack", "seq": seq, "outcome": "accepted"})
                override_enabled = True
                override_value = value
                effective_speed_target = value
                base_turn = -240 if not turn_suppressed else -240
                applied_turn = 0 if turn_suppressed else base_turn
                left_target = value + 7.5
                right_target = value - 7.5
                emit_telemetry(
                    connection,
                    motion_phase,
                    tuning_mode_enabled,
                    turn_suppressed,
                    override_enabled,
                    override_value,
                    effective_speed_target,
                    left_target,
                    right_target,
                    left_target * 0.78,
                    right_target * 0.76,
                    base_turn,
                    applied_turn,
                    frames=max(4, min(8, ttl_ms // 400 if ttl_ms > 0 else 4)),
                )
                continue

            if cmd == "set_turn_suppressed":
                value = bool(frame.get("value", False))
                if not tuning_mode_enabled:
                    send_json_line(connection, {"type": "ack", "seq": seq, "outcome": "rejected", "reason": "tuning mode is disabled"})
                    continue
                turn_suppressed = value
                send_json_line(connection, {"type": "ack", "seq": seq, "outcome": "accepted"})
                continue

            if cmd == "enable_tuning_mode":
                tuning_mode_enabled = True
                send_json_line(connection, {"type": "ack", "seq": seq, "outcome": "accepted"})
                emit_telemetry(connection, motion_phase, tuning_mode_enabled, turn_suppressed, override_enabled, override_value, effective_speed_target, 0.0, 0.0, 0.0, 0.0, 0, 0, frames=2)
                continue

            if cmd == "disable_tuning_mode":
                tuning_mode_enabled = False
                turn_suppressed = False
                override_enabled = False
                override_value = None
                effective_speed_target = 0.0
                send_json_line(connection, {"type": "ack", "seq": seq, "outcome": "accepted"})
                send_json_line(
                    connection,
                    {
                        "type": "state",
                        "event": "snapshot_cleared",
                        "reason": "tuning disabled by command",
                        "tuning_mode_enabled": False,
                        "turn_suppressed": False,
                        "target_speed_override_enabled": False,
                        "target_speed_override_value": None,
                        "effective_speed_target": 0.0,
                    },
                )
                emit_telemetry(connection, "DISARMED", False, False, False, None, 0.0, 0.0, 0.0, 0.0, 0.0, 0, 0, frames=2)
                break

            if cmd == "start":
                motion_phase = "RUNNING"
                send_json_line(connection, {"type": "ack", "seq": seq, "outcome": "accepted"})
                emit_telemetry(connection, motion_phase, tuning_mode_enabled, turn_suppressed, override_enabled, override_value, effective_speed_target, 0.0, 0.0, 0.0, 0.0, 0, 0, frames=2)
                continue

            if cmd == "stop":
                motion_phase = "DISARMED"
                override_enabled = False
                override_value = None
                effective_speed_target = 0.0
                send_json_line(
                    connection,
                    {
                        "type": "state",
                        "event": "override_cleared",
                        "reason": "override TTL expired",
                        "tuning_mode_enabled": tuning_mode_enabled,
                        "turn_suppressed": turn_suppressed,
                        "target_speed_override_enabled": False,
                        "target_speed_override_value": None,
                        "effective_speed_target": 0.0,
                    },
                )
                send_json_line(connection, {"type": "ack", "seq": seq, "outcome": "accepted"})
                emit_telemetry(connection, motion_phase, tuning_mode_enabled, turn_suppressed, False, None, 0.0, 0.0, 0.0, 0.0, 0.0, 0, 0, frames=3)
                continue

            send_json_line(connection, {"type": "ack", "seq": seq, "outcome": "rejected", "reason": "unsupported command"})

    log("[peer] session complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
