#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

RUN_ID="${1:-v8_robust_shadow_embedding_20260520_0001}"
SESSION="${V8_ROBUST_EMBED_SESSION:-v8_robust_shadow_embedding_20260520_0001}"
ROOT="experiments/${RUN_ID}"
FILTERS="${V8_ROBUST_EMBED_FILTERS:-4,8,16}"
EMBED_DIM="${V8_ROBUST_EMBED_DIM:-24}"
FILTER_TAG="${FILTERS//,/_}"
OUT_SUFFIX="${V8_ROBUST_EMBED_OUT_SUFFIX:-s2d_c${FILTER_TAG}_d${EMBED_DIM}_shadow_metric_raw}"
OUT="${ROOT}/${OUT_SUFFIX}"
TFLITE_DIR="${OUT}/tflite_export"
WRAP_DIR="${OUT}/tflite_compile_input"
COMP_SUFFIX="${V8_ROBUST_EMBED_COMPILE_SUFFIX:-s2d_c${FILTER_TAG}_d${EMBED_DIM}_shadow_metric_compile}"
EVAL_SUFFIX="${V8_ROBUST_EMBED_EVAL_SUFFIX:-s2d_c${FILTER_TAG}_d${EMBED_DIM}_shadow_metric_highstress}"
COMP="${ROOT}/${COMP_SUFFIX}"
EVAL="${ROOT}/${EVAL_SUFFIX}"

DEFAULT_STRESS="rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5,cam_blur2a0,cam_blur3a90,cam_blur5a45,cam_blur5a135,cam_noise0p02,cam_noise0p04,cam_blur3a0_noise0p02,cam_blur5a45_noise0p04,cam_brightm0p06,cam_brightm0p10,cam_bright0p06,cam_bright0p10,cam_contrastm0p12,cam_contrastm0p18,cam_contrast0p15,cam_blur4a45,cam_blur6a90,cam_shiftu1,cam_shiftd1,cam_shiftl1,cam_shiftr1,cam_shiftul1,cam_shiftdr1,cam_shiftu2,cam_shiftl2,cam_shiftdr1_noise0p04,cam_shiftu1_noise0p04,cam_blur4a45_noise0p03,cam_blur6a135_noise0p03"
TRAIN_STRESS="${V8_ROBUST_EMBED_TRAIN_STRESS:-${V8_ROBUST_EMBED_STRESS:-${DEFAULT_STRESS}}}"
COMPILE_STRESS="${V8_ROBUST_EMBED_COMPILE_STRESS:-${V8_ROBUST_EMBED_STRESS:-${DEFAULT_STRESS}}}"
EPOCHS="${V8_ROBUST_EMBED_EPOCHS:-900}"
WARMUP="${V8_ROBUST_EMBED_WARMUP:-150}"
LR="${V8_ROBUST_EMBED_LR:-0.0012}"
SEED="${V8_ROBUST_EMBED_SEED:-20261021}"
LAMBDA_D4="${V8_ROBUST_EMBED_LAMBDA_D4:-1.0}"
LAMBDA_STRESS="${V8_ROBUST_EMBED_LAMBDA_STRESS:-1.5}"
LAMBDA_VAR="${V8_ROBUST_EMBED_LAMBDA_VAR:-0.25}"
LAMBDA_COV="${V8_ROBUST_EMBED_LAMBDA_COV:-0.02}"
LAMBDA_NORM="${V8_ROBUST_EMBED_LAMBDA_NORM:-0.01}"
NORM_TARGET="${V8_ROBUST_EMBED_NORM_TARGET:-1.0}"
VARIANCE_FLOOR="${V8_ROBUST_EMBED_VARIANCE_FLOOR:-0.08}"
LOW_MARGIN_TEACHER="${V8_ROBUST_EMBED_LOW_MARGIN_TEACHER:-}"
LOW_MARGIN_THRESHOLD="${V8_ROBUST_EMBED_LOW_MARGIN_THRESHOLD:-8}"
LOW_MARGIN_EXTRA_WEIGHT="${V8_ROBUST_EMBED_LOW_MARGIN_EXTRA_WEIGHT:-0.0}"
COMPILED_TEACHER="${V8_ROBUST_EMBED_COMPILED_TEACHER:-}"
LAMBDA_COMPILED_MARGIN="${V8_ROBUST_EMBED_LAMBDA_COMPILED_MARGIN:-0.0}"
LAMBDA_COMPILED_PULL="${V8_ROBUST_EMBED_LAMBDA_COMPILED_PULL:-0.0}"
COMPILED_MARGIN_TARGET="${V8_ROBUST_EMBED_COMPILED_MARGIN_TARGET:-0.02}"
COMPILED_MARGIN_ALPHA="${V8_ROBUST_EMBED_COMPILED_MARGIN_ALPHA:-32}"
TEACHER_START_EPOCH="${V8_ROBUST_EMBED_TEACHER_START_EPOCH:-1}"
LAMBDA_QCOMPILED_MARGIN="${V8_ROBUST_EMBED_LAMBDA_QCOMPILED_MARGIN:-0.0}"
QCOMPILED_MARGIN_TARGET="${V8_ROBUST_EMBED_QCOMPILED_MARGIN_TARGET:-128}"
QCOMPILED_MARGIN_ALPHA="${V8_ROBUST_EMBED_QCOMPILED_MARGIN_ALPHA:-0.02}"
QCOMPILED_SCALE="${V8_ROBUST_EMBED_QCOMPILED_SCALE:-64}"
QCOMPILED_START_EPOCH="${V8_ROBUST_EMBED_QCOMPILED_START_EPOCH:-1}"
QCOMPILED_WEIGHT_MODE="${V8_ROBUST_EMBED_QCOMPILED_WEIGHT_MODE:-uniform}"
QPAIR_TEACHER="${V8_ROBUST_EMBED_QPAIR_TEACHER:-}"
LAMBDA_QPAIR_MARGIN="${V8_ROBUST_EMBED_LAMBDA_QPAIR_MARGIN:-0.0}"
QPAIR_MARGIN_TARGET="${V8_ROBUST_EMBED_QPAIR_MARGIN_TARGET:-128}"
QPAIR_MARGIN_ALPHA="${V8_ROBUST_EMBED_QPAIR_MARGIN_ALPHA:-0.02}"
QPAIR_SCALE="${V8_ROBUST_EMBED_QPAIR_SCALE:-64}"
QPAIR_START_EPOCH="${V8_ROBUST_EMBED_QPAIR_START_EPOCH:-1}"
DYNAMIC_QPAIR_TEACHER="${V8_ROBUST_EMBED_DYNAMIC_QPAIR_TEACHER:-}"
LAMBDA_DYNAMIC_QPAIR_MARGIN="${V8_ROBUST_EMBED_LAMBDA_DYNAMIC_QPAIR_MARGIN:-0.0}"
DYNAMIC_QPAIR_MARGIN_TARGET="${V8_ROBUST_EMBED_DYNAMIC_QPAIR_MARGIN_TARGET:-128}"
DYNAMIC_QPAIR_MARGIN_ALPHA="${V8_ROBUST_EMBED_DYNAMIC_QPAIR_MARGIN_ALPHA:-0.02}"
DYNAMIC_QPAIR_SCALE="${V8_ROBUST_EMBED_DYNAMIC_QPAIR_SCALE:-64}"
DYNAMIC_QPAIR_START_EPOCH="${V8_ROBUST_EMBED_DYNAMIC_QPAIR_START_EPOCH:-1}"
LAMBDA_QPROXY_MARGIN="${V8_ROBUST_EMBED_LAMBDA_QPROXY_MARGIN:-0.0}"
QPROXY_MARGIN_TARGET="${V8_ROBUST_EMBED_QPROXY_MARGIN_TARGET:-128}"
QPROXY_MARGIN_ALPHA="${V8_ROBUST_EMBED_QPROXY_MARGIN_ALPHA:-0.02}"
QPROXY_SCALE="${V8_ROBUST_EMBED_QPROXY_SCALE:-64}"
QPROXY_START_EPOCH="${V8_ROBUST_EMBED_QPROXY_START_EPOCH:-1}"

mkdir -p "${OUT}" "${TFLITE_DIR}" "${WRAP_DIR}" "${COMP}" "${EVAL}"
printf '%s\n' "${RUN_ID}" > experiments/v8_robust_shadow_embedding_active_run.txt

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "session": "${SESSION}",
  "target": "Metric embedding robustness without adding high-pressure samples to training or prototype tables.",
  "high_pressure_usage": "evaluation_only",
  "filters": "${FILTERS}",
  "embedding_dim": ${EMBED_DIM},
  "epochs": ${EPOCHS},
  "warmup_epochs": ${WARMUP},
  "learning_rate": ${LR},
  "seed": ${SEED},
  "lambda_d4": ${LAMBDA_D4},
  "lambda_stress": ${LAMBDA_STRESS},
  "lambda_var": ${LAMBDA_VAR},
  "lambda_cov": ${LAMBDA_COV},
  "lambda_norm": ${LAMBDA_NORM},
  "norm_target": ${NORM_TARGET},
  "variance_floor": ${VARIANCE_FLOOR},
  "train_stress": "${TRAIN_STRESS}",
  "compile_stress": "${COMPILE_STRESS}",
  "compiled_teacher": "${COMPILED_TEACHER}",
  "lambda_compiled_margin": ${LAMBDA_COMPILED_MARGIN},
  "lambda_compiled_pull": ${LAMBDA_COMPILED_PULL},
  "compiled_margin_target": ${COMPILED_MARGIN_TARGET},
  "compiled_margin_alpha": ${COMPILED_MARGIN_ALPHA},
  "teacher_start_epoch": ${TEACHER_START_EPOCH},
  "lambda_qcompiled_margin": ${LAMBDA_QCOMPILED_MARGIN},
  "qcompiled_margin_target": ${QCOMPILED_MARGIN_TARGET},
  "qcompiled_margin_alpha": ${QCOMPILED_MARGIN_ALPHA},
  "qcompiled_scale": ${QCOMPILED_SCALE},
  "qcompiled_start_epoch": ${QCOMPILED_START_EPOCH},
  "qcompiled_weight_mode": "${QCOMPILED_WEIGHT_MODE}",
  "qpair_teacher": "${QPAIR_TEACHER}",
  "lambda_qpair_margin": ${LAMBDA_QPAIR_MARGIN},
  "qpair_margin_target": ${QPAIR_MARGIN_TARGET},
  "qpair_margin_alpha": ${QPAIR_MARGIN_ALPHA},
  "qpair_scale": ${QPAIR_SCALE},
  "qpair_start_epoch": ${QPAIR_START_EPOCH},
  "dynamic_qpair_teacher": "${DYNAMIC_QPAIR_TEACHER}",
  "lambda_dynamic_qpair_margin": ${LAMBDA_DYNAMIC_QPAIR_MARGIN},
  "dynamic_qpair_margin_target": ${DYNAMIC_QPAIR_MARGIN_TARGET},
  "dynamic_qpair_margin_alpha": ${DYNAMIC_QPAIR_MARGIN_ALPHA},
  "dynamic_qpair_scale": ${DYNAMIC_QPAIR_SCALE},
  "dynamic_qpair_start_epoch": ${DYNAMIC_QPAIR_START_EPOCH},
  "low_margin_teacher": "${LOW_MARGIN_TEACHER}",
  "low_margin_threshold": ${LOW_MARGIN_THRESHOLD},
  "low_margin_extra_weight": ${LOW_MARGIN_EXTRA_WEIGHT},
  "lambda_qproxy_margin": ${LAMBDA_QPROXY_MARGIN},
  "qproxy_margin_target": ${QPROXY_MARGIN_TARGET},
  "qproxy_margin_alpha": ${QPROXY_MARGIN_ALPHA},
  "qproxy_scale": ${QPROXY_SCALE},
  "qproxy_start_epoch": ${QPROXY_START_EPOCH}
}
EOF

if tmux has-session -t "${SESSION}" 2>/dev/null; then
  echo "exists ${SESSION}"
  exit 0
fi

CMD=$(cat <<EOF
set -euo pipefail
cd '${SCRIPT_DIR}'
echo robust_shadow_embedding_train_start
./run_gpu.sh venv/bin/python train_v8_end_to_end_embedding.py \
  --dataset-dir dataset \
  --output-dir '${OUT}' \
  --filters '${FILTERS}' \
  --embedding-dim '${EMBED_DIM}' \
  --embedding-output-mode raw \
  --metric-normalize-embeddings \
  --seed '${SEED}' \
  --epochs '${EPOCHS}' \
  --warmup-epochs '${WARMUP}' \
  --learning-rate '${LR}' \
  --lambda-d4 '${LAMBDA_D4}' \
  --lambda-stress '${LAMBDA_STRESS}' \
  --lambda-var '${LAMBDA_VAR}' \
  --lambda-cov '${LAMBDA_COV}' \
  --lambda-norm '${LAMBDA_NORM}' \
  --norm-target '${NORM_TARGET}' \
  --variance-floor '${VARIANCE_FLOOR}' \
  ${COMPILED_TEACHER:+--compiled-teacher-npz '${COMPILED_TEACHER}'} \
  --lambda-compiled-margin '${LAMBDA_COMPILED_MARGIN}' \
  --lambda-compiled-pull '${LAMBDA_COMPILED_PULL}' \
  --compiled-margin-target '${COMPILED_MARGIN_TARGET}' \
  --compiled-margin-alpha '${COMPILED_MARGIN_ALPHA}' \
  --teacher-start-epoch '${TEACHER_START_EPOCH}' \
  --lambda-qcompiled-margin '${LAMBDA_QCOMPILED_MARGIN}' \
  --qcompiled-margin-target '${QCOMPILED_MARGIN_TARGET}' \
  --qcompiled-margin-alpha '${QCOMPILED_MARGIN_ALPHA}' \
  --qcompiled-scale '${QCOMPILED_SCALE}' \
  --qcompiled-start-epoch '${QCOMPILED_START_EPOCH}' \
  --qcompiled-weight-mode '${QCOMPILED_WEIGHT_MODE}' \
  ${QPAIR_TEACHER:+--qpair-teacher-npz '${QPAIR_TEACHER}'} \
  --lambda-qpair-margin '${LAMBDA_QPAIR_MARGIN}' \
  --qpair-margin-target '${QPAIR_MARGIN_TARGET}' \
  --qpair-margin-alpha '${QPAIR_MARGIN_ALPHA}' \
  --qpair-scale '${QPAIR_SCALE}' \
  --qpair-start-epoch '${QPAIR_START_EPOCH}' \
  ${DYNAMIC_QPAIR_TEACHER:+--dynamic-qpair-teacher-npz '${DYNAMIC_QPAIR_TEACHER}'} \
  --lambda-dynamic-qpair-margin '${LAMBDA_DYNAMIC_QPAIR_MARGIN}' \
  --dynamic-qpair-margin-target '${DYNAMIC_QPAIR_MARGIN_TARGET}' \
  --dynamic-qpair-margin-alpha '${DYNAMIC_QPAIR_MARGIN_ALPHA}' \
  --dynamic-qpair-scale '${DYNAMIC_QPAIR_SCALE}' \
  --dynamic-qpair-start-epoch '${DYNAMIC_QPAIR_START_EPOCH}' \
  ${LOW_MARGIN_TEACHER:+--low-margin-teacher-npz '${LOW_MARGIN_TEACHER}'} \
  --low-margin-threshold '${LOW_MARGIN_THRESHOLD}' \
  --low-margin-extra-weight '${LOW_MARGIN_EXTRA_WEIGHT}' \
  --lambda-qproxy-margin '${LAMBDA_QPROXY_MARGIN}' \
  --qproxy-margin-target '${QPROXY_MARGIN_TARGET}' \
  --qproxy-margin-alpha '${QPROXY_MARGIN_ALPHA}' \
  --qproxy-scale '${QPROXY_SCALE}' \
  --qproxy-start-epoch '${QPROXY_START_EPOCH}' \
  --stress '${TRAIN_STRESS}' \
  --prototype-sources medoid,kmeans \
  --k-values 1,2,4,8,16 \
  --quant-scales 16,24,32,48,64,96 2>&1 | tee -a '${OUT}/run.log'
echo robust_shadow_embedding_tflite_start
venv/bin/python export_v8_embedding_tflite.py \
  --model '${OUT}/embedding_model.keras' \
  --dataset-dir dataset \
  --output-dir '${TFLITE_DIR}' 2>&1 | tee -a '${TFLITE_DIR}/run.log'
ln -sf '../tflite_export/embedding_int8.tflite' '${WRAP_DIR}/parent_int8.tflite'
cp '${OUT}/train_config.json' '${WRAP_DIR}/train_config.json'
echo robust_shadow_embedding_compile_start
venv/bin/python compile_v8_parent_logits_memory.py \
  --parent-run-dir '${WRAP_DIR}' \
  --dataset-dir dataset \
  --output-dir '${COMP}' \
  --stress '${COMPILE_STRESS}' \
  --feature-sources int8_tflite \
  --prototype-subsets clean,clean_rotmirror,all \
  --residual-bases clean,clean_rotmirror \
  --residual-target-int8-margin 8 2>&1 | tee -a '${COMP}/run.log'
echo robust_shadow_embedding_highstress_start
venv/bin/python stress_test_v8_low_margin.py \
  --tflite '${TFLITE_DIR}/embedding_int8.tflite' \
  --params-npz '${COMP}/best_parent_logits_memory_params.npz' \
  --selection-params-npz experiments/v8_parent_logits_prune_merge_20260520_0001/primary_quick/best_pruned_merged_parent_logits_params.npz \
  --dataset-dir dataset \
  --output-dir '${EVAL}' \
  --low-margin-threshold 8 \
  --control-margin-min 128 \
  --seed 20260520 \
  --perturbs all \
  --batch-size 512 2>&1 | tee -a '${EVAL}/run.log'
echo robust_shadow_embedding_done
EOF
)

printf -v QUOTED_CMD '%q' "${CMD}"
tmux new-session -d -s "${SESSION}" "bash -lc ${QUOTED_CMD}"
echo "launched ${SESSION}"
echo "root=${ROOT}"
echo "train=${OUT}"
echo "compile=${COMP}"
echo "eval=${EVAL}"
