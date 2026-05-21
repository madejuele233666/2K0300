#!/usr/bin/env bash
set -euo pipefail

RUN_ID="${1:-v8_phaseB_focus_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
STAGE="${V8_PHASEB_STAGE:-stageB_focus_end_to_end_embedding}"
STAGE_DIR="${ROOT}/${STAGE}"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-30)"

EPOCHS="${V8_PHASEB_EPOCHS:-900}"
WARMUP_EPOCHS="${V8_PHASEB_WARMUP_EPOCHS:-250}"
LEARNING_RATE="${V8_PHASEB_LR:-0.0015}"
LAMBDA_D4="${V8_PHASEB_LAMBDA_D4:-1.0}"
LAMBDA_STRESS="${V8_PHASEB_LAMBDA_STRESS:-0.25}"
PROTOTYPE_SOURCES="${V8_PHASEB_PROTOTYPE_SOURCES:-medoid,kmeans}"
K_VALUES="${V8_PHASEB_K_VALUES:-16,24,32,48,64,96}"
QUANT_SCALES="${V8_PHASEB_QUANT_SCALES:-8,12,16,24,32,48,64,96,128}"
OMP_THREADS="${OMP_THREADS:-1}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-1}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"

CONFIGS="${V8_PHASEB_CONFIGS:-\
c6_12_24_d24_s20260521:6-12-24:24:20260521;\
c6_12_24_d32_s20260521:6-12-24:32:20260521;\
c6_12_24_d24_s20260522:6-12-24:24:20260522;\
c6_12_24_d32_s20260522:6-12-24:32:20260522;\
c6_12_24_d24_s20260523:6-12-24:24:20260523;\
c6_12_24_d16_s20260521:6-12-24:16:20260521;\
c5_10_20_d24_s20260521:5-10-20:24:20260521;\
c5_10_20_d32_s20260521:5-10-20:32:20260521;\
c5_10_20_d24_s20260522:5-10-20:24:20260522;\
c5_10_20_d32_s20260522:5-10-20:32:20260522;\
c5_10_20_d24_s20260523:5-10-20:24:20260523;\
c5_10_20_d16_s20260521:5-10-20:16:20260521;\
c4_8_16_d24_s20260521:4-8-16:24:20260521;\
c4_8_16_d32_s20260521:4-8-16:32:20260521;\
c4_8_16_d24_s20260522:4-8-16:24:20260522;\
c4_8_16_d32_s20260522:4-8-16:32:20260522;\
c4_8_16_d16_s20260521:4-8-16:16:20260521;\
c3_6_12_d32_s20260521:3-6-12:32:20260521}"

mkdir -p "${ROOT}" "${STAGE_DIR}"
printf '%s\n' "${RUN_ID}" > experiments/v8_phaseB_focus_active_run.txt

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
  "prototype_sources": "${PROTOTYPE_SOURCES}",
  "k_values": "${K_VALUES}",
  "quant_scales": "${QUANT_SCALES}",
  "note": "V8 Phase B focused @tf.function control/primary configs with expanded K sweep."
}
EOF

IFS=';' read -r -a CONFIG_ARRAY <<< "${CONFIGS}"
index=0
for item in "${CONFIG_ARRAY[@]}"; do
  IFS=':' read -r NAME FILTERS EMBEDDING_DIM SEED <<< "${item}"
  FILTERS_CSV="${FILTERS//-/,}"
  OUT_DIR="${STAGE_DIR}/${NAME}"
  SESSION="v8bf_${RUN_TAG}_${index}"
  mkdir -p "${OUT_DIR}"
  if tmux has-session -t "${SESSION}" 2>/dev/null; then
    echo "exists ${SESSION}"
    index=$((index + 1))
    continue
  fi
  CMD="cd /home/madejuele/projects/2K0300/model_training && OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} TF_FORCE_GPU_ALLOW_GROWTH=true ./run_gpu.sh venv/bin/python train_v8_end_to_end_embedding.py --dataset-dir dataset --output-dir ${OUT_DIR} --filters ${FILTERS_CSV} --embedding-dim ${EMBEDDING_DIM} --seed ${SEED} --epochs ${EPOCHS} --warmup-epochs ${WARMUP_EPOCHS} --learning-rate ${LEARNING_RATE} --lambda-d4 ${LAMBDA_D4} --lambda-stress ${LAMBDA_STRESS} --prototype-sources ${PROTOTYPE_SOURCES} --k-values ${K_VALUES} --quant-scales ${QUANT_SCALES} 2>&1 | tee -a ${OUT_DIR}/run.log"
  tmux new-session -d -s "${SESSION}" "${CMD}"
  echo "launched ${SESSION} ${NAME} filters=${FILTERS_CSV} dim=${EMBEDDING_DIM} seed=${SEED}"
  index=$((index + 1))
done

echo "root=${ROOT}"
echo "stage_dir=${STAGE_DIR}"
