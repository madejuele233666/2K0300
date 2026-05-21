#!/usr/bin/env bash
set -euo pipefail

RUN_ID="${1:-v6_parent_primary_coarse_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
STAGE_NAME="${V6_STAGE_NAME:-stage1_coarse}"
OUT_BASE="${ROOT}/${STAGE_NAME}"
CANDIDATES_JSON="${OUT_BASE}/v6_candidates.json"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-44)"

SHARDS="${V6_SHARDS:-12}"
LIMIT="${V6_LIMIT:-768}"
SEEDS="${V6_SEEDS:-20262801}"
EPOCHS="${V6_EPOCHS:-170}"
PATIENCE="${V6_PATIENCE:-26}"
CALIBRATION_LIMIT="${CALIBRATION_LIMIT:-192}"
MAX_BOARD_US="${V6_MAX_BOARD_US:-18000}"
TEACHER_TFLITE="${V6_TEACHER_TFLITE:-experiments/v4_stress_directed_20260508_215328/final/shard_2/runs/sd097_seed20261501/sixclass_best_mild_int8.tflite}"
STRESS_LIST="${STRESS_LIST:-rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5,cam_blur2a0,cam_blur3a90,cam_blur5a45,cam_blur5a135,cam_noise0p02,cam_noise0p04,cam_blur3a0_noise0p02,cam_blur5a45_noise0p04}"
OMP_THREADS="${OMP_THREADS:-1}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-1}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"

mkdir -p "${OUT_BASE}"
printf '%s\n' "${RUN_ID}" > experiments/v6_active_run.txt

if [[ ! -f "${TEACHER_TFLITE}" ]]; then
  echo "missing teacher tflite: ${TEACHER_TFLITE}" >&2
  exit 2
fi

if [[ ! -f "${CANDIDATES_JSON}" ]]; then
  venv/bin/python generate_v6_parent_primary_candidates.py \
    --output "${CANDIDATES_JSON}" \
    --limit "${LIMIT}" \
    --seed 20262800 \
    --max-board-us "${MAX_BOARD_US}" \
    --print-top 32 \
    2>&1 | tee -a "${OUT_BASE}/generate.log"
fi

cat > "${OUT_BASE}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "stage": "${STAGE_NAME}",
  "shards": ${SHARDS},
  "limit": ${LIMIT},
  "seeds": "${SEEDS}",
  "epochs": ${EPOCHS},
  "patience": ${PATIENCE},
  "teacher_tflite": "${TEACHER_TFLITE}",
  "candidates_json": "${CANDIDATES_JSON}",
  "stress_list": "${STRESS_LIST}",
  "note": "closed-set C4 scan with box/circuit/instance heads and camera blur/noise stress"
}
EOF

for i in $(seq 0 $((SHARDS - 1))); do
  out_dir="${OUT_BASE}/shard_${i}"
  session="v6pp_${RUN_TAG}_${STAGE_NAME}_${i}"
  mkdir -p "${out_dir}"
  if tmux has-session -t "${session}" 2>/dev/null; then
    echo "exists ${session}"
    continue
  fi
  cmd="cd /home/madejuele/projects/2K0300/model_training && OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} ./run_gpu.sh venv/bin/python train_tiny32_v5_visual_subclass_scan.py --mode coarse --lane all --dataset-dir dataset --output-dir ${out_dir} --shard-count ${SHARDS} --shard-index ${i} --seeds ${SEEDS} --epochs ${EPOCHS} --patience ${PATIENCE} --stress ${STRESS_LIST} --calibration-limit ${CALIBRATION_LIMIT} --resume --candidates-json ${CANDIDATES_JSON} --max-trials ${LIMIT} --teacher-tflite ${TEACHER_TFLITE} 2>&1 | tee -a ${out_dir}/run.log"
  tmux new-session -d -s "${session}" "${cmd}"
  echo "launched ${session}"
done

echo "v6_parent_primary root=${OUT_BASE} candidates=${CANDIDATES_JSON} shards=${SHARDS} limit=${LIMIT} teacher=${TEACHER_TFLITE}"
