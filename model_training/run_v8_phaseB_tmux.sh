#!/usr/bin/env bash
set -euo pipefail

RUN_ID="${1:-v8_phaseB_embedding_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
STAGE="${V8_PHASEB_STAGE:-stageB_end_to_end_embedding}"
STAGE_DIR="${ROOT}/${STAGE}"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-34)"

CONFIGS="${V8_PHASEB_CONFIGS:-c5_10_20_d16:5-10-20:16;c5_10_20_d24:5-10-20:24;c4_8_16_d16:4-8-16:16;c4_8_16_d24:4-8-16:24}"
EPOCHS="${V8_PHASEB_EPOCHS:-900}"
WARMUP_EPOCHS="${V8_PHASEB_WARMUP_EPOCHS:-250}"
SEED="${V8_PHASEB_SEED:-20260519}"
LEARNING_RATE="${V8_PHASEB_LR:-0.0015}"
LAMBDA_D4="${V8_PHASEB_LAMBDA_D4:-1.0}"
LAMBDA_STRESS="${V8_PHASEB_LAMBDA_STRESS:-0.25}"
PROTOTYPE_SOURCES="${V8_PHASEB_PROTOTYPE_SOURCES:-medoid,kmeans}"
K_VALUES="${V8_PHASEB_K_VALUES:-1,2,4,8,16}"
QUANT_SCALES="${V8_PHASEB_QUANT_SCALES:-8,12,16,24,32,48,64,96,128}"
OMP_THREADS="${OMP_THREADS:-1}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-1}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"

mkdir -p "${ROOT}" "${STAGE_DIR}"
printf '%s\n' "${RUN_ID}" > experiments/v8_phaseB_active_run.txt

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "stage": "${STAGE}",
  "stage_dir": "${STAGE_DIR}",
  "configs": "${CONFIGS}",
  "epochs": ${EPOCHS},
  "warmup_epochs": ${WARMUP_EPOCHS},
  "seed": ${SEED},
  "learning_rate": ${LEARNING_RATE},
  "lambda_d4": ${LAMBDA_D4},
  "lambda_stress": ${LAMBDA_STRESS},
  "prototype_sources": "${PROTOTYPE_SOURCES}",
  "k_values": "${K_VALUES}",
  "quant_scales": "${QUANT_SCALES}",
  "note": "V8 Phase B end-to-end tiny embedding backbone. Configs run sequentially in one tmux session to avoid GPU contention."
}
EOF

if [[ "${V8_PHASEB_FOREGROUND:-0}" != "1" ]]; then
  SESSION="v8b_${RUN_TAG}"
  if tmux has-session -t "${SESSION}" 2>/dev/null; then
    echo "exists ${SESSION}"
    echo "root=${ROOT}"
    exit 0
  fi
  CMD="cd /home/madejuele/projects/2K0300/model_training && V8_PHASEB_FOREGROUND=1 OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} ./run_v8_phaseB_tmux.sh ${RUN_ID} 2>&1 | tee -a ${ROOT}/phaseB.log"
  tmux new-session -d -s "${SESSION}" "${CMD}"
  echo "launched ${SESSION}"
  echo "root=${ROOT}"
  echo "stage_dir=${STAGE_DIR}"
  exit 0
fi

IFS=';' read -r -a CONFIG_ARRAY <<< "${CONFIGS}"
for item in "${CONFIG_ARRAY[@]}"; do
  IFS=':' read -r NAME FILTERS EMBEDDING_DIM <<< "${item}"
  FILTERS_CSV="${FILTERS//-/,}"
  OUT_DIR="${STAGE_DIR}/${NAME}"
  mkdir -p "${OUT_DIR}"
  echo "phaseB_start name=${NAME} filters=${FILTERS_CSV} embedding_dim=${EMBEDDING_DIM}"
  ./run_gpu.sh venv/bin/python train_v8_end_to_end_embedding.py \
    --dataset-dir dataset \
    --output-dir "${OUT_DIR}" \
    --filters "${FILTERS_CSV}" \
    --embedding-dim "${EMBEDDING_DIM}" \
    --seed "${SEED}" \
    --epochs "${EPOCHS}" \
    --warmup-epochs "${WARMUP_EPOCHS}" \
    --learning-rate "${LEARNING_RATE}" \
    --lambda-d4 "${LAMBDA_D4}" \
    --lambda-stress "${LAMBDA_STRESS}" \
    --prototype-sources "${PROTOTYPE_SOURCES}" \
    --k-values "${K_VALUES}" \
    --quant-scales "${QUANT_SCALES}"
  echo "phaseB_done name=${NAME}"
done
