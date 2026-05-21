#!/usr/bin/env bash
set -euo pipefail

RUN_ID="${1:-$(cat experiments/v5_active_run.txt)}"
ROOT="experiments/${RUN_ID}"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-44)"
STAGE_NAME="${V5_PARENT_FIRST_STAGE:-stage1_parentfirst}"
OUT_BASE="${ROOT}/${STAGE_NAME}"
CANDIDATES_JSON="${OUT_BASE}/parentfirst_candidates.json"

SHARDS="${V5_PARENT_FIRST_SHARDS:-8}"
LIMIT="${V5_PARENT_FIRST_LIMIT:-384}"
SEEDS="${V5_PARENT_FIRST_SEEDS:-20262701,20262702,20262703}"
EPOCHS="${V5_PARENT_FIRST_EPOCHS:-220}"
PATIENCE="${V5_PARENT_FIRST_PATIENCE:-34}"
CALIBRATION_LIMIT="${CALIBRATION_LIMIT:-192}"
MAX_BOARD_US="${V5_PARENT_FIRST_MAX_BOARD_US:-18000}"
STRESS_LIST="${STRESS_LIST:-rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5}"
OMP_THREADS="${OMP_THREADS:-1}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-1}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"

mkdir -p "${OUT_BASE}"

if [[ ! -f "${CANDIDATES_JSON}" ]]; then
  venv/bin/python generate_v5_visual_candidates.py parentfirst \
    --output "${CANDIDATES_JSON}" \
    --limit "${LIMIT}" \
    --seed 20262700 \
    --lane all \
    --max-board-us "${MAX_BOARD_US}" \
    --include-controls \
    --print-top 32 \
    2>&1 | tee -a "${OUT_BASE}/generate.log"
fi

for i in $(seq 0 $((SHARDS - 1))); do
  out_dir="${OUT_BASE}/shard_${i}"
  session="v5pf_${RUN_TAG}_${STAGE_NAME}_${i}"
  mkdir -p "${out_dir}"
  if tmux has-session -t "${session}" 2>/dev/null; then
    echo "exists ${session}"
    continue
  fi
  cmd="cd /home/madejuele/projects/2K0300/model_training && OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} ./run_gpu.sh venv/bin/python train_tiny32_v5_visual_subclass_scan.py --mode coarse --lane all --dataset-dir dataset --output-dir ${out_dir} --shard-count ${SHARDS} --shard-index ${i} --seeds ${SEEDS} --epochs ${EPOCHS} --patience ${PATIENCE} --stress ${STRESS_LIST} --calibration-limit ${CALIBRATION_LIMIT} --resume --candidates-json ${CANDIDATES_JSON} --max-trials ${LIMIT} 2>&1 | tee -a ${out_dir}/run.log"
  tmux new-session -d -s "${session}" "${cmd}"
  echo "launched ${session}"
done

echo "parent_first root=${OUT_BASE} candidates=${CANDIDATES_JSON} shards=${SHARDS} limit=${LIMIT}"
