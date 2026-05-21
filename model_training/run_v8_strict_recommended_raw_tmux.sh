#!/usr/bin/env bash
set -euo pipefail

RUN_ID="${1:-v8_strict_recommended_raw_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
STAGE="${V8_STRICT_STAGE:-stageB_strict_recommended_raw_embedding}"
STAGE_DIR="${ROOT}/${STAGE}"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-30)"

EPOCHS="${V8_STRICT_EPOCHS:-900}"
WARMUP_EPOCHS="${V8_STRICT_WARMUP_EPOCHS:-250}"
LEARNING_RATE="${V8_STRICT_LR:-0.0015}"
LAMBDA_D4="${V8_STRICT_LAMBDA_D4:-1.0}"
LAMBDA_STRESS="${V8_STRICT_LAMBDA_STRESS:-0.25}"
LAMBDA_VAR="${V8_STRICT_LAMBDA_VAR:-0.25}"
LAMBDA_COV="${V8_STRICT_LAMBDA_COV:-0.02}"
LAMBDA_NORM="${V8_STRICT_LAMBDA_NORM:-0.08}"
NORM_TARGET="${V8_STRICT_NORM_TARGET:-1.0}"
PROTOTYPE_SOURCES="${V8_STRICT_PROTOTYPE_SOURCES:-medoid,kmeans}"
K_VALUES="${V8_STRICT_K_VALUES:-4,8,16,24,32,48,64,96,128}"
QUANT_SCALES="${V8_STRICT_QUANT_SCALES:-8,12,16,24,32,48,64,96,128}"
TRUE_LOO_TOP="${V8_STRICT_TRUE_LOO_TOP:-0}"
OMP_THREADS="${OMP_THREADS:-1}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-1}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"

CONFIGS="${V8_STRICT_CONFIGS:-\
c6_12_24_d24_s20260601:6-12-24:24:20260601;\
c6_12_24_d24_s20260602:6-12-24:24:20260602;\
c6_12_24_d32_s20260603:6-12-24:32:20260603;\
c5_10_20_d24_s20260604:5-10-20:24:20260604;\
c5_10_20_d32_s20260605:5-10-20:32:20260605;\
c4_8_16_d24_s20260606:4-8-16:24:20260606}"

mkdir -p "${ROOT}" "${STAGE_DIR}"
printf '%s\n' "${RUN_ID}" > experiments/v8_strict_recommended_raw_active_run.txt

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
  "prototype_sources": "${PROTOTYPE_SOURCES}",
  "k_values": "${K_VALUES}",
  "quant_scales": "${QUANT_SCALES}",
  "true_loo_top": ${TRUE_LOO_TOP},
  "embedding_output_mode": "raw",
  "strict_recommended_raw_tflite_ops": ["SPACE_TO_DEPTH", "CONV_2D", "MAX_POOL_2D", "MEAN", "FULLY_CONNECTED"],
  "note": "V8 strict recommended operator rerun. The TFLite embedding graph ends at raw Dense output; normalization is only a training regularizer."
}
EOF

IFS=';' read -r -a CONFIG_ARRAY <<< "${CONFIGS}"
index=0
for item in "${CONFIG_ARRAY[@]}"; do
  IFS=':' read -r NAME FILTERS EMBEDDING_DIM SEED <<< "${item}"
  FILTERS_CSV="${FILTERS//-/,}"
  OUT_DIR="${STAGE_DIR}/${NAME}"
  SESSION="v8sr_${RUN_TAG}_${index}"
  mkdir -p "${OUT_DIR}"
  if tmux has-session -t "${SESSION}" 2>/dev/null; then
    echo "exists ${SESSION}"
    index=$((index + 1))
    continue
  fi
  CMD="cd /home/madejuele/projects/2K0300/model_training && OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} TF_FORCE_GPU_ALLOW_GROWTH=true ./run_gpu.sh venv/bin/python train_v8_end_to_end_embedding.py --dataset-dir dataset --output-dir ${OUT_DIR} --filters ${FILTERS_CSV} --embedding-dim ${EMBEDDING_DIM} --seed ${SEED} --epochs ${EPOCHS} --warmup-epochs ${WARMUP_EPOCHS} --learning-rate ${LEARNING_RATE} --lambda-d4 ${LAMBDA_D4} --lambda-stress ${LAMBDA_STRESS} --lambda-var ${LAMBDA_VAR} --lambda-cov ${LAMBDA_COV} --lambda-norm ${LAMBDA_NORM} --norm-target ${NORM_TARGET} --embedding-output-mode raw --prototype-sources ${PROTOTYPE_SOURCES} --k-values ${K_VALUES} --quant-scales ${QUANT_SCALES} --true-loo-top ${TRUE_LOO_TOP} 2>&1 | tee -a ${OUT_DIR}/run.log"
  tmux new-session -d -s "${SESSION}" "${CMD}"
  echo "launched ${SESSION} ${NAME} filters=${FILTERS_CSV} dim=${EMBEDDING_DIM} seed=${SEED}"
  index=$((index + 1))
done

echo "root=${ROOT}"
echo "stage_dir=${STAGE_DIR}"
