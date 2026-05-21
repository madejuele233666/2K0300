#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

RUN_ID="${1:-v8_parent_classifier_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
STAGE="${V8_PARENT_STAGE:-stageD_parent_classifier}"
STAGE_DIR="${ROOT}/${STAGE}"
SESSION="${V8_PARENT_SESSION:-v8_parent_${RUN_ID//[^A-Za-z0-9_]/_}}"

EPOCHS="${V8_PARENT_EPOCHS:-3000}"
LEARNING_RATE="${V8_PARENT_LR:-0.002}"
LOG_EVERY="${V8_PARENT_LOG_EVERY:-150}"
SAMPLE_WEIGHT_MODE="${V8_PARENT_SAMPLE_WEIGHT_MODE:-parent_balanced}"
CODE_DIM_DEFAULT="${V8_PARENT_CODE_DIM:-3}"
TEACHER_NPZ="${V8_PARENT_TEACHER_NPZ:-}"
PROTOTYPE_MARGIN_WEIGHT="${V8_PARENT_MARGIN_WEIGHT:-0}"
PROTOTYPE_MARGIN_TARGET="${V8_PARENT_MARGIN_TARGET:-16}"
PROTOTYPE_MARGIN_ALPHA="${V8_PARENT_MARGIN_ALPHA:-0.05}"
PROTOTYPE_CODE_ANCHOR_WEIGHT="${V8_PARENT_CODE_ANCHOR_WEIGHT:-0}"
PROTOTYPE_LOW_MARGIN_THRESHOLD="${V8_PARENT_LOW_MARGIN_THRESHOLD:-8}"
PROTOTYPE_LOW_MARGIN_WEIGHT="${V8_PARENT_LOW_MARGIN_WEIGHT:-3.0}"
OMP_THREADS="${OMP_THREADS:-1}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-1}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"

# New format: name:architecture:filters:code_dim:seed:activation:pool:extra_conv
# Legacy format without code_dim is still accepted and uses V8_PARENT_CODE_DIM.
CONFIGS="${V8_PARENT_CONFIGS:-\
s2d_c2_4_8_d3_s20260931:spacetodepth_conv:2-4-8:3:20260931:relu6:max:0;\
s2d_c2_6_12_d3_s20260932:spacetodepth_conv:2-6-12:3:20260932:relu6:max:0;\
hyb_c2_6_12_d3_s20260933:spacetodepth_hybrid:2-6-12:3:20260933:relu6:max:0;\
hyb_c3_6_12_d3_s20260934:spacetodepth_hybrid:3-6-12:3:20260934:relu6:max:0}"

mkdir -p "${ROOT}" "${STAGE_DIR}"
printf '%s\n' "${RUN_ID}" > experiments/v8_parent_classifier_active_run.txt

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "stage": "${STAGE}",
  "stage_dir": "${STAGE_DIR}",
  "configs": "${CONFIGS}",
  "epochs": ${EPOCHS},
  "learning_rate": ${LEARNING_RATE},
  "sample_weight_mode": "${SAMPLE_WEIGHT_MODE}",
  "code_dim_default": ${CODE_DIM_DEFAULT},
  "teacher_npz": "${TEACHER_NPZ}",
  "prototype_margin_weight": ${PROTOTYPE_MARGIN_WEIGHT},
  "prototype_margin_target": ${PROTOTYPE_MARGIN_TARGET},
  "prototype_margin_alpha": ${PROTOTYPE_MARGIN_ALPHA},
  "target": "Margin closure branch. Preserve normal clean/rot_mirror/stress and int8 TFLite replay, then reduce high-pressure stress wrong rate after prototype compilation."
}
EOF

if tmux has-session -t "${SESSION}" 2>/dev/null; then
  echo "exists ${SESSION}"
  exit 0
fi

CMD=$(cat <<EOF
cd /home/madejuele/projects/2K0300/model_training
IFS=';' read -r -a CONFIG_ARRAY <<< '${CONFIGS}'
for item in "\${CONFIG_ARRAY[@]}"; do
  IFS=':' read -r NAME ARCH FILTERS FIELD4 FIELD5 FIELD6 FIELD7 FIELD8 <<< "\${item}"
  if [[ -n "\${FIELD8}" ]]; then
    CODE_DIM="\${FIELD4}"
    SEED="\${FIELD5}"
    ACTIVATION="\${FIELD6}"
    POOL="\${FIELD7}"
    EXTRA_CONV="\${FIELD8}"
  else
    CODE_DIM='${CODE_DIM_DEFAULT}'
    SEED="\${FIELD4}"
    ACTIVATION="\${FIELD5}"
    POOL="\${FIELD6}"
    EXTRA_CONV="\${FIELD7}"
  fi
  OUT_DIR='${STAGE_DIR}'/"\${NAME}"
  mkdir -p "\${OUT_DIR}"
  EXTRA_FLAG=''
  if [[ "\${EXTRA_CONV}" == '1' ]]; then
    EXTRA_FLAG='--extra-conv'
  fi
  TEACHER_FLAGS=''
  if [[ -n '${TEACHER_NPZ}' ]]; then
    TEACHER_FLAGS='--prototype-teacher-npz ${TEACHER_NPZ} --stress-from-prototype-npz --prototype-margin-weight ${PROTOTYPE_MARGIN_WEIGHT} --prototype-margin-target ${PROTOTYPE_MARGIN_TARGET} --prototype-margin-alpha ${PROTOTYPE_MARGIN_ALPHA} --prototype-code-anchor-weight ${PROTOTYPE_CODE_ANCHOR_WEIGHT} --prototype-low-margin-threshold ${PROTOTYPE_LOW_MARGIN_THRESHOLD} --prototype-low-margin-weight ${PROTOTYPE_LOW_MARGIN_WEIGHT}'
  fi
  echo "parent_classifier_start \${NAME} arch=\${ARCH} filters=\${FILTERS} code_dim=\${CODE_DIM} seed=\${SEED}"
  OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} TF_FORCE_GPU_ALLOW_GROWTH=true ./run_gpu.sh venv/bin/python train_v8_parent_classifier.py \
    --dataset-dir dataset \
    --output-dir "\${OUT_DIR}" \
    --filters "\${FILTERS//-/,}" \
    --code-dim "\${CODE_DIM}" \
    --seed "\${SEED}" \
    --epochs ${EPOCHS} \
    --learning-rate ${LEARNING_RATE} \
    --backbone-architecture "\${ARCH}" \
    --activation "\${ACTIVATION}" \
    --pool "\${POOL}" \
    --sample-weight-mode ${SAMPLE_WEIGHT_MODE} \
    --log-every ${LOG_EVERY} \
    \${TEACHER_FLAGS} \
    \${EXTRA_FLAG} 2>&1 | tee -a "\${OUT_DIR}/run.log"
  echo "parent_classifier_done \${NAME}"
done
EOF
)

printf -v QUOTED_CMD '%q' "${CMD}"
tmux new-session -d -s "${SESSION}" "bash -lc ${QUOTED_CMD}"
echo "launched ${SESSION}"
echo "root=${ROOT}"
echo "stage_dir=${STAGE_DIR}"
