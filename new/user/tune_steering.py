#!/usr/bin/env python3
"""Passive host-side steering debug workflow for normal runtime runs."""

from __future__ import annotations

import argparse
from bisect import bisect_left
import csv
import json
import os
import re
import select
import shlex
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, Optional

from steering_media_capture import SteeringMediaListener


def log(message: str) -> None:
    print(message, flush=True)


def now_monotonic_ms() -> int:
    return int(time.monotonic() * 1000.0)


def utc_timestamp() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def nested(frame: Dict[str, Any], *keys: str, default: Any = "") -> Any:
    value: Any = frame
    for key in keys:
        if not isinstance(value, dict):
            return default
        value = value.get(key, default)
    return value


def parse_scalar(value: str) -> Any:
    if value == "true":
        return True
    if value == "false":
        return False
    try:
        if any(char in value for char in (".", "e", "E")):
            return float(value)
        return int(value)
    except ValueError:
        return value


class StreamingCsvRecorder:
    FIELDNAMES = [
        "timestamp_utc",
        "host_monotonic_ms",
        "elapsed_ms",
        "frame_type",
        "seq",
        "cmd",
        "outcome",
        "event",
        "reason",
        "motion_phase",
        "tuning_mode_enabled",
        "turn_suppressed",
        "target_speed_override_enabled",
        "target_speed_override_value",
        "effective_speed_target",
        "left_speed_target",
        "right_speed_target",
        "left_measured_speed",
        "right_measured_speed",
        "left_pwm_command",
        "right_pwm_command",
        "actuator.raw_turn_output",
        "actuator.applied_turn_output",
        "reference_control.ready",
        "degraded.active",
        "degraded.reason",
        "curvature.lookahead_distance_m",
        "curvature.curvature_command",
        "curvature.computed",
        "yaw_control.yaw_rate_target",
        "reference.mode",
        "reference.source",
    ]

    def __init__(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        self._file = path.open("w", encoding="utf-8", newline="")
        self._writer = csv.DictWriter(self._file, fieldnames=self.FIELDNAMES)
        self._writer.writeheader()

    def write(self, row: Dict[str, Any]) -> None:
        self._writer.writerow(row)

    def flush(self) -> None:
        self._file.flush()

    def close(self) -> None:
        self._file.flush()
        self._file.close()


class PassiveAssistantListener:
    JSON_FRAME_PREFIX = b'{"type":"'
    MAX_JSON_FRAME_BYTES = 4096

    def __init__(
        self,
        listen_host: str,
        listen_port: int,
        csv_path: Path,
        *,
        accept_timeout_s: float = 8.0,
        log_fn: Optional[Callable[[str], None]] = None,
    ) -> None:
        self._listen_host = listen_host
        self._listen_port = listen_port
        self._csv_path = csv_path
        self._accept_timeout_s = max(0.1, accept_timeout_s)
        self._log = log_fn or (lambda message: None)
        self._server: Optional[socket.socket] = None
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._rx_buffer = bytearray()
        self._csv = StreamingCsvRecorder(csv_path)
        self._start_monotonic_ms = 0
        self._summary: Dict[str, Any] = {
            "listen_host": listen_host,
            "listen_port": listen_port,
            "csv_path": str(csv_path),
            "connected": False,
            "connection_address": None,
            "ack_frames": 0,
            "state_frames": 0,
            "telemetry_frames": 0,
            "unknown_frames": 0,
            "receiver_error": None,
            "first_host_receive_monotonic_ms": None,
            "last_host_receive_monotonic_ms": None,
        }
        self._ever_connected = False
        self._accept_timeout_logged = False

    def start(self) -> None:
        self._server = socket.create_server((self._listen_host, self._listen_port), reuse_port=False)
        self._server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server.settimeout(0.25)
        self._thread = threading.Thread(target=self._run, name="passive-assistant-listener", daemon=True)
        self._thread.start()
        self._log(
            f"[control] waiting for assistant connection on {self._listen_host}:{self._listen_port}"
        )

    def close(self) -> Dict[str, Any]:
        self._stop.set()
        if self._server is not None:
            try:
                self._server.close()
            except OSError:
                pass
            self._server = None
        if self._thread is not None:
            self._thread.join(timeout=2.0)
            self._thread = None
        self._csv.close()
        summary_path = self._csv_path.with_name("assistant_summary.json")
        with summary_path.open("w", encoding="utf-8") as file:
            json.dump(self._summary, file, indent=2, ensure_ascii=False)
            file.write("\n")
        self._summary["summary_path"] = str(summary_path)
        return dict(self._summary)

    def _run(self) -> None:
        assert self._server is not None
        connection: Optional[socket.socket] = None
        try:
            deadline = time.monotonic() + self._accept_timeout_s
            while not self._stop.is_set():
                if connection is None:
                    try:
                        connection, address = self._server.accept()
                    except TimeoutError:
                        if (
                            not self._accept_timeout_logged
                            and not self._ever_connected
                            and time.monotonic() >= deadline
                        ):
                            self._accept_timeout_logged = True
                            self._log("[control] no assistant connection arrived before timeout; continuing to listen")
                        continue
                    except OSError as error:
                        if not self._stop.is_set():
                            self._summary["receiver_error"] = f"assistant accept failed: {error}"
                        return
                    self._ever_connected = True
                    self._summary["connected"] = True
                    self._summary["connection_address"] = f"{address[0]}:{address[1]}"
                    self._start_monotonic_ms = now_monotonic_ms()
                    self._rx_buffer.clear()
                    self._log(f"[control] connected by {address[0]}:{address[1]}")
                    try:
                        connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    except OSError:
                        pass
                    continue

                try:
                    readable, _, _ = select.select([connection], [], [], 0.2)
                except OSError as error:
                    if not self._stop.is_set():
                        self._summary["receiver_error"] = f"assistant select failed: {error}"
                    return
                if not readable:
                    continue
                try:
                    chunk = connection.recv(4096)
                except OSError as error:
                    if not self._stop.is_set():
                        self._log(f"[control] receive error: {error}; waiting for reconnect")
                    try:
                        connection.close()
                    except OSError:
                        pass
                    self._rx_buffer.clear()
                    connection = None
                    continue
                if not chunk:
                    self._log("[control] peer disconnected; waiting for reconnect")
                    try:
                        connection.close()
                    except OSError:
                        pass
                    self._rx_buffer.clear()
                    connection = None
                    continue
                self._rx_buffer.extend(chunk)
                for line in self._extract_json_lines(self._rx_buffer):
                    self._handle_line(line)
        finally:
            if connection is not None:
                try:
                    connection.close()
                except OSError:
                    pass

    def _extract_json_lines(self, rx_buffer: bytearray) -> Iterable[str]:
        prefix = self.JSON_FRAME_PREFIX
        keep_tail = max(0, len(prefix) - 1)

        while True:
            prefix_index = rx_buffer.find(prefix)
            if prefix_index < 0:
                if len(rx_buffer) > keep_tail:
                    del rx_buffer[:-keep_tail]
                return

            if prefix_index > 0:
                del rx_buffer[:prefix_index]

            newline_index = rx_buffer.find(b"\n")
            if newline_index < 0:
                if len(rx_buffer) > self.MAX_JSON_FRAME_BYTES:
                    del rx_buffer[0]
                return

            candidate = bytes(rx_buffer[:newline_index])
            del rx_buffer[: newline_index + 1]
            if not candidate:
                continue
            if len(candidate) > self.MAX_JSON_FRAME_BYTES:
                continue
            if any(byte < 0x20 or byte > 0x7E for byte in candidate):
                continue

            yield candidate.decode("ascii")

    def _handle_line(self, line: str) -> None:
        receive_monotonic_ms = now_monotonic_ms()
        self._summary["first_host_receive_monotonic_ms"] = (
            receive_monotonic_ms
            if self._summary["first_host_receive_monotonic_ms"] is None
            else self._summary["first_host_receive_monotonic_ms"]
        )
        self._summary["last_host_receive_monotonic_ms"] = receive_monotonic_ms

        try:
            frame = json.loads(line)
        except json.JSONDecodeError:
            self._summary["unknown_frames"] = int(self._summary["unknown_frames"]) + 1
            return

        elapsed_ms = receive_monotonic_ms - self._start_monotonic_ms
        frame_type = str(frame.get("type", ""))
        row = {
            "timestamp_utc": utc_timestamp(),
            "host_monotonic_ms": receive_monotonic_ms,
            "elapsed_ms": elapsed_ms,
            "frame_type": frame_type,
            "seq": frame.get("seq", ""),
            "cmd": frame.get("cmd", ""),
            "outcome": frame.get("outcome", ""),
            "event": frame.get("event", ""),
            "reason": frame.get("reason", ""),
            "motion_phase": frame.get("motion_phase", ""),
            "tuning_mode_enabled": frame.get("tuning_mode_enabled", ""),
            "turn_suppressed": frame.get("turn_suppressed", ""),
            "target_speed_override_enabled": frame.get("target_speed_override_enabled", ""),
            "target_speed_override_value": frame.get("target_speed_override_value", ""),
            "effective_speed_target": frame.get("effective_speed_target", ""),
            "left_speed_target": frame.get("left_speed_target", ""),
            "right_speed_target": frame.get("right_speed_target", ""),
            "left_measured_speed": frame.get("left_measured_speed", ""),
            "right_measured_speed": frame.get("right_measured_speed", ""),
            "left_pwm_command": frame.get("left_pwm_command", ""),
            "right_pwm_command": frame.get("right_pwm_command", ""),
            "actuator.raw_turn_output": nested(frame, "actuator", "raw_turn_output"),
            "actuator.applied_turn_output": nested(frame, "actuator", "applied_turn_output"),
            "reference_control.ready": nested(frame, "reference_control", "ready"),
            "degraded.active": nested(frame, "degraded", "active"),
            "degraded.reason": nested(frame, "degraded", "reason"),
            "curvature.lookahead_distance_m": nested(frame, "curvature", "lookahead_distance_m"),
            "curvature.curvature_command": nested(frame, "curvature", "curvature_command"),
            "curvature.computed": nested(frame, "curvature", "computed"),
            "yaw_control.yaw_rate_target": nested(frame, "yaw_control", "yaw_rate_target"),
            "reference.mode": nested(frame, "reference", "mode"),
            "reference.source": nested(frame, "reference", "source"),
        }
        self._csv.write(row)
        if frame_type != "telemetry":
            self._csv.flush()

        if frame_type == "ack":
            self._summary["ack_frames"] = int(self._summary["ack_frames"]) + 1
            self._log(
                f"[control] ack seq={frame.get('seq')} outcome={frame.get('outcome')} reason={frame.get('reason') or '-'}"
            )
            return

        if frame_type == "state":
            self._summary["state_frames"] = int(self._summary["state_frames"]) + 1
            self._log(
                "[control] "
                f"state event={frame.get('event')} reason={frame.get('reason') or '-'} "
                f"phase={frame.get('motion_phase')}"
            )
            return

        if frame_type == "telemetry":
            self._summary["telemetry_frames"] = int(self._summary["telemetry_frames"]) + 1
            telemetry_frames = int(self._summary["telemetry_frames"])
            if telemetry_frames % 25 == 0:
                self._csv.flush()
                self._log(
                    "[control] "
                    f"telemetry phase={frame.get('motion_phase')} "
                    f"ref={nested(frame, 'reference', 'mode')} source={nested(frame, 'reference', 'source')} "
                    f"lookahead_m={nested(frame, 'curvature', 'lookahead_distance_m')} "
                    f"curvature_command={nested(frame, 'curvature', 'curvature_command')} "
                    f"yaw_rate_target={nested(frame, 'yaw_control', 'yaw_rate_target')} "
                    f"left={frame.get('left_measured_speed')}/{frame.get('left_speed_target')} "
                    f"right={frame.get('right_measured_speed')}/{frame.get('right_speed_target')} "
                    f"raw_turn={nested(frame, 'actuator', 'raw_turn_output')} "
                    f"applied_turn={nested(frame, 'actuator', 'applied_turn_output')}"
                )
            return

        self._summary["unknown_frames"] = int(self._summary["unknown_frames"]) + 1


class BoardSteeringLogCapture:
    SNAPSHOT_PATTERN = re.compile(r"^\[(?P<level>[A-Z]+)\]\[(?P<code>[^\]]+)\]\[(?P<ts>\d+)\] (?P<message>.*)$")

    def __init__(
        self,
        board_ip: str,
        board_user: str,
        remote_log: str,
        output_dir: Path,
        *,
        connect_timeout_s: int = 5,
        server_alive_interval_s: int = 5,
        server_alive_count_max: int = 2,
        tail_lines: int = 0,
        log_fn: Optional[Callable[[str], None]] = None,
    ) -> None:
        self._board_ip = board_ip
        self._board_user = board_user
        self._remote_log = remote_log
        self._output_dir = output_dir
        self._connect_timeout_s = connect_timeout_s
        self._server_alive_interval_s = server_alive_interval_s
        self._server_alive_count_max = server_alive_count_max
        self._tail_lines = max(0, tail_lines)
        self._log = log_fn or (lambda message: None)
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._process: Optional[subprocess.Popen[str]] = None
        self._summary: Dict[str, Any] = {
            "board_ip": board_ip,
            "board_user": board_user,
            "remote_log": remote_log,
            "raw_log_path": str(output_dir / "board_runtime.log"),
            "steering_snapshot_path": str(output_dir / "board_steering_snapshot.jsonl"),
            "snapshot_count": 0,
            "receiver_error": None,
        }

    def start(self) -> None:
        self._output_dir.mkdir(parents=True, exist_ok=True)
        self._thread = threading.Thread(target=self._run, name="board-steering-log-capture", daemon=True)
        self._thread.start()
        self._log(f"[board] tailing {self._board_user}@{self._board_ip}:{self._remote_log}")

    def close(self) -> Dict[str, Any]:
        self._stop.set()
        if self._process is not None and self._process.poll() is None:
            self._process.terminate()
        if self._thread is not None:
            self._thread.join(timeout=3.0)
            self._thread = None
        if self._process is not None and self._process.poll() is None:
            self._process.kill()
            self._process.wait(timeout=2.0)
        summary_path = self._output_dir / "board_summary.json"
        with summary_path.open("w", encoding="utf-8") as file:
            json.dump(self._summary, file, indent=2, ensure_ascii=False)
            file.write("\n")
        self._summary["summary_path"] = str(summary_path)
        return dict(self._summary)

    def _run(self) -> None:
        raw_log_path = Path(str(self._summary["raw_log_path"]))
        steering_path = Path(str(self._summary["steering_snapshot_path"]))
        remote_cmd = f"tail -n {self._tail_lines} -F {shlex.quote(self._remote_log)}"
        ssh_cmd = [
            "ssh",
            "-o",
            f"ConnectTimeout={self._connect_timeout_s}",
            "-o",
            f"ServerAliveInterval={self._server_alive_interval_s}",
            "-o",
            f"ServerAliveCountMax={self._server_alive_count_max}",
            "-o",
            "StrictHostKeyChecking=accept-new",
            f"{self._board_user}@{self._board_ip}",
            remote_cmd,
        ]
        try:
            with raw_log_path.open("w", encoding="utf-8") as raw_file, steering_path.open(
                "w", encoding="utf-8"
            ) as steering_file:
                self._process = subprocess.Popen(
                    ssh_cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
                )
                assert self._process.stdout is not None
                while not self._stop.is_set():
                    line = self._process.stdout.readline()
                    if not line:
                        if self._process.poll() is not None:
                            if not self._stop.is_set() and self._process.returncode not in (0, -15):
                                self._summary["receiver_error"] = (
                                    f"remote log tail exited rc={self._process.returncode}"
                                )
                            return
                        time.sleep(0.05)
                        continue
                    raw_file.write(line)
                    raw_file.flush()
                    snapshot = self._parse_snapshot_line(line.rstrip("\n"))
                    if snapshot is None:
                        continue
                    steering_file.write(json.dumps(snapshot, ensure_ascii=False) + "\n")
                    steering_file.flush()
                    self._summary["snapshot_count"] = int(self._summary["snapshot_count"]) + 1
                    if int(self._summary["snapshot_count"]) % 25 == 0:
                        self._log(
	                            "[board] "
	                            f"snapshot frame_id={snapshot.get('frame_id')} "
	                            f"ref={nested(snapshot, 'reference', 'mode')} "
                            f"source={nested(snapshot, 'reference', 'source')} "
                            f"lookahead_m={nested(snapshot, 'curvature', 'lookahead_distance_m')} "
                            f"curvature_command={nested(snapshot, 'curvature', 'curvature_command')} "
                            f"yaw_rate_target={nested(snapshot, 'yaw_control', 'yaw_rate_target')} "
                            f"raw_turn={nested(snapshot, 'actuator', 'raw_turn_output')} "
                            f"applied_turn={nested(snapshot, 'actuator', 'applied_turn_output')}"
                        )
        except OSError as error:
            if not self._stop.is_set():
                self._summary["receiver_error"] = f"remote log tail failed: {error}"
        finally:
            if self._process is not None and self._process.poll() is None:
                self._process.terminate()
                try:
                    self._process.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    self._process.kill()
                    self._process.wait(timeout=2.0)

    def _parse_snapshot_line(self, line: str) -> Optional[Dict[str, Any]]:
        match = self.SNAPSHOT_PATTERN.match(line)
        if match is None or match.group("code") != "control.steering_snapshot":
            return None
        record: Dict[str, Any] = {
            "diag_level": match.group("level"),
            "diag_code": match.group("code"),
            "board_timestamp_ms": int(match.group("ts")),
            "raw_line": line,
        }
        for token in match.group("message").split():
            if "=" not in token:
                continue
            key, value = token.split("=", 1)
            parsed_value = parse_scalar(value)
            if "." in key:
                prefix, child_key = key.split(".", 1)
                nested = record.get(prefix)
                if not isinstance(nested, dict):
                    nested = {}
                    record[prefix] = nested
                nested[child_key] = parsed_value
                continue
            record[key] = parsed_value
        return record


def load_jsonl(path: Path) -> list[Dict[str, Any]]:
    rows: list[Dict[str, Any]] = []
    if not path.is_file():
        return rows
    with path.open("r", encoding="utf-8") as file:
        for line in file:
            if not line.strip():
                continue
            rows.append(json.loads(line))
    return rows


def build_steering_media_alignment(output_dir: Path, board_summary: Dict[str, Any], media_summary: Dict[str, Any]) -> Dict[str, Any]:
    steering_rows = load_jsonl(Path(str(board_summary["steering_snapshot_path"])))
    media_rows = load_jsonl(Path(str(media_summary.get("metadata_path", ""))))
    max_fallback_capture_delta_ms = 200
    alignment_summary: Dict[str, Any] = {
        "timestamp_utc": utc_timestamp(),
        "board_snapshot_count": len(steering_rows),
        "media_frame_count": len(media_rows),
        "alignment_basis": "frame_id exact match, fallback nearest capture_time_ms within 200ms",
        "fallback_capture_time_window_ms": max_fallback_capture_delta_ms,
    }
    if not steering_rows or not media_rows:
        alignment_summary["alignment_available"] = False
        alignment_summary["alignment_count"] = 0
        return alignment_summary

    steering_by_frame: Dict[int, Dict[str, Any]] = {}
    steering_by_capture: Dict[int, Dict[str, Any]] = {}
    for row in steering_rows:
        frame_id = row.get("frame_id")
        capture_time_ms = row.get("capture_time_ms")
        if isinstance(frame_id, int):
            steering_by_frame[frame_id] = row
        if isinstance(capture_time_ms, int):
            steering_by_capture[capture_time_ms] = row
    steering_capture_times = sorted(steering_by_capture.keys())

    alignment_path = output_dir / "steering_media_alignment.jsonl"
    alignment_count = 0
    exact_match_count = 0
    fallback_match_count = 0
    fallback_rejected_count = 0
    capture_delta_ms_values: list[int] = []
    with alignment_path.open("w", encoding="utf-8") as file:
        for media_row in media_rows:
            matched = None
            match_mode = ""
            frame_id = media_row.get("frame_id")
            capture_time_ms = media_row.get("capture_time_ms")
            if isinstance(frame_id, int):
                matched = steering_by_frame.get(frame_id)
                if matched is not None:
                    match_mode = "frame_id"
            if matched is None and isinstance(capture_time_ms, int) and steering_capture_times:
                insert_at = bisect_left(steering_capture_times, capture_time_ms)
                candidate_times: list[int] = []
                if insert_at < len(steering_capture_times):
                    candidate_times.append(steering_capture_times[insert_at])
                if insert_at > 0:
                    candidate_times.append(steering_capture_times[insert_at - 1])
                nearest_capture_time = min(candidate_times, key=lambda value: abs(value - capture_time_ms))
                if abs(nearest_capture_time - capture_time_ms) <= max_fallback_capture_delta_ms:
                    matched = steering_by_capture.get(nearest_capture_time)
                    match_mode = "nearest_capture_time_ms"
                else:
                    fallback_rejected_count += 1
            if matched is None:
                continue
            alignment_count += 1
            if match_mode == "frame_id":
                exact_match_count += 1
            elif match_mode == "nearest_capture_time_ms":
                fallback_match_count += 1
            capture_delta_ms = None
            if isinstance(capture_time_ms, int) and isinstance(matched.get("capture_time_ms"), int):
                capture_delta_ms = capture_time_ms - int(matched["capture_time_ms"])
                capture_delta_ms_values.append(abs(capture_delta_ms))
            record = {
                "frame_id": frame_id,
                "capture_time_ms": capture_time_ms,
                "frame_path": media_row.get("frame_path"),
                "media_host_received_monotonic_ms": media_row.get("host_received_monotonic_ms"),
                "media_motion_phase": media_row.get("motion_phase"),
                "board_timestamp_ms": matched.get("board_timestamp_ms"),
                "board_motion_phase": matched.get("phase"),
                "board_capture_time_ms": matched.get("capture_time_ms"),
                "match_mode": match_mode,
                "capture_time_delta_ms": capture_delta_ms,
                "reference.mode": nested(matched, "reference", "mode"),
                "reference.source": nested(matched, "reference", "source"),
                "reference_control.ready": nested(matched, "reference_control", "ready"),
                "degraded.active": nested(matched, "degraded", "active"),
                "degraded.reason": nested(matched, "degraded", "reason"),
                "curvature.lookahead_distance_m": nested(matched, "curvature", "lookahead_distance_m"),
                "curvature.curvature_command": nested(matched, "curvature", "curvature_command"),
                "yaw_control.yaw_rate_target": nested(matched, "yaw_control", "yaw_rate_target"),
                "actuator.raw_turn_output": nested(matched, "actuator", "raw_turn_output"),
                "actuator.applied_turn_output": nested(matched, "actuator", "applied_turn_output"),
            }
            file.write(json.dumps(record, ensure_ascii=False) + "\n")

    alignment_summary["alignment_available"] = True
    alignment_summary["alignment_count"] = alignment_count
    alignment_summary["exact_match_count"] = exact_match_count
    alignment_summary["fallback_match_count"] = fallback_match_count
    alignment_summary["fallback_rejected_count"] = fallback_rejected_count
    alignment_summary["alignment_path"] = str(alignment_path)
    if capture_delta_ms_values:
        alignment_summary["max_capture_time_delta_ms"] = max(capture_delta_ms_values)
        alignment_summary["mean_capture_time_delta_ms"] = (
            sum(capture_delta_ms_values) / len(capture_delta_ms_values)
        )
    return alignment_summary


def default_output_dir() -> Path:
    timestamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    return Path(__file__).resolve().parent.parent / "verification" / f"steering-debug-{timestamp}"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Passive steering debug capture for normal runtime runs")
    parser.add_argument("--listen-host", default="0.0.0.0", help="host/interface to bind for assistant control")
    parser.add_argument("--listen-port", type=int, default=8888, help="assistant control TCP port to bind")
    parser.add_argument(
        "--assistant-accept-timeout-s",
        type=float,
        default=8.0,
        help="time budget for the assistant control connection to arrive",
    )
    parser.add_argument(
        "--media-listen-host",
        default=None,
        help="optional host/interface for steering media listener; defaults to --listen-host",
    )
    parser.add_argument(
        "--media-listen-port",
        type=int,
        default=None,
        help="optional TCP port for the steering media sidecar",
    )
    parser.add_argument(
        "--media-accept-timeout-s",
        type=float,
        default=8.0,
        help="time budget for the steering media connection to arrive",
    )
    parser.add_argument(
        "--output-dir",
        default=str(default_output_dir()),
        help="directory for assistant/media/board evidence bundle",
    )
    parser.add_argument("--duration-s", type=float, default=20.0, help="capture duration in seconds")
    parser.add_argument("--board-ip", required=True, help="board IP for remote log tail")
    parser.add_argument("--board-user", default="root", help="board SSH user")
    parser.add_argument("--remote-log", default="/home/root/new_runtime.log", help="remote runtime log path")
    parser.add_argument("--tail-lines", type=int, default=0, help="initial tail depth before follow")
    parser.add_argument("--ssh-connect-timeout", type=int, default=5, help="ssh connect timeout seconds")
    parser.add_argument("--ssh-server-alive-interval", type=int, default=5, help="ssh server alive interval seconds")
    parser.add_argument("--ssh-server-alive-count-max", type=int, default=2, help="ssh server alive count max")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    log(f"[capture] output_dir={output_dir}")

    assistant_listener = PassiveAssistantListener(
        args.listen_host,
        args.listen_port,
        output_dir / "assistant_control.csv",
        accept_timeout_s=args.assistant_accept_timeout_s,
        log_fn=log,
    )
    assistant_listener.start()

    media_listener: Optional[SteeringMediaListener] = None
    if args.media_listen_port is not None:
        media_listener = SteeringMediaListener(
            args.media_listen_host or args.listen_host,
            args.media_listen_port,
            output_dir / "steering-media",
            accept_timeout_s=args.media_accept_timeout_s,
            log_fn=log,
        )
        media_listener.start()

    board_capture = BoardSteeringLogCapture(
        args.board_ip,
        args.board_user,
        args.remote_log,
        output_dir,
        connect_timeout_s=args.ssh_connect_timeout,
        server_alive_interval_s=args.ssh_server_alive_interval,
        server_alive_count_max=args.ssh_server_alive_count_max,
        tail_lines=args.tail_lines,
        log_fn=log,
    )
    board_capture.start()

    interrupted = False
    deadline = time.monotonic() + max(0.1, args.duration_s)
    try:
        while time.monotonic() < deadline:
            time.sleep(0.1)
    except KeyboardInterrupt:
        interrupted = True
        log("[capture] interrupted, closing listeners")

    assistant_summary = assistant_listener.close()
    media_summary = media_listener.close() if media_listener is not None else None
    board_summary = board_capture.close()

    summary: Dict[str, Any] = {
        "timestamp_utc": utc_timestamp(),
        "output_dir": str(output_dir),
        "duration_s": args.duration_s,
        "interrupted": interrupted,
        "assistant_summary": assistant_summary,
        "board_summary": board_summary,
    }
    if media_summary is not None:
        summary["media_summary"] = media_summary
        summary["steering_media_alignment"] = build_steering_media_alignment(output_dir, board_summary, media_summary)

    summary_path = output_dir / "summary.json"
    with summary_path.open("w", encoding="utf-8") as file:
        json.dump(summary, file, indent=2, ensure_ascii=False)
        file.write("\n")

    log(f"[summary] summary={summary_path}")
    log(f"[summary] board_steering_snapshots={board_summary['snapshot_count']}")
    log(f"[summary] assistant_connected={assistant_summary['connected']}")
    log(f"[summary] assistant_telemetry_frames={assistant_summary['telemetry_frames']}")
    if media_summary is not None:
        log(f"[summary] steering_media_connected={media_summary['connected']}")
        log(f"[summary] steering_media_frames={media_summary['frame_count']}")

    if assistant_summary["receiver_error"] is not None:
        log(f"[summary] assistant_error={assistant_summary['receiver_error']}")
        return 1
    if board_summary["receiver_error"] is not None:
        log(f"[summary] board_error={board_summary['receiver_error']}")
        return 1
    if media_summary is not None and media_summary["receiver_error"] is not None:
        log(f"[summary] steering_media_error={media_summary['receiver_error']}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
