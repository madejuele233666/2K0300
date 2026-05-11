#!/usr/bin/env bash
set -euo pipefail

RUN_ID="${1:-v4_stability_fine_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-40)"

STAGE1_SHARDS="${STAGE1_SHARDS:-6}"
STAGE2_SHARDS="${STAGE2_SHARDS:-6}"
FINAL_SHARDS="${FINAL_SHARDS:-3}"
STAGE1_LIMIT="${STAGE1_LIMIT:-120}"
STAGE2_TOP_K="${STAGE2_TOP_K:-12}"
FINAL_TOP_K="${FINAL_TOP_K:-3}"
STAGE1_SEEDS="${STAGE1_SEEDS:-20261001,20261002}"
STAGE2_SEEDS="${STAGE2_SEEDS:-20261101,20261102,20261103,20261104,20261105}"
FINAL_SEEDS="${FINAL_SEEDS:-20261201}"

mkdir -p "${ROOT}/stage1" "${ROOT}/stage2" "${ROOT}/final"

count_json_candidates() {
  venv/bin/python - "$1" <<'PY'
import json, sys
print(len(json.loads(open(sys.argv[1], encoding="utf-8").read()).get("candidates", [])))
PY
}

seed_count() {
  venv/bin/python - "$1" <<'PY'
import sys
print(len([part for part in sys.argv[1].split(",") if part.strip()]))
PY
}

count_results() {
  local pattern="$1"
  find ${pattern} -name retest_results.jsonl -print0 2>/dev/null | xargs -0 cat 2>/dev/null | wc -l
}

launch_shards() {
  local stage="$1"
  local candidates_json="$2"
  local out_base="$3"
  local shards="$4"
  local seeds="$5"
  local final_epochs="$6"
  local final_patience="$7"
  local with_full="$8"
  local candidate_count
  candidate_count="$(count_json_candidates "${candidates_json}")"
  mkdir -p "${out_base}"
  for i in $(seq 0 $((shards - 1))); do
    local out_dir="${out_base}/shard_${i}"
    local session="v4stab_${RUN_TAG}_${stage}_${i}"
    local extra=""
    if [[ "${with_full}" == "yes" ]]; then
      extra="--with-full"
    fi
    mkdir -p "${out_dir}"
    if tmux has-session -t "${session}" 2>/dev/null; then
      echo "[${stage}] session exists: ${session}"
      continue
    fi
    tmux new-session -d -s "${session}" \
      "cd /home/madejuele/projects/2K0300/model_training && ./run_gpu.sh venv/bin/python retest_tiny32_v4_candidates.py --candidates-json ${candidates_json} --candidate-limit ${candidate_count} --output-dir ${out_dir} --seeds ${seeds} --shard-count ${shards} --shard-index ${i} --final-epochs ${final_epochs} --final-patience ${final_patience} --calibration-limit 192 --speed-weight 0.14 --resume ${extra} 2>&1 | tee ${out_dir}/run.log"
    echo "[${stage}] launched ${session}"
  done
}

wait_shards() {
  local stage="$1"
  local out_base="$2"
  local shards="$3"
  local expected="$4"
  while true; do
    local alive=0
    for i in $(seq 0 $((shards - 1))); do
      if tmux has-session -t "v4stab_${RUN_TAG}_${stage}_${i}" 2>/dev/null; then
        alive=$((alive + 1))
      fi
    done
    local done_count
    done_count="$(count_results "${out_base}/shard_*")"
    echo "[$(date +%H:%M:%S)] ${stage}: done=${done_count}/${expected} alive=${alive}"
    if [[ "${alive}" -eq 0 ]]; then
      if [[ "${done_count}" -ne "${expected}" ]]; then
        echo "${stage} incomplete: expected ${expected}, got ${done_count}" >&2
        for d in "${out_base}"/shard_*; do
          [[ -d "${d}" ]] || continue
          echo "== ${d} =="
          tail -n 40 "${d}/run.log" 2>/dev/null || true
        done
        exit 1
      fi
      break
    fi
    sleep 60
  done
}

echo "run_id=${RUN_ID}"
echo "root=${ROOT}"

STAGE1_CANDIDATES="${ROOT}/stage1_candidates.json"
if [[ ! -f "${STAGE1_CANDIDATES}" ]]; then
  venv/bin/python generate_v4_stability_candidates.py generate \
    --output "${STAGE1_CANDIDATES}" \
    --limit "${STAGE1_LIMIT}" \
    --seed 20261000
fi
STAGE1_CAND_COUNT="$(count_json_candidates "${STAGE1_CANDIDATES}")"
STAGE1_EXPECTED=$((STAGE1_CAND_COUNT * $(seed_count "${STAGE1_SEEDS}")))
launch_shards "s1" "${STAGE1_CANDIDATES}" "${ROOT}/stage1" "${STAGE1_SHARDS}" "${STAGE1_SEEDS}" 280 36 no
wait_shards "s1" "${ROOT}/stage1" "${STAGE1_SHARDS}" "${STAGE1_EXPECTED}"

venv/bin/python summarize_v4_retest_results.py \
  --glob "${ROOT}/stage1/shard_*/retest_results.jsonl" \
  --output-prefix "${ROOT}/stage1_summary"

STAGE2_CANDIDATES="${ROOT}/stage2_candidates.json"
venv/bin/python generate_v4_stability_candidates.py select \
  --summary "${ROOT}/stage1_summary_summary.json" \
  --candidates "${STAGE1_CANDIDATES}" \
  --output "${STAGE2_CANDIDATES}" \
  --top-k "${STAGE2_TOP_K}" \
  --force-base

STAGE2_CAND_COUNT="$(count_json_candidates "${STAGE2_CANDIDATES}")"
STAGE2_EXPECTED=$((STAGE2_CAND_COUNT * $(seed_count "${STAGE2_SEEDS}")))
launch_shards "s2" "${STAGE2_CANDIDATES}" "${ROOT}/stage2" "${STAGE2_SHARDS}" "${STAGE2_SEEDS}" 320 42 no
wait_shards "s2" "${ROOT}/stage2" "${STAGE2_SHARDS}" "${STAGE2_EXPECTED}"

venv/bin/python summarize_v4_retest_results.py \
  --glob "${ROOT}/stage2/shard_*/retest_results.jsonl" \
  --output-prefix "${ROOT}/stage2_summary"

FINAL_CANDIDATES="${ROOT}/final_candidates.json"
venv/bin/python generate_v4_stability_candidates.py select \
  --summary "${ROOT}/stage2_summary_summary.json" \
  --candidates "${STAGE2_CANDIDATES}" \
  --output "${FINAL_CANDIDATES}" \
  --top-k "${FINAL_TOP_K}"

FINAL_CAND_COUNT="$(count_json_candidates "${FINAL_CANDIDATES}")"
FINAL_EXPECTED=$((FINAL_CAND_COUNT * $(seed_count "${FINAL_SEEDS}")))
launch_shards "full" "${FINAL_CANDIDATES}" "${ROOT}/final" "${FINAL_SHARDS}" "${FINAL_SEEDS}" 340 46 yes
wait_shards "full" "${ROOT}/final" "${FINAL_SHARDS}" "${FINAL_EXPECTED}"

venv/bin/python summarize_v4_retest_results.py \
  --glob "${ROOT}/final/shard_*/retest_results.jsonl" \
  --output-prefix "${ROOT}/final_summary"

echo "complete root=${ROOT}"
