#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

OUT_DIR="experiments/v3_fine_accuracy_20260507_081935"
mkdir -p "${OUT_DIR}"
LOG="${OUT_DIR}/resume_watch.log"

echo "watcher_started=$(date -Is)" >> "${LOG}"

while tmux has-session -t v3fine_stability 2>/dev/null; do
  echo "waiting_stability $(date -Is)" >> "${LOG}"
  sleep 60
done

until awk '/MemAvailable:/ {exit ($2 > 4000000) ? 0 : 1}' /proc/meminfo; do
  echo "waiting_memory $(date -Is)" >> "${LOG}"
  sleep 60
done

echo "restarting_accuracy=$(date -Is)" >> "${LOG}"
exec ./run_gpu.sh venv/bin/python train_tiny32_v3_fine_scan.py \
  --lane accuracy \
  --max-trials 80 \
  --folds 3 \
  --epochs 160 \
  --patience 20 \
  --final-epochs 260 \
  --final-patience 36 \
  --full-epochs 0 \
  --final-top-k 5 \
  --seed 20260512 \
  --speed-weight 0.04 \
  --output-dir "${OUT_DIR}" \
  --resume
