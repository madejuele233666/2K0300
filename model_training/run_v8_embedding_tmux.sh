#!/usr/bin/env bash
set -euo pipefail

RUN_ID="${1:-v8_pure_embedding_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
STAGE="${V8_STAGE:-stageA_frozen_fast_metric}"
STAGE_DIR="${ROOT}/${STAGE}"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-34)"

FEATURE_CACHE="${V8_FEATURE_CACHE:-experiments/v7_phase6_fastbackbone_proto_20260519_0001/feature_cache/fast_old_stress_gap_logits.npz}"
PHASE="${V8_PHASE:-phaseA}"
EMBEDDING_DIMS="${V8_EMBEDDING_DIMS:-16,24,32}"
SEEDS="${V8_SEEDS:-20260519}"
EPOCHS="${V8_EPOCHS:-600}"
LEARNING_RATE="${V8_LEARNING_RATE:-0.01}"
PROXY_SCALE="${V8_PROXY_SCALE:-16.0}"
LAMBDA_D4="${V8_LAMBDA_D4:-1.0}"
LAMBDA_VAR="${V8_LAMBDA_VAR:-0.25}"
LAMBDA_COV="${V8_LAMBDA_COV:-0.02}"
VARIANCE_FLOOR="${V8_VARIANCE_FLOOR:-0.08}"
PROTOTYPE_SOURCES="${V8_PROTOTYPE_SOURCES:-medoid,kmeans}"
K_VALUES="${V8_K_VALUES:-1,2,4,8,16}"
QUANT_SCALES="${V8_QUANT_SCALES:-8,12,16,24,32,48,64,96,128}"
OMP_THREADS="${OMP_THREADS:-1}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-1}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"

mkdir -p "${ROOT}" "${STAGE_DIR}"
printf '%s\n' "${RUN_ID}" > experiments/v8_active_run.txt

if [[ ! -f "${FEATURE_CACHE}" ]]; then
  echo "missing required feature cache: ${FEATURE_CACHE}" >&2
  exit 2
fi

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "stage": "${STAGE}",
  "stage_dir": "${STAGE_DIR}",
  "feature_cache": "${FEATURE_CACHE}",
  "phase": "${PHASE}",
  "embedding_dims": "${EMBEDDING_DIMS}",
  "seeds": "${SEEDS}",
  "epochs": ${EPOCHS},
  "learning_rate": ${LEARNING_RATE},
  "proxy_scale": ${PROXY_SCALE},
  "lambda_d4": ${LAMBDA_D4},
  "lambda_var": ${LAMBDA_VAR},
  "lambda_cov": ${LAMBDA_COV},
  "variance_floor": ${VARIANCE_FLOOR},
  "prototype_sources": "${PROTOTYPE_SOURCES}",
  "k_values": "${K_VALUES}",
  "quant_scales": "${QUANT_SCALES}",
  "note": "V8 Phase A starts from frozen fast GAP: A0 compression baseline plus A1 shallow projection/proxy training."
}
EOF

SESSION="v8emb_${RUN_TAG}"
if tmux has-session -t "${SESSION}" 2>/dev/null; then
  echo "exists ${SESSION}"
  echo "root=${ROOT}"
  exit 0
fi

CMD="cd /home/madejuele/projects/2K0300/model_training && OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} ./run_gpu.sh venv/bin/python train_v8_pure_embedding.py --feature-cache ${FEATURE_CACHE} --output-dir ${STAGE_DIR} --phase ${PHASE} --embedding-dims ${EMBEDDING_DIMS} --seeds ${SEEDS} --epochs ${EPOCHS} --learning-rate ${LEARNING_RATE} --proxy-scale ${PROXY_SCALE} --lambda-d4 ${LAMBDA_D4} --lambda-var ${LAMBDA_VAR} --lambda-cov ${LAMBDA_COV} --variance-floor ${VARIANCE_FLOOR} --prototype-sources ${PROTOTYPE_SOURCES} --k-values ${K_VALUES} --quant-scales ${QUANT_SCALES} 2>&1 | tee -a ${ROOT}/run.log"
tmux new-session -d -s "${SESSION}" "${CMD}"

echo "launched ${SESSION}"
echo "root=${ROOT}"
echo "stage_dir=${STAGE_DIR}"
