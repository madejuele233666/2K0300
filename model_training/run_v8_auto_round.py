#!/usr/bin/env python3
"""Summarize the latest V8 Phase B round and launch the next tmux round."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any


PASS_EPS = 1e-9
DEFAULT_STAGE = "stageB_focus_end_to_end_embedding"
LAUNCHER = "run_v8_phaseB_focus_tmux.sh"


@dataclass(frozen=True)
class ResultRow:
    round_id: str
    config_name: str
    summary_path: Path
    filters: tuple[int, ...]
    embedding_dim: int
    seed: int
    prototype_source: str
    k_per_subclass: int
    prototype_count: int
    int8_scale: float
    rotmirror: float
    stress: float
    fixed_stress: float
    int8_rotmirror: float
    int8_stress: float
    int8_fixed_stress: float
    margin_min: float

    @property
    def all_pass(self) -> bool:
        return min(self.rotmirror, self.stress, self.int8_rotmirror, self.int8_stress) >= 1.0 - PASS_EPS

    @property
    def fixed_all_pass(self) -> bool:
        return min(self.fixed_stress, self.int8_fixed_stress) >= 1.0 - PASS_EPS

    @property
    def min_metric(self) -> float:
        return min(self.rotmirror, self.stress, self.int8_rotmirror, self.int8_stress)

    @property
    def filter_name(self) -> str:
        return "_".join(str(x) for x in self.filters)

    @property
    def next_name_prefix(self) -> str:
        return f"c{self.filters[0]}_{self.filters[1]}_{self.filters[2]}_d{self.embedding_dim}"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--experiments-dir", type=Path, default=Path("experiments"))
    parser.add_argument("--stage", default=DEFAULT_STAGE)
    parser.add_argument("--round-id", help="Summarize this exact experiment round instead of the latest complete one.")
    parser.add_argument("--next-run-id", help="Run id for the next launched round.")
    parser.add_argument("--jobs", type=int, default=4, help="Number of next configs to launch.")
    parser.add_argument("--epochs", type=int, default=900)
    parser.add_argument("--warmup-epochs", type=int, default=250)
    parser.add_argument("--prototype-sources", default="kmeans")
    parser.add_argument("--quant-scales", default="32,48,64,96,128")
    parser.add_argument("--no-launch", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--allow-active-gpu-sessions", action="store_true")
    parser.add_argument("--top", type=int, default=8)
    return parser.parse_args()


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def round_stage_dir(experiments_dir: Path, round_id: str, stage: str) -> Path:
    return experiments_dir / round_id / stage


def expected_config_count(round_root: Path) -> int | None:
    config_path = round_root / "launch_config.json"
    if not config_path.exists():
        return None
    try:
        configs = str(load_json(config_path).get("configs", ""))
    except Exception:
        return None
    return len([item for item in configs.split(";") if item.strip()])


def find_round_dirs(experiments_dir: Path, stage: str) -> list[Path]:
    if not experiments_dir.exists():
        return []
    return sorted(
        (p for p in experiments_dir.iterdir() if (p / stage).is_dir()),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )


def find_latest_complete_round(experiments_dir: Path, stage: str) -> Path:
    fallback: Path | None = None
    for root in find_round_dirs(experiments_dir, stage):
        summaries = list((root / stage).glob("*/summary.json"))
        if not summaries:
            continue
        if fallback is None:
            fallback = root
        expected = expected_config_count(root)
        if expected is None or len(summaries) >= expected:
            return root
    if fallback is not None:
        return fallback
    raise SystemExit(f"No V8 summaries found under {experiments_dir}/*/{stage}.")


def row_from_summary(path: Path, experiments_dir: Path, stage: str) -> ResultRow:
    data = load_json(path)
    best = data.get("best") or {}
    config = data.get("config") or {}
    config_name = path.parent.name
    round_id = path.parents[1].name if path.parents[0].name == stage else path.parents[2].name
    filters = tuple(int(x) for x in config.get("filters", parse_filters_from_name(config_name)))
    embedding_dim = int(config.get("embedding_dim", parse_dim_from_name(config_name)))
    seed = int(config.get("seed", parse_seed_from_name(config_name)))
    return ResultRow(
        round_id=round_id,
        config_name=config_name,
        summary_path=path,
        filters=filters,
        embedding_dim=embedding_dim,
        seed=seed,
        prototype_source=str(best.get("prototype_source", "")),
        k_per_subclass=int(best.get("k_per_subclass", 0)),
        prototype_count=int(best.get("prototype_count", 0)),
        int8_scale=float(best.get("int8_scale", 0.0)),
        rotmirror=float(best.get("rotmirror_min_accuracy", 0.0)),
        stress=float(best.get("stress_min_accuracy", 0.0)),
        fixed_stress=float(best.get("fixed_stress_min_accuracy", 0.0)),
        int8_rotmirror=float(best.get("int8_rotmirror_min_accuracy", 0.0)),
        int8_stress=float(best.get("int8_stress_min_accuracy", 0.0)),
        int8_fixed_stress=float(best.get("int8_fixed_stress_min_accuracy", 0.0)),
        margin_min=float(best.get("margin_min", 0.0)),
    )


def parse_filters_from_name(name: str) -> tuple[int, int, int]:
    match = re.search(r"c(\d+)_(\d+)_(\d+)", name)
    if not match:
        return (6, 12, 24)
    return tuple(int(x) for x in match.groups())  # type: ignore[return-value]


def parse_dim_from_name(name: str) -> int:
    match = re.search(r"_d(\d+)", name)
    return int(match.group(1)) if match else 32


def parse_seed_from_name(name: str) -> int:
    match = re.search(r"_s(\d+)", name)
    return int(match.group(1)) if match else 20260500


def collect_rows(root: Path, experiments_dir: Path, stage: str) -> list[ResultRow]:
    return [row_from_summary(p, experiments_dir, stage) for p in sorted((root / stage).glob("*/summary.json"))]


def collect_all_focus_rows(experiments_dir: Path, stage: str) -> list[ResultRow]:
    rows: list[ResultRow] = []
    for root in find_round_dirs(experiments_dir, stage):
        for path in (root / stage).glob("*/summary.json"):
            try:
                rows.append(row_from_summary(path, experiments_dir, stage))
            except Exception:
                continue
    return rows


def rank_key(row: ResultRow) -> tuple[Any, ...]:
    return (
        row.all_pass,
        row.fixed_all_pass,
        row.min_metric,
        row.int8_fixed_stress,
        row.fixed_stress,
        -row.prototype_count,
        row.margin_min,
    )


def sort_rows(rows: list[ResultRow]) -> list[ResultRow]:
    return sorted(rows, key=rank_key, reverse=True)


def max_seen_seed(rows: list[ResultRow]) -> int:
    seeds = [row.seed for row in rows if row.seed > 0]
    return max(seeds) if seeds else 20260500


def choose_next(rows: list[ResultRow], all_rows: list[ResultRow], jobs: int) -> tuple[list[str], str, str]:
    ranked = sort_rows(rows)
    if not ranked:
        raise SystemExit("Selected round has no summary rows.")
    all_pass = [row for row in ranked if row.all_pass]
    fixed_pass = [row for row in ranked if row.fixed_all_pass]
    base = min(all_pass, key=lambda row: (row.prototype_count, -row.margin_min)) if all_pass else ranked[0]
    next_seed = max_seen_seed(all_rows) + 1

    if all_pass and base.prototype_count <= 512:
        mode = "smalltable"
        reason = (
            f"{base.config_name} passed raw+int8 rotmirror/stress with {base.prototype_count} prototypes; "
            "continue pressure toward <=512 and test compiler-friendly smaller embeddings."
        )
        dims = [base.embedding_dim]
        if base.embedding_dim >= 32:
            dims.extend([24, 28])
        else:
            dims.append(max(16, base.embedding_dim - 4))
        k_values = "16,24,32,48,64" if base.prototype_count <= 384 else "24,32,48,64"
    elif all_pass:
        mode = "compress"
        reason = (
            f"{base.config_name} passed all core metrics but needs {base.prototype_count} prototypes; "
            "keep the winning backbone, search lower K, and re-inject d24 for compiler-friendly deployment."
        )
        dims = [base.embedding_dim]
        if base.embedding_dim >= 32:
            dims.extend([24, 28])
        k_values = "24,32,48,64,96"
    elif fixed_pass:
        mode = "repair"
        reason = (
            f"No raw+int8 all-pass row, but {base.config_name} fixed-stress passes; "
            "try the same family with more K headroom and include d24 if the current branch is d32."
        )
        dims = [base.embedding_dim]
        if base.embedding_dim >= 32:
            dims.append(24)
        k_values = "32,48,64,96,128"
    else:
        mode = "capacity"
        reason = (
            f"No all-pass row in the previous round; best min metric is {base.min_metric:.6f}. "
            "add capacity while preserving the best filter family."
        )
        dims = [min(40, base.embedding_dim + 4)]
        k_values = "48,64,96,128"

    configs: list[str] = []
    seed = next_seed
    dim_index = 0
    while len(configs) < jobs:
        dim = dims[dim_index % len(dims)]
        filters = "-".join(str(x) for x in base.filters)
        name = f"c{base.filters[0]}_{base.filters[1]}_{base.filters[2]}_d{dim}_s{seed}"
        configs.append(f"{name}:{filters}:{dim}:{seed}")
        seed += 1
        dim_index += 1
    return configs, k_values, f"{mode}: {reason}"


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


def format_row(row: ResultRow) -> str:
    status = "PASS" if row.all_pass else ("FIXED" if row.fixed_all_pass else "MISS")
    return (
        f"| {status} | {row.config_name} | d{row.embedding_dim} | k={row.k_per_subclass} | "
        f"{row.prototype_count} | {row.rotmirror:.6f} | {row.stress:.6f} | "
        f"{row.int8_rotmirror:.6f} | {row.int8_stress:.6f} | {row.fixed_stress:.6f} | "
        f"{row.int8_fixed_stress:.6f} | {row.margin_min:.6g} |"
    )


def make_report(
    previous_root: Path,
    rows: list[ResultRow],
    all_rows: list[ResultRow],
    next_configs: list[str],
    k_values: str,
    next_run_id: str,
    rationale: str,
    launched: bool,
) -> str:
    ranked = sort_rows(rows)
    all_pass = [row for row in ranked if row.all_pass]
    misses = [row for row in ranked if not row.all_pass]
    previous_best_all_pass = [
        row for row in all_rows if row.all_pass and row.round_id != previous_root.name
    ]
    global_smallest = min(previous_best_all_pass, key=lambda row: row.prototype_count, default=None)
    round_smallest = min(all_pass, key=lambda row: row.prototype_count, default=None)
    lines = [
        f"# V8 Auto Round Report: {previous_root.name}",
        "",
        f"- generated_at: {datetime.now().isoformat(timespec='seconds')}",
        f"- completed_rows: {len(rows)}",
        f"- raw_int8_all_pass_rows: {len(all_pass)}",
        f"- launched_next: {str(launched).lower()}",
        f"- next_run_id: {next_run_id}",
        "",
        "## Gains",
    ]
    if round_smallest is not None:
        improved = (
            global_smallest is None or round_smallest.prototype_count < global_smallest.prototype_count
        )
        suffix = "new raw GPU table best" if improved else "not smaller than previous global best"
        lines.append(
            f"- Smallest raw+int8 all-pass row: {round_smallest.config_name}, "
            f"{round_smallest.prototype_count} prototypes, k={round_smallest.k_per_subclass} ({suffix})."
        )
    else:
        lines.append("- No raw+int8 rotmirror/stress all-pass row in this round.")
    if ranked:
        best = ranked[0]
        lines.append(
            f"- Best score row: {best.config_name}, min_core={best.min_metric:.6f}, "
            f"fixed_int8={best.int8_fixed_stress:.6f}, prototypes={best.prototype_count}."
        )
    lines.append("")
    lines.append("## Losses")
    if misses:
        for row in misses[:6]:
            lines.append(
                f"- {row.config_name}: min_core={row.min_metric:.6f}, "
                f"rot={row.rotmirror:.6f}, stress={row.stress:.6f}, "
                f"i8rot={row.int8_rotmirror:.6f}, i8stress={row.int8_stress:.6f}."
            )
    else:
        lines.append("- No misses in core raw/int8 rotmirror/stress metrics.")
    lines.extend(
        [
            "",
            "## Ranking",
            "| status | config | dim | k | prototypes | rot | stress | i8rot | i8stress | fixed | i8fixed | margin_min |",
            "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
        ]
    )
    for row in ranked:
        lines.append(format_row(row))
    lines.extend(
        [
            "",
            "## Next Round",
            f"- rationale: {rationale}",
            f"- k_values: {k_values}",
            f"- configs: {';'.join(next_configs)}",
        ]
    )
    return "\n".join(lines) + "\n"


def write_report_files(next_root: Path, report: str, payload: dict[str, Any]) -> None:
    next_root.mkdir(parents=True, exist_ok=True)
    (next_root / "auto_round_report.md").write_text(report, encoding="utf-8")
    (next_root / "auto_round_report.json").write_text(
        json.dumps(payload, indent=2, ensure_ascii=True), encoding="utf-8"
    )


def launch_next(
    model_training_dir: Path,
    run_id: str,
    configs: list[str],
    k_values: str,
    args: argparse.Namespace,
) -> None:
    env = os.environ.copy()
    env.update(
        {
            "V8_PHASEB_CONFIGS": ";".join(configs),
            "V8_PHASEB_EPOCHS": str(args.epochs),
            "V8_PHASEB_WARMUP_EPOCHS": str(args.warmup_epochs),
            "V8_PHASEB_PROTOTYPE_SOURCES": args.prototype_sources,
            "V8_PHASEB_K_VALUES": k_values,
            "V8_PHASEB_QUANT_SCALES": args.quant_scales,
        }
    )
    subprocess.run(
        [str(model_training_dir / LAUNCHER), run_id],
        cwd=model_training_dir,
        env=env,
        check=True,
    )


def main() -> int:
    args = parse_args()
    model_training_dir = Path(__file__).resolve().parent
    experiments_dir = args.experiments_dir
    if not experiments_dir.is_absolute():
        experiments_dir = model_training_dir / experiments_dir

    previous_root = (
        experiments_dir / args.round_id
        if args.round_id
        else find_latest_complete_round(experiments_dir, args.stage)
    )
    rows = collect_rows(previous_root, experiments_dir, args.stage)
    all_rows = collect_all_focus_rows(experiments_dir, args.stage)
    if not rows:
        raise SystemExit(f"No summary rows found in {previous_root / args.stage}.")

    next_run_id = args.next_run_id or "v8auto_" + datetime.now().strftime("%Y%m%d_%H%M%S")
    next_configs, k_values, rationale = choose_next(rows, all_rows, args.jobs)
    next_root = experiments_dir / next_run_id

    active_sessions = active_gpu_sessions()
    can_launch = not args.no_launch and not args.dry_run
    if active_sessions and not args.allow_active_gpu_sessions:
        can_launch = False
        rationale += f" Launch held because active GPU sessions exist: {','.join(active_sessions)}."

    report = make_report(
        previous_root=previous_root,
        rows=rows,
        all_rows=all_rows,
        next_configs=next_configs,
        k_values=k_values,
        next_run_id=next_run_id,
        rationale=rationale,
        launched=can_launch,
    )
    payload = {
        "previous_round": previous_root.name,
        "next_run_id": next_run_id,
        "next_configs": next_configs,
        "k_values": k_values,
        "prototype_sources": args.prototype_sources,
        "quant_scales": args.quant_scales,
        "rationale": rationale,
        "active_gpu_sessions": active_sessions,
        "launched": can_launch,
    }
    write_report_files(next_root, report, payload)
    print(report, end="")

    if can_launch:
        launch_next(model_training_dir, next_run_id, next_configs, k_values, args)
    elif args.dry_run:
        print("dry_run=true; not launching.")
    elif args.no_launch:
        print("no_launch=true; not launching.")
    else:
        print("launch_skipped=true; active GPU sessions detected.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
