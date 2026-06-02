#!/usr/bin/env bash
set -euo pipefail

RUN_ID="${1:-v6_ctd_round1_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
STAGE="${V6_CTD_STAGE:-stage6_ctd_round1}"
STAGE_DIR="${ROOT}/${STAGE}"
ATTR_DIR="${V6_CTD_ATTR_DIR:-experiments/v6_parent100_20260515_0001/error_attribution}"
TEACHER_LABELS="${ROOT}/ctd_teacher_labels.npz"
TEACHER_SUMMARY="${ROOT}/ctd_teacher_labels_summary.csv"
CANDIDATES_JSON="${STAGE_DIR}/ctd_round1_candidates.json"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-34)"

SHARDS="${V6_CTD_SHARDS:-16}"
LIMIT="${V6_CTD_LIMIT:-160}"
ANCHOR_LIMIT="${V6_CTD_ANCHOR_LIMIT:-16}"
SEEDS="${V6_CTD_SEEDS:-20263201,20263203}"
EPOCHS="${V6_CTD_EPOCHS:-220}"
PATIENCE="${V6_CTD_PATIENCE:-34}"
CALIBRATION_LIMIT="${CALIBRATION_LIMIT:-304}"
MAX_BOARD_US="${V6_CTD_MAX_BOARD_US:-18000}"
STRESS_LIST="${STRESS_LIST:-rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5,cam_blur2a0,cam_blur3a90,cam_blur5a45,cam_blur5a135,cam_noise0p02,cam_noise0p04,cam_blur3a0_noise0p02,cam_blur5a45_noise0p04}"
OMP_THREADS="${OMP_THREADS:-1}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-1}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"

mkdir -p "${ROOT}" "${STAGE_DIR}"
printf '%s\n' "${RUN_ID}" > experiments/v6_active_run.txt

if [[ ! -f "${TEACHER_LABELS}" ]]; then
  venv/bin/python build_v6_correct_teacher_labels.py \
    --dataset-dir dataset \
    --selected-models "${ATTR_DIR}/selected_models.csv" \
    --model-summary "${ATTR_DIR}/model_summary.csv" \
    --sample-summary "${ATTR_DIR}/sample_error_summary.csv" \
    --error-events "${ATTR_DIR}/error_events.csv" \
    --output "${TEACHER_LABELS}" \
    --summary-csv "${TEACHER_SUMMARY}" \
    --model-root . \
    --temperature 2.0 \
    2>&1 | tee -a "${ROOT}/build_correct_teacher.log"
fi

if [[ ! -f "${CANDIDATES_JSON}" ]]; then
  venv/bin/python generate_v6_ctd_candidates.py \
    --selected-models "${ATTR_DIR}/selected_models.csv" \
    --output "${CANDIDATES_JSON}" \
    --limit "${LIMIT}" \
    --anchor-limit "${ANCHOR_LIMIT}" \
    --max-board-us "${MAX_BOARD_US}" \
    --seed 20263200 \
    2>&1 | tee -a "${ROOT}/generate_ctd_candidates.log"
fi

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "stage": "${STAGE}",
  "stage_dir": "${STAGE_DIR}",
  "attr_dir": "${ATTR_DIR}",
  "teacher_labels": "${TEACHER_LABELS}",
  "teacher_summary": "${TEACHER_SUMMARY}",
  "candidates_json": "${CANDIDATES_JSON}",
  "shards": ${SHARDS},
  "limit": ${LIMIT},
  "anchor_limit": ${ANCHOR_LIMIT},
  "seeds": "${SEEDS}",
  "epochs": ${EPOCHS},
  "patience": ${PATIENCE},
  "calibration_limit": ${CALIBRATION_LIMIT},
  "max_board_us": ${MAX_BOARD_US},
  "stress_list": "${STRESS_LIST}",
  "note": "Correct-Teacher Distillation round 1; no continuous watcher"
}
EOF

for i in $(seq 0 $((SHARDS - 1))); do
  OUT_DIR="${STAGE_DIR}/shard_${i}"
  SESSION="v6ctd1_${RUN_TAG}_${i}"
  mkdir -p "${OUT_DIR}"
  if tmux has-session -t "${SESSION}" 2>/dev/null; then
    echo "exists ${SESSION}"
    continue
  fi
  CMD="cd /home/madejuele/projects/2K0300/model_training && OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} ./run_gpu.sh venv/bin/python train_tiny32_v5_visual_subclass_scan.py --mode fine --lane all --dataset-dir dataset --output-dir ${OUT_DIR} --shard-count ${SHARDS} --shard-index ${i} --seeds ${SEEDS} --epochs ${EPOCHS} --patience ${PATIENCE} --stress ${STRESS_LIST} --calibration-limit ${CALIBRATION_LIMIT} --resume --candidates-json ${CANDIDATES_JSON} --max-trials ${LIMIT} --correct-teacher-labels ${TEACHER_LABELS} 2>&1 | tee -a ${OUT_DIR}/run.log"
  tmux new-session -d -s "${SESSION}" "${CMD}"
  echo "launched ${SESSION}"
done

echo "v6_ctd_round1 root=${ROOT} stage=${STAGE_DIR} shards=${SHARDS} candidates=${LIMIT}"
