#!/usr/bin/env bash
set -euo pipefail

RUN_ID="${1:-v6_parent100_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
RETEST_STAGE="${V6_RETEST_STAGE:-stage5_parent100_retest}"
NEIGHBOR_STAGE="${V6_NEIGHBOR_STAGE:-stage5_parent100_neighborhood}"
RETEST_DIR="${ROOT}/${RETEST_STAGE}"
NEIGHBOR_DIR="${ROOT}/${NEIGHBOR_STAGE}"
RETEST_JSON="${RETEST_DIR}/parent100_retest_candidates.json"
NEIGHBOR_JSON="${NEIGHBOR_DIR}/parent100_neighborhood_candidates.json"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-34)"

RETEST_SHARDS="${V6_RETEST_SHARDS:-8}"
NEIGHBOR_SHARDS="${V6_NEIGHBOR_SHARDS:-16}"
RETEST_LIMIT="${V6_RETEST_LIMIT:-28}"
NEIGHBOR_LIMIT="${V6_NEIGHBOR_LIMIT:-384}"
ANCHOR_LIMIT="${V6_ANCHOR_LIMIT:-28}"
RETEST_SEEDS="${V6_RETEST_SEEDS:-20263104,20263105,20263106,20263107,20263108,20263109,20263110,20263111}"
NEIGHBOR_SEEDS="${V6_NEIGHBOR_SEEDS:-20263104,20263106}"
RETEST_EPOCHS="${V6_RETEST_EPOCHS:-240}"
NEIGHBOR_EPOCHS="${V6_NEIGHBOR_EPOCHS:-240}"
PATIENCE="${V6_PATIENCE:-38}"
CALIBRATION_LIMIT="${CALIBRATION_LIMIT:-304}"
MAX_BOARD_US="${V6_MAX_BOARD_US:-18000}"
TEACHER_TFLITE="${V6_TEACHER_TFLITE:-experiments/v4_stress_directed_20260508_215328/final/shard_2/runs/sd097_seed20261501/sixclass_best_mild_int8.tflite}"
STRESS_LIST="${STRESS_LIST:-rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5,cam_blur2a0,cam_blur3a90,cam_blur5a45,cam_blur5a135,cam_noise0p02,cam_noise0p04,cam_blur3a0_noise0p02,cam_blur5a45_noise0p04}"
OMP_THREADS="${OMP_THREADS:-1}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-1}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"

mkdir -p "${RETEST_DIR}" "${NEIGHBOR_DIR}"
printf '%s\n' "${RUN_ID}" > experiments/v6_active_run.txt

if [[ ! -f "${TEACHER_TFLITE}" ]]; then
  echo "missing teacher tflite: ${TEACHER_TFLITE}" >&2
  exit 2
fi

if [[ ! -f "${RETEST_JSON}" || ! -f "${NEIGHBOR_JSON}" ]]; then
  venv/bin/python generate_v6_parent100_candidates.py \
    --output-retest "${RETEST_JSON}" \
    --output-neighborhood "${NEIGHBOR_JSON}" \
    --retest-limit "${RETEST_LIMIT}" \
    --neighborhood-limit "${NEIGHBOR_LIMIT}" \
    --anchor-limit "${ANCHOR_LIMIT}" \
    --max-board-us "${MAX_BOARD_US}" \
    --seed 20263150 \
    2>&1 | tee -a "${ROOT}/generate_parent100.log"
fi

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "retest_stage": "${RETEST_STAGE}",
  "neighborhood_stage": "${NEIGHBOR_STAGE}",
  "retest_shards": ${RETEST_SHARDS},
  "neighborhood_shards": ${NEIGHBOR_SHARDS},
  "retest_limit": ${RETEST_LIMIT},
  "neighborhood_limit": ${NEIGHBOR_LIMIT},
  "retest_seeds": "${RETEST_SEEDS}",
  "neighborhood_seeds": "${NEIGHBOR_SEEDS}",
  "retest_epochs": ${RETEST_EPOCHS},
  "neighborhood_epochs": ${NEIGHBOR_EPOCHS},
  "patience": ${PATIENCE},
  "calibration_limit": ${CALIBRATION_LIMIT},
  "max_board_us": ${MAX_BOARD_US},
  "teacher_tflite": "${TEACHER_TFLITE}",
  "stress_list": "${STRESS_LIST}",
  "note": "parent-100 retest plus local neighborhood sweep; no continuous watcher"
}
EOF

launch_group() {
  local stage_name="$1"
  local out_base="$2"
  local candidates_json="$3"
  local shards="$4"
  local limit="$5"
  local seeds="$6"
  local epochs="$7"
  local mode="$8"
  local prefix="$9"

  for i in $(seq 0 $((shards - 1))); do
    local out_dir="${out_base}/shard_${i}"
    local session="${prefix}_${RUN_TAG}_${i}"
    mkdir -p "${out_dir}"
    if tmux has-session -t "${session}" 2>/dev/null; then
      echo "exists ${session}"
      continue
    fi
    local cmd="cd /home/madejuele/projects/2K0300/model_training && OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} ./run_gpu.sh venv/bin/python train_tiny32_v5_visual_subclass_scan.py --mode ${mode} --lane all --dataset-dir dataset --output-dir ${out_dir} --shard-count ${shards} --shard-index ${i} --seeds ${seeds} --epochs ${epochs} --patience ${PATIENCE} --stress ${STRESS_LIST} --calibration-limit ${CALIBRATION_LIMIT} --resume --candidates-json ${candidates_json} --max-trials ${limit} --teacher-tflite ${TEACHER_TFLITE} 2>&1 | tee -a ${out_dir}/run.log"
    tmux new-session -d -s "${session}" "${cmd}"
    echo "launched ${session} stage=${stage_name}"
  done
}

launch_group "${RETEST_STAGE}" "${RETEST_DIR}" "${RETEST_JSON}" "${RETEST_SHARDS}" "${RETEST_LIMIT}" "${RETEST_SEEDS}" "${RETEST_EPOCHS}" "retest" "v6p100r"
launch_group "${NEIGHBOR_STAGE}" "${NEIGHBOR_DIR}" "${NEIGHBOR_JSON}" "${NEIGHBOR_SHARDS}" "${NEIGHBOR_LIMIT}" "${NEIGHBOR_SEEDS}" "${NEIGHBOR_EPOCHS}" "fine" "v6p100n"

echo "v6_parent100 root=${ROOT} retest=${RETEST_DIR} neighborhood=${NEIGHBOR_DIR} total_shards=$((RETEST_SHARDS + NEIGHBOR_SHARDS))"
