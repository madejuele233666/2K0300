#!/usr/bin/env bash
set -euo pipefail

RUN_ID="${1:-v8_closed_set_compiler_cpu_queue_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
THREADS_PER_JOB="${V8_CPU_COMPILER_THREADS_PER_JOB:-4}"
SOURCES="${V8_CPU_COMPILER_SOURCES:-existing,kmeans,medoid,quant_medoid,kcenter}"
K_VALUES="${V8_CPU_COMPILER_K_VALUES:-24,32,48,64,96}"
QUANT_SCALES="${V8_CPU_COMPILER_QUANT_SCALES:-24,32,48,64,96,128}"
LOW_MARGIN_TOP="${V8_CPU_COMPILER_LOW_MARGIN_TOP:-192}"
DEFENSE_PER_EVENT="${V8_CPU_COMPILER_DEFENSE_PER_EVENT:-3}"
MAX_RESERVED="${V8_CPU_COMPILER_MAX_RESERVED:-40}"
SNAPSHOT_BUDGETS="${V8_CPU_COMPILER_SNAPSHOT_BUDGETS:-0,4,8,16,24,32,40}"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-30)"

CONFIGS="${V8_CPU_COMPILER_CONFIGS:-\
c6_d24_s21:experiments/v8_phaseB_focus_tf_function_20260519_0003/stageB_focus_end_to_end_embedding/c6_12_24_d24_s20260521/best_v8_embedding_prototype_params.npz;\
c6_d24_s22:experiments/v8_phaseB_focus_tf_function_20260519_0003/stageB_focus_end_to_end_embedding/c6_12_24_d24_s20260522/best_v8_embedding_prototype_params.npz;\
c6_d24_s23:experiments/v8_phaseB_focus_tf_function_20260519_0003/stageB_focus_end_to_end_embedding/c6_12_24_d24_s20260523/best_v8_embedding_prototype_params.npz;\
c6_d32_s21:experiments/v8_phaseB_focus_tf_function_20260519_0003/stageB_focus_end_to_end_embedding/c6_12_24_d32_s20260521/best_v8_embedding_prototype_params.npz;\
c5_d24_s21:experiments/v8_phaseB_focus_tf_function_20260519_0001/stageB_focus_end_to_end_embedding/c5_10_20_d24_s20260521/best_v8_embedding_prototype_params.npz;\
c5_d24_s22:experiments/v8_phaseB_focus_tf_function_20260519_0001/stageB_focus_end_to_end_embedding/c5_10_20_d24_s20260522/best_v8_embedding_prototype_params.npz;\
c5_d24_s23:experiments/v8_phaseB_focus_tf_function_20260519_0001/stageB_focus_end_to_end_embedding/c5_10_20_d24_s20260523/best_v8_embedding_prototype_params.npz;\
c4_d24_s22:experiments/v8_phaseB_focus_tf_function_20260519_0001/stageB_focus_end_to_end_embedding/c4_8_16_d24_s20260522/best_v8_embedding_prototype_params.npz}"

mkdir -p "${ROOT}"
cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "threads_per_job": ${THREADS_PER_JOB},
  "sources": "${SOURCES}",
  "k_values": "${K_VALUES}",
  "quant_scales": "${QUANT_SCALES}",
  "low_margin_top": ${LOW_MARGIN_TOP},
  "defense_per_event": ${DEFENSE_PER_EVENT},
  "max_reserved": ${MAX_RESERVED},
  "snapshot_budgets": "${SNAPSHOT_BUDGETS}",
  "configs": "${CONFIGS}",
  "note": "Wide CPU-only closed-set compiler queue for V8 embeddings."
}
EOF

IFS=';' read -r -a CONFIG_ARRAY <<< "${CONFIGS}"
index=0
for item in "${CONFIG_ARRAY[@]}"; do
  IFS=':' read -r NAME PARAMS_NPZ <<< "${item}"
  OUT_DIR="${ROOT}/${NAME}"
  SESSION="v8cc_${RUN_TAG}_${index}"
  mkdir -p "${OUT_DIR}"
  if [[ ! -f "${PARAMS_NPZ}" ]]; then
    echo "missing ${NAME}: ${PARAMS_NPZ}"
    index=$((index + 1))
    continue
  fi
  if tmux has-session -t "${SESSION}" 2>/dev/null; then
    echo "exists ${SESSION}"
    index=$((index + 1))
    continue
  fi
  CMD="cd /home/madejuele/projects/2K0300/model_training && mkdir -p ${OUT_DIR} && OMP_NUM_THREADS=${THREADS_PER_JOB} OPENBLAS_NUM_THREADS=${THREADS_PER_JOB} MKL_NUM_THREADS=${THREADS_PER_JOB} NUMEXPR_NUM_THREADS=${THREADS_PER_JOB} venv/bin/python run_v8_closed_set_prototype_compiler.py --params-npz ${PARAMS_NPZ} --output-dir ${OUT_DIR} --sources ${SOURCES} --k-values ${K_VALUES} --low-margin-top ${LOW_MARGIN_TOP} --defense-per-event ${DEFENSE_PER_EVENT} --max-reserved ${MAX_RESERVED} --snapshot-budgets ${SNAPSHOT_BUDGETS} --quant-scales ${QUANT_SCALES} 2>&1 | tee -a ${OUT_DIR}/run.log"
  tmux new-session -d -s "${SESSION}" "${CMD}"
  echo "launched ${SESSION} ${NAME}"
  index=$((index + 1))
done

echo "root=${ROOT}"
