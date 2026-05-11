#!/usr/bin/env bash
set -euo pipefail

LOG="/home/madejuele/projects/2K0300/model_training/experiments/v3_fine_balanced_20260507_081935/memory_guard.log"
THRESHOLD_KB="${1:-700000}"

mkdir -p "$(dirname "${LOG}")"
echo "guard_started=$(date -Is) threshold_kB=${THRESHOLD_KB}" >> "${LOG}"

while tmux has-session -t v3fine_balanced 2>/dev/null; do
  avail="$(awk '/MemAvailable:/ {print $2}' /proc/meminfo)"
  swap_free="$(awk '/SwapFree:/ {print $2}' /proc/meminfo)"
  echo "sample $(date -Is) MemAvailable_kB=${avail} SwapFree_kB=${swap_free}" >> "${LOG}"
  if [[ "${avail}" -lt "${THRESHOLD_KB}" ]]; then
    echo "killing_balanced_low_memory $(date -Is) MemAvailable_kB=${avail}" >> "${LOG}"
    tmux kill-session -t v3fine_balanced 2>/dev/null || true
    break
  fi
  sleep 30
done

echo "guard_done=$(date -Is)" >> "${LOG}"
