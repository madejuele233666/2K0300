#!/usr/bin/env python3
"""Canonical host-side TCP capture for assistant control and steering media.

The board runtime follows the official SEEKFREE example: it is a TCP client
that connects to a host TCP server and then publishes assistant JSON lines and
optional camera/media envelopes. This script owns that host-server role without
requiring a WSL relay.
"""

from __future__ import annotations

import argparse
import csv
import json
import select
import socket
import sys
import threading
import time
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, Optional

from steering_media_live_server import SteeringMediaLiveServer

SOCKET_BUFFER_BYTES = 4 * 1024 * 1024
MAX_JSON_FRAME_BYTES = 4096
MAX_MEDIA_HEADER_BYTES = 512 * 1024
MAX_MEDIA_PAYLOAD_BYTES = 2 * 1024 * 1024
MEDIA_RECORD_MODES = ("all", "metadata", "none")


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


def create_listen_socket(host: str, port: int) -> socket.socket:
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    if sys.platform == "win32" and hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
        server.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
    elif sys.platform != "win32":
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCKET_BUFFER_BYTES)
    except OSError:
        pass
    server.bind((host, port))
    server.listen(1)
    server.settimeout(0.25)
    return server


class AssistantJsonListener:
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
        "reference.mode",
        "reference.source",
        "eligibility.usable",
        "eligibility.reason",
        "reference_control.ready",
        "reference_control.reason",
        "safety_gate.veto_active",
        "safety_gate.reason",
        "lateral_error.weighted_lateral_error_m",
        "yaw_control.turn_output_target",
        "actuator.raw_turn_output",
        "actuator.applied_turn_output",
        "raw_json",
    ]

    def __init__(
        self,
        listen_host: str,
        listen_port: int,
        output_dir: Path,
        *,
        accept_timeout_s: float = 8.0,
        log_fn: Optional[Callable[[str], None]] = None,
    ) -> None:
        self._listen_host = listen_host
        self._listen_port = listen_port
        self._output_dir = output_dir
        self._csv_path = output_dir / "assistant_control.csv"
        self._jsonl_path = output_dir / "assistant_control.jsonl"
        self._accept_timeout_s = max(0.1, accept_timeout_s)
        self._log = log_fn or (lambda message: None)
        self._server: Optional[socket.socket] = None
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._rx_buffer = bytearray()
        self._csv_file: Optional[Any] = None
        self._csv_writer: Optional[csv.DictWriter[Any]] = None
        self._jsonl_file: Optional[Any] = None
        self._start_monotonic_ms = 0
        self._summary: Dict[str, Any] = {
            "listen_host": listen_host,
            "listen_port": listen_port,
            "bound": False,
            "csv_path": str(self._csv_path),
            "jsonl_path": str(self._jsonl_path),
            "connected": False,
            "connection_address": None,
            "ack_frames": 0,
            "state_frames": 0,
            "telemetry_frames": 0,
            "unknown_frames": 0,
            "json_frames": 0,
            "receiver_error": None,
            "first_host_receive_monotonic_ms": None,
            "last_host_receive_monotonic_ms": None,
        }
        self._ever_connected = False
        self._accept_timeout_logged = False

    def start(self) -> None:
        self._output_dir.mkdir(parents=True, exist_ok=True)
        self._csv_file = self._csv_path.open("w", encoding="utf-8", newline="")
        self._csv_writer = csv.DictWriter(self._csv_file, fieldnames=self.FIELDNAMES)
        self._csv_writer.writeheader()
        self._jsonl_file = self._jsonl_path.open("w", encoding="utf-8")
        self._log(f"[control] binding assistant listener on {self._listen_host}:{self._listen_port}")
        self._server = create_listen_socket(self._listen_host, self._listen_port)
        self._summary["bound"] = True
        self._thread = threading.Thread(target=self._run, name="assistant-json-listener", daemon=True)
        self._thread.start()
        self._log(f"[control] waiting for assistant connection on {self._listen_host}:{self._listen_port}")

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
        if self._csv_file is not None:
            self._csv_file.flush()
            self._csv_file.close()
            self._csv_file = None
        if self._jsonl_file is not None:
            self._jsonl_file.flush()
            self._jsonl_file.close()
            self._jsonl_file = None
        summary_path = self._output_dir / "assistant_summary.json"
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
                    connection.settimeout(0.2)
                    continue

                assert self._server is not None
                readable, _, _ = select.select([self._server], [], [], 0)
                if readable:
                    try:
                        next_connection, address = self._server.accept()
                    except (TimeoutError, OSError):
                        next_connection = None
                    if next_connection is not None:
                        self._log(
                            "[control] replacing stale assistant connection with "
                            f"{address[0]}:{address[1]}"
                        )
                        try:
                            connection.close()
                        except OSError:
                            pass
                        connection = next_connection
                        self._summary["connection_address"] = f"{address[0]}:{address[1]}"
                        self._start_monotonic_ms = now_monotonic_ms()
                        self._rx_buffer.clear()
                        try:
                            connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                        except OSError:
                            pass
                        connection.settimeout(0.2)
                        continue

                try:
                    chunk = connection.recv(4096)
                except TimeoutError:
                    continue
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
        while True:
            newline_index = rx_buffer.find(b"\n")
            if newline_index < 0:
                if len(rx_buffer) > MAX_JSON_FRAME_BYTES:
                    del rx_buffer[: len(rx_buffer) - MAX_JSON_FRAME_BYTES]
                return
            candidate = bytes(rx_buffer[:newline_index])
            del rx_buffer[: newline_index + 1]
            if not candidate:
                continue
            if len(candidate) > MAX_JSON_FRAME_BYTES:
                self._summary["unknown_frames"] = int(self._summary["unknown_frames"]) + 1
                continue
            if any(byte < 0x20 or byte > 0x7E for byte in candidate):
                self._summary["unknown_frames"] = int(self._summary["unknown_frames"]) + 1
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

        frame_type = str(frame.get("type", ""))
        self._summary["json_frames"] = int(self._summary["json_frames"]) + 1
        row = {
            "timestamp_utc": utc_timestamp(),
            "host_monotonic_ms": receive_monotonic_ms,
            "elapsed_ms": receive_monotonic_ms - self._start_monotonic_ms,
            "frame_type": frame_type,
            "seq": frame.get("seq", ""),
            "cmd": frame.get("cmd", ""),
            "outcome": frame.get("outcome", ""),
            "event": frame.get("event", ""),
            "reason": frame.get("reason", ""),
            "motion_phase": frame.get("motion_phase", ""),
            "reference.mode": nested(frame, "reference", "mode"),
            "reference.source": nested(frame, "reference", "source"),
            "eligibility.usable": nested(frame, "eligibility", "usable"),
            "eligibility.reason": nested(frame, "eligibility", "reason"),
            "reference_control.ready": nested(frame, "reference_control", "ready"),
            "reference_control.reason": nested(frame, "reference_control", "reason"),
            "safety_gate.veto_active": nested(frame, "safety_gate", "veto_active"),
            "safety_gate.reason": nested(frame, "safety_gate", "reason"),
            "lateral_error.weighted_lateral_error_m": nested(
                frame, "lateral_error", "weighted_lateral_error_m"
            ),
            "yaw_control.turn_output_target": nested(frame, "yaw_control", "turn_output_target"),
            "actuator.raw_turn_output": nested(frame, "actuator", "raw_turn_output"),
            "actuator.applied_turn_output": nested(frame, "actuator", "applied_turn_output"),
            "raw_json": line,
        }
        assert self._csv_writer is not None
        assert self._jsonl_file is not None
        self._csv_writer.writerow(row)
        self._jsonl_file.write(json.dumps({"host_received_utc": utc_timestamp(), **frame}, ensure_ascii=False) + "\n")

        if frame_type == "ack":
            self._summary["ack_frames"] = int(self._summary["ack_frames"]) + 1
            self._flush()
            self._log(f"[control] ack seq={frame.get('seq')} outcome={frame.get('outcome')} reason={frame.get('reason') or '-'}")
            return
        if frame_type == "state":
            self._summary["state_frames"] = int(self._summary["state_frames"]) + 1
            self._flush()
            self._log(f"[control] state event={frame.get('event')} reason={frame.get('reason') or '-'} phase={frame.get('motion_phase')}")
            return
        if frame_type == "telemetry":
            self._summary["telemetry_frames"] = int(self._summary["telemetry_frames"]) + 1
            telemetry_frames = int(self._summary["telemetry_frames"])
            if telemetry_frames % 25 == 0:
                self._flush()
                self._log(
                    "[control] "
                    f"telemetry phase={frame.get('motion_phase')} "
                    f"ref={nested(frame, 'reference', 'mode')} source={nested(frame, 'reference', 'source')} "
                    f"usable={nested(frame, 'eligibility', 'usable')} "
                    f"gate={nested(frame, 'safety_gate', 'reason')} "
                    f"lateral_error={nested(frame, 'lateral_error', 'weighted_lateral_error_m')} "
                    f"turn_output_target={nested(frame, 'yaw_control', 'turn_output_target')} "
                    f"raw_turn={nested(frame, 'actuator', 'raw_turn_output')} "
                    f"applied_turn={nested(frame, 'actuator', 'applied_turn_output')}"
                )
            return
        self._summary["unknown_frames"] = int(self._summary["unknown_frames"]) + 1
        self._flush()

    def _flush(self) -> None:
        if self._csv_file is not None:
            self._csv_file.flush()
        if self._jsonl_file is not None:
            self._jsonl_file.flush()


class SteeringMediaListener:
    def __init__(
        self,
        listen_host: str,
        listen_port: int,
        output_dir: Path,
        *,
        accept_timeout_s: float = 8.0,
        log_fn: Optional[Callable[[str], None]] = None,
        live_server: Optional[SteeringMediaLiveServer] = None,
        record_mode: str = "all",
    ) -> None:
        self._listen_host = listen_host
        self._listen_port = listen_port
        self._output_dir = output_dir
        self._accept_timeout_s = max(0.1, accept_timeout_s)
        self._log = log_fn or (lambda message: None)
        self._live_server = live_server
        if record_mode not in MEDIA_RECORD_MODES:
            raise ValueError(f"unsupported media record mode: {record_mode}")
        self._record_mode = record_mode
        self._server: Optional[socket.socket] = None
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._summary: Dict[str, Any] = {
            "listen_host": listen_host,
            "listen_port": listen_port,
            "bound": False,
            "output_dir": str(output_dir),
            "connected": False,
            "connection_address": None,
            "config_snapshot_path": None,
            "metadata_path": str(output_dir / "frame_metadata.jsonl"),
            "frame_dir": str(output_dir / "frames"),
            "frame_count": 0,
            "record_mode": record_mode,
            "raw_frames_recorded": 0,
            "metadata_frames_recorded": 0,
            "payload_bytes": 0,
            "decoded_payload_bytes": 0,
            "rx_bytes": 0,
            "partial_buffer_bytes": 0,
            "envelope_count": 0,
            "config_count": 0,
            "image_envelope_count": 0,
            "max_header_len": 0,
            "max_payload_len": 0,
            "last_header_type": None,
            "receiver_error": None,
            "first_host_receive_monotonic_ms": None,
            "last_host_receive_monotonic_ms": None,
            "first_frame_host_receive_monotonic_ms": None,
            "last_frame_host_receive_monotonic_ms": None,
            "min_frame_interval_ms": None,
            "max_frame_interval_ms": None,
            "mean_frame_interval_ms": None,
            "effective_fps": 0.0,
            "last_frame_id": None,
            "last_capture_time_ms": None,
            "live_publish_errors": 0,
        }
        self._metadata_lock = threading.Lock()
        self._ever_connected = False
        self._accept_timeout_logged = False
        self._last_frame_receive_monotonic_ms: Optional[int] = None
        self._frame_interval_total_ms = 0
        self._frame_interval_count = 0
        self._last_progress_log_ms = 0

    def start(self) -> None:
        self._output_dir.mkdir(parents=True, exist_ok=True)
        if self._record_mode == "all":
            (self._output_dir / "frames").mkdir(parents=True, exist_ok=True)
        self._log(f"[media] binding steering media listener on {self._listen_host}:{self._listen_port}")
        self._server = create_listen_socket(self._listen_host, self._listen_port)
        self._summary["bound"] = True
        self._thread = threading.Thread(target=self._run, name="steering-media-listener", daemon=True)
        self._thread.start()
        self._log(f"[media] waiting for steering media connection on {self._listen_host}:{self._listen_port}")

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
        self._finalize_summary()
        summary_path = self._output_dir / "summary.json"
        with summary_path.open("w", encoding="utf-8") as file:
            json.dump(self._summary, file, indent=2, ensure_ascii=False)
            file.write("\n")
        self._summary["summary_path"] = str(summary_path)
        return dict(self._summary)

    def _finalize_summary(self) -> None:
        if self._frame_interval_count > 0:
            self._summary["mean_frame_interval_ms"] = (
                self._frame_interval_total_ms / self._frame_interval_count
            )
        first_frame_ms = self._summary.get("first_frame_host_receive_monotonic_ms")
        last_frame_ms = self._summary.get("last_frame_host_receive_monotonic_ms")
        frame_count = int(self._summary.get("frame_count", 0) or 0)
        if isinstance(first_frame_ms, int) and isinstance(last_frame_ms, int) and frame_count > 1:
            duration_s = max(0.001, (last_frame_ms - first_frame_ms) / 1000.0)
            self._summary["effective_fps"] = (frame_count - 1) / duration_s

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
                            self._log("[media] no steering media connection arrived before timeout; continuing to listen")
                        continue
                    except OSError as error:
                        if not self._stop.is_set():
                            self._summary["receiver_error"] = f"steering media accept failed: {error}"
                        return

                    self._ever_connected = True
                    self._summary["connected"] = True
                    self._summary["connection_address"] = f"{address[0]}:{address[1]}"
                    self._log(f"[media] connected by {address[0]}:{address[1]}")
                    try:
                        connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    except OSError:
                        pass
                    try:
                        connection.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCKET_BUFFER_BYTES)
                    except OSError:
                        pass
                    connection.settimeout(0.2)
                    rx_buffer = bytearray()
                    continue

                assert self._server is not None
                readable, _, _ = select.select([self._server], [], [], 0)
                if readable:
                    try:
                        next_connection, address = self._server.accept()
                    except (TimeoutError, OSError):
                        next_connection = None
                    if next_connection is not None:
                        self._log(
                            "[media] replacing stale steering media connection with "
                            f"{address[0]}:{address[1]}"
                        )
                        try:
                            connection.close()
                        except OSError:
                            pass
                        connection = next_connection
                        self._summary["connection_address"] = f"{address[0]}:{address[1]}"
                        rx_buffer = bytearray()
                        self._summary["partial_buffer_bytes"] = 0
                        try:
                            connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                        except OSError:
                            pass
                        try:
                            connection.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCKET_BUFFER_BYTES)
                        except OSError:
                            pass
                        connection.settimeout(0.2)
                        continue

                try:
                    chunk = connection.recv(65536)
                except TimeoutError:
                    continue
                except OSError as error:
                    if not self._stop.is_set():
                        self._log(f"[media] receive error: {error}; waiting for reconnect")
                    try:
                        connection.close()
                    except OSError:
                        pass
                    connection = None
                    continue
                if not chunk:
                    self._log("[media] peer disconnected; waiting for reconnect")
                    try:
                        connection.close()
                    except OSError:
                        pass
                    self._summary["partial_buffer_bytes"] = len(rx_buffer)
                    connection = None
                    continue
                self._summary["rx_bytes"] = int(self._summary["rx_bytes"]) + len(chunk)
                rx_buffer.extend(chunk)
                self._summary["partial_buffer_bytes"] = len(rx_buffer)
                self._consume_buffer(rx_buffer)
                self._summary["partial_buffer_bytes"] = len(rx_buffer)
        finally:
            if connection is not None:
                try:
                    connection.close()
                except OSError:
                    pass

    def _consume_buffer(self, rx_buffer: bytearray) -> None:
        while len(rx_buffer) >= 8:
            header_len = int.from_bytes(rx_buffer[0:4], byteorder="big", signed=False)
            payload_len = int.from_bytes(rx_buffer[4:8], byteorder="big", signed=False)
            self._summary["max_header_len"] = max(int(self._summary["max_header_len"]), header_len)
            self._summary["max_payload_len"] = max(int(self._summary["max_payload_len"]), payload_len)
            if header_len > MAX_MEDIA_HEADER_BYTES or payload_len > MAX_MEDIA_PAYLOAD_BYTES:
                self._summary["receiver_error"] = (
                    f"invalid steering media envelope length: header={header_len} payload={payload_len}"
                )
                rx_buffer.clear()
                self._summary["partial_buffer_bytes"] = 0
                return
            total_len = 8 + header_len + payload_len
            if len(rx_buffer) < total_len:
                return

            header_bytes = bytes(rx_buffer[8 : 8 + header_len])
            payload = bytes(rx_buffer[8 + header_len : total_len])
            del rx_buffer[:total_len]
            self._summary["partial_buffer_bytes"] = len(rx_buffer)
            self._summary["envelope_count"] = int(self._summary["envelope_count"]) + 1

            receive_monotonic_ms = now_monotonic_ms()
            self._summary["first_host_receive_monotonic_ms"] = (
                receive_monotonic_ms
                if self._summary["first_host_receive_monotonic_ms"] is None
                else self._summary["first_host_receive_monotonic_ms"]
            )
            self._summary["last_host_receive_monotonic_ms"] = receive_monotonic_ms

            try:
                header = json.loads(header_bytes.decode("utf-8"))
            except Exception as error:
                self._summary["receiver_error"] = f"steering media header decode failed: {error}"
                return
            self._handle_frame(header, payload, receive_monotonic_ms)

    def _handle_frame(self, header: Dict[str, Any], payload: bytes, receive_monotonic_ms: int) -> None:
        frame_type = header.get("type")
        self._summary["last_header_type"] = frame_type
        if frame_type == "config_snapshot":
            self._summary["config_count"] = int(self._summary["config_count"]) + 1
            config_path = self._output_dir / "config_snapshot.json"
            with config_path.open("w", encoding="utf-8") as file:
                json.dump(header, file, indent=2, ensure_ascii=False)
                file.write("\n")
            self._summary["config_snapshot_path"] = str(config_path)
            self._log(
                "[media] config_snapshot "
                f"interval_ms={header.get('media_publish_interval_ms')} "
                f"yaw_rate_pid_p={header.get('param_snapshot', {}).get('yaw_rate_pid', {}).get('p')}"
            )
            self._publish_live(header, payload, receive_monotonic_ms)
            return

        if frame_type != "image_frame":
            self._summary["receiver_error"] = f"unsupported steering media frame type: {frame_type!r}"
            return
        self._summary["image_envelope_count"] = int(self._summary["image_envelope_count"]) + 1

        frame_width = int(header.get("width", 0))
        frame_height = int(header.get("height", 0))
        expected_payload_bytes = self._expected_payload_bytes(header, frame_width, frame_height)
        if frame_width <= 0 or frame_height <= 0:
            self._summary["receiver_error"] = (
                f"invalid steering image dimensions: width={frame_width}, height={frame_height}"
            )
            return

        if len(payload) != expected_payload_bytes:
            self._summary["receiver_error"] = (
                "invalid steering image payload size: "
                f"expected {expected_payload_bytes}, got {len(payload)}"
            )
            return

        frame_id = int(header.get("frame_id", 0))
        decoded_payload = self._decode_image_payload(header, payload, frame_width, frame_height)
        self._update_frame_stats(header, payload, receive_monotonic_ms)
        self._summary["decoded_payload_bytes"] = (
            int(self._summary["decoded_payload_bytes"]) + len(decoded_payload)
        )
        self._summary["frame_count"] = int(self._summary["frame_count"]) + 1
        self._publish_live(header, payload, receive_monotonic_ms)
        frame_path = self._record_image_frame(header, decoded_payload, receive_monotonic_ms)
        if frame_path is not None:
            self._summary["raw_frames_recorded"] = int(self._summary["raw_frames_recorded"]) + 1
        if self._record_mode in ("all", "metadata"):
            self._record_image_metadata(header, frame_path, receive_monotonic_ms)
            self._summary["metadata_frames_recorded"] = int(self._summary["metadata_frames_recorded"]) + 1
        if self._should_log_progress(receive_monotonic_ms):
            steering = header.get("steering_snapshot", {})
            self._log(
                "[media] "
                f"frames={self._summary['frame_count']} frame_id={frame_id} "
                f"fps={self._summary.get('effective_fps', 0.0):.2f} "
                f"record={self._record_mode} "
                f"encoding={header.get('payload_encoding', header.get('pixel_format', 'raw'))} "
                f"frame_source={header.get('frame_source')} "
                f"phase={header.get('motion_phase')} "
                f"ref={nested(steering, 'reference', 'mode')} "
                f"source={nested(steering, 'reference', 'source')} "
                f"path_candidates={nested(steering, 'visual_reference', 'path_candidates', 'count')} "
                f"usable={nested(steering, 'eligibility', 'usable')} "
                f"gate={nested(steering, 'safety_gate', 'reason')} "
                f"lateral_error={nested(steering, 'lateral_error', 'weighted_lateral_error_m')} "
                f"turn_output_target={nested(steering, 'yaw_control', 'turn_output_target')} "
                f"raw_turn={nested(steering, 'actuator', 'raw_turn_output')} "
                f"applied_turn={nested(steering, 'actuator', 'applied_turn_output')}"
            )

    def _record_image_frame(
        self, header: Dict[str, Any], payload: bytes, receive_monotonic_ms: int
    ) -> Optional[Path]:
        if self._record_mode != "all":
            return None
        frame_id = int(header.get("frame_id", 0))
        frame_path = self._output_dir / "frames" / f"frame-{frame_id:06d}.raw"
        frame_path.write_bytes(payload)
        return frame_path

    def _expected_payload_bytes(self, header: Dict[str, Any], width: int, height: int) -> int:
        pixels = max(0, width) * max(0, height)
        bits = self._packed_gray_bits(header)
        if bits is not None:
            return (pixels * bits + 7) // 8
        return pixels

    def _packed_gray_bits(self, header: Dict[str, Any]) -> Optional[int]:
        encoding = header.get("payload_encoding")
        pixel_format = header.get("pixel_format")
        if encoding == "gray1_packed" or pixel_format == "gray1":
            return 1
        if encoding == "gray2_packed" or pixel_format == "gray2":
            return 2
        if encoding == "gray4_packed" or pixel_format == "gray4":
            return 4
        return None

    def _decode_image_payload(
        self, header: Dict[str, Any], payload: bytes, width: int, height: int
    ) -> bytes:
        bits = self._packed_gray_bits(header)
        if bits is None:
            return payload
        max_level = (1 << bits) - 1
        decoded = bytearray(width * height)
        for index in range(width * height):
            bit_index = index * bits
            packed = payload[bit_index // 8]
            shift = 8 - bits - (bit_index % 8)
            level = (packed >> shift) & max_level
            decoded[index] = (level * 255) // max(1, max_level)
        return bytes(decoded)

    def _record_image_metadata(
        self, header: Dict[str, Any], frame_path: Optional[Path], receive_monotonic_ms: int
    ) -> None:
        metadata = {
            "host_received_utc": utc_timestamp(),
            "host_received_monotonic_ms": receive_monotonic_ms,
            "frame_path": str(frame_path) if frame_path is not None else None,
            **header,
        }
        with self._metadata_lock:
            metadata_path = Path(str(self._summary["metadata_path"]))
            with metadata_path.open("a", encoding="utf-8") as file:
                file.write(json.dumps(metadata, ensure_ascii=False) + "\n")

    def _publish_live(self, header: Dict[str, Any], payload: bytes, receive_monotonic_ms: int) -> None:
        if self._live_server is None:
            return
        try:
            self._live_server.publish(header, payload, receive_monotonic_ms)
        except Exception as error:
            self._summary["live_publish_errors"] = int(self._summary["live_publish_errors"]) + 1
            self._log(f"[live] publish error: {error}")

    def _update_frame_stats(
        self, header: Dict[str, Any], payload: bytes, receive_monotonic_ms: int
    ) -> None:
        first_frame_ms = self._summary["first_frame_host_receive_monotonic_ms"]
        self._summary["first_frame_host_receive_monotonic_ms"] = (
            receive_monotonic_ms if first_frame_ms is None else first_frame_ms
        )
        self._summary["last_frame_host_receive_monotonic_ms"] = receive_monotonic_ms
        self._summary["payload_bytes"] = int(self._summary["payload_bytes"]) + len(payload)
        self._summary["last_frame_id"] = header.get("frame_id")
        self._summary["last_capture_time_ms"] = header.get("capture_time_ms")

        if self._last_frame_receive_monotonic_ms is not None:
            interval_ms = receive_monotonic_ms - self._last_frame_receive_monotonic_ms
            self._frame_interval_total_ms += interval_ms
            self._frame_interval_count += 1
            min_interval = self._summary["min_frame_interval_ms"]
            max_interval = self._summary["max_frame_interval_ms"]
            self._summary["min_frame_interval_ms"] = (
                interval_ms if min_interval is None else min(int(min_interval), interval_ms)
            )
            self._summary["max_frame_interval_ms"] = (
                interval_ms if max_interval is None else max(int(max_interval), interval_ms)
            )
            self._summary["mean_frame_interval_ms"] = (
                self._frame_interval_total_ms / self._frame_interval_count
            )

        self._last_frame_receive_monotonic_ms = receive_monotonic_ms
        frame_count = int(self._summary.get("frame_count", 0) or 0) + 1
        first = self._summary.get("first_frame_host_receive_monotonic_ms")
        if isinstance(first, int) and frame_count > 1:
            duration_s = max(0.001, (receive_monotonic_ms - first) / 1000.0)
            self._summary["effective_fps"] = (frame_count - 1) / duration_s

    def _should_log_progress(self, receive_monotonic_ms: int) -> bool:
        if self._last_progress_log_ms == 0:
            self._last_progress_log_ms = receive_monotonic_ms
            return True
        if receive_monotonic_ms - self._last_progress_log_ms < 1000:
            return False
        self._last_progress_log_ms = receive_monotonic_ms
        return True


def default_output_dir() -> Path:
    timestamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    return Path(__file__).resolve().parent.parent / "verification" / f"host-capture-{timestamp}"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Canonical host-side assistant/media TCP capture")
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
        help="directory for assistant/media evidence bundle",
    )
    parser.add_argument(
        "--live-web",
        action="store_true",
        help="serve a local read-only browser viewer for steering media frames",
    )
    parser.add_argument(
        "--live-host",
        default="127.0.0.1",
        help="host/interface for the optional live web viewer",
    )
    parser.add_argument(
        "--live-port",
        type=int,
        default=8765,
        help="TCP port for the optional live web viewer",
    )
    parser.add_argument(
        "--live-display-mode",
        choices=("bev", "raw"),
        default="bev",
        help="initial live viewer image mode: bev shows the BEV-warped image, raw shows the camera frame",
    )
    parser.add_argument(
        "--media-record-mode",
        choices=MEDIA_RECORD_MODES,
        default="all",
        help="steering media evidence recording mode: all writes raw frames and metadata, metadata skips raw files, none keeps live/summary only",
    )
    parser.add_argument("--duration-s", type=float, default=20.0, help="capture duration in seconds")
    parser.add_argument("--board-ip", default=None, help=argparse.SUPPRESS)
    parser.add_argument("--board-user", default=None, help=argparse.SUPPRESS)
    parser.add_argument("--remote-log", default=None, help=argparse.SUPPRESS)
    parser.add_argument("--tail-lines", default=None, help=argparse.SUPPRESS)
    parser.add_argument("--ssh-connect-timeout", default=None, help=argparse.SUPPRESS)
    parser.add_argument("--ssh-server-alive-interval", default=None, help=argparse.SUPPRESS)
    parser.add_argument("--ssh-server-alive-count-max", default=None, help=argparse.SUPPRESS)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    log(f"[capture] output_dir={output_dir}")

    assistant_listener = AssistantJsonListener(
        args.listen_host,
        args.listen_port,
        output_dir,
        accept_timeout_s=args.assistant_accept_timeout_s,
        log_fn=log,
    )
    media_listener: Optional[SteeringMediaListener] = None
    live_server: Optional[SteeringMediaLiveServer] = None
    started = False
    interrupted = False
    try:
        if args.live_web:
            live_server = SteeringMediaLiveServer(args.live_host, args.live_port, args.live_display_mode)
            try:
                live_server.start()
            except OSError as error:
                log(
                    "[live] failed to start viewer "
                    f"on {args.live_host}:{args.live_port}: {error}"
                )
                return 1
            log(f"[live] viewer_url={live_server.url}")
        assistant_listener.start()
        if args.media_listen_port is not None:
            media_listener = SteeringMediaListener(
                args.media_listen_host or args.listen_host,
                args.media_listen_port,
                output_dir / "steering-media",
                accept_timeout_s=args.media_accept_timeout_s,
                log_fn=log,
                live_server=live_server,
                record_mode=args.media_record_mode,
            )
            media_listener.start()
        started = True
        ready_media = (
            f"{args.media_listen_host or args.listen_host}:{args.media_listen_port}"
            if args.media_listen_port is not None
            else "disabled"
        )
        log(
            "[host_capture] READY "
            f"control={args.listen_host}:{args.listen_port} media={ready_media} output={output_dir}"
        )

        deadline = time.monotonic() + max(0.1, args.duration_s)
        while time.monotonic() < deadline:
            time.sleep(0.1)
    except KeyboardInterrupt:
        interrupted = True
        log("[capture] interrupted, closing listeners")
    finally:
        assistant_summary = assistant_listener.close() if started else {"receiver_error": "assistant listener failed to start"}
        media_summary = media_listener.close() if media_listener is not None else None
        live_summary = live_server.close() if live_server is not None else None

    summary: Dict[str, Any] = {
        "timestamp_utc": utc_timestamp(),
        "output_dir": str(output_dir),
        "duration_s": args.duration_s,
        "interrupted": interrupted,
        "assistant_summary": assistant_summary,
    }
    if media_summary is not None:
        summary["media_summary"] = media_summary
    if live_summary is not None:
        summary["live_summary"] = live_summary

    summary_path = output_dir / "summary.json"
    with summary_path.open("w", encoding="utf-8") as file:
        json.dump(summary, file, indent=2, ensure_ascii=False)
        file.write("\n")

    log(f"[summary] summary={summary_path}")
    log(f"[summary] assistant_bound={assistant_summary.get('bound')}")
    log(f"[summary] assistant_connected={assistant_summary.get('connected')}")
    log(f"[summary] assistant_telemetry_frames={assistant_summary.get('telemetry_frames')}")
    if media_summary is not None:
        log(f"[summary] steering_media_bound={media_summary.get('bound')}")
        log(f"[summary] steering_media_connected={media_summary.get('connected')}")
        log(f"[summary] steering_media_frames={media_summary.get('frame_count')}")
    if live_summary is not None:
        log(f"[summary] live_url={live_summary.get('url')}")
        log(f"[summary] live_messages={live_summary.get('messages_published')}")

    if assistant_summary.get("receiver_error") is not None:
        log(f"[summary] assistant_error={assistant_summary['receiver_error']}")
        return 1
    if media_summary is not None and media_summary.get("receiver_error") is not None:
        log(f"[summary] steering_media_error={media_summary['receiver_error']}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
