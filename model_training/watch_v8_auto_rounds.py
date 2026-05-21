#!/usr/bin/env python3
"""Long-running watcher for V8 auto-summary and next-round launches."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any

import run_v8_auto_round as auto_round


DEFAULT_STATE = Path("experiments/v8_auto_round_watcher_state.json")
DEFAULT_LOG = Path("experiments/v8_auto_round_watcher.log")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--experiments-dir", type=Path, default=Path("experiments"))
    parser.add_argument("--stage", default=auto_round.DEFAULT_STAGE)
    parser.add_argument("--sleep", type=float, default=30.0)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--max-rounds", type=int, default=0, help="0 means run forever.")
    parser.add_argument("--state", type=Path, default=DEFAULT_STATE)
    parser.add_argument("--log", type=Path, default=DEFAULT_LOG)
    parser.add_argument("--epochs", type=int, default=900)
    parser.add_argument("--warmup-epochs", type=int, default=250)
    parser.add_argument("--prototype-sources", default="kmeans")
    parser.add_argument("--quant-scales", default="32,48,64,96,128")
    parser.add_argument("--allow-active-gpu-sessions", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def now() -> str:
    return datetime.now().isoformat(timespec="seconds")


def load_state(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {"processed_rounds": [], "launch_count": 0}
    try:
        state = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return {"processed_rounds": [], "launch_count": 0}
    state.setdefault("processed_rounds", [])
    state.setdefault("launch_count", 0)
    return state


def save_state(path: Path, state: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(state, indent=2, ensure_ascii=True), encoding="utf-8")


def log(path: Path, message: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    line = f"{now()} {message}"
    print(line, flush=True)
    with path.open("a", encoding="utf-8") as handle:
        handle.write(line + "\n")


def active_gpu_sessions() -> list[str]:
    proc = subprocess.run(["tmux", "ls"], text=True, capture_output=True, check=False)
    if proc.returncode != 0:
        return []
    sessions = []
    for line in proc.stdout.splitlines():
        name = line.split(":", 1)[0]
        if name.startswith("v8bf_"):
            sessions.append(name)
    return sessions


def run_auto_round(
    script: Path,
    round_id: str,
    next_run_id: str,
    args: argparse.Namespace,
    log_path: Path,
) -> int:
    cmd = [
        sys.executable,
        str(script),
        "--round-id",
        round_id,
        "--next-run-id",
        next_run_id,
        "--jobs",
        str(args.jobs),
        "--epochs",
        str(args.epochs),
        "--warmup-epochs",
        str(args.warmup_epochs),
        "--prototype-sources",
        args.prototype_sources,
        "--quant-scales",
        args.quant_scales,
    ]
    if args.allow_active_gpu_sessions:
        cmd.append("--allow-active-gpu-sessions")
    if args.dry_run:
        cmd.append("--dry-run")
    log(log_path, "auto_round_cmd=" + " ".join(cmd))
    proc = subprocess.run(cmd, text=True, capture_output=True, check=False)
    if proc.stdout:
        for line in proc.stdout.rstrip().splitlines():
            log(log_path, "auto_round_stdout " + line)
    if proc.stderr:
        for line in proc.stderr.rstrip().splitlines():
            log(log_path, "auto_round_stderr " + line)
    return proc.returncode


def main() -> int:
    args = parse_args()
    model_training_dir = Path(__file__).resolve().parent
    experiments_dir = args.experiments_dir
    if not experiments_dir.is_absolute():
        experiments_dir = model_training_dir / experiments_dir
    state_path = args.state if args.state.is_absolute() else model_training_dir / args.state
    log_path = args.log if args.log.is_absolute() else model_training_dir / args.log
    script = model_training_dir / "run_v8_auto_round.py"

    log(log_path, "watcher_start")
    while True:
        state = load_state(state_path)
        if args.max_rounds and int(state.get("launch_count", 0)) >= args.max_rounds:
            log(log_path, f"watcher_stop max_rounds={args.max_rounds}")
            return 0

        active = active_gpu_sessions()
        if active and not args.allow_active_gpu_sessions:
            log(log_path, "active_gpu_sessions=" + ",".join(active))
            time.sleep(args.sleep)
            continue

        try:
            latest = auto_round.find_latest_complete_round(experiments_dir, args.stage)
        except SystemExit as exc:
            log(log_path, f"no_complete_round {exc}")
            time.sleep(args.sleep)
            continue

        processed = set(str(x) for x in state.get("processed_rounds", []))
        if latest.name in processed:
            log(log_path, f"idle latest_already_processed={latest.name}")
            time.sleep(args.sleep)
            continue

        next_run_id = "v8auto_" + datetime.now().strftime("%Y%m%d_%H%M%S")
        log(log_path, f"process_round={latest.name} next_run_id={next_run_id}")
        rc = run_auto_round(script, latest.name, next_run_id, args, log_path)
        if rc == 0:
            state = load_state(state_path)
            processed_list = list(dict.fromkeys([*state.get("processed_rounds", []), latest.name]))
            state["processed_rounds"] = processed_list
            state["last_processed_round"] = latest.name
            state["last_next_run_id"] = next_run_id
            state["last_processed_at"] = now()
            state["launch_count"] = int(state.get("launch_count", 0)) + (0 if args.dry_run else 1)
            save_state(state_path, state)
            log(log_path, f"processed_ok={latest.name}")
        else:
            log(log_path, f"processed_failed={latest.name} rc={rc}")
            time.sleep(max(args.sleep, 60.0))
            continue

        time.sleep(args.sleep)


if __name__ == "__main__":
    sys.exit(main())
