#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

RUN_ID="${1:-v8_tta_orbit_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
mkdir -p "${ROOT}"
printf "%s\n" "${RUN_ID}" > experiments/v8_tta_orbit_active_run.txt
RUN_TAG="$(printf "%s" "${RUN_ID}" | tr -c "A-Za-z0-9_" "_" | cut -c1-34)"

U6_RUN="experiments/v8_resource_parallel_20260521_0001/u6_wide_d20_identity_sgrank"
U10_RUN="experiments/v8_resource_parallel_20260521_0001/u10_wide_d20_rawlowedge_sgrank"
L1_RUN="experiments/v8_low_focus_parallel_20260521_0001/l1_u6_ft_strong_sg"
L2_RUN="experiments/v8_low_focus_parallel_20260521_0001/l2_u6_ft_dynqpair_axis"

U6_PARAMS="experiments/v8_resource_parallel_20260521_0001/u6_wide_d20_identity_sgrank_compile_dynamic_m4/best_parent_logits_memory_params.npz"
C1_PARAMS="experiments/v8_low_focus_parallel_20260521_0001/c1_u6_t8_compile_m8/best_parent_logits_memory_params.npz"
C5_PARAMS="experiments/v8_low_focus_parallel_20260521_0001/c5_u10_t8_sd_compile_m8/best_parent_logits_memory_params.npz"
L1_PARAMS="experiments/v8_low_focus_parallel_20260521_0001/l1_u6_ft_strong_sg_compile_m8/best_parent_logits_memory_params.npz"
L2_PARAMS="experiments/v8_low_focus_parallel_20260521_0001/l2_u6_ft_dynqpair_axis_compile_m8/best_parent_logits_memory_params.npz"

U6_STRESS="experiments/v8_resource_parallel_20260521_0001/u6_wide_d20_identity_sgrank_highstress_canonical/stress_events.csv"
C1_STRESS="experiments/v8_low_focus_parallel_20260521_0001/c1_u6_t8_highstress_canonical/stress_events.csv"
C5_STRESS="experiments/v8_low_focus_parallel_20260521_0001/c5_u10_t8_sd_highstress_canonical/stress_events.csv"
L1_STRESS="experiments/v8_low_focus_parallel_20260521_0001/l1_u6_ft_strong_sg_highstress_canonical/stress_events.csv"
L2_STRESS="experiments/v8_low_focus_parallel_20260521_0001/l2_u6_ft_dynqpair_axis_highstress_canonical/stress_events.csv"

launch_session() {
  local session="$1"
  local cmd="$2"
  if tmux has-session -t "${session}" 2>/dev/null; then
    echo "exists ${session}"
    return 0
  fi
  printf -v quoted_cmd "%q" "${cmd}"
  tmux new-session -d -s "${session}" "bash -lc ${quoted_cmd}"
  echo "launched ${session}"
}

launch_tta() {
  local name="$1"
  local tflite="$2"
  local params="$3"
  local stress="$4"
  local out_dir="${ROOT}/${name}"
  local session="v8tta_${RUN_TAG}_${name}"
  for path in "${tflite}" "${params}" "${stress}"; do
    if [[ ! -e "${path}" ]]; then
      echo "missing required path: ${path}" >&2
      exit 1
    fi
  done
  local cmd
  cmd=$(cat <<EOC
set -euo pipefail
cd ${SCRIPT_DIR}
mkdir -p ${out_dir}
echo tta_start ${out_dir}
CUDA_VISIBLE_DEVICES="" OMP_NUM_THREADS=2 TF_NUM_INTRAOP_THREADS=2 TF_NUM_INTEROP_THREADS=1 venv/bin/python analyze_v8_tta_orbit_policy.py \\
  --tflite ${tflite} \\
  --params-npz ${params} \\
  --stress-events-csv ${stress} \\
  --dataset-dir dataset \\
  --output-dir ${out_dir} \\
  --orbit-views identity,rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270 \\
  --guard-thresholds 1,2,4,8,16,32,64,128 \\
  --highstress-seed 20260520 2>&1 | tee -a ${out_dir}/run.log
echo tta_done ${out_dir}
EOC
)
  launch_session "${session}" "${cmd}"
}

cat > "${ROOT}/launch_config.json" <<EOC
{
  "run_id": "${RUN_ID}",
  "purpose": "Fixed TTA/orbit inference policies over existing normal-trained V8 models; high-pressure is evaluation only.",
  "high_pressure_usage": "evaluation_only",
  "training_usage": "none"
}
EOC

launch_tta "u6_m4" "${U6_RUN}/parent_int8.tflite" "${U6_PARAMS}" "${U6_STRESS}"
launch_tta "c1_u6_m8" "${U6_RUN}/parent_int8.tflite" "${C1_PARAMS}" "${C1_STRESS}"
launch_tta "c5_rawlowedge_m8" "${U10_RUN}/parent_int8.tflite" "${C5_PARAMS}" "${C5_STRESS}"
launch_tta "l1_strong_sg_m8" "${L1_RUN}/parent_int8.tflite" "${L1_PARAMS}" "${L1_STRESS}"
launch_tta "l2_dynqpair_m8" "${L2_RUN}/parent_int8.tflite" "${L2_PARAMS}" "${L2_STRESS}"

echo "root=${ROOT}"
echo "sessions:"
tmux ls | grep -E "v8tta_${RUN_TAG}" | sort || true
