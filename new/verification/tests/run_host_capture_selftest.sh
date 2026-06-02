#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

python3 - "${REPO_ROOT}" <<'PY'
from __future__ import annotations

import json
import base64
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request
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
                        "reference": {"mode": "interval_center", "source": "circle_v2_inner"},
                    },
                    separators=(",", ":"),
                )
                + "\n"
            ).encode("ascii")
        )


def send_media_frame(port: int, frame_id: int = 3, payload_start: int = 0) -> bytes:
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
        payload = bytes((payload_start + index) % 256 for index in range(12))
        send_envelope(
            sock,
            {
                "type": "image_frame",
                "frame_id": frame_id,
                "width": 4,
                "height": 3,
                "source_width": 4,
                "source_height": 3,
                "downsample": 1,
                "capture_time_ms": 12345 + frame_id,
                "motion_phase": "AUTO",
                "steering_snapshot": {
                    "reference": {"mode": "interval_center", "source": "circle_v2_inner"},
                    "circle_v2": {
                        "enabled": True,
                        "frame_phase": "inner_trace",
                        "next_phase": "inner_trace",
                        "dir": "left",
                        "reference_role": "inner_trace",
                        "reason": "none",
                    },
                    "eligibility": {"usable": True},
                    "safety_gate": {"reason": "none"},
                    "lateral_error": {"weighted_lateral_error_m": 0.01},
                    "yaw_control": {"turn_output_target": 0.2},
                    "actuator": {"raw_turn_output": 12, "applied_turn_output": 12},
                },
            },
            payload,
        )
        return payload


def read_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise AssertionError("socket closed while reading websocket frame")
        data.extend(chunk)
    return bytes(data)


def connect_live_websocket(port: int) -> socket.socket:
    sock = socket.create_connection(("127.0.0.1", port), timeout=2.0)
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    request = (
        "GET /ws HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    )
    sock.sendall(request.encode("ascii"))
    response = bytearray()
    while b"\r\n\r\n" not in response:
        response.extend(sock.recv(4096))
    assert b" 101 " in response.split(b"\r\n", 1)[0], response.decode("latin1", errors="replace")
    sock.settimeout(3.0)
    return sock


def assert_live_port_conflict_is_controlled() -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as occupied:
        occupied.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        occupied.bind(("127.0.0.1", 0))
        occupied.listen(1)
        occupied_port = int(occupied.getsockname()[1])
        result = subprocess.run(
            [
                sys.executable,
                str(script_path),
                "--listen-host",
                "127.0.0.1",
                "--listen-port",
                str(free_port()),
                "--live-web",
                "--live-host",
                "127.0.0.1",
                "--live-port",
                str(occupied_port),
                "--duration-s",
                "0.1",
                "--output-dir",
                str(output_dir / "live-port-conflict"),
            ],
            cwd=str(repo_root),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=4.0,
        )
    assert result.returncode == 1, result.stdout
    assert "[live] failed to start viewer" in result.stdout, result.stdout
    assert "Traceback" not in result.stdout, result.stdout


def send_ws_client_frame(sock: socket.socket, opcode: int, payload: bytes) -> None:
    first = 0x80 | (opcode & 0x0F)
    mask = os.urandom(4)
    length = len(payload)
    if length < 126:
        header = bytes([first, 0x80 | length])
    elif length <= 0xFFFF:
        header = bytes([first, 0x80 | 126]) + struct.pack("!H", length)
    else:
        header = bytes([first, 0x80 | 127]) + struct.pack("!Q", length)
    masked = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
    sock.sendall(header + mask + masked)


def read_ws_frame(sock: socket.socket) -> tuple[int, bytes]:
    first, second = read_exact(sock, 2)
    opcode = first & 0x0F
    masked = (second & 0x80) != 0
    length = second & 0x7F
    if length == 126:
        length = struct.unpack("!H", read_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", read_exact(sock, 8))[0]
    mask = read_exact(sock, 4) if masked else b""
    payload = read_exact(sock, length)
    if masked:
        payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
    return opcode, payload


def decode_live_envelope(payload: bytes) -> tuple[dict[str, object], bytes]:
    assert len(payload) >= 8
    header_len = int.from_bytes(payload[0:4], "big")
    payload_len = int.from_bytes(payload[4:8], "big")
    assert len(payload) == 8 + header_len + payload_len
    header = json.loads(payload[8 : 8 + header_len].decode("utf-8"))
    image = payload[8 + header_len :]
    return header, image


def fetch_latest(port: int, sequence: int) -> tuple[int, bytes]:
    with urllib.request.urlopen(
        f"http://127.0.0.1:{port}/latest.bin?seq={sequence}",
        timeout=2.0,
    ) as response:
        return int(response.status), response.read()


def read_live_image(sock: socket.socket, expected_frame_id: int) -> tuple[dict[str, object], bytes]:
    deadline = time.monotonic() + 4.0
    while time.monotonic() < deadline:
        opcode, payload = read_ws_frame(sock)
        assert opcode == 0x2
        header, image = decode_live_envelope(payload)
        if header.get("type") == "image_frame" and int(header.get("frame_id", 0)) == expected_frame_id:
            return header, image
    raise AssertionError(f"timed out waiting for live image frame {expected_frame_id}")


def send_media_stream(port: int, frame_count: int, width: int = 320, height: int = 240) -> None:
    def send_envelope(sock: socket.socket, header: dict[str, object], payload: bytes = b"") -> None:
        header_bytes = json.dumps(header, separators=(",", ":")).encode("utf-8")
        sock.sendall(len(header_bytes).to_bytes(4, "big"))
        sock.sendall(len(payload).to_bytes(4, "big"))
        sock.sendall(header_bytes)
        sock.sendall(payload)

    payload = bytes(index % 256 for index in range(width * height))
    with socket.create_connection(("127.0.0.1", port), timeout=2.0) as sock:
        send_envelope(
            sock,
            {
                "type": "config_snapshot",
                "media_publish_interval_ms": 33,
                "param_snapshot": {"yaw_rate_pid": {"p": 1.5}},
            },
        )
        for frame_id in range(1, frame_count + 1):
            send_envelope(
                sock,
                {
                    "type": "image_frame",
                    "frame_id": frame_id,
                    "width": width,
                    "height": height,
                    "source_width": width,
                    "source_height": height,
                    "downsample": 1,
                    "capture_time_ms": 50000 + frame_id,
                    "publish_time_ms": 50000 + frame_id,
                    "motion_phase": "AUTO",
                    "steering_snapshot": {
                        "reference": {"mode": "interval_center", "source": "straight"},
                        "eligibility": {"usable": True},
                        "reference_control": {"ready": True, "reason": "ok"},
                        "safety_gate": {"veto_active": False, "reason": "none"},
                        "lateral_error": {"weighted_lateral_error_m": 0.0},
                        "yaw_control": {"turn_output_target": 0.0},
                        "actuator": {"raw_turn_output": 0, "applied_turn_output": 0},
                    },
                },
                payload,
            )


def assert_high_fps_live_mode() -> None:
    fast_output_dir = Path(tempfile.mkdtemp(prefix="ls2k-host-capture-fast-live-"))
    fast_control_port = free_port()
    fast_media_port = free_port()
    fast_live_port = free_port()
    frame_count = 80
    process = subprocess.Popen(
        [
            sys.executable,
            str(script_path),
            "--listen-host",
            "127.0.0.1",
            "--listen-port",
            str(fast_control_port),
            "--assistant-accept-timeout-s",
            "0.2",
            "--media-listen-host",
            "127.0.0.1",
            "--media-listen-port",
            str(fast_media_port),
            "--media-accept-timeout-s",
            "0.2",
            "--media-record-mode",
            "none",
            "--live-web",
            "--live-host",
            "127.0.0.1",
            "--live-port",
            str(fast_live_port),
            "--duration-s",
            "2.0",
            "--output-dir",
            str(fast_output_dir),
        ],
        cwd=str(repo_root),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    captured: list[str] = []
    try:
        captured.extend(wait_for_ready(process))
        send_media_stream(fast_media_port, frame_count)
        rc = process.wait(timeout=6.0)
        if process.stdout is not None:
            captured.extend(line.rstrip("\n") for line in process.stdout.readlines())
        assert rc == 0, f"fast host_capture rc={rc}, output={captured}"
        summary = json.loads((fast_output_dir / "summary.json").read_text(encoding="utf-8"))
        media = summary["media_summary"]
        live = summary["live_summary"]
        assert media["record_mode"] == "none"
        assert media["frame_count"] == frame_count
        assert media["payload_bytes"] == frame_count * 320 * 240
        assert media["raw_frames_recorded"] == 0
        assert media["metadata_frames_recorded"] == 0
        assert media["receiver_error"] is None
        assert live["image_messages"] == frame_count
        assert live["last_frame_id"] == frame_count
        assert live["last_message_bytes"] > 320 * 240
        assert not (fast_output_dir / "steering-media" / "frames").exists()
        assert not (fast_output_dir / "steering-media" / "frame_metadata.jsonl").exists()
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=2.0)
        shutil.rmtree(fast_output_dir)


control_port = free_port()
media_port = free_port()
live_port = free_port()
assert_live_port_conflict_is_controlled()
assert_high_fps_live_mode()
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
        "--live-web",
        "--live-host",
        "127.0.0.1",
        "--live-port",
        str(live_port),
        "--duration-s",
        "2.0",
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
    live_sock = connect_live_websocket(live_port)
    slow_live_sock = connect_live_websocket(live_port)
    send_ws_client_frame(live_sock, 0x1, b'{"cmd":"start","speed":999}')
    send_ws_client_frame(live_sock, 0x2, b"\x00\x01ignored")
    send_json_line(control_port, {
        "type": "telemetry",
        "motion_phase": "AUTO",
        "reference": {"mode": "interval_center", "source": "circle_v2_inner"},
        "circle_v2": {
            "enabled": True,
            "frame_phase": "inner_trace",
            "next_phase": "inner_trace",
            "dir": "left",
            "reference_role": "inner_trace",
            "reason": "none",
        },
        "eligibility": {"usable": True, "reason": "ok"},
        "reference_control": {"ready": True, "reason": "ok"},
        "safety_gate": {"veto_active": False, "reason": "none"},
        "lateral_error": {"weighted_lateral_error_m": 0.02},
        "yaw_control": {"turn_output_target": 0.1},
        "actuator": {"raw_turn_output": 4, "applied_turn_output": 4},
    })
    first_payload = send_media_frame(media_port, frame_id=3, payload_start=0)
    first_header, first_live_payload = read_live_image(live_sock, 3)
    assert first_live_payload == first_payload
    assert first_header["width"] == 4
    assert first_header["height"] == 3
    assert first_header["source_width"] == 4
    assert first_header["source_height"] == 3
    assert "steering_snapshot" in first_header
    assert int(first_header["live_sequence"]) >= 1
    status, body = fetch_latest(live_port, int(first_header["live_sequence"]))
    assert status == 204
    assert body == b""
    slow_live_sock.close()
    second_payload = send_media_frame(media_port, frame_id=4, payload_start=12)
    second_header, second_live_payload = read_live_image(live_sock, 4)
    assert second_live_payload == second_payload
    assert second_header["frame_id"] == 4
    live_sock.close()
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
live = summary["live_summary"]
assert assistant["bound"] is True
assert assistant["connected"] is True
assert assistant["telemetry_frames"] == 1
assert assistant["ack_frames"] == 1
assert assistant["unknown_frames"] == 0
assert media["bound"] is True
assert media["connected"] is True
assert media["frame_count"] == 2
assert media["record_mode"] == "all"
assert media["payload_bytes"] == 24
assert media["raw_frames_recorded"] == 2
assert media["metadata_frames_recorded"] == 2
assert media["config_count"] == 2
assert media["image_envelope_count"] == 2
assert media["live_publish_errors"] == 0
assert live["messages_published"] >= 4
assert live["image_messages"] >= 2
assert live["clients_connected"] >= 2
assert (output_dir / "assistant_control.csv").is_file()
assert (output_dir / "assistant_control.jsonl").is_file()
assert (output_dir / "steering-media" / "config_snapshot.json").is_file()
assert (output_dir / "steering-media" / "frame_metadata.jsonl").is_file()
assert (output_dir / "steering-media" / "frames" / "frame-000003.raw").read_bytes() == bytes(range(12))
assert (output_dir / "steering-media" / "frames" / "frame-000004.raw").read_bytes() == bytes(range(12, 24))

print("host_capture_selftest passed")
shutil.rmtree(output_dir)
PY
