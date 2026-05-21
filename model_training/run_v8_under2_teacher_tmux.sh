#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

RUN_ID="${1:-v8_under2_teacher_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
STAGE="${V8_U2_STAGE:-stageC_under2_teacher_embedding}"
STAGE_DIR="${ROOT}/${STAGE}"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-30)"

TEACHER_NPZ="${V8_U2_TEACHER_NPZ:-experiments/v8_fast4ms_repairs_20260520_0001/stageB_prototype_repair/s2d_c4_8_16_d24_s20260802_existing_repair/best_v8_repaired_joint2_params.npz}"

EPOCHS="${V8_U2_EPOCHS:-1200}"
WARMUP_EPOCHS="${V8_U2_WARMUP_EPOCHS:-200}"
LEARNING_RATE="${V8_U2_LR:-0.0018}"
PROXY_SCALE="${V8_U2_PROXY_SCALE:-24.0}"
LAMBDA_D4="${V8_U2_LAMBDA_D4:-1.2}"
LAMBDA_STRESS="${V8_U2_LAMBDA_STRESS:-0.35}"
LAMBDA_VAR="${V8_U2_LAMBDA_VAR:-0.30}"
LAMBDA_COV="${V8_U2_LAMBDA_COV:-0.02}"
LAMBDA_NORM="${V8_U2_LAMBDA_NORM:-0.55}"
NORM_TARGET="${V8_U2_NORM_TARGET:-1.0}"
LAMBDA_COMPILED_MARGIN="${V8_U2_LAMBDA_COMPILED_MARGIN:-0.50}"
LAMBDA_COMPILED_PULL="${V8_U2_LAMBDA_COMPILED_PULL:-0.02}"
COMPILED_MARGIN_TARGET="${V8_U2_COMPILED_MARGIN_TARGET:-0.02}"
COMPILED_MARGIN_ALPHA="${V8_U2_COMPILED_MARGIN_ALPHA:-32.0}"
TEACHER_START_EPOCH="${V8_U2_TEACHER_START_EPOCH:-1}"
PROTOTYPE_SOURCES="${V8_U2_PROTOTYPE_SOURCES:-kmeans,medoid}"
K_VALUES="${V8_U2_K_VALUES:-16,24,32,48,64,80,96,128,160,192}"
QUANT_SCALES="${V8_U2_QUANT_SCALES:-16,24,32,48,64,96,128}"
TRUE_LOO_TOP="${V8_U2_TRUE_LOO_TOP:-0}"
OMP_THREADS="${OMP_THREADS:-1}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-1}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"

# name:architecture:filters:embedding_dim:seed:activation:pool:extra_conv
CONFIGS="${V8_U2_CONFIGS:-\
hyb_c2_4_8_d24_s20260901:spacetodepth_hybrid:2-4-8:24:20260901:relu6:max:0;\
hyb_c2_6_12_d24_s20260902:spacetodepth_hybrid:2-6-12:24:20260902:relu6:max:0;\
s2d_c2_4_8_d24_s20260903:spacetodepth_conv:2-4-8:24:20260903:relu6:max:0;\
s2d_c2_6_12_d24_s20260904:spacetodepth_conv:2-6-12:24:20260904:relu6:max:0;\
hyb_c3_6_12_d24_s20260905:spacetodepth_hybrid:3-6-12:24:20260905:relu6:max:0;\
hyb_c2_4_8_d24_s20260906:spacetodepth_hybrid:2-4-8:24:20260906:relu6:avg:0}"

if [[ ! -f "${TEACHER_NPZ}" ]]; then
  echo "missing teacher npz: ${TEACHER_NPZ}" >&2
  exit 1
fi

mkdir -p "${ROOT}" "${STAGE_DIR}"
printf '%s\n' "${RUN_ID}" > experiments/v8_under2_teacher_active_run.txt

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "stage": "${STAGE}",
  "stage_dir": "${STAGE_DIR}",
  "teacher_npz": "${TEACHER_NPZ}",
  "configs": "${CONFIGS}",
  "epochs": ${EPOCHS},
  "warmup_epochs": ${WARMUP_EPOCHS},
  "learning_rate": ${LEARNING_RATE},
  "proxy_scale": ${PROXY_SCALE},
  "lambda_d4": ${LAMBDA_D4},
  "lambda_stress": ${LAMBDA_STRESS},
  "lambda_var": ${LAMBDA_VAR},
  "lambda_cov": ${LAMBDA_COV},
  "lambda_norm": ${LAMBDA_NORM},
  "norm_target": ${NORM_TARGET},
  "lambda_compiled_margin": ${LAMBDA_COMPILED_MARGIN},
  "lambda_compiled_pull": ${LAMBDA_COMPILED_PULL},
  "compiled_margin_target": ${COMPILED_MARGIN_TARGET},
  "compiled_margin_alpha": ${COMPILED_MARGIN_ALPHA},
  "teacher_start_epoch": ${TEACHER_START_EPOCH},
  "prototype_sources": "${PROTOTYPE_SOURCES}",
  "k_values": "${K_VALUES}",
  "quant_scales": "${QUANT_SCALES}",
  "target": "board_total_conservative_us <= 2000; clean/rot_mirror/stress and int8 variants all 100%; prefer <=1000 if a compiled non-parametric table remains small enough."
}
EOF

IFS=';' read -r -a CONFIG_ARRAY <<< "${CONFIGS}"
index=0
for item in "${CONFIG_ARRAY[@]}"; do
  IFS=':' read -r NAME ARCH FILTERS EMBEDDING_DIM SEED ACTIVATION POOL EXTRA_CONV <<< "${item}"
  FILTERS_CSV="${FILTERS//-/,}"
  OUT_DIR="${STAGE_DIR}/${NAME}"
  SESSION="v8u2_${RUN_TAG}_${index}"
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
  CMD="cd /home/madejuele/projects/2K0300/model_training && OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} TF_FORCE_GPU_ALLOW_GROWTH=true ./run_gpu.sh venv/bin/python train_v8_end_to_end_embedding.py --dataset-dir dataset --output-dir ${OUT_DIR} --filters ${FILTERS_CSV} --embedding-dim ${EMBEDDING_DIM} --seed ${SEED} --epochs ${EPOCHS} --warmup-epochs ${WARMUP_EPOCHS} --learning-rate ${LEARNING_RATE} --proxy-scale ${PROXY_SCALE} --lambda-d4 ${LAMBDA_D4} --lambda-stress ${LAMBDA_STRESS} --lambda-var ${LAMBDA_VAR} --lambda-cov ${LAMBDA_COV} --lambda-norm ${LAMBDA_NORM} --norm-target ${NORM_TARGET} --metric-normalize-embeddings --embedding-output-mode raw --backbone-architecture ${ARCH} --activation ${ACTIVATION} --pool ${POOL} ${EXTRA_FLAG} --compiled-teacher-npz ${TEACHER_NPZ} --lambda-compiled-margin ${LAMBDA_COMPILED_MARGIN} --lambda-compiled-pull ${LAMBDA_COMPILED_PULL} --compiled-margin-target ${COMPILED_MARGIN_TARGET} --compiled-margin-alpha ${COMPILED_MARGIN_ALPHA} --teacher-start-epoch ${TEACHER_START_EPOCH} --prototype-sources ${PROTOTYPE_SOURCES} --k-values ${K_VALUES} --quant-scales ${QUANT_SCALES} --true-loo-top ${TRUE_LOO_TOP} 2>&1 | tee -a ${OUT_DIR}/run.log"
  tmux new-session -d -s "${SESSION}" "${CMD}"
  echo "launched ${SESSION} ${NAME} arch=${ARCH} filters=${FILTERS_CSV} dim=${EMBEDDING_DIM} seed=${SEED}"
  index=$((index + 1))
done

echo "root=${ROOT}"
echo "stage_dir=${STAGE_DIR}"
