#!/usr/bin/env bash
set -euo pipefail

RUN_ID="${1:-v8_compiled_teacher_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
STAGE="${V8_TEACHER_STAGE:-stageB2_6_compiled_teacher_finetune}"
STAGE_DIR="${ROOT}/${STAGE}"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-30)"

INIT_MODEL="${V8_TEACHER_INIT_MODEL:-experiments/v8_phaseB_focus_tf_function_20260519_0003/stageB_focus_end_to_end_embedding/c6_12_24_d24_s20260522/embedding_model.keras}"
TEACHER_NPZ="${V8_TEACHER_NPZ:-experiments/v8_closed_set_compiler_teacher_kmeans32_20260519_0001/best_compiled_v8_prototype_params.npz}"
EPOCHS="${V8_TEACHER_EPOCHS:-360}"
WARMUP_EPOCHS="${V8_TEACHER_WARMUP_EPOCHS:-0}"
FILTERS="${V8_TEACHER_FILTERS:-6,12,24}"
EMBEDDING_DIM="${V8_TEACHER_EMBEDDING_DIM:-24}"
PROTOTYPE_SOURCES="${V8_TEACHER_PROTOTYPE_SOURCES:-kmeans,medoid}"
K_VALUES="${V8_TEACHER_K_VALUES:-24,32,48,64}"
QUANT_SCALES="${V8_TEACHER_QUANT_SCALES:-32,48,64,96,128}"
LAMBDA_D4="${V8_TEACHER_LAMBDA_D4:-1.0}"
LAMBDA_STRESS="${V8_TEACHER_LAMBDA_STRESS:-0.25}"
INIT_PROXIES_FLAG="${V8_TEACHER_INIT_PROXIES_FLAG:---init-proxies-from-embeddings}"
OMP_THREADS="${OMP_THREADS:-1}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-1}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"

CONFIGS="${V8_TEACHER_CONFIGS:-\
t_lr5e5_m002_p001_s20260522:0.00005:0.02:0.01:0.02:20260522;\
t_lr5e5_m005_p001_s20260523:0.00005:0.05:0.01:0.02:20260523;\
t_lr8e5_m005_p002_s20260524:0.00008:0.05:0.02:0.02:20260524;\
t_lr5e5_m010_p001_s20260525:0.00005:0.10:0.01:0.03:20260525}"

mkdir -p "${ROOT}" "${STAGE_DIR}"
printf '%s\n' "${RUN_ID}" > experiments/v8_compiled_teacher_active_run.txt

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "stage": "${STAGE}",
  "stage_dir": "${STAGE_DIR}",
  "init_model": "${INIT_MODEL}",
  "teacher_npz": "${TEACHER_NPZ}",
  "configs": "${CONFIGS}",
  "epochs": ${EPOCHS},
  "warmup_epochs": ${WARMUP_EPOCHS},
  "filters": "${FILTERS}",
  "embedding_dim": ${EMBEDDING_DIM},
  "prototype_sources": "${PROTOTYPE_SOURCES}",
  "k_values": "${K_VALUES}",
  "quant_scales": "${QUANT_SCALES}",
  "init_proxies_flag": "${INIT_PROXIES_FLAG}",
  "note": "V8 B2.6 warm-start fine-tune with closed-set compiled prototype teacher."
}
EOF

IFS=';' read -r -a CONFIG_ARRAY <<< "${CONFIGS}"
index=0
for item in "${CONFIG_ARRAY[@]}"; do
  IFS=':' read -r NAME LR LAMBDA_MARGIN LAMBDA_PULL MARGIN_TARGET SEED <<< "${item}"
  OUT_DIR="${STAGE_DIR}/${NAME}"
  SESSION="v8ct_${RUN_TAG}_${index}"
  mkdir -p "${OUT_DIR}"
  if tmux has-session -t "${SESSION}" 2>/dev/null; then
    echo "exists ${SESSION}"
    index=$((index + 1))
    continue
  fi
  CMD="cd /home/madejuele/projects/2K0300/model_training && OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} TF_FORCE_GPU_ALLOW_GROWTH=true ./run_gpu.sh venv/bin/python train_v8_end_to_end_embedding.py --dataset-dir dataset --output-dir ${OUT_DIR} --filters ${FILTERS} --embedding-dim ${EMBEDDING_DIM} --seed ${SEED} --epochs ${EPOCHS} --warmup-epochs ${WARMUP_EPOCHS} --learning-rate ${LR} --lambda-d4 ${LAMBDA_D4} --lambda-stress ${LAMBDA_STRESS} --prototype-sources ${PROTOTYPE_SOURCES} --k-values ${K_VALUES} --quant-scales ${QUANT_SCALES} --init-model ${INIT_MODEL} --compiled-teacher-npz ${TEACHER_NPZ} --lambda-compiled-margin ${LAMBDA_MARGIN} --lambda-compiled-pull ${LAMBDA_PULL} --compiled-margin-target ${MARGIN_TARGET} --teacher-start-epoch 1 ${INIT_PROXIES_FLAG} 2>&1 | tee -a ${OUT_DIR}/run.log"
  tmux new-session -d -s "${SESSION}" "${CMD}"
  echo "launched ${SESSION} ${NAME} lr=${LR} margin=${LAMBDA_MARGIN} pull=${LAMBDA_PULL} target=${MARGIN_TARGET} seed=${SEED}"
  index=$((index + 1))
done

echo "root=${ROOT}"
echo "stage_dir=${STAGE_DIR}"
