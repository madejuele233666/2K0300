#!/usr/bin/env python3
"""Build, upload, run, resume, and summarize the board TFLM op benchmark."""

from __future__ import annotations

import argparse
import csv
import json
import os
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[2]
TFLM_ROOT = REPO_ROOT / "true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/libraries/zf_components/tflm"
USER_CMAKE_DIR = REPO_ROOT / "new/user"
BUILD_ROOT = SCRIPT_DIR / "build"
GENERATED_DIR = SCRIPT_DIR / "generated"
PROGRESS_PATH = SCRIPT_DIR / "progress.json"

CSV_HEADER = [
    "category",
    "case",
    "ops",
    "status",
    "arena_used_bytes",
    "warmup_us",
    "invoke_min_us",
    "invoke_avg_us",
    "invoke_p95_us",
    "error",
]

CORE_OPS = {
    "CONV_2D",
    "DEPTHWISE_CONV_2D",
    "FULLY_CONNECTED",
    "MAX_POOL_2D",
    "AVERAGE_POOL_2D",
    "MEAN",
    "SOFTMAX",
    "RESHAPE",
    "RELU",
    "RELU6",
    "QUANTIZE",
    "DEQUANTIZE",
    "ADD",
    "MUL",
    "CONCATENATION",
    "MAXIMUM",
    "MINIMUM",
    "SPACE_TO_DEPTH",
    "HARD_SWISH",
}


def log(message: str) -> None:
    print(f"[tflm-op-bench] {message}", flush=True)


def load_progress() -> dict[str, object]:
    if PROGRESS_PATH.is_file():
        return json.loads(PROGRESS_PATH.read_text(encoding="utf-8"))
    return {"completed_stages": []}


def save_progress(progress: dict[str, object]) -> None:
    progress["updated_at"] = datetime.now().isoformat(timespec="seconds")
    PROGRESS_PATH.write_text(json.dumps(progress, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def mark_stage(progress: dict[str, object], stage: str) -> None:
    completed = list(progress.get("completed_stages", []))
    if stage not in completed:
        completed.append(stage)
    progress["completed_stages"] = completed
    save_progress(progress)


def stage_done(progress: dict[str, object], stage: str) -> bool:
    return stage in set(progress.get("completed_stages", []))


def run_cmd(
    cmd: list[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
    timeout: int | None = None,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    log("$ " + " ".join(cmd))
    completed = subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        env=env,
        timeout=timeout,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if completed.stdout:
        print(completed.stdout, end="")
    if check and completed.returncode != 0:
        raise RuntimeError(f"command failed rc={completed.returncode}: {' '.join(cmd)}")
    return completed


def retry_cmd(cmd: list[str], retries: int, delay_s: float, **kwargs: object) -> subprocess.CompletedProcess[str]:
    last: Exception | None = None
    for attempt in range(1, retries + 1):
        try:
            return run_cmd(cmd, **kwargs)
        except Exception as exc:
            last = exc
            log(f"attempt {attempt}/{retries} failed: {exc}")
            if attempt < retries:
                time.sleep(delay_s)
    raise RuntimeError(f"command failed after {retries} attempts: {' '.join(cmd)}") from last


def ssh_base(args: argparse.Namespace) -> list[str]:
    return [
        "ssh",
        "-o",
        f"ConnectTimeout={args.ssh_timeout}",
        "-o",
        "ServerAliveInterval=5",
        "-o",
        "ServerAliveCountMax=2",
        "-o",
        "StrictHostKeyChecking=accept-new",
        f"{args.board_user}@{args.board_ip}",
    ]


def scp_base(args: argparse.Namespace) -> list[str]:
    return [
        "scp",
        "-O",
        "-o",
        f"ConnectTimeout={args.ssh_timeout}",
        "-o",
        "ServerAliveInterval=5",
        "-o",
        "ServerAliveCountMax=2",
        "-o",
        "StrictHostKeyChecking=accept-new",
    ]


def run_ssh(args: argparse.Namespace, remote_script: str, *, check: bool = True) -> subprocess.CompletedProcess[str]:
    return retry_cmd(ssh_base(args) + [remote_script], args.retries, args.retry_delay_s, check=check)


def generate_assets(args: argparse.Namespace, progress: dict[str, object]) -> None:
    if stage_done(progress, "assets") and not args.force:
        log("assets already generated; skipping")
        return
    cmd = [
        args.generator_python,
        str(SCRIPT_DIR / "generate_benchmark_assets.py"),
        "--repo-root",
        str(REPO_ROOT),
        "--output",
        str(GENERATED_DIR / "benchmark_models_generated.h"),
        "--manifest",
        str(GENERATED_DIR / "model_manifest.json"),
        "--max-default-models",
        str(args.max_default_models),
    ]
    if args.include_synthetic:
        cmd.append("--include-synthetic")
        cmd.extend(["--synthetic-suite", args.synthetic_suite])
    if args.synthetic_only:
        cmd.append("--synthetic-only")
    for source in args.source_tflite:
        cmd.extend(["--source-tflite", source])
    run_cmd(cmd)
    mark_stage(progress, "assets")


def build_tflm(args: argparse.Namespace, progress: dict[str, object]) -> Path:
    lib_path = BUILD_ROOT / "lib" / "libtflm.a"
    if stage_done(progress, "tflm_build") and lib_path.is_file() and not args.force:
        log("benchmark libtflm.a already built; skipping")
        return lib_path
    build_dir = BUILD_ROOT / "tflm"
    lib_dir = BUILD_ROOT / "lib"
    if args.force and build_dir.exists():
        shutil.rmtree(build_dir)
    lib_dir.mkdir(parents=True, exist_ok=True)
    run_cmd(
        [
            "cmake",
            "-S",
            str(SCRIPT_DIR),
            "-B",
            str(build_dir),
            f"-DLS2K_TFLM_ROOT={TFLM_ROOT}",
            f"-DTFLM_ARCHIVE_OUTPUT_DIRECTORY={lib_dir}",
        ]
    )
    run_cmd(["cmake", "--build", str(build_dir), "--target", "tflm", "-j", str(args.jobs)])
    if not lib_path.is_file():
        raise RuntimeError(f"benchmark TFLM library missing: {lib_path}")
    mark_stage(progress, "tflm_build")
    return lib_path


def build_benchmark(args: argparse.Namespace, progress: dict[str, object], lib_path: Path) -> Path:
    binary = BUILD_ROOT / "new_user" / "tflm_op_benchmark"
    if stage_done(progress, "benchmark_build") and binary.is_file() and not args.force:
        log("benchmark binary already built; skipping")
        return binary
    build_dir = BUILD_ROOT / "new_user"
    if args.force and build_dir.exists():
        shutil.rmtree(build_dir)
    run_cmd(
        [
            "cmake",
            "-S",
            str(USER_CMAKE_DIR),
            "-B",
            str(build_dir),
            "-DLS2K_TFLM_OP_BENCH=ON",
            f"-DLS2K_TFLM_LIB_PATH={lib_path}",
            "-DLS2K_TFLM_OP_BENCH_MODELS_HEADER=benchmark_models_generated.h",
        ]
    )
    run_cmd(["cmake", "--build", str(build_dir), "--target", "tflm_op_benchmark", "-j", str(args.jobs)])
    if not binary.is_file():
        raise RuntimeError(f"benchmark binary missing: {binary}")
    mark_stage(progress, "benchmark_build")
    return binary


def preflight_board(args: argparse.Namespace, progress: dict[str, object]) -> None:
    if args.skip_upload:
        log("skip-upload enabled; board preflight skipped")
        return
    if stage_done(progress, "preflight") and not args.force:
        log("board preflight already complete; skipping")
        return
    run_ssh(args, "echo board_preflight_ok")
    if args.stop_runtime:
        run_ssh(
            args,
            "if [ -f /home/root/new_runtime.pid ]; then kill $(cat /home/root/new_runtime.pid) 2>/dev/null || true; rm -f /home/root/new_runtime.pid; fi; pkill -f '^/home/root/new$' 2>/dev/null || true; true",
            check=False,
        )
    mark_stage(progress, "preflight")


def upload_binary(args: argparse.Namespace, progress: dict[str, object], binary: Path) -> None:
    if args.skip_upload:
        log("skip-upload enabled; upload skipped")
        return
    if stage_done(progress, "upload") and not args.force:
        log("upload already complete; skipping")
        return
    remote = f"{args.board_user}@{args.board_ip}:{args.board_path}/tflm_op_benchmark"
    retry_cmd(scp_base(args) + [str(binary), remote], args.retries, args.retry_delay_s)
    run_ssh(args, f"chmod +x {args.board_path}/tflm_op_benchmark")
    mark_stage(progress, "upload")


def remote_paths(args: argparse.Namespace) -> tuple[str, str, str, str]:
    remote_bin = f"{args.board_path}/tflm_op_benchmark"
    remote_log = f"{args.board_path}/tflm_op_benchmark.log"
    remote_pid = f"{args.board_path}/tflm_op_benchmark.pid"
    remote_exit = f"{args.board_path}/tflm_op_benchmark.exit"
    return remote_bin, remote_log, remote_pid, remote_exit


def run_remote(args: argparse.Namespace, progress: dict[str, object]) -> None:
    if args.skip_upload:
        log("skip-upload enabled; remote run skipped")
        return
    if stage_done(progress, "remote_run") and not args.force:
        log("remote run already complete; skipping")
        return
    remote_bin, remote_log, remote_pid, remote_exit = remote_paths(args)
    remote_args = [f"--warmup {args.warmup}", f"--loops {args.loops}"]
    for value in args.case_filter:
        remote_args.append(f"--case-filter {value}")
    for value in args.skip_case:
        remote_args.append(f"--skip-case {value}")
    start_script = (
        f"rm -f {remote_log} {remote_pid} {remote_exit}; "
        f"( {remote_bin} {' '.join(remote_args)} > {remote_log} 2>&1; echo $? > {remote_exit} ) & "
        f"echo $! > {remote_pid}; echo benchmark_started pid=$(cat {remote_pid})"
    )
    run_ssh(args, start_script)

    deadline = time.time() + args.remote_timeout_s
    while time.time() < deadline:
        check = run_ssh(
            args,
            f"if [ -f {remote_pid} ] && kill -0 $(cat {remote_pid}) 2>/dev/null; then echo RUNNING; else echo DONE; fi",
            check=False,
        )
        if "DONE" in check.stdout:
            break
        time.sleep(args.poll_interval_s)
    else:
        run_ssh(args, f"kill $(cat {remote_pid}) 2>/dev/null || true; echo 124 > {remote_exit}", check=False)
        raise RuntimeError("remote benchmark timed out")

    exit_check = run_ssh(args, f"cat {remote_exit} 2>/dev/null || echo missing_exit", check=False)
    if "0" not in exit_check.stdout.split():
        log(f"remote benchmark exit status: {exit_check.stdout.strip()}")
    mark_stage(progress, "remote_run")


def collect_log(args: argparse.Namespace, progress: dict[str, object], result_dir: Path) -> Path:
    log_path = result_dir / "raw_board_tflm_op_benchmark.log"
    if args.skip_upload:
        log("skip-upload enabled; no board log to collect")
        return log_path
    if stage_done(progress, "collect") and log_path.is_file() and not args.force:
        log("remote log already collected; skipping")
        return log_path
    _, remote_log, _, _ = remote_paths(args)
    retry_cmd(
        scp_base(args) + [f"{args.board_user}@{args.board_ip}:{remote_log}", str(log_path)],
        args.retries,
        args.retry_delay_s,
    )
    mark_stage(progress, "collect")
    return log_path


def parse_csv_rows(log_path: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    if not log_path.is_file():
        return rows
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("CSV,"):
            continue
        parsed = next(csv.reader([line]))
        if len(parsed) < 11 or parsed[1] == "category":
            continue
        row = dict(zip(CSV_HEADER, parsed[1:11]))
        rows.append(row)
    return rows


def write_category_csv(path: Path, rows: list[dict[str, str]], category: str) -> None:
    selected = [row for row in rows if row.get("category") == category]
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_HEADER)
        writer.writeheader()
        writer.writerows(selected)


def split_ops(value: str) -> list[str]:
    return [part for part in value.replace(";", "|").split("|") if part and part != "NONE"]


def summarize_rows(rows: list[dict[str, str]], result_dir: Path) -> None:
    availability = {row["case"]: row for row in rows if row.get("category") == "availability"}
    attempted_bench_ops: set[str] = set()
    successful_bench_ops: set[str] = set()
    failed_ops: set[str] = set()
    failed_cases: list[dict[str, str]] = []
    model_rows = [row for row in rows if row.get("category") == "modelbench"]
    micro_rows = [row for row in rows if row.get("category") == "microbench"]

    for row in model_rows + micro_rows:
        ops = split_ops(row.get("ops", ""))
        attempted_bench_ops.update(ops)
        if row.get("status") == "OK":
            successful_bench_ops.update(ops)
        elif row.get("status") not in {"SKIPPED", ""}:
            failed_ops.update(ops)
            failed_cases.append(
                {
                    "category": row.get("category", ""),
                    "case": row.get("case", ""),
                    "ops": row.get("ops", ""),
                    "status": row.get("status", ""),
                    "error": row.get("error", ""),
                }
            )

    green: list[str] = []
    yellow: list[str] = []
    red: list[str] = []
    for op, row in sorted(availability.items()):
        if row.get("status") != "OK":
            red.append(op)
        elif op in successful_bench_ops or op in CORE_OPS:
            green.append(op)
        else:
            yellow.append(op)
    for op in sorted(failed_ops):
        if op not in red and op not in green:
            red.append(op)

    summary = {
        "generated_at": datetime.now().isoformat(timespec="seconds"),
        "row_count": len(rows),
        "availability_count": len(availability),
        "modelbench_count": len(model_rows),
        "microbench_count": len(micro_rows),
        "attempted_benchmark_ops": sorted(attempted_bench_ops),
        "successful_benchmark_ops": sorted(successful_bench_ops),
        "missing_benchmark_ops": sorted(
            op for op, row in availability.items() if row.get("status") == "OK" and op not in attempted_bench_ops
        ),
        "failed_benchmark_cases": failed_cases,
        "green_ops": green,
        "yellow_ops": yellow,
        "red_ops": red,
        "notes": [
            "GREEN means resolver OK and either core candidate or observed in a successful modelbench row.",
            "YELLOW means resolver OK but no direct timing evidence yet.",
            "RED means resolver/model failure or explicit benchmark failure.",
        ],
    }
    (result_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    (result_dir / "green_ops.txt").write_text("\n".join(green) + ("\n" if green else ""), encoding="utf-8")
    (result_dir / "yellow_ops.txt").write_text("\n".join(yellow) + ("\n" if yellow else ""), encoding="utf-8")
    (result_dir / "red_ops.txt").write_text("\n".join(red) + ("\n" if red else ""), encoding="utf-8")


def parse_and_summarize(result_dir: Path, log_path: Path) -> None:
    rows = parse_csv_rows(log_path)
    write_category_csv(result_dir / "board_ops_availability.csv", rows, "availability")
    write_category_csv(result_dir / "board_model_benchmark.csv", rows, "modelbench")
    write_category_csv(result_dir / "board_ops_benchmark.csv", rows, "microbench")
    summarize_rows(rows, result_dir)
    log(f"summary written: {result_dir / 'summary.json'}")


def prepare_result_dir(args: argparse.Namespace, progress: dict[str, object]) -> Path:
    if args.results_dir:
        result_dir = Path(args.results_dir)
        if not result_dir.is_absolute():
            result_dir = REPO_ROOT / result_dir
    else:
        existing = progress.get("result_dir")
        if existing and not args.force:
            result_dir = Path(str(existing))
        else:
            stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            result_dir = SCRIPT_DIR / "results" / stamp
    result_dir.mkdir(parents=True, exist_ok=True)
    progress["result_dir"] = str(result_dir)
    save_progress(progress)
    return result_dir


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--all", action="store_true", help="Run the full automated benchmark flow")
    parser.add_argument("--auto-advance", action="store_true", help="Continue stages without prompting")
    parser.add_argument("--build-only", action="store_true", help="Generate and build, but do not upload or run")
    parser.add_argument("--skip-upload", action="store_true", help="Do not contact the board")
    parser.add_argument("--summarize-only", metavar="LOG", help="Parse an existing board benchmark log")
    parser.add_argument("--force", action="store_true", help="Ignore completed checkpoint stages")
    parser.add_argument("--results-dir")
    parser.add_argument("--source-tflite", action="append", default=[])
    parser.add_argument("--max-default-models", type=int, default=2)
    default_generator_python = REPO_ROOT / "model_training/venv/bin/python"
    parser.add_argument(
        "--generator-python",
        default=str(default_generator_python if default_generator_python.exists() else Path(sys.executable)),
    )
    parser.add_argument("--include-synthetic", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--synthetic-only", action="store_true")
    parser.add_argument("--synthetic-suite", choices=["core", "full", "valuable"], default="full")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--board-ip", default=os.environ.get("BOARD_IP", "10.100.170.226"))
    parser.add_argument("--board-user", default=os.environ.get("BOARD_USER", "root"))
    parser.add_argument("--board-path", default=os.environ.get("BOARD_PATH", "/home/root"))
    parser.add_argument("--ssh-timeout", type=int, default=5)
    parser.add_argument("--retries", type=int, default=3)
    parser.add_argument("--retry-delay-s", type=float, default=5.0)
    parser.add_argument("--poll-interval-s", type=float, default=2.0)
    parser.add_argument("--remote-timeout-s", type=int, default=120)
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--loops", type=int, default=30)
    parser.add_argument("--case-filter", action="append", default=[])
    parser.add_argument("--skip-case", action="append", default=[])
    parser.add_argument("--stop-runtime", action=argparse.BooleanOptionalAction, default=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    progress = load_progress()
    result_dir = prepare_result_dir(args, progress)

    if args.summarize_only:
        parse_and_summarize(result_dir, Path(args.summarize_only))
        return 0

    if not (args.all or args.build_only):
        log("nothing selected; use --all --auto-advance or --build-only")
        return 2

    if args.auto_advance:
        log("auto-advance enabled")

    generate_assets(args, progress)
    lib_path = build_tflm(args, progress)
    binary = build_benchmark(args, progress, lib_path)

    if args.build_only:
        log(f"build-only complete: {binary}")
        return 0

    preflight_board(args, progress)
    upload_binary(args, progress, binary)
    run_remote(args, progress)
    log_path = collect_log(args, progress, result_dir)
    parse_and_summarize(result_dir, log_path)
    mark_stage(progress, "summary")
    log(f"result_dir={result_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
