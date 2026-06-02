#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

RUN_ID="${1:-v8_robust_shadow_parent_20260520_0001}"
SESSION="${V8_ROBUST_SHADOW_SESSION:-v8_robust_shadow_20260520_0001}"
ROOT="experiments/${RUN_ID}"
CODE_DIM="${V8_ROBUST_SHADOW_CODE_DIM:-3}"
FILTERS="${V8_ROBUST_SHADOW_FILTERS:-2,4,8}"
FILTER_TAG="c${FILTERS//,/_}"
OUT="${ROOT}/s2d_${FILTER_TAG}_d${CODE_DIM}_shadow_init"
COMP="${ROOT}/s2d_${FILTER_TAG}_d${CODE_DIM}_shadow_init_compile"
EVAL="${ROOT}/s2d_${FILTER_TAG}_d${CODE_DIM}_shadow_init_highstress"

STRESS="${V8_ROBUST_SHADOW_STRESS:-rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5,cam_blur2a0,cam_blur3a90,cam_blur5a45,cam_blur5a135,cam_noise0p02,cam_noise0p04,cam_blur3a0_noise0p02,cam_blur5a45_noise0p04,cam_brightm0p06,cam_brightm0p10,cam_bright0p06,cam_bright0p10,cam_contrastm0p12,cam_contrastm0p18,cam_contrast0p15,cam_blur4a45,cam_blur6a90,cam_shiftu1,cam_shiftd1,cam_shiftl1,cam_shiftr1,cam_shiftul1,cam_shiftdr1,cam_shiftu2,cam_shiftl2,cam_shiftdr1_noise0p04,cam_shiftu1_noise0p04,cam_blur4a45_noise0p03,cam_blur6a135_noise0p03}"
EPOCHS="${V8_ROBUST_SHADOW_EPOCHS:-1000}"
LR="${V8_ROBUST_SHADOW_LR:-0.0005}"
SEED="${V8_ROBUST_SHADOW_SEED:-20261011}"
INIT_MODEL="${V8_ROBUST_SHADOW_INIT_MODEL:-experiments/v8_parent_classifier_20260520_0002/stageD_parent_classifier/s2d_c2_4_8_s20260931/parent_model.keras}"
PROTOTYPE_TEACHER_NPZ="${V8_ROBUST_SHADOW_PROTOTYPE_TEACHER_NPZ:-}"
STRESS_FROM_PROTOTYPE_NPZ="${V8_ROBUST_SHADOW_STRESS_FROM_PROTOTYPE_NPZ:-0}"
PROTOTYPE_MARGIN_WEIGHT="${V8_ROBUST_SHADOW_PROTOTYPE_MARGIN_WEIGHT:-0.0}"
PROTOTYPE_MARGIN_TARGET="${V8_ROBUST_SHADOW_PROTOTYPE_MARGIN_TARGET:-16}"
PROTOTYPE_MARGIN_ALPHA="${V8_ROBUST_SHADOW_PROTOTYPE_MARGIN_ALPHA:-0.05}"
PROTOTYPE_CODE_ANCHOR_WEIGHT="${V8_ROBUST_SHADOW_PROTOTYPE_CODE_ANCHOR_WEIGHT:-0.0}"
PROTOTYPE_OUTPUT_SCALE="${V8_ROBUST_SHADOW_PROTOTYPE_OUTPUT_SCALE:-0.0879310742020607}"
PROTOTYPE_OUTPUT_ZERO="${V8_ROBUST_SHADOW_PROTOTYPE_OUTPUT_ZERO:-36}"
PROTOTYPE_LOW_MARGIN_THRESHOLD="${V8_ROBUST_SHADOW_PROTOTYPE_LOW_MARGIN_THRESHOLD:-8}"
PROTOTYPE_LOW_MARGIN_WEIGHT="${V8_ROBUST_SHADOW_PROTOTYPE_LOW_MARGIN_WEIGHT:-3.0}"
QPAIR_TEACHER_NPZ="${V8_ROBUST_SHADOW_QPAIR_TEACHER_NPZ:-}"
QPAIR_MARGIN_WEIGHT="${V8_ROBUST_SHADOW_QPAIR_MARGIN_WEIGHT:-0.0}"
QPAIR_MARGIN_TARGET="${V8_ROBUST_SHADOW_QPAIR_MARGIN_TARGET:-8}"
QPAIR_MARGIN_ALPHA="${V8_ROBUST_SHADOW_QPAIR_MARGIN_ALPHA:-0.05}"
QPAIR_START_EPOCH="${V8_ROBUST_SHADOW_QPAIR_START_EPOCH:-1}"
DYNAMIC_QPAIR_TEACHER_NPZ="${V8_ROBUST_SHADOW_DYNAMIC_QPAIR_TEACHER_NPZ:-}"
DYNAMIC_QPAIR_MARGIN_WEIGHT="${V8_ROBUST_SHADOW_DYNAMIC_QPAIR_MARGIN_WEIGHT:-0.0}"
DYNAMIC_QPAIR_MARGIN_TARGET="${V8_ROBUST_SHADOW_DYNAMIC_QPAIR_MARGIN_TARGET:-8}"
DYNAMIC_QPAIR_MARGIN_ALPHA="${V8_ROBUST_SHADOW_DYNAMIC_QPAIR_MARGIN_ALPHA:-0.05}"
DYNAMIC_QPAIR_START_EPOCH="${V8_ROBUST_SHADOW_DYNAMIC_QPAIR_START_EPOCH:-1}"
QANCHOR_TEACHER_NPZ="${V8_ROBUST_SHADOW_QANCHOR_TEACHER_NPZ:-}"
QANCHOR_WEIGHT="${V8_ROBUST_SHADOW_QANCHOR_WEIGHT:-0.0}"
QANCHOR_START_EPOCH="${V8_ROBUST_SHADOW_QANCHOR_START_EPOCH:-1}"
LOGIT_TEACHER_NPZ="${V8_ROBUST_SHADOW_LOGIT_TEACHER_NPZ:-}"
LOGIT_TEACHER_WEIGHT="${V8_ROBUST_SHADOW_LOGIT_TEACHER_WEIGHT:-0.0}"
LOGIT_TEACHER_START_EPOCH="${V8_ROBUST_SHADOW_LOGIT_TEACHER_START_EPOCH:-1}"
ORBIT_CONSISTENCY_WEIGHT="${V8_ROBUST_SHADOW_ORBIT_CONSISTENCY_WEIGHT:-0.0}"
ORBIT_CONSISTENCY_START_EPOCH="${V8_ROBUST_SHADOW_ORBIT_CONSISTENCY_START_EPOCH:-1}"
ALLOW_PARTIAL_INIT_OUTPUT="${V8_ROBUST_SHADOW_ALLOW_PARTIAL_INIT_OUTPUT:-0}"

TRAIN_EXTRA_ARGS=""
if [[ -n "${PROTOTYPE_TEACHER_NPZ}" ]]; then
  TRAIN_EXTRA_ARGS="${TRAIN_EXTRA_ARGS} --prototype-teacher-npz ${PROTOTYPE_TEACHER_NPZ} --prototype-margin-weight ${PROTOTYPE_MARGIN_WEIGHT} --prototype-margin-target ${PROTOTYPE_MARGIN_TARGET} --prototype-margin-alpha ${PROTOTYPE_MARGIN_ALPHA} --prototype-code-anchor-weight ${PROTOTYPE_CODE_ANCHOR_WEIGHT} --prototype-low-margin-threshold ${PROTOTYPE_LOW_MARGIN_THRESHOLD} --prototype-low-margin-weight ${PROTOTYPE_LOW_MARGIN_WEIGHT}"
fi
TRAIN_EXTRA_ARGS="${TRAIN_EXTRA_ARGS} --prototype-output-scale ${PROTOTYPE_OUTPUT_SCALE} --prototype-output-zero ${PROTOTYPE_OUTPUT_ZERO}"
if [[ "${STRESS_FROM_PROTOTYPE_NPZ}" == "1" ]]; then
  TRAIN_EXTRA_ARGS="${TRAIN_EXTRA_ARGS} --stress-from-prototype-npz"
fi
if [[ -n "${QPAIR_TEACHER_NPZ}" ]]; then
  TRAIN_EXTRA_ARGS="${TRAIN_EXTRA_ARGS} --qpair-teacher-npz ${QPAIR_TEACHER_NPZ} --qpair-margin-weight ${QPAIR_MARGIN_WEIGHT} --qpair-margin-target ${QPAIR_MARGIN_TARGET} --qpair-margin-alpha ${QPAIR_MARGIN_ALPHA} --qpair-start-epoch ${QPAIR_START_EPOCH}"
fi
if [[ -n "${DYNAMIC_QPAIR_TEACHER_NPZ}" ]]; then
  TRAIN_EXTRA_ARGS="${TRAIN_EXTRA_ARGS} --dynamic-qpair-teacher-npz ${DYNAMIC_QPAIR_TEACHER_NPZ} --dynamic-qpair-margin-weight ${DYNAMIC_QPAIR_MARGIN_WEIGHT} --dynamic-qpair-margin-target ${DYNAMIC_QPAIR_MARGIN_TARGET} --dynamic-qpair-margin-alpha ${DYNAMIC_QPAIR_MARGIN_ALPHA} --dynamic-qpair-start-epoch ${DYNAMIC_QPAIR_START_EPOCH}"
fi
if [[ -n "${QANCHOR_TEACHER_NPZ}" ]]; then
  TRAIN_EXTRA_ARGS="${TRAIN_EXTRA_ARGS} --qanchor-teacher-npz ${QANCHOR_TEACHER_NPZ} --qanchor-weight ${QANCHOR_WEIGHT} --qanchor-start-epoch ${QANCHOR_START_EPOCH}"
fi
if [[ -n "${LOGIT_TEACHER_NPZ}" ]]; then
  TRAIN_EXTRA_ARGS="${TRAIN_EXTRA_ARGS} --logit-teacher-npz ${LOGIT_TEACHER_NPZ} --logit-teacher-weight ${LOGIT_TEACHER_WEIGHT} --logit-teacher-start-epoch ${LOGIT_TEACHER_START_EPOCH}"
fi
if [[ "${ORBIT_CONSISTENCY_WEIGHT}" != "0.0" && "${ORBIT_CONSISTENCY_WEIGHT}" != "0" ]]; then
  TRAIN_EXTRA_ARGS="${TRAIN_EXTRA_ARGS} --orbit-consistency-weight ${ORBIT_CONSISTENCY_WEIGHT} --orbit-consistency-start-epoch ${ORBIT_CONSISTENCY_START_EPOCH}"
fi
if [[ "${ALLOW_PARTIAL_INIT_OUTPUT}" == "1" ]]; then
  TRAIN_EXTRA_ARGS="${TRAIN_EXTRA_ARGS} --allow-partial-init-output"
fi

mkdir -p "${OUT}" "${COMP}" "${EVAL}"
printf '%s\n' "${RUN_ID}" > experiments/v8_robust_shadow_active_run.txt

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "session": "${SESSION}",
  "target": "High-pressure robustness without adding high-pressure samples to training or prototype tables.",
  "high_pressure_usage": "evaluation_only",
  "epochs": ${EPOCHS},
  "code_dim": ${CODE_DIM},
  "filters": "${FILTERS}",
  "learning_rate": ${LR},
  "seed": ${SEED},
  "init_model": "${INIT_MODEL}",
  "stress": "${STRESS}",
  "prototype_teacher_npz": "${PROTOTYPE_TEACHER_NPZ}",
  "stress_from_prototype_npz": "${STRESS_FROM_PROTOTYPE_NPZ}",
  "prototype_margin_weight": ${PROTOTYPE_MARGIN_WEIGHT},
  "prototype_margin_target": ${PROTOTYPE_MARGIN_TARGET},
  "prototype_margin_alpha": ${PROTOTYPE_MARGIN_ALPHA},
  "prototype_code_anchor_weight": ${PROTOTYPE_CODE_ANCHOR_WEIGHT},
  "prototype_output_scale": ${PROTOTYPE_OUTPUT_SCALE},
  "prototype_output_zero": ${PROTOTYPE_OUTPUT_ZERO},
  "prototype_low_margin_threshold": ${PROTOTYPE_LOW_MARGIN_THRESHOLD},
  "prototype_low_margin_weight": ${PROTOTYPE_LOW_MARGIN_WEIGHT},
  "qpair_teacher_npz": "${QPAIR_TEACHER_NPZ}",
  "qpair_margin_weight": ${QPAIR_MARGIN_WEIGHT},
  "qpair_margin_target": ${QPAIR_MARGIN_TARGET},
  "qpair_margin_alpha": ${QPAIR_MARGIN_ALPHA},
  "qpair_start_epoch": ${QPAIR_START_EPOCH},
  "dynamic_qpair_teacher_npz": "${DYNAMIC_QPAIR_TEACHER_NPZ}",
  "dynamic_qpair_margin_weight": ${DYNAMIC_QPAIR_MARGIN_WEIGHT},
  "dynamic_qpair_margin_target": ${DYNAMIC_QPAIR_MARGIN_TARGET},
  "dynamic_qpair_margin_alpha": ${DYNAMIC_QPAIR_MARGIN_ALPHA},
  "dynamic_qpair_start_epoch": ${DYNAMIC_QPAIR_START_EPOCH},
  "qanchor_teacher_npz": "${QANCHOR_TEACHER_NPZ}",
  "qanchor_weight": ${QANCHOR_WEIGHT},
  "qanchor_start_epoch": ${QANCHOR_START_EPOCH},
  "logit_teacher_npz": "${LOGIT_TEACHER_NPZ}",
  "logit_teacher_weight": ${LOGIT_TEACHER_WEIGHT},
  "logit_teacher_start_epoch": ${LOGIT_TEACHER_START_EPOCH},
  "orbit_consistency_weight": ${ORBIT_CONSISTENCY_WEIGHT},
  "orbit_consistency_start_epoch": ${ORBIT_CONSISTENCY_START_EPOCH},
  "allow_partial_init_output": "${ALLOW_PARTIAL_INIT_OUTPUT}"
}
EOF

if tmux has-session -t "${SESSION}" 2>/dev/null; then
  echo "exists ${SESSION}"
  exit 0
fi

CMD=$(cat <<EOF
set -euo pipefail
cd '${SCRIPT_DIR}'
echo robust_shadow_train_start
./run_gpu.sh venv/bin/python train_v8_parent_classifier.py \
  --dataset-dir dataset \
  --output-dir '${OUT}' \
  --filters '${FILTERS}' \
  --code-dim '${CODE_DIM}' \
  --seed '${SEED}' \
  --epochs '${EPOCHS}' \
  --learning-rate '${LR}' \
  --init-model '${INIT_MODEL}' \
  --stress '${STRESS}' \
  --sample-weight-mode parent_balanced \
  ${TRAIN_EXTRA_ARGS} \
  --log-every 100 2>&1 | tee -a '${OUT}/run.log'
echo robust_shadow_compile_start
venv/bin/python compile_v8_parent_logits_memory.py \
  --parent-run-dir '${OUT}' \
  --dataset-dir dataset \
  --output-dir '${COMP}' \
  --stress '${STRESS}' \
  --feature-sources int8_tflite \
  --prototype-subsets clean,clean_rotmirror,all \
  --residual-bases clean,clean_rotmirror \
  --residual-target-int8-margin 8 2>&1 | tee -a '${COMP}/run.log'
echo robust_shadow_highstress_start
venv/bin/python stress_test_v8_low_margin.py \
  --tflite '${OUT}/parent_int8.tflite' \
  --params-npz '${COMP}/best_parent_logits_memory_params.npz' \
  --selection-params-npz experiments/v8_parent_logits_prune_merge_20260520_0001/primary_quick/best_pruned_merged_parent_logits_params.npz \
  --dataset-dir dataset \
  --output-dir '${EVAL}' \
  --low-margin-threshold 8 \
  --control-margin-min 128 \
  --seed 20260520 \
  --perturbs all \
  --batch-size 512 2>&1 | tee -a '${EVAL}/run.log'
echo robust_shadow_done
EOF
)

printf -v QUOTED_CMD '%q' "${CMD}"
tmux new-session -d -s "${SESSION}" "bash -lc ${QUOTED_CMD}"
echo "launched ${SESSION}"
echo "root=${ROOT}"
echo "train=${OUT}"
echo "compile=${COMP}"
echo "eval=${EVAL}"
