#!/usr/bin/env python3
"""Minimal host-side speed tuning workflow for the bidirectional assistant link."""

from __future__ import annotations

import argparse
import csv
import json
import select
import socket
import subprocess
import sys
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Deque, Dict, Iterable, Optional, Tuple


def log(message: str) -> None:
    print(message, flush=True)


def now_monotonic_ms() -> int:
    return int(time.monotonic() * 1000.0)


def utc_timestamp() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def bootstrap_matplotlib(disabled: bool) -> Optional[Tuple[Any, Any]]:
    if disabled:
        return None

    try:
        import matplotlib.pyplot as plt  # type: ignore

        return plt, None
    except Exception as import_error:
        log(f"[plot] matplotlib import failed: {import_error}")

    log("[plot] attempting `python -m pip install --user matplotlib`")
    install = subprocess.run(
        [sys.executable, "-m", "pip", "install", "--user", "matplotlib"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if install.returncode != 0:
        log("[plot] matplotlib install failed; continuing with CSV-only capture")
        if install.stdout:
            sys.stdout.write(install.stdout)
            sys.stdout.flush()
        return None

    try:
        import matplotlib.pyplot as plt  # type: ignore

        log("[plot] matplotlib installed successfully")
        return plt, install.stdout
    except Exception as import_error:
        log(f"[plot] matplotlib still unavailable after install: {import_error}")
        return None


@dataclass
class AckResult:
    seq: int
    outcome: str
    reason: str


class CsvRecorder:
    FIELDNAMES = [
        "timestamp_utc",
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
        "raw_turn_output",
        "applied_turn_output",
    ]

    def __init__(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        self._path = path
        self._rows: list[Dict[str, Any]] = []

    def write(self, row: Dict[str, Any]) -> None:
        self._rows.append(row)

    def flush(self) -> None:
        return None

    def close(self) -> None:
        with self._path.open("w", encoding="utf-8", newline="") as file:
            writer = csv.DictWriter(file, fieldnames=self.FIELDNAMES)
            writer.writeheader()
            writer.writerows(self._rows)


class LivePlotter:
    def __init__(self, plt_module: Any) -> None:
        self._plt = plt_module
        self._plt.ion()
        self._fig, (self._left_ax, self._right_ax) = self._plt.subplots(2, 1, figsize=(10, 7), sharex=True)
        self._left_target_line, = self._left_ax.plot([], [], label="left target")
        self._left_measured_line, = self._left_ax.plot([], [], label="left measured")
        self._right_target_line, = self._right_ax.plot([], [], label="right target")
        self._right_measured_line, = self._right_ax.plot([], [], label="right measured")
        self._left_ax.set_ylabel("Left Speed")
        self._right_ax.set_ylabel("Right Speed")
        self._right_ax.set_xlabel("Elapsed ms")
        self._left_ax.legend(loc="upper left")
        self._right_ax.legend(loc="upper left")
        self._fig.suptitle("Assistant Speed Tuning")
        self._x: Deque[float] = deque(maxlen=4000)
        self._left_target: Deque[float] = deque(maxlen=4000)
        self._left_measured: Deque[float] = deque(maxlen=4000)
        self._right_target: Deque[float] = deque(maxlen=4000)
        self._right_measured: Deque[float] = deque(maxlen=4000)
        self._last_redraw_ms = 0

    def update(self, elapsed_ms: int, telemetry: Dict[str, Any]) -> None:
        self._x.append(float(elapsed_ms))
        self._left_target.append(float(telemetry.get("left_speed_target", 0.0) or 0.0))
        self._left_measured.append(float(telemetry.get("left_measured_speed", 0.0) or 0.0))
        self._right_target.append(float(telemetry.get("right_speed_target", 0.0) or 0.0))
        self._right_measured.append(float(telemetry.get("right_measured_speed", 0.0) or 0.0))

        redraw_ms = now_monotonic_ms()
        if self._last_redraw_ms != 0 and redraw_ms - self._last_redraw_ms < 200:
            return
        self._last_redraw_ms = redraw_ms

        self._left_target_line.set_data(self._x, self._left_target)
        self._left_measured_line.set_data(self._x, self._left_measured)
        self._right_target_line.set_data(self._x, self._right_target)
        self._right_measured_line.set_data(self._x, self._right_measured)

        for axis in (self._left_ax, self._right_ax):
            axis.relim()
            axis.autoscale_view()
        self._fig.canvas.draw_idle()
        self._fig.canvas.flush_events()

    def close(self) -> None:
        self._plt.ioff()
        self._plt.close(self._fig)


class AssistantSession:
    JSON_FRAME_PREFIX = b'{"type":"'
    MAX_JSON_FRAME_BYTES = 4096

    def __init__(self,
                 connection: socket.socket,
                 csv_path: Path,
                 plotter: Optional[LivePlotter],
                 *,
                 capture_telemetry: bool = True) -> None:
        self._connection = connection
        self._plotter = plotter
        self._capture_telemetry = capture_telemetry
        self._csv = CsvRecorder(csv_path)
        self._start_monotonic_ms = now_monotonic_ms()
        self._next_seq = 1
        self._pending_acks: Dict[int, AckResult] = {}
        self._sent_commands: Dict[int, str] = {}
        self._state_events: Dict[str, int] = {}
        self._telemetry_frames = 0
        self._receiver_error: Optional[str] = None
        self._rx_buffer = bytearray()

    def start(self) -> None:
        try:
            self._connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        except OSError as error:
            log(f"[warn] failed to enable TCP_NODELAY: {error}")

    def close(self) -> None:
        try:
            self._connection.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self._connection.close()
        self._csv.close()
        if self._plotter is not None:
            self._plotter.close()

    def send_command_async(self, cmd: str, **payload: Any) -> int:
        if self._receiver_error is not None:
            raise RuntimeError(self._receiver_error)
        seq = self._next_seq
        self._next_seq += 1
        self._sent_commands[seq] = cmd

        message = {"type": "command", "cmd": cmd, "seq": seq}
        message.update(payload)
        encoded = json.dumps(message, separators=(",", ":")) + "\n"
        self._connection.sendall(encoded.encode("utf-8"))
        log(f"[send] seq={seq} cmd={cmd} payload={json.dumps(payload, separators=(',', ':'))}")
        return seq

    def wait_for_ack(self, seq: int, timeout_s: float) -> AckResult:
        deadline = time.monotonic() + timeout_s
        if not self._pump_until(deadline, lambda: seq in self._pending_acks):
            if self._receiver_error is not None:
                raise RuntimeError(self._receiver_error)
            raise TimeoutError(f"timed out waiting for ack seq={seq} cmd={self._sent_commands.get(seq, '')}")
        return self._pending_acks.pop(seq)

    def send_command(self, cmd: str, timeout_s: float, **payload: Any) -> AckResult:
        seq = self.send_command_async(cmd, **payload)
        return self.wait_for_ack(seq, timeout_s)

    def wait_for_state_event(self, event: str, timeout_s: float) -> bool:
        deadline = time.monotonic() + timeout_s
        if self._pump_until(deadline, lambda: self._state_events.get(event, 0) > 0):
            return True
        if self._receiver_error is not None:
            raise RuntimeError(self._receiver_error)
        return False

    def pump_for(self, duration_s: float) -> None:
        deadline = time.monotonic() + max(0.0, duration_s)
        self._pump_until(deadline, lambda: False)

    def summary(self) -> Dict[str, Any]:
        return {
            "telemetry_frames": self._telemetry_frames,
            "state_events": dict(self._state_events),
            "receiver_error": self._receiver_error,
        }

    def _pump_until(self, deadline: float, predicate: Any) -> bool:
        while time.monotonic() < deadline:
            if predicate():
                return True
            if self._receiver_error is not None:
                return False

            timeout_s = min(0.005, max(0.0, deadline - time.monotonic()))
            try:
                readable, _, _ = select.select([self._connection], [], [], timeout_s)
            except OSError as error:
                self._receiver_error = f"assistant receive failed: {error}"
                return False
            if not readable:
                continue

            try:
                chunk = self._connection.recv(4096)
            except OSError as error:
                self._receiver_error = f"assistant receive failed: {error}"
                return False
            if not chunk:
                self._receiver_error = "assistant connection closed unexpectedly"
                return False

            self._rx_buffer.extend(chunk)
            for line in self._extract_json_lines(self._rx_buffer):
                self._handle_line(line)

        return predicate()

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
        elapsed_ms = now_monotonic_ms() - self._start_monotonic_ms
        timestamp = utc_timestamp()
        try:
            frame = json.loads(line)
        except json.JSONDecodeError:
            log(f"[recv] invalid json line: {line}")
            return

        frame_type = frame.get("type")
        row = {
            "timestamp_utc": timestamp,
            "elapsed_ms": elapsed_ms,
            "frame_type": frame_type,
            "seq": frame.get("seq", ""),
            "cmd": self._sent_commands.get(int(frame.get("seq", 0)), "") if isinstance(frame.get("seq"), int) else "",
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
            "raw_turn_output": frame.get("raw_turn_output", ""),
            "applied_turn_output": frame.get("applied_turn_output", ""),
        }
        if frame_type != "telemetry" or self._capture_telemetry:
            self._csv.write(row)
        if frame_type != "telemetry":
            self._csv.flush()

        if frame_type == "ack":
            seq = int(frame["seq"])
            outcome = str(frame.get("outcome", ""))
            reason = str(frame.get("reason", ""))
            log(f"[ack] seq={seq} outcome={outcome} reason={reason or '-'}")
            self._pending_acks[seq] = AckResult(seq=seq, outcome=outcome, reason=reason)
            return

        if frame_type == "state":
            event = str(frame.get("event", ""))
            reason = str(frame.get("reason", ""))
            override_value = frame.get("target_speed_override_value")
            log(
                "[state] "
                f"event={event} reason={reason or '-'} tuning_mode={frame.get('tuning_mode_enabled')} "
                f"turn_suppressed={frame.get('turn_suppressed')} override_enabled={frame.get('target_speed_override_enabled')} "
                f"override_value={override_value!r} effective_speed_target={frame.get('effective_speed_target')}"
            )
            self._state_events[event] = self._state_events.get(event, 0) + 1
            return

        if frame_type == "telemetry":
            if not self._capture_telemetry:
                return
            self._telemetry_frames += 1
            if self._plotter is not None:
                self._plotter.update(elapsed_ms, frame)
            if self._telemetry_frames % 25 == 0:
                self._csv.flush()
                log(
                    "[telemetry] "
                    f"phase={frame.get('motion_phase')} left={frame.get('left_measured_speed')}/{frame.get('left_speed_target')} "
                    f"right={frame.get('right_measured_speed')}/{frame.get('right_speed_target')} "
                    f"override={frame.get('target_speed_override_value')!r} raw_turn={frame.get('raw_turn_output')} "
                    f"applied_turn={frame.get('applied_turn_output')}"
                )
            return

        log(f"[recv] unknown frame type: {frame_type!r}")


def parse_speed_sequence(sequence_text: str) -> Iterable[float]:
    if not sequence_text.strip():
        raise ValueError("speed sequence must not be empty")
    return [float(token.strip()) for token in sequence_text.split(",") if token.strip()]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Minimal assistant host tuning workflow")
    parser.add_argument("--listen-host", default="0.0.0.0", help="host/interface to bind")
    parser.add_argument("--listen-port", type=int, default=8888, help="TCP port to bind")
    parser.add_argument(
        "--csv",
        default=str(Path(__file__).resolve().parent.parent / "verification" / "phase-d-speed-tuning.csv"),
        help="CSV path for captured ACK/state/telemetry",
    )
    parser.add_argument(
        "--sequence",
        default="20,40,60,77",
        help="comma-separated target speed sequence in runtime internal units",
    )
    parser.add_argument("--runs", type=int, default=1, help="number of repeated speed-sequence runs")
    parser.add_argument("--ttl-ms", type=int, default=2500, help="override TTL per target-speed command")
    parser.add_argument("--step-dwell-ms", type=int, default=1200, help="sleep after each accepted target-speed step")
    parser.add_argument("--ack-timeout-s", type=float, default=5.0, help="ack/state wait timeout")
    parser.add_argument("--startup-delay-ms", type=int, default=0, help="delay after connect before first command")
    parser.add_argument("--command-gap-ms", type=int, default=0, help="gap between consecutive commands")
    parser.add_argument("--stop-settle-ms", type=int, default=600, help="delay after stop before disable")
    parser.add_argument(
        "--post-start-delay-ms",
        type=int,
        default=0,
        help="extra dwell after accepted start before the first target-speed command",
    )
    parser.add_argument(
        "--turn-suppressed",
        action="store_true",
        help="send `set_turn_suppressed=true` after tuning mode is enabled",
    )
    parser.add_argument(
        "--disabled-mode-checks",
        action="store_true",
        help="exercise rejected `set_target_speed` and `set_turn_suppressed` while tuning mode is off",
    )
    parser.add_argument(
        "--invalid-target-speed",
        type=float,
        default=None,
        help="send one rejected invalid target-speed command after the valid sequence",
    )
    parser.add_argument(
        "--validation-only",
        action="store_true",
        help="exercise rejected/accepted tuning commands without issuing start/stop or motion-driving targets",
    )
    parser.add_argument("--no-plot", action="store_true", help="disable live plotting")
    return parser.parse_args()


def expect_ack(session: AssistantSession,
               cmd: str,
               timeout_s: float,
               expected_outcome: str,
               **payload: Any) -> AckResult:
    ack = session.send_command(cmd, timeout_s=timeout_s, **payload)
    if ack.outcome != expected_outcome:
        raise RuntimeError(
            f"unexpected ack for cmd={cmd} seq={ack.seq}: expected {expected_outcome}, got {ack.outcome} ({ack.reason})"
        )
    return ack


def command_gap(session: AssistantSession, gap_ms: int) -> None:
    if gap_ms > 0:
        session.pump_for(gap_ms / 1000.0)


def main() -> int:
    args = parse_args()
    sequence = list(parse_speed_sequence(args.sequence))
    csv_path = Path(args.csv).resolve()

    matplotlib_result = bootstrap_matplotlib(args.no_plot)
    plotter: Optional[LivePlotter] = None
    if matplotlib_result is not None:
        plt_module, install_log = matplotlib_result
        if install_log:
            log("[plot] install fallback succeeded and plotting is enabled")
        plotter = LivePlotter(plt_module)

    with socket.create_server((args.listen_host, args.listen_port), reuse_port=False) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        log(f"[listen] waiting for assistant connection on {args.listen_host}:{args.listen_port}")
        connection, address = server.accept()

    log(f"[listen] connected by {address[0]}:{address[1]}")
    session = AssistantSession(
        connection,
        csv_path,
        plotter,
        capture_telemetry=not args.validation_only,
    )
    session.start()
    session.pump_for(max(0, args.startup_delay_ms) / 1000.0)

    try:
        if args.validation_only:
            if args.disabled_mode_checks:
                expect_ack(
                    session,
                    "set_target_speed",
                    timeout_s=args.ack_timeout_s,
                    expected_outcome="rejected",
                    value=sequence[0],
                    ttl_ms=args.ttl_ms,
                )
                session.pump_for(0.02)
                expect_ack(
                    session,
                    "set_turn_suppressed",
                    timeout_s=args.ack_timeout_s,
                    expected_outcome="rejected",
                    value=True,
                )
                session.pump_for(0.05)

            expect_ack(
                session,
                "enable_tuning_mode",
                timeout_s=args.ack_timeout_s,
                expected_outcome="accepted",
            )
            session.pump_for(0.10)
            if args.invalid_target_speed is not None:
                expect_ack(
                    session,
                    "set_target_speed",
                    timeout_s=args.ack_timeout_s,
                    expected_outcome="rejected",
                    value=args.invalid_target_speed,
                    ttl_ms=args.ttl_ms,
                )
                session.pump_for(0.15)
            expect_ack(
                session,
                "disable_tuning_mode",
                timeout_s=args.ack_timeout_s,
                expected_outcome="accepted",
            )
            session.pump_for(0.15)
        else:
            if args.disabled_mode_checks:
                expect_ack(
                    session,
                    "set_target_speed",
                    timeout_s=args.ack_timeout_s,
                    expected_outcome="rejected",
                    value=sequence[0],
                    ttl_ms=args.ttl_ms,
                )
                command_gap(session, args.command_gap_ms)
                expect_ack(
                    session,
                    "set_turn_suppressed",
                    timeout_s=args.ack_timeout_s,
                    expected_outcome="rejected",
                    value=True,
                )
                command_gap(session, args.command_gap_ms)

            expect_ack(session, "enable_tuning_mode", timeout_s=args.ack_timeout_s, expected_outcome="accepted")
            command_gap(session, args.command_gap_ms)
            if args.turn_suppressed:
                expect_ack(
                    session,
                    "set_turn_suppressed",
                    timeout_s=args.ack_timeout_s,
                    expected_outcome="accepted",
                    value=True,
                )
                command_gap(session, args.command_gap_ms)
            expect_ack(session, "start", timeout_s=args.ack_timeout_s, expected_outcome="accepted")
            command_gap(session, args.command_gap_ms)
            session.pump_for(max(0, args.post_start_delay_ms) / 1000.0)

            for run_index in range(args.runs):
                log(f"[run] starting tuning run {run_index + 1}/{args.runs}")
                for speed in sequence:
                    expect_ack(
                        session,
                        "set_target_speed",
                        timeout_s=args.ack_timeout_s,
                        expected_outcome="accepted",
                        value=speed,
                        ttl_ms=args.ttl_ms,
                    )
                    session.pump_for(max(0, args.step_dwell_ms) / 1000.0)
                    command_gap(session, args.command_gap_ms)

            if args.invalid_target_speed is not None:
                expect_ack(
                    session,
                    "set_target_speed",
                    timeout_s=args.ack_timeout_s,
                    expected_outcome="rejected",
                    value=args.invalid_target_speed,
                    ttl_ms=args.ttl_ms,
                )
                command_gap(session, args.command_gap_ms)

            expect_ack(session, "stop", timeout_s=args.ack_timeout_s, expected_outcome="accepted")
            session.pump_for(max(0, args.stop_settle_ms) / 1000.0)
            command_gap(session, args.command_gap_ms)
            expect_ack(session, "disable_tuning_mode", timeout_s=args.ack_timeout_s, expected_outcome="accepted")
        if not session.wait_for_state_event("snapshot_cleared", timeout_s=args.ack_timeout_s):
            log("[warn] snapshot_cleared state event was not observed before timeout")
    finally:
        summary = session.summary()
        session.close()

    log("[summary] csv=" + str(csv_path))
    log("[summary] telemetry_frames=" + str(summary["telemetry_frames"]))
    log("[summary] state_events=" + json.dumps(summary["state_events"], sort_keys=True))
    if summary["receiver_error"]:
        log("[summary] receiver_error=" + str(summary["receiver_error"]))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
