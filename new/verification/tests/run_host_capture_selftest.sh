#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

python3 - "${REPO_ROOT}" <<'PY'
from __future__ import annotations

import json
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


repo_root = Path(sys.argv[1])
script_path = repo_root / "new" / "user" / "host_capture.py"
output_dir = Path(tempfile.mkdtemp(prefix="ls2k-host-capture-selftest-"))


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def wait_for_ready(process: subprocess.Popen[str]) -> list[str]:
    lines: list[str] = []
    deadline = time.monotonic() + 8.0
    while time.monotonic() < deadline:
        line = process.stdout.readline() if process.stdout is not None else ""
        if line:
            lines.append(line.rstrip("\n"))
            if "[host_capture] READY " in line:
                return lines
            continue
        if process.poll() is not None:
            raise AssertionError(f"host_capture exited before READY, rc={process.returncode}, output={lines}")
        time.sleep(0.05)
    raise AssertionError(f"timed out waiting for READY, output={lines}")


def send_json_line(port: int, payload: dict[str, object]) -> None:
    with socket.create_connection(("127.0.0.1", port), timeout=2.0) as sock:
        sock.sendall((json.dumps(payload, separators=(",", ":")) + "\n").encode("ascii"))
        sock.sendall(
            (
                json.dumps(
                    {
                        "type": "ack",
                        "seq": 7,
                        "outcome": "accepted",
                        "reference": {"mode": "circle_entry", "source": "circle_left"},
                    },
                    separators=(",", ":"),
                )
                + "\n"
            ).encode("ascii")
        )


def send_media_frame(port: int) -> None:
    def send_envelope(sock: socket.socket, header: dict[str, object], payload: bytes = b"") -> None:
        header_bytes = json.dumps(header, separators=(",", ":")).encode("utf-8")
        sock.sendall(len(header_bytes).to_bytes(4, "big"))
        sock.sendall(len(payload).to_bytes(4, "big"))
        sock.sendall(header_bytes)
        sock.sendall(payload)

    with socket.create_connection(("127.0.0.1", port), timeout=2.0) as sock:
        send_envelope(
            sock,
            {
                "type": "config_snapshot",
                "media_publish_interval_ms": 80,
                "param_snapshot": {"yaw_rate_pid": {"p": 1.5}},
            },
        )
        payload = bytes(range(12))
        send_envelope(
            sock,
            {
                "type": "image_frame",
                "frame_id": 3,
                "width": 4,
                "height": 3,
                "capture_time_ms": 12345,
                "motion_phase": "AUTO",
                "steering_snapshot": {
                    "reference": {"mode": "circle_entry", "source": "circle_left"},
                    "eligibility": {"usable": True},
                    "safety_gate": {"reason": "none"},
                    "lateral_error": {"weighted_lateral_error_m": 0.01},
                    "yaw_control": {"turn_output_target": 0.2},
                    "actuator": {"raw_turn_output": 12, "applied_turn_output": 12},
                },
            },
            payload,
        )


control_port = free_port()
media_port = free_port()
process = subprocess.Popen(
    [
        sys.executable,
        str(script_path),
        "--listen-host",
        "127.0.0.1",
        "--listen-port",
        str(control_port),
        "--media-listen-host",
        "127.0.0.1",
        "--media-listen-port",
        str(media_port),
        "--duration-s",
        "1.2",
        "--output-dir",
        str(output_dir),
    ],
    cwd=str(repo_root),
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    bufsize=1,
)

captured_lines: list[str] = []
try:
    captured_lines.extend(wait_for_ready(process))
    send_json_line(control_port, {
        "type": "telemetry",
        "motion_phase": "AUTO",
        "reference": {"mode": "circle_entry", "source": "circle_left"},
        "eligibility": {"usable": True, "reason": "ok"},
        "reference_control": {"ready": True, "reason": "ok"},
        "safety_gate": {"veto_active": False, "reason": "none"},
        "lateral_error": {"weighted_lateral_error_m": 0.02},
        "yaw_control": {"turn_output_target": 0.1},
        "actuator": {"raw_turn_output": 4, "applied_turn_output": 4},
    })
    send_media_frame(media_port)
    rc = process.wait(timeout=6.0)
    if process.stdout is not None:
        captured_lines.extend(line.rstrip("\n") for line in process.stdout.readlines())
    assert rc == 0, f"host_capture rc={rc}, output={captured_lines}"
finally:
    if process.poll() is None:
        process.terminate()
        process.wait(timeout=2.0)

summary = json.loads((output_dir / "summary.json").read_text(encoding="utf-8"))
assistant = summary["assistant_summary"]
media = summary["media_summary"]
assert assistant["bound"] is True
assert assistant["connected"] is True
assert assistant["telemetry_frames"] == 1
assert assistant["ack_frames"] == 1
assert media["bound"] is True
assert media["connected"] is True
assert media["frame_count"] == 1
assert media["payload_bytes"] == 12
assert (output_dir / "assistant_control.csv").is_file()
assert (output_dir / "assistant_control.jsonl").is_file()
assert (output_dir / "steering-media" / "config_snapshot.json").is_file()
assert (output_dir / "steering-media" / "frame_metadata.jsonl").is_file()
assert (output_dir / "steering-media" / "frames" / "frame-000003.raw").read_bytes() == bytes(range(12))

print("host_capture_selftest passed")
shutil.rmtree(output_dir)
PY
