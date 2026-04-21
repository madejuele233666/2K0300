#!/usr/bin/env python3
import argparse
import csv
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


SUMMARY_RE = re.compile(r"bench PWM pulse (?P<fields>.+)")
KV_RE = re.compile(r"([A-Za-z0-9_]+)=([^ ]+)")


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[1]
    default_csv = repo_root / "verification" / "phase-d-pwm-encoder-calibration.csv"

    parser = argparse.ArgumentParser(
        description="Run open-loop PWM pulses on the board and record encoder deltas."
    )
    parser.add_argument(
        "--sequence",
        default="200,400,600,800,1000,1200,1500,1800,2200,2600,3000",
        help="comma-separated PWM sequence for each wheel",
    )
    parser.add_argument(
        "--side",
        choices=("left", "right", "both"),
        default="both",
        help="which logical wheel(s) to calibrate",
    )
    parser.add_argument("--repeats", type=int, default=3, help="repeats per PWM value")
    parser.add_argument("--pulse-ms", type=int, default=180, help="bench pulse width in ms")
    parser.add_argument("--settle-ms", type=int, default=80, help="settle time passed to the bench mode")
    parser.add_argument("--cooldown-ms", type=int, default=150, help="delay between samples in ms")
    parser.add_argument("--max-attempts", type=int, default=3, help="max attempts per sample")
    parser.add_argument(
        "--csv",
        default=str(default_csv),
        help="output CSV path",
    )
    parser.add_argument("--board-ip", default=os.environ.get("BOARD_IP", "10.100.170.226"))
    parser.add_argument("--board-user", default=os.environ.get("BOARD_USER", "root"))
    parser.add_argument("--remote-bin", default=os.environ.get("BOARD_BIN", "/home/root/new"))
    parser.add_argument(
        "--remote-params",
        default=os.environ.get("LS2K_REMOTE_PARAMS_PATH", "/home/root/default_params.json"),
    )
    parser.add_argument(
        "--remote-profile",
        default=os.environ.get("LS2K_REMOTE_PROFILE_PATH", "/home/root/hardware_profile.json"),
    )
    parser.add_argument(
        "--allow-degraded-startup",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="set LS2K_ALLOW_DEGRADED_STARTUP=1 for the remote bench run",
    )
    parser.add_argument(
        "--stop-runtime",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="stop the managed runtime before calibration",
    )
    return parser.parse_args()


def parse_sequence(raw: str) -> list[int]:
    values: list[int] = []
    for token in raw.split(","):
        token = token.strip()
        if not token:
            continue
        values.append(int(token))
    if not values:
        raise ValueError("PWM sequence is empty")
    return values


def run(cmd: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, check=check, text=True, capture_output=True)


def stop_runtime(board_user: str, board_ip: str, remote_bin: str) -> None:
    remote_script = f"""
set -euo pipefail
pkill -f "^{re.escape(remote_bin)}$" 2>/dev/null || true
rm -f /home/root/new_runtime.pid 2>/dev/null || true
echo runtime_stopped_or_absent
"""
    run(["ssh", f"{board_user}@{board_ip}", remote_script])


def parse_summary(output: str) -> dict[str, str]:
    for line in output.splitlines():
        match = SUMMARY_RE.search(line)
        if not match:
            continue
        fields = dict(KV_RE.findall(match.group("fields")))
        if "apply_ok" in fields:
            return fields
    raise RuntimeError(f"bench.pwm.summary not found in output:\n{output}")


def to_int(fields: dict[str, str], key: str) -> int:
    return int(fields.get(key, "0"))


def to_bool(fields: dict[str, str], key: str) -> bool:
    return fields.get(key, "false").lower() == "true"


def sample_remote(
    *,
    board_user: str,
    board_ip: str,
    remote_bin: str,
    remote_params: str,
    remote_profile: str,
    allow_degraded_startup: bool,
    pulse_ms: int,
    settle_ms: int,
    left_pwm: int,
    right_pwm: int,
    max_attempts: int,
) -> tuple[dict[str, str], str]:
    env_parts = [
        f"LS2K_PARAMS_PATH={remote_params}",
        f"LS2K_PROFILE_PATH={remote_profile}",
        f"LS2K_BENCH_PWM_MS={pulse_ms}",
        f"LS2K_BENCH_SETTLE_MS={settle_ms}",
        f"LS2K_BENCH_PWM_LEFT={left_pwm}",
        f"LS2K_BENCH_PWM_RIGHT={right_pwm}",
    ]
    if allow_degraded_startup:
        env_parts.insert(0, "LS2K_ALLOW_DEGRADED_STARTUP=1")
    remote_cmd = " ".join(env_parts + [remote_bin])
    last_error: Exception | None = None
    last_output = ""
    for attempt in range(1, max(1, max_attempts) + 1):
        try:
            result = run(["ssh", f"{board_user}@{board_ip}", remote_cmd])
            last_output = result.stdout
            return parse_summary(result.stdout), result.stdout
        except Exception as exc:
            last_error = exc
            if isinstance(exc, RuntimeError):
                last_output = str(exc)
            if attempt < max(1, max_attempts):
                time.sleep(0.2)
                continue
            break
    raise RuntimeError(f"sample failed after {max_attempts} attempt(s):\n{last_output}") from last_error


def build_rows(args: argparse.Namespace, sequence: list[int]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    sides = ("left", "right") if args.side == "both" else (args.side,)
    for side in sides:
        for pwm in sequence:
            for repeat in range(1, args.repeats + 1):
                left_pwm = pwm if side == "left" else 0
                right_pwm = pwm if side == "right" else 0
                fields, stdout = sample_remote(
                    board_user=args.board_user,
                    board_ip=args.board_ip,
                    remote_bin=args.remote_bin,
                    remote_params=args.remote_params,
                    remote_profile=args.remote_profile,
                    allow_degraded_startup=args.allow_degraded_startup,
                    pulse_ms=args.pulse_ms,
                    settle_ms=args.settle_ms,
                    left_pwm=left_pwm,
                    right_pwm=right_pwm,
                    max_attempts=args.max_attempts,
                )
                row: dict[str, object] = {
                    "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                    "side": side,
                    "command_left_pwm": left_pwm,
                    "command_right_pwm": right_pwm,
                    "pwm": pwm,
                    "repeat": repeat,
                    "apply_ok": int(to_bool(fields, "apply_ok")),
                    "before_valid": int(to_bool(fields, "before_valid")),
                    "before_left": to_int(fields, "before_left"),
                    "before_right": to_int(fields, "before_right"),
                    "during_valid": int(to_bool(fields, "during_valid")),
                    "during_left": to_int(fields, "during_left"),
                    "during_right": to_int(fields, "during_right"),
                    "after1_valid": int(to_bool(fields, "after1_valid")),
                    "after1_left": to_int(fields, "after1_left"),
                    "after1_right": to_int(fields, "after1_right"),
                    "after2_valid": int(to_bool(fields, "after2_valid")),
                    "after2_left": to_int(fields, "after2_left"),
                    "after2_right": to_int(fields, "after2_right"),
                    "total_left": to_int(fields, "during_left")
                    + to_int(fields, "after1_left")
                    + to_int(fields, "after2_left"),
                    "total_right": to_int(fields, "during_right")
                    + to_int(fields, "after1_right")
                    + to_int(fields, "after2_right"),
                }
                rows.append(row)
                active_total = row["total_left"] if side == "left" else row["total_right"]
                print(
                    f"sample side={side} pwm={pwm} repeat={repeat} "
                    f"apply_ok={row['apply_ok']} active_total={active_total}"
                )
                if args.cooldown_ms > 0:
                    time.sleep(args.cooldown_ms / 1000.0)
    return rows


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "timestamp_utc",
        "side",
        "command_left_pwm",
        "command_right_pwm",
        "pwm",
        "repeat",
        "apply_ok",
        "before_valid",
        "before_left",
        "before_right",
        "during_valid",
        "during_left",
        "during_right",
        "after1_valid",
        "after1_left",
        "after1_right",
        "after2_valid",
        "after2_left",
        "after2_right",
        "total_left",
        "total_right",
    ]
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def print_summary(rows: list[dict[str, object]]) -> None:
    if not rows:
        print("no samples collected")
        return
    grouped: dict[tuple[str, int], list[int]] = {}
    for row in rows:
        side = str(row["side"])
        pwm = int(row["pwm"])
        total = int(row["total_left"] if side == "left" else row["total_right"])
        grouped.setdefault((side, pwm), []).append(total)

    print("summary:")
    for side, pwm in sorted(grouped):
        samples = grouped[(side, pwm)]
        avg = sum(samples) / len(samples)
        print(f"  side={side} pwm={pwm} avg_total={avg:.2f} samples={samples}")


def main() -> int:
    args = parse_args()
    sequence = parse_sequence(args.sequence)
    if args.repeats <= 0:
        raise SystemExit("--repeats must be > 0")
    if args.stop_runtime:
        stop_runtime(args.board_user, args.board_ip, args.remote_bin)
    rows = build_rows(args, sequence)
    csv_path = Path(args.csv).resolve()
    write_csv(csv_path, rows)
    print_summary(rows)
    print(f"csv={csv_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("interrupted", file=sys.stderr)
        raise SystemExit(130)
