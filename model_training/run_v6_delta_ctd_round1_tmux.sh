#!/usr/bin/env bash
set -euo pipefail

RUN_ID="${1:-v6_delta_ctd_round1_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
STAGE="${V6_DELTA_STAGE:-stage7_delta_ctd_round1}"
STAGE_DIR="${ROOT}/${STAGE}"
TEACHER_LABELS="${ROOT}/delta_teacher_labels.npz"
TEACHER_SUMMARY="${ROOT}/delta_teacher_labels_summary.csv"
CANDIDATES_JSON="${STAGE_DIR}/delta_ctd_round1_candidates.json"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-34)"

OLD_TFLITE="${V6_DELTA_OLD_TFLITE:-experiments/v6_parent100_20260515_0001/stage5_parent100_neighborhood/shard_12/artifacts/p100_head_parent_s4_integrated_084_a5024/seed_20263104/model_int8.tflite}"
RESCUE_TFLITE="${V6_DELTA_RESCUE_TFLITE:-experiments/v6_ctd_round1_20260516_0001/stage6_ctd_round1/shard_10/artifacts/ctd1_head_parent_weapon_c4_box_circuit_t0.2_p100_cal_balanced_clean_s4_int/seed_20263203/model_int8.tflite}"
PARENT_CANDIDATES="${V6_DELTA_PARENT_CANDIDATES:-experiments/v6_parent100_20260515_0001/stage5_parent100_neighborhood/parent100_neighborhood_candidates.json}"
CTD_CANDIDATES="${V6_DELTA_CTD_CANDIDATES:-experiments/v6_ctd_round1_20260516_0001/stage6_ctd_round1/ctd_round1_candidates.json}"

SHARDS="${V6_DELTA_SHARDS:-16}"
LIMIT="${V6_DELTA_LIMIT:-52}"
SEEDS="${V6_DELTA_SEEDS:-20263301,20263303}"
EPOCHS="${V6_DELTA_EPOCHS:-220}"
PATIENCE="${V6_DELTA_PATIENCE:-34}"
CALIBRATION_LIMIT="${CALIBRATION_LIMIT:-304}"
STRESS_LIST="${STRESS_LIST:-rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5,cam_blur2a0,cam_blur3a90,cam_blur5a45,cam_blur5a135,cam_noise0p02,cam_noise0p04,cam_blur3a0_noise0p02,cam_blur5a45_noise0p04}"
OMP_THREADS="${OMP_THREADS:-1}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-1}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"

mkdir -p "${ROOT}" "${STAGE_DIR}"
printf '%s\n' "${RUN_ID}" > experiments/v6_active_run.txt

if [[ ! -f "${OLD_TFLITE}" ]]; then
  echo "missing old tflite: ${OLD_TFLITE}" >&2
  exit 2
fi
if [[ ! -f "${RESCUE_TFLITE}" ]]; then
  echo "missing rescue tflite: ${RESCUE_TFLITE}" >&2
  exit 2
fi

if [[ ! -f "${TEACHER_LABELS}" ]]; then
  venv/bin/python build_v6_delta_teacher_labels.py \
    --dataset-dir dataset \
    --old-tflite "${OLD_TFLITE}" \
    --rescue-tflite "${RESCUE_TFLITE}" \
    --output "${TEACHER_LABELS}" \
    --summary-csv "${TEACHER_SUMMARY}" \
    2>&1 | tee -a "${ROOT}/build_delta_teacher.log"
fi

if [[ ! -f "${CANDIDATES_JSON}" ]]; then
  venv/bin/python generate_v6_delta_ctd_candidates.py \
    --parent-candidates "${PARENT_CANDIDATES}" \
    --ctd-candidates "${CTD_CANDIDATES}" \
    --output "${CANDIDATES_JSON}" \
    --limit "${LIMIT}" \
    2>&1 | tee -a "${ROOT}/generate_delta_candidates.log"
fi

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "stage": "${STAGE}",
  "stage_dir": "${STAGE_DIR}",
  "old_tflite": "${OLD_TFLITE}",
  "rescue_tflite": "${RESCUE_TFLITE}",
  "teacher_labels": "${TEACHER_LABELS}",
  "teacher_summary": "${TEACHER_SUMMARY}",
  "candidates_json": "${CANDIDATES_JSON}",
  "shards": ${SHARDS},
  "limit": ${LIMIT},
  "seeds": "${SEEDS}",
  "epochs": ${EPOCHS},
  "patience": ${PATIENCE},
  "calibration_limit": ${CALIBRATION_LIMIT},
  "stress_list": "${STRESS_LIST}",
  "note": "Delta CTD: old-best preserve plus CTD rescue teacher, no continuous watcher"
}
EOF

for i in $(seq 0 $((SHARDS - 1))); do
  OUT_DIR="${STAGE_DIR}/shard_${i}"
  SESSION="v6delta1_${RUN_TAG}_${i}"
  mkdir -p "${OUT_DIR}"
  if tmux has-session -t "${SESSION}" 2>/dev/null; then
    echo "exists ${SESSION}"
    continue
  fi
  CMD="cd /home/madejuele/projects/2K0300/model_training && OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} ./run_gpu.sh venv/bin/python train_tiny32_v5_visual_subclass_scan.py --mode fine --lane all --dataset-dir dataset --output-dir ${OUT_DIR} --shard-count ${SHARDS} --shard-index ${i} --seeds ${SEEDS} --epochs ${EPOCHS} --patience ${PATIENCE} --stress ${STRESS_LIST} --calibration-limit ${CALIBRATION_LIMIT} --resume --candidates-json ${CANDIDATES_JSON} --max-trials ${LIMIT} --correct-teacher-labels ${TEACHER_LABELS} 2>&1 | tee -a ${OUT_DIR}/run.log"
  tmux new-session -d -s "${SESSION}" "${CMD}"
  echo "launched ${SESSION}"
done

echo "v6_delta_ctd_round1 root=${ROOT} stage=${STAGE_DIR} shards=${SHARDS} candidates=${LIMIT}"
