#!/usr/bin/env bash
set -euo pipefail

RUN_ID="${1:-v7_expert_teacher_round1_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
TEACHER_DIR="${ROOT}/teacher_bundles"
STABLE_STAGE="${V7_STABLE_STAGE:-stage1_stable_teacher}"
RESCUE_STAGE="${V7_RESCUE_STAGE:-stage1_rescue_teacher}"
STABLE_DIR="${ROOT}/${STABLE_STAGE}"
RESCUE_DIR="${ROOT}/${RESCUE_STAGE}"
STABLE_CANDIDATES="${STABLE_DIR}/stable_teacher_candidates.json"
RESCUE_CANDIDATES="${RESCUE_DIR}/rescue_teacher_candidates.json"
STABLE_TEACHER_LABELS="${TEACHER_DIR}/stable_teacher_labels.npz"
RESCUE_TEACHER_LABELS="${TEACHER_DIR}/rescue_teacher_labels.npz"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-34)"

OLD_TFLITE="${V7_OLD_TFLITE:-experiments/v6_parent100_20260515_0001/stage5_parent100_neighborhood/shard_12/artifacts/p100_head_parent_s4_integrated_084_a5024/seed_20263104/model_int8.tflite}"
RESCUE_TFLITE="${V7_RESCUE_TFLITE:-experiments/v6_ctd_round1_20260516_0001/stage6_ctd_round1/shard_10/artifacts/ctd1_head_parent_weapon_c4_box_circuit_t0.2_p100_cal_balanced_clean_s4_int/seed_20263203/model_int8.tflite}"
PARENT_CANDIDATES="${V7_PARENT_CANDIDATES:-experiments/v6_parent100_20260515_0001/stage5_parent100_neighborhood/parent100_neighborhood_candidates.json}"
CTD_CANDIDATES="${V7_CTD_CANDIDATES:-experiments/v6_ctd_round1_20260516_0001/stage6_ctd_round1/ctd_round1_candidates.json}"

STABLE_SHARDS="${V7_STABLE_SHARDS:-8}"
RESCUE_SHARDS="${V7_RESCUE_SHARDS:-8}"
STABLE_LIMIT="${V7_STABLE_LIMIT:-64}"
RESCUE_LIMIT="${V7_RESCUE_LIMIT:-80}"
MAX_BOARD_US="${V7_MAX_BOARD_US:-12000}"
STABLE_SEEDS="${V7_STABLE_SEEDS:-20263401,20263403}"
RESCUE_SEEDS="${V7_RESCUE_SEEDS:-20263411,20263413}"
EPOCHS="${V7_EPOCHS:-240}"
PATIENCE="${V7_PATIENCE:-38}"
CALIBRATION_LIMIT="${CALIBRATION_LIMIT:-304}"
STRESS_LIST="${STRESS_LIST:-rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5,cam_blur2a0,cam_blur3a90,cam_blur5a45,cam_blur5a135,cam_noise0p02,cam_noise0p04,cam_blur3a0_noise0p02,cam_blur5a45_noise0p04}"
OMP_THREADS="${OMP_THREADS:-1}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-1}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"

mkdir -p "${ROOT}" "${TEACHER_DIR}" "${STABLE_DIR}" "${RESCUE_DIR}"
printf '%s\n' "${RUN_ID}" > experiments/v7_active_run.txt

for required in "${OLD_TFLITE}" "${RESCUE_TFLITE}" "${PARENT_CANDIDATES}" "${CTD_CANDIDATES}"; do
  if [[ ! -f "${required}" ]]; then
    echo "missing required file: ${required}" >&2
    exit 2
  fi
done

if [[ ! -f "${STABLE_TEACHER_LABELS}" || ! -f "${RESCUE_TEACHER_LABELS}" ]]; then
  venv/bin/python build_v7_expert_teacher_labels.py \
    --dataset-dir dataset \
    --old-tflite "${OLD_TFLITE}" \
    --rescue-tflite "${RESCUE_TFLITE}" \
    --output-dir "${TEACHER_DIR}" \
    --temperature 1.0 \
    2>&1 | tee -a "${ROOT}/build_expert_teacher_labels.log"
fi

if [[ ! -f "${STABLE_CANDIDATES}" || ! -f "${RESCUE_CANDIDATES}" ]]; then
  venv/bin/python generate_v7_expert_teacher_candidates.py \
    --parent-candidates "${PARENT_CANDIDATES}" \
    --ctd-candidates "${CTD_CANDIDATES}" \
    --output-dir "${ROOT}/candidate_sets" \
    --stable-limit "${STABLE_LIMIT}" \
    --rescue-limit "${RESCUE_LIMIT}" \
    --max-board-us "${MAX_BOARD_US}" \
    --seed 20263400 \
    2>&1 | tee -a "${ROOT}/generate_expert_candidates.log"
  cp "${ROOT}/candidate_sets/stable_teacher_candidates.json" "${STABLE_CANDIDATES}"
  cp "${ROOT}/candidate_sets/rescue_teacher_candidates.json" "${RESCUE_CANDIDATES}"
fi

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "stable_stage": "${STABLE_STAGE}",
  "rescue_stage": "${RESCUE_STAGE}",
  "stable_dir": "${STABLE_DIR}",
  "rescue_dir": "${RESCUE_DIR}",
  "old_tflite": "${OLD_TFLITE}",
  "rescue_tflite": "${RESCUE_TFLITE}",
  "stable_teacher_labels": "${STABLE_TEACHER_LABELS}",
  "rescue_teacher_labels": "${RESCUE_TEACHER_LABELS}",
  "stable_candidates": "${STABLE_CANDIDATES}",
  "rescue_candidates": "${RESCUE_CANDIDATES}",
  "stable_shards": ${STABLE_SHARDS},
  "rescue_shards": ${RESCUE_SHARDS},
  "stable_limit": ${STABLE_LIMIT},
  "rescue_limit": ${RESCUE_LIMIT},
  "max_board_us": ${MAX_BOARD_US},
  "stable_seeds": "${STABLE_SEEDS}",
  "rescue_seeds": "${RESCUE_SEEDS}",
  "epochs": ${EPOCHS},
  "patience": ${PATIENCE},
  "calibration_limit": ${CALIBRATION_LIMIT},
  "stress_list": "${STRESS_LIST}",
  "note": "V7 stage 1 trains stable and rescue experts before MoE/gate fusion; no foreground watcher"
}
EOF

launch_stage() {
  local role="$1"
  local stage_dir="$2"
  local candidates="$3"
  local teacher_labels="$4"
  local shards="$5"
  local limit="$6"
  local seeds="$7"
  local prefix="$8"

  for i in $(seq 0 $((shards - 1))); do
    local out_dir="${stage_dir}/shard_${i}"
    local session="${prefix}_${RUN_TAG}_${i}"
    mkdir -p "${out_dir}"
    if tmux has-session -t "${session}" 2>/dev/null; then
      echo "exists ${session}"
      continue
    fi
    local cmd="cd /home/madejuele/projects/2K0300/model_training && OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} ./run_gpu.sh venv/bin/python train_tiny32_v5_visual_subclass_scan.py --mode fine --lane all --dataset-dir dataset --output-dir ${out_dir} --shard-count ${shards} --shard-index ${i} --seeds ${seeds} --epochs ${EPOCHS} --patience ${PATIENCE} --stress ${STRESS_LIST} --calibration-limit ${CALIBRATION_LIMIT} --resume --candidates-json ${candidates} --max-trials ${limit} --correct-teacher-labels ${teacher_labels} 2>&1 | tee -a ${out_dir}/run.log"
    tmux new-session -d -s "${session}" "${cmd}"
    echo "launched ${session} role=${role}"
  done
}

launch_stage "stable" "${STABLE_DIR}" "${STABLE_CANDIDATES}" "${STABLE_TEACHER_LABELS}" "${STABLE_SHARDS}" "${STABLE_LIMIT}" "${STABLE_SEEDS}" "v7stable1"
launch_stage "rescue" "${RESCUE_DIR}" "${RESCUE_CANDIDATES}" "${RESCUE_TEACHER_LABELS}" "${RESCUE_SHARDS}" "${RESCUE_LIMIT}" "${RESCUE_SEEDS}" "v7rescue1"

echo "v7_expert_teacher_round1 root=${ROOT} stable=${STABLE_DIR} rescue=${RESCUE_DIR} shards=$((STABLE_SHARDS + RESCUE_SHARDS))"
