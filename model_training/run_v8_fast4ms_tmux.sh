#!/usr/bin/env bash
set -euo pipefail

RUN_ID="${1:-v8_fast4ms_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
STAGE="${V8_FAST_STAGE:-stageB_fast4ms_embedding}"
STAGE_DIR="${ROOT}/${STAGE}"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-30)"

EPOCHS="${V8_FAST_EPOCHS:-900}"
WARMUP_EPOCHS="${V8_FAST_WARMUP_EPOCHS:-250}"
LEARNING_RATE="${V8_FAST_LR:-0.0015}"
LAMBDA_D4="${V8_FAST_LAMBDA_D4:-1.0}"
LAMBDA_STRESS="${V8_FAST_LAMBDA_STRESS:-0.30}"
LAMBDA_VAR="${V8_FAST_LAMBDA_VAR:-0.25}"
LAMBDA_COV="${V8_FAST_LAMBDA_COV:-0.02}"
LAMBDA_NORM="${V8_FAST_LAMBDA_NORM:-0.50}"
NORM_TARGET="${V8_FAST_NORM_TARGET:-1.0}"
PROTOTYPE_SOURCES="${V8_FAST_PROTOTYPE_SOURCES:-medoid,kmeans}"
K_VALUES="${V8_FAST_K_VALUES:-8,16,24,32,48,64,96,128}"
QUANT_SCALES="${V8_FAST_QUANT_SCALES:-8,12,16,24,32,48,64,96,128}"
TRUE_LOO_TOP="${V8_FAST_TRUE_LOO_TOP:-0}"
OMP_THREADS="${OMP_THREADS:-1}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-1}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"

# name:architecture:filters:embedding_dim:seed:activation:pool:extra_conv
CONFIGS="${V8_FAST_CONFIGS:-\
s2d_c3_6_12_d24_s20260801:spacetodepth_conv:3-6-12:24:20260801:relu6:max:0;\
s2d_c4_8_16_d24_s20260802:spacetodepth_conv:4-8-16:24:20260802:relu6:max:0;\
s2d_c4_8_16_d32_s20260803:spacetodepth_conv:4-8-16:32:20260803:relu6:max:0;\
double_s2d_c3_6_12_d24_s20260804:double_spacetodepth_conv:3-6-12:24:20260804:relu6:max:0;\
double_s2d_c4_8_16_d24_s20260805:double_spacetodepth_conv:4-8-16:24:20260805:relu6:max:0;\
dw_pool_c3_6_12_d24_s20260806:depthwise_pool:3-6-12:24:20260806:relu6:max:0;\
dw_pool_c4_8_16_d24_s20260807:depthwise_pool:4-8-16:24:20260807:relu6:max:0;\
s2d_dw_c4_8_16_d24_s20260808:spacetodepth_depthwise:4-8-16:24:20260808:relu6:max:0}"

mkdir -p "${ROOT}" "${STAGE_DIR}"
printf '%s\n' "${RUN_ID}" > experiments/v8_fast4ms_active_run.txt

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "stage": "${STAGE}",
  "stage_dir": "${STAGE_DIR}",
  "configs": "${CONFIGS}",
  "epochs": ${EPOCHS},
  "warmup_epochs": ${WARMUP_EPOCHS},
  "learning_rate": ${LEARNING_RATE},
  "lambda_d4": ${LAMBDA_D4},
  "lambda_stress": ${LAMBDA_STRESS},
  "lambda_var": ${LAMBDA_VAR},
  "lambda_cov": ${LAMBDA_COV},
  "lambda_norm": ${LAMBDA_NORM},
  "norm_target": ${NORM_TARGET},
  "metric_normalize_embeddings": true,
  "prototype_sources": "${PROTOTYPE_SOURCES}",
  "k_values": "${K_VALUES}",
  "quant_scales": "${QUANT_SCALES}",
  "true_loo_top": ${TRUE_LOO_TOP},
  "embedding_output_mode": "raw",
  "target": "board_total_conservative_us <= 4000; prefer <= 2000 avg; clean/rot_mirror/stress and int8 variants all 100%."
}
EOF

IFS=';' read -r -a CONFIG_ARRAY <<< "${CONFIGS}"
index=0
for item in "${CONFIG_ARRAY[@]}"; do
  IFS=':' read -r NAME ARCH FILTERS EMBEDDING_DIM SEED ACTIVATION POOL EXTRA_CONV <<< "${item}"
  FILTERS_CSV="${FILTERS//-/,}"
  OUT_DIR="${STAGE_DIR}/${NAME}"
  SESSION="v8f4_${RUN_TAG}_${index}"
  EXTRA_FLAG=""
  if [[ "${EXTRA_CONV}" == "1" ]]; then
    EXTRA_FLAG="--extra-conv"
  fi
  mkdir -p "${OUT_DIR}"
  if tmux has-session -t "${SESSION}" 2>/dev/null; then
    echo "exists ${SESSION}"
    index=$((index + 1))
    continue
  fi
  CMD="cd /home/madejuele/projects/2K0300/model_training && OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} TF_FORCE_GPU_ALLOW_GROWTH=true ./run_gpu.sh venv/bin/python train_v8_end_to_end_embedding.py --dataset-dir dataset --output-dir ${OUT_DIR} --filters ${FILTERS_CSV} --embedding-dim ${EMBEDDING_DIM} --seed ${SEED} --epochs ${EPOCHS} --warmup-epochs ${WARMUP_EPOCHS} --learning-rate ${LEARNING_RATE} --lambda-d4 ${LAMBDA_D4} --lambda-stress ${LAMBDA_STRESS} --lambda-var ${LAMBDA_VAR} --lambda-cov ${LAMBDA_COV} --lambda-norm ${LAMBDA_NORM} --norm-target ${NORM_TARGET} --metric-normalize-embeddings --embedding-output-mode raw --backbone-architecture ${ARCH} --activation ${ACTIVATION} --pool ${POOL} ${EXTRA_FLAG} --prototype-sources ${PROTOTYPE_SOURCES} --k-values ${K_VALUES} --quant-scales ${QUANT_SCALES} --true-loo-top ${TRUE_LOO_TOP} 2>&1 | tee -a ${OUT_DIR}/run.log"
  tmux new-session -d -s "${SESSION}" "${CMD}"
  echo "launched ${SESSION} ${NAME} arch=${ARCH} filters=${FILTERS_CSV} dim=${EMBEDDING_DIM} seed=${SEED}"
  index=$((index + 1))
done

echo "root=${ROOT}"
echo "stage_dir=${STAGE_DIR}"
