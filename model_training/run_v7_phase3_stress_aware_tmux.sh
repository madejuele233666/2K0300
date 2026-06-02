#!/usr/bin/env bash
set -euo pipefail

RUN_ID="${1:-v7_phase3_stress_aware_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
STAGE="${V7_PHASE3_STAGE:-stage1_stress_aware_adapter_router}"
STAGE_DIR="${ROOT}/${STAGE}"
CACHE_DIR="${ROOT}/feature_cache"
FEATURE_CACHE="${CACHE_DIR}/old_rescue_clean_all_stress_gap_logits.npz"
RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-34)"

OLD_TFLITE="${V7_OLD_TFLITE:-experiments/v6_parent100_20260515_0001/stage5_parent100_neighborhood/shard_12/artifacts/p100_head_parent_s4_integrated_084_a5024/seed_20263104/model_int8.tflite}"
RESCUE_TFLITE="${V7_RESCUE_TFLITE:-experiments/v6_ctd_round1_20260516_0001/stage6_ctd_round1/shard_10/artifacts/ctd1_head_parent_weapon_c4_box_circuit_t0.2_p100_cal_balanced_clean_s4_int/seed_20263203/model_int8.tflite}"
STRESS_LIST="${STRESS_LIST:-rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,noise_0p06,hblur5_noise_0p06,diagblur5_noise_0p08,noise_0p10,vblur5,diagblur5,cam_blur2a0,cam_blur3a90,cam_blur5a45,cam_blur5a135,cam_noise0p02,cam_noise0p04,cam_blur3a0_noise0p02,cam_blur5a45_noise0p04}"

SHARDS="${V7_PHASE3_SHARDS:-16}"
MAX_ADAPTER_COMBOS="${V7_PHASE3_MAX_ADAPTER_COMBOS:-1200}"
ROUTER_TOP_ADAPTERS="${V7_PHASE3_ROUTER_TOP_ADAPTERS:-96}"
MAX_THRESHOLDS="${V7_PHASE3_MAX_THRESHOLDS:-36}"
MAX_GATES_PER_ADAPTER="${V7_PHASE3_MAX_GATES_PER_ADAPTER:-360}"
OMP_THREADS="${OMP_THREADS:-1}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-1}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"

VIEW_PROFILES="${V7_PHASE3_VIEW_PROFILES:-rotmirror,rotmirror_noise,rotmirror_blur_noise,rotmirror_camera_light,rotmirror_camera_full,worst_phase2,all_stress}"
POSITIVE_PROFILES="${V7_PHASE3_POSITIVE_PROFILES:-clean_rescue_all_views,view_rescue_preserve_locked,hybrid_clean_stress,margin_improve_hard,stress_union_conservative,stress_recovery_allow_stable}"
WEIGHT_PROFILES="${V7_PHASE3_WEIGHT_PROFILES:-balanced,preserve_locked,stable_locked,rescue_heavy,group_dro,camera_guard}"
TARGET_MODES="${V7_PHASE3_TARGET_MODES:-ctd_delta,margin_target,hybrid_ctd_margin}"
L2_VALUES="${V7_PHASE3_L2:-0.0001,0.001,0.01,0.1,1,10}"
GATE_L2_VALUES="${V7_PHASE3_GATE_L2:-0.0001,0.001,0.01,0.1}"
RANK_VALUES="${V7_PHASE3_RANK:-full,1,2,3,4,6}"
MASK_PERCENTILES="${V7_PHASE3_MASK_PERCENTILE:-0,60,75,85,92,96}"
ALPHA_VALUES="${V7_PHASE3_ALPHA:-0.5,0.75,1.0,1.25,1.5,2.0}"
MARGIN_VALUES="${V7_PHASE3_MARGIN:-1,2,4,6,8}"
BLEND_VALUES="${V7_PHASE3_BLEND:-0.25,0.5,0.75}"

mkdir -p "${ROOT}" "${STAGE_DIR}" "${CACHE_DIR}"
printf '%s\n' "${RUN_ID}" > experiments/v7_active_run.txt

for required in "${OLD_TFLITE}" "${RESCUE_TFLITE}"; do
  if [[ ! -f "${required}" ]]; then
    echo "missing required file: ${required}" >&2
    exit 2
  fi
done

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "stage": "${STAGE}",
  "stage_dir": "${STAGE_DIR}",
  "old_tflite": "${OLD_TFLITE}",
  "rescue_tflite": "${RESCUE_TFLITE}",
  "feature_cache": "${FEATURE_CACHE}",
  "shards": ${SHARDS},
  "max_adapter_combos_per_shard": ${MAX_ADAPTER_COMBOS},
  "router_top_adapters_per_shard": ${ROUTER_TOP_ADAPTERS},
  "max_thresholds": ${MAX_THRESHOLDS},
  "max_gates_per_adapter": ${MAX_GATES_PER_ADAPTER},
  "stress_list": "${STRESS_LIST}",
  "view_profiles": "${VIEW_PROFILES}",
  "positive_profiles": "${POSITIVE_PROFILES}",
  "weight_profiles": "${WEIGHT_PROFILES}",
  "target_modes": "${TARGET_MODES}",
  "l2": "${L2_VALUES}",
  "gate_l2": "${GATE_L2_VALUES}",
  "rank": "${RANK_VALUES}",
  "mask_percentile": "${MASK_PERCENTILES}",
  "alpha": "${ALPHA_VALUES}",
  "margin": "${MARGIN_VALUES}",
  "blend": "${BLEND_VALUES}",
  "note": "Phase3 stress-aware adapter/router search. rot/mirror views are mandatory in every view profile; blur/noise/camera profiles validate stress routing."
}
EOF

if [[ ! -f "${FEATURE_CACHE}" ]]; then
  OMP_NUM_THREADS="${OMP_THREADS}" \
  TF_NUM_INTRAOP_THREADS="${TF_INTRA_THREADS}" \
  TF_NUM_INTEROP_THREADS="${TF_INTER_THREADS}" \
  venv/bin/python run_v7_phase3_stress_aware_search.py \
    --dataset-dir dataset \
    --old-tflite "${OLD_TFLITE}" \
    --rescue-tflite "${RESCUE_TFLITE}" \
    --output-dir "${ROOT}/cache_prepare" \
    --feature-cache "${FEATURE_CACHE}" \
    --stress "${STRESS_LIST}" \
    --prepare-cache-only \
    2>&1 | tee -a "${ROOT}/prepare_feature_cache.log"
fi

for i in $(seq 0 $((SHARDS - 1))); do
  OUT_DIR="${STAGE_DIR}/shard_${i}"
  SESSION="v7p3_${RUN_TAG}_${i}"
  mkdir -p "${OUT_DIR}"
  if tmux has-session -t "${SESSION}" 2>/dev/null; then
    echo "exists ${SESSION}"
    continue
  fi
  CMD="cd /home/madejuele/projects/2K0300/model_training && OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} venv/bin/python run_v7_phase3_stress_aware_search.py --dataset-dir dataset --old-tflite ${OLD_TFLITE} --rescue-tflite ${RESCUE_TFLITE} --output-dir ${OUT_DIR} --feature-cache ${FEATURE_CACHE} --stress ${STRESS_LIST} --shard-count ${SHARDS} --shard-index ${i} --view-profiles ${VIEW_PROFILES} --positive-profiles ${POSITIVE_PROFILES} --weight-profiles ${WEIGHT_PROFILES} --target-modes ${TARGET_MODES} --l2 ${L2_VALUES} --gate-l2 ${GATE_L2_VALUES} --rank ${RANK_VALUES} --mask-percentile ${MASK_PERCENTILES} --alpha ${ALPHA_VALUES} --margin ${MARGIN_VALUES} --blend ${BLEND_VALUES} --max-adapter-combos ${MAX_ADAPTER_COMBOS} --router-top-adapters ${ROUTER_TOP_ADAPTERS} --max-thresholds ${MAX_THRESHOLDS} --max-gates-per-adapter ${MAX_GATES_PER_ADAPTER} 2>&1 | tee -a ${OUT_DIR}/run.log"
  tmux new-session -d -s "${SESSION}" "${CMD}"
  echo "launched ${SESSION}"
done

echo "v7_phase3_stress_aware root=${ROOT} stage=${STAGE_DIR} shards=${SHARDS}"
