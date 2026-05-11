#!/usr/bin/env bash
set -euo pipefail

RUN_ID="${1:-$(cat experiments/v5_active_run.txt)}"
ROOT="experiments/${RUN_ID}"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-44)"
SUPERVISOR_SESSION="v5super_${RUN_TAG}"

MAX_SESSIONS="${V5_MAX_SESSIONS:-10}"
# The watcher is intentionally quiet: no GPU/memory polling, no log scanning, and
# a long sleep between resume checks. Training sessions do the heavy work.
POLL_SECONDS="${V5_POLL_SECONDS:-900}"
OMP_THREADS="${OMP_THREADS:-1}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-1}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"
CALIBRATION_LIMIT="${CALIBRATION_LIMIT:-192}"
STRESS_LIST="${STRESS_LIST:-rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5}"

STAGE2_SHARDS="${STAGE2_SHARDS:-8}"
FINE_SHARDS="${FINE_SHARDS:-10}"
STAGE2_TOP_K="${STAGE2_TOP_K:-32}"
FINE1_TOP_K="${FINE1_TOP_K:-8}"
FINE1_LIMIT="${FINE1_LIMIT:-360}"
FINE2_TOP_K="${FINE2_TOP_K:-5}"
FINE2_LIMIT="${FINE2_LIMIT:-260}"
FINE3_TOP_K="${FINE3_TOP_K:-4}"
FINE3_LIMIT="${FINE3_LIMIT:-180}"
FINAL_TOP_K="${FINAL_TOP_K:-5}"
FINAL_SELECT_K="${FINAL_SELECT_K:-10}"

mkdir -p "${ROOT}"

log() {
  printf '[%(%F %T)T] %s\n' -1 "$*"
}

write_watcher_state() {
  local stage="$1"
  local detail="$2"
  cat > "${ROOT}/watcher_state.json" <<JSON
{"time":"$(date +%FT%T%z)","run_id":"${RUN_ID}","stage":"${stage}","detail":"${detail}","max_sessions":${MAX_SESSIONS},"poll_seconds":${POLL_SECONDS}}
JSON
}

count_json_candidates() {
  venv/bin/python - "$1" <<'PY'
import json, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as handle:
    data = json.load(handle)
print(len(data.get("candidates", data if isinstance(data, list) else [])))
PY
}

count_results() {
  local pattern="$1"
  local files=()
  while IFS= read -r -d '' path; do
    files+=("${path}")
  done < <(find ${pattern} -name trial_results.jsonl -print0 2>/dev/null || true)
  if [[ "${#files[@]}" -eq 0 ]]; then
    echo 0
    return
  fi
  cat "${files[@]}" 2>/dev/null | wc -l
}

active_group_sessions() {
  tmux list-sessions 2>/dev/null | rg -c "^v5_${RUN_ID}_(fast|balance|accuracy|hardquant|fast_extra|balance_extra|accuracy_extra|all_bonus|stage2|fine1|fine2|fine3)_" || true
}

group_alive() {
  local group="$1"
  tmux list-sessions 2>/dev/null | rg -c "^v5_${RUN_ID}_${group}_" || true
}

run_train_cmd() {
  local mode="$1"
  local lane="$2"
  local out_dir="$3"
  local seeds="$4"
  local epochs="$5"
  local patience="$6"
  local shard_count="$7"
  local shard_index="$8"
  local extra_args="$9"
  printf 'cd /home/madejuele/projects/2K0300/model_training && OMP_NUM_THREADS=%s TF_NUM_INTRAOP_THREADS=%s TF_NUM_INTEROP_THREADS=%s ./run_gpu.sh venv/bin/python train_tiny32_v5_visual_subclass_scan.py --mode %s --lane %s --dataset-dir dataset --output-dir %s --shard-count %s --shard-index %s --seeds %s --epochs %s --patience %s --stress %s --calibration-limit %s --resume %s 2>&1 | tee -a %s/run.log' \
    "${OMP_THREADS}" "${TF_INTRA_THREADS}" "${TF_INTER_THREADS}" \
    "${mode}" "${lane}" "${out_dir}" "${shard_count}" "${shard_index}" \
    "${seeds}" "${epochs}" "${patience}" "${STRESS_LIST}" "${CALIBRATION_LIMIT}" \
    "${extra_args}" "${out_dir}"
}

launch_stage1_group() {
  local group="$1"
  local lane="$2"
  local max_trials="$3"
  local shards="$4"
  local seeds="$5"
  local epochs="$6"
  local patience="$7"
  local aggressive="$8"
  local candidates_json="$9"
  local out_base="${ROOT}/stage1_${group}"
  mkdir -p "${out_base}"
  for i in $(seq 0 $((shards - 1))); do
    local out_dir="${out_base}/shard_${i}"
    local session="v5_${RUN_ID}_${group}_${i}"
    mkdir -p "${out_dir}"
    if tmux has-session -t "${session}" 2>/dev/null; then
      continue
    fi
    local extra="--max-trials ${max_trials}"
    if [[ "${aggressive}" == "yes" ]]; then
      extra="${extra} --aggressive"
    fi
    if [[ -n "${candidates_json}" ]]; then
      extra="${extra} --candidates-json ${candidates_json}"
    fi
    local cmd
    cmd="$(run_train_cmd coarse "${lane}" "${out_dir}" "${seeds}" "${epochs}" "${patience}" "${shards}" "${i}" "${extra}")"
    tmux new-session -d -s "${session}" "${cmd}"
    log "launched ${session}"
  done
}

ensure_stage1_complete() {
  local names=(fast balance accuracy hardquant fast_extra balance_extra accuracy_extra all_bonus)
  local lanes=(fast balance accuracy accuracy fast balance accuracy all)
  local limits=(200 320 240 999 999 999 999 240)
  local expected=(200 320 240 160 160 240 120 240)
  local seeds=(20261701,20261702 20261801,20261802 20261901,20261902 20262001,20262002 20262201,20262202 20262101,20262102 20262301,20262302 20262501,20262502)
  local epochs=(140 180 200 200 140 180 200 190)
  local patience=(18 24 28 28 18 24 28 26)
  local aggressive=(yes yes yes no no no no yes)
  local candidates=("" "" "" "${ROOT}/stage1_hardquant/hardquant_candidates.json" "${ROOT}/stage1_fast_extra/fast_extra_candidates.json" "${ROOT}/stage1_balance_extra/balance_extra_candidates.json" "${ROOT}/stage1_accuracy_extra/accuracy_extra_candidates.json" "")

  while true; do
    local all_done=1
    local active
    active="$(active_group_sessions)"
    local status=()
    for idx in "${!names[@]}"; do
      local name="${names[$idx]}"
      local got
      got="$(count_results "${ROOT}/stage1_${name}/shard_*")"
      status+=("${name}=${got}/${expected[$idx]}")
      if [[ "${got}" -lt "${expected[$idx]}" ]]; then
        all_done=0
        if [[ "$(group_alive "${name}")" -eq 0 && "${active}" -lt "${MAX_SESSIONS}" ]]; then
          if [[ -n "${candidates[$idx]}" && ! -f "${candidates[$idx]}" ]]; then
            log "missing candidates for ${name}: ${candidates[$idx]}"
          elif [[ $((active + 2)) -le "${MAX_SESSIONS}" ]]; then
            launch_stage1_group "${name}" "${lanes[$idx]}" "${limits[$idx]}" 2 "${seeds[$idx]}" "${epochs[$idx]}" "${patience[$idx]}" "${aggressive[$idx]}" "${candidates[$idx]}"
            active=$((active + 2))
          fi
        fi
      fi
    done
    log "stage1 active=${active} ${status[*]}"
    write_watcher_state "stage1" "active=${active} ${status[*]}"
    if [[ "${all_done}" -eq 1 && "${active}" -eq 0 ]]; then
      break
    fi
    sleep "${POLL_SECONDS}"
  done
}

launch_eval_shards() {
  local stage="$1"
  local mode="$2"
  local candidates_json="$3"
  local out_base="$4"
  local shards="$5"
  local seeds="$6"
  local epochs="$7"
  local patience="$8"
  local lane="${9:-all}"
  mkdir -p "${out_base}"
  local count
  count="$(count_json_candidates "${candidates_json}")"
  for i in $(seq 0 $((shards - 1))); do
    local out_dir="${out_base}/shard_${i}"
    local session="v5_${RUN_ID}_${stage}_${i}"
    mkdir -p "${out_dir}"
    if tmux has-session -t "${session}" 2>/dev/null; then
      continue
    fi
    local extra="--candidates-json ${candidates_json} --max-trials ${count}"
    local cmd
    cmd="$(run_train_cmd "${mode}" "${lane}" "${out_dir}" "${seeds}" "${epochs}" "${patience}" "${shards}" "${i}" "${extra}")"
    tmux new-session -d -s "${session}" "${cmd}"
    log "launched ${session}"
  done
}

wait_eval_stage() {
  local stage="$1"
  local out_base="$2"
  local expected="$3"
  while true; do
    local alive
    alive="$(group_alive "${stage}")"
    local done_count
    done_count="$(count_results "${out_base}/shard_*")"
    log "${stage}: done=${done_count}/${expected} alive=${alive}"
    write_watcher_state "${stage}" "done=${done_count}/${expected} alive=${alive}"
    if [[ "${done_count}" -ge "${expected}" ]]; then
      break
    fi
    if [[ "${alive}" -eq 0 ]]; then
      log "${stage} stopped before expected results; relaunching handled by caller"
      return 1
    fi
    sleep "${POLL_SECONDS}"
  done
}

run_sharded_stage() {
  local stage="$1"
  local mode="$2"
  local candidates_json="$3"
  local out_base="$4"
  local shards="$5"
  local seeds="$6"
  local epochs="$7"
  local patience="$8"
  local lane="${9:-all}"
  local expected
  expected="$(count_json_candidates "${candidates_json}")"
  while true; do
    launch_eval_shards "${stage}" "${mode}" "${candidates_json}" "${out_base}" "${shards}" "${seeds}" "${epochs}" "${patience}" "${lane}"
    if wait_eval_stage "${stage}" "${out_base}" "${expected}"; then
      break
    fi
  done
}

summarize_inputs() {
  local output="$1"
  shift
  local args=()
  for path in "$@"; do
    args+=(--input "${path}")
  done
  venv/bin/python generate_v5_visual_candidates.py summarize "${args[@]}" --output "${output}" --print-top 40
}

best_score() {
  venv/bin/python - "$1" <<'PY'
import json, sys
with open(sys.argv[1], encoding="utf-8") as handle:
    data = json.load(handle)
best = data.get("best") or {}
print(float(best.get("score_min", best.get("score_mean", 0.0)) or 0.0))
PY
}

log "run_id=${RUN_ID} root=${ROOT} max_sessions=${MAX_SESSIONS}"
ensure_stage1_complete

summarize_inputs "${ROOT}/stage1_merged_summary.json" "${ROOT}"
venv/bin/python generate_v5_visual_candidates.py select \
  --input "${ROOT}/stage1_merged_summary.json" \
  --output "${ROOT}/stage2_selected.json" \
  --top-k "${STAGE2_TOP_K}" \
  --per-lane 5 \
  --axis-k 3 \
  --force-anchors

run_sharded_stage stage2 retest "${ROOT}/stage2_selected.json" "${ROOT}/stage2_retest" "${STAGE2_SHARDS}" \
  20262401,20262402,20262403,20262404,20262405 230 34 all
summarize_inputs "${ROOT}/stage2_retest_summary.json" "${ROOT}/stage2_retest"

venv/bin/python generate_v5_visual_candidates.py fine \
  --input "${ROOT}/stage2_retest_summary.json" \
  --output "${ROOT}/stage3_fine1_candidates.json" \
  --top-k "${FINE1_TOP_K}" \
  --limit "${FINE1_LIMIT}"
run_sharded_stage fine1 fine "${ROOT}/stage3_fine1_candidates.json" "${ROOT}/stage3_fine1" "${FINE_SHARDS}" \
  20263201,20263202,20263203 250 38 all
summarize_inputs "${ROOT}/stage3_fine1_summary.json" "${ROOT}/stage3_fine1"
summarize_inputs "${ROOT}/stage3_combined_summary.json" "${ROOT}/stage2_retest" "${ROOT}/stage3_fine1"
score_before="$(best_score "${ROOT}/stage2_retest_summary.json")"
score_after="$(best_score "${ROOT}/stage3_combined_summary.json")"
log "fine1 best_score_before=${score_before} combined=${score_after}"

venv/bin/python generate_v5_visual_candidates.py fine \
  --input "${ROOT}/stage3_combined_summary.json" \
  --output "${ROOT}/stage4_fine2_candidates.json" \
  --top-k "${FINE2_TOP_K}" \
  --limit "${FINE2_LIMIT}"
run_sharded_stage fine2 fine "${ROOT}/stage4_fine2_candidates.json" "${ROOT}/stage4_fine2" "${FINE_SHARDS}" \
  20263301,20263302,20263303,20263304 270 42 all
summarize_inputs "${ROOT}/stage4_fine2_summary.json" "${ROOT}/stage4_fine2"
summarize_inputs "${ROOT}/stage4_combined_summary.json" "${ROOT}/stage2_retest" "${ROOT}/stage3_fine1" "${ROOT}/stage4_fine2"
score_before="$(best_score "${ROOT}/stage3_combined_summary.json")"
score_after="$(best_score "${ROOT}/stage4_combined_summary.json")"
log "fine2 best_score_before=${score_before} combined=${score_after}"

if venv/bin/python - "${score_before}" "${score_after}" <<'PY'
import sys
sys.exit(0 if float(sys.argv[2]) > float(sys.argv[1]) + 0.001 else 1)
PY
then
  venv/bin/python generate_v5_visual_candidates.py fine \
    --input "${ROOT}/stage4_combined_summary.json" \
    --output "${ROOT}/stage5_fine3_candidates.json" \
    --top-k "${FINE3_TOP_K}" \
    --limit "${FINE3_LIMIT}"
  run_sharded_stage fine3 fine "${ROOT}/stage5_fine3_candidates.json" "${ROOT}/stage5_fine3" "${FINE_SHARDS}" \
    20263401,20263402,20263403,20263404 290 44 all
  summarize_inputs "${ROOT}/stage5_fine3_summary.json" "${ROOT}/stage5_fine3"
  summarize_inputs "${ROOT}/stage5_combined_summary.json" "${ROOT}/stage2_retest" "${ROOT}/stage3_fine1" "${ROOT}/stage4_fine2" "${ROOT}/stage5_fine3"
  FINAL_SOURCE="${ROOT}/stage5_combined_summary.json"
else
  FINAL_SOURCE="${ROOT}/stage4_combined_summary.json"
fi

venv/bin/python generate_v5_visual_candidates.py select \
  --input "${FINAL_SOURCE}" \
  --output "${ROOT}/final_selected.json" \
  --top-k "${FINAL_SELECT_K}" \
  --per-lane 2 \
  --axis-k 1

FINAL_COUNT="$(count_json_candidates "${ROOT}/final_selected.json")"
FINAL_OUT="${ROOT}/final_full_retest"
mkdir -p "${FINAL_OUT}"
OMP_NUM_THREADS=3 TF_NUM_INTRAOP_THREADS=3 TF_NUM_INTEROP_THREADS=2 ./run_gpu.sh venv/bin/python train_tiny32_v5_visual_subclass_scan.py \
  --mode final \
  --lane all \
  --dataset-dir dataset \
  --output-dir "${FINAL_OUT}" \
  --candidates-json "${ROOT}/final_selected.json" \
  --max-trials "${FINAL_COUNT}" \
  --shard-count 1 \
  --shard-index 0 \
  --seeds 20264101,20264102,20264103,20264104,20264105 \
  --epochs 320 \
  --patience 48 \
  --final-top-k "${FINAL_TOP_K}" \
  --full-epochs 340 \
  --stress "${STRESS_LIST}" \
  --calibration-limit 224 \
  --resume \
  2>&1 | tee -a "${FINAL_OUT}/run.log"

summarize_inputs "${ROOT}/final_retest_summary.json" "${FINAL_OUT}"
log "complete root=${ROOT} final=${FINAL_OUT}"
