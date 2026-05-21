#!/usr/bin/env bash
set -euo pipefail

RUN_ID="${1:-v8_extended_sweep_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-30)"

SOURCES="${V8_EXT_SOURCES:-kmeans,medoid,kcenter,quant_medoid}"
K_VALUES="${V8_EXT_K_VALUES:-16,24,32,48,64,96,128,192}"
QUANT_SCALES="${V8_EXT_QUANT_SCALES:-8,12,16,24,32,48,64,96,128}"
TRUE_LOO_TOP="${V8_EXT_TRUE_LOO_TOP:-2}"
BOUNDARY_Q="${V8_EXT_BOUNDARY_Q:-0.01}"
SEED="${V8_EXT_SEED:-20260519}"

PARAMS="${V8_EXT_PARAMS:-\
c6_12_24_d24:experiments/v8_phaseB_parallel16_gpu_20260519_0001/stageB_parallel16_end_to_end_embedding/c6_12_24_d24_s20260519/best_v8_embedding_prototype_params.npz;\
c6_12_24_d32:experiments/v8_phaseB_parallel16_gpu_20260519_0001/stageB_parallel16_end_to_end_embedding/c6_12_24_d32_s20260519/best_v8_embedding_prototype_params.npz;\
c5_10_20_d32:experiments/v8_phaseB_parallel16_gpu_20260519_0001/stageB_parallel16_end_to_end_embedding/c5_10_20_d32_s20260519/best_v8_embedding_prototype_params.npz;\
c6_12_24_d16:experiments/v8_phaseB_parallel16_gpu_20260519_0001/stageB_parallel16_end_to_end_embedding/c6_12_24_d16_s20260519/best_v8_embedding_prototype_params.npz;\
c5_10_20_d24:experiments/v8_phaseB_parallel16_gpu_20260519_0001/stageB_parallel16_end_to_end_embedding/c5_10_20_d24_s20260519/best_v8_embedding_prototype_params.npz;\
c5_10_20_d16:experiments/v8_phaseB_parallel16_gpu_20260519_0001/stageB_parallel16_end_to_end_embedding/c5_10_20_d16_s20260520/best_v8_embedding_prototype_params.npz;\
c4_8_16_d24_s20:experiments/v8_phaseB_parallel16_gpu_20260519_0001/stageB_parallel16_end_to_end_embedding/c4_8_16_d24_s20260520/best_v8_embedding_prototype_params.npz;\
c4_8_16_d32:experiments/v8_phaseB_parallel16_gpu_20260519_0001/stageB_parallel16_end_to_end_embedding/c4_8_16_d32_s20260519/best_v8_embedding_prototype_params.npz;\
c4_8_16_d24:experiments/v8_phaseB_parallel16_gpu_20260519_0001/stageB_parallel16_end_to_end_embedding/c4_8_16_d24_s20260519/best_v8_embedding_prototype_params.npz;\
c4_8_16_d16:experiments/v8_phaseB_parallel16_gpu_20260519_0001/stageB_parallel16_end_to_end_embedding/c4_8_16_d16_s20260520/best_v8_embedding_prototype_params.npz;\
c3_6_12_d24:experiments/v8_phaseB_parallel16_gpu_20260519_0001/stageB_parallel16_end_to_end_embedding/c3_6_12_d24_s20260519/best_v8_embedding_prototype_params.npz;\
c3_6_12_d16:experiments/v8_phaseB_parallel16_gpu_20260519_0001/stageB_parallel16_end_to_end_embedding/c3_6_12_d16_s20260519/best_v8_embedding_prototype_params.npz}"

mkdir -p "${ROOT}"
printf '%s\n' "${RUN_ID}" > experiments/v8_extended_sweep_active_run.txt

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "sources": "${SOURCES}",
  "k_values": "${K_VALUES}",
  "quant_scales": "${QUANT_SCALES}",
  "true_loo_top": ${TRUE_LOO_TOP},
  "boundary_low_margin_quantile": ${BOUNDARY_Q},
  "params": "${PARAMS}",
  "note": "V8-C0 extended prototype budget curve plus true rebuild-LOO diagnostics."
}
EOF

IFS=';' read -r -a PARAM_ARRAY <<< "${PARAMS}"
index=0
for item in "${PARAM_ARRAY[@]}"; do
  IFS=':' read -r NAME PARAM_NPZ <<< "${item}"
  OUT_DIR="${ROOT}/${NAME}"
  SESSION="v8ext_${RUN_TAG}_${index}"
  mkdir -p "${OUT_DIR}"
  if [[ ! -f "${PARAM_NPZ}" ]]; then
    echo "missing ${PARAM_NPZ}" >&2
    exit 2
  fi
  if tmux has-session -t "${SESSION}" 2>/dev/null; then
    echo "exists ${SESSION}"
    index=$((index + 1))
    continue
  fi
  CMD="cd /home/madejuele/projects/2K0300/model_training && venv/bin/python run_v8_extended_prototype_sweep.py --params-npz ${PARAM_NPZ} --output-dir ${OUT_DIR} --sources ${SOURCES} --k-values ${K_VALUES} --quant-scales ${QUANT_SCALES} --true-loo-top ${TRUE_LOO_TOP} --boundary-low-margin-quantile ${BOUNDARY_Q} --seed ${SEED} 2>&1 | tee -a ${OUT_DIR}/run.log"
  tmux new-session -d -s "${SESSION}" "${CMD}"
  echo "launched ${SESSION} ${NAME}"
  index=$((index + 1))
done

echo "root=${ROOT}"
