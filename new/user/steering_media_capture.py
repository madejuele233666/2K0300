#!/usr/bin/env python3
"""Host-side steering media listener and recorder."""

from __future__ import annotations

import json
import socket
import threading
import time
from pathlib import Path
from typing import Any, Callable, Dict, Optional

SOCKET_BUFFER_BYTES = 256 * 1024


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


class SteeringMediaListener:
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
        self._accept_timeout_s = max(0.1, accept_timeout_s)
        self._log = log_fn or (lambda message: None)
        self._server: Optional[socket.socket] = None
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._summary: Dict[str, Any] = {
            "listen_host": listen_host,
            "listen_port": listen_port,
            "output_dir": str(output_dir),
            "connected": False,
            "connection_address": None,
            "config_snapshot_path": None,
            "metadata_path": str(output_dir / "frame_metadata.jsonl"),
            "frame_dir": str(output_dir / "frames"),
            "frame_count": 0,
            "payload_bytes": 0,
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
        (self._output_dir / "frames").mkdir(parents=True, exist_ok=True)
        self._server = socket.create_server((self._listen_host, self._listen_port), reuse_port=False)
        self._server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, SOCKET_BUFFER_BYTES)
        self._server.settimeout(0.25)
        self._thread = threading.Thread(target=self._run, name="steering-media-listener", daemon=True)
        self._thread.start()
        self._log(
            f"[media] waiting for steering media connection on {self._listen_host}:{self._listen_port}"
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
                    connection = None
                    continue
                rx_buffer.extend(chunk)
                self._consume_buffer(rx_buffer)
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
            total_len = 8 + header_len + payload_len
            if len(rx_buffer) < total_len:
                return

            header_bytes = bytes(rx_buffer[8 : 8 + header_len])
            payload = bytes(rx_buffer[8 + header_len : total_len])
            del rx_buffer[:total_len]

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
        if frame_type == "config_snapshot":
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
            return

        if frame_type != "image_frame":
            self._summary["receiver_error"] = f"unsupported steering media frame type: {frame_type!r}"
            return

        frame_width = int(header.get("width", 0))
        frame_height = int(header.get("height", 0))
        expected_payload_bytes = max(0, frame_width) * max(0, frame_height)
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
        frame_path = self._output_dir / "frames" / f"frame-{frame_id:06d}.raw"
        frame_path.write_bytes(payload)
        self._update_frame_stats(header, payload, receive_monotonic_ms)
        metadata = {
            "host_received_utc": utc_timestamp(),
            "host_received_monotonic_ms": receive_monotonic_ms,
            "frame_path": str(frame_path),
            **header,
        }
        with self._metadata_lock:
            metadata_path = Path(str(self._summary["metadata_path"]))
            with metadata_path.open("a", encoding="utf-8") as file:
                file.write(json.dumps(metadata, ensure_ascii=False) + "\n")
        self._summary["frame_count"] = int(self._summary["frame_count"]) + 1
        if self._should_log_progress(receive_monotonic_ms):
            steering = header.get("steering_snapshot", {})
            self._log(
                "[media] "
                f"frames={self._summary['frame_count']} frame_id={frame_id} "
                f"fps={self._summary.get('effective_fps', 0.0):.2f} "
                f"phase={header.get('motion_phase')} "
                f"ref={nested(steering, 'reference', 'mode')} "
                f"source={nested(steering, 'reference', 'source')} "
                f"usable={nested(steering, 'eligibility', 'usable')} "
                f"gate={nested(steering, 'safety_gate', 'reason')} "
                f"lateral_error={nested(steering, 'lateral_error', 'weighted_lateral_error_m')} "
                f"turn_output_target={nested(steering, 'yaw_control', 'turn_output_target')} "
                f"raw_turn={nested(steering, 'actuator', 'raw_turn_output')} "
                f"applied_turn={nested(steering, 'actuator', 'applied_turn_output')}"
            )

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
