#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

RUN_ID="${1:-v8_synthetic_gate_current_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
mkdir -p "${ROOT}"
printf '%s\n' "${RUN_ID}" > experiments/v8_synthetic_gate_current_active_run.txt

RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-34)"
OMP_THREADS="${OMP_THREADS:-2}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-2}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"

BASE_PARAMS="experiments/v8_parent_c2612_d4_multiteacher_20260520_0001/s2d_c2_6_12_d4_shadow_init_compile/best_parent_logits_memory_params.npz"
U6_RUN="experiments/v8_resource_parallel_20260521_0001/u6_wide_d20_identity_sgrank"
U7_RUN="experiments/v8_resource_parallel_20260521_0001/u7_d16_identity_sg_sourcedecision"
U8_RUN="experiments/v8_resource_parallel_20260521_0001/u8_d16_rawlowedge_sg_dynqpair"
U10_RUN="experiments/v8_resource_parallel_20260521_0001/u10_wide_d20_rawlowedge_sgrank"
L1_RUN="experiments/v8_low_focus_parallel_20260521_0001/l1_u6_ft_strong_sg"
L2_RUN="experiments/v8_low_focus_parallel_20260521_0001/l2_u6_ft_dynqpair_axis"
L3_RUN="experiments/v8_low_focus_parallel_20260521_0001/l3_u6_ft_source_decision_margin"
L4_RUN="experiments/v8_low_focus_parallel_20260521_0001/l4_d24_partial_strong_sg"

U6_PARAMS="experiments/v8_resource_parallel_20260521_0001/u6_wide_d20_identity_sgrank_compile_dynamic_m4/best_parent_logits_memory_params.npz"
U7_PARAMS="experiments/v8_resource_parallel_20260521_0001/u7_d16_identity_sg_sourcedecision_compile_dynamic_m4/best_parent_logits_memory_params.npz"
U8_PARAMS="experiments/v8_resource_parallel_20260521_0001/u8_d16_rawlowedge_sg_dynqpair_compile_dynamic_m4/best_parent_logits_memory_params.npz"
C1_PARAMS="experiments/v8_low_focus_parallel_20260521_0001/c1_u6_t8_compile_m8/best_parent_logits_memory_params.npz"
C3_PARAMS="experiments/v8_low_focus_parallel_20260521_0001/c3_u6_t16_sd_compile_m16/best_parent_logits_memory_params.npz"
C5_PARAMS="experiments/v8_low_focus_parallel_20260521_0001/c5_u10_t8_sd_compile_m8/best_parent_logits_memory_params.npz"
L1_PARAMS="experiments/v8_low_focus_parallel_20260521_0001/l1_u6_ft_strong_sg_compile_m8/best_parent_logits_memory_params.npz"
L2_PARAMS="experiments/v8_low_focus_parallel_20260521_0001/l2_u6_ft_dynqpair_axis_compile_m8/best_parent_logits_memory_params.npz"
L3_PARAMS="experiments/v8_low_focus_parallel_20260521_0001/l3_u6_ft_source_decision_margin_compile_m8/best_parent_logits_memory_params.npz"
L4_PARAMS="experiments/v8_low_focus_parallel_20260521_0001/l4_d24_partial_strong_sg_compile_m8/best_parent_logits_memory_params.npz"

U6_STRESS="experiments/v8_resource_parallel_20260521_0001/u6_wide_d20_identity_sgrank_highstress_canonical/stress_events.csv"
U7_STRESS="experiments/v8_resource_parallel_20260521_0001/u7_d16_identity_sg_sourcedecision_highstress_canonical/stress_events.csv"
U8_STRESS="experiments/v8_resource_parallel_20260521_0001/u8_d16_rawlowedge_sg_dynqpair_highstress_canonical/stress_events.csv"
C1_STRESS="experiments/v8_low_focus_parallel_20260521_0001/c1_u6_t8_highstress_canonical/stress_events.csv"
C3_STRESS="experiments/v8_low_focus_parallel_20260521_0001/c3_u6_t16_sd_highstress_canonical/stress_events.csv"
C5_STRESS="experiments/v8_low_focus_parallel_20260521_0001/c5_u10_t8_sd_highstress_canonical/stress_events.csv"
L1_STRESS="experiments/v8_low_focus_parallel_20260521_0001/l1_u6_ft_strong_sg_highstress_canonical/stress_events.csv"
L2_STRESS="experiments/v8_low_focus_parallel_20260521_0001/l2_u6_ft_dynqpair_axis_highstress_canonical/stress_events.csv"
L3_STRESS="experiments/v8_low_focus_parallel_20260521_0001/l3_u6_ft_source_decision_margin_highstress_canonical/stress_events.csv"
L4_STRESS="experiments/v8_low_focus_parallel_20260521_0001/l4_d24_partial_strong_sg_highstress_canonical/stress_events.csv"

PERTURBS="${PERTURBS:-shift_u1,shift_d1,shift_l1,shift_r1,shift_u2,shift_d2,shift_l2,shift_r2,shift_u1_noise0p04,shift_l1_noise0p04,blur3a45,blur3a135,blur5a45,blur5a135,blur7a45,blur7a135,blur5a45_noise0p04,blur5a135_noise0p04,blur7a45_noise0p04,noise_0p04,noise_0p08,noise_0p10,contrast_m0p10,contrast_m0p20,bright_m0p08,bright_m0p12}"
FEATURE_MODES="${FEATURE_MODES:-margin+pred+family,margin+pred+wrong_parent+family,code+margin+pred+family,code+dist+margin+pred+wrong_parent+family}"
HIDDEN_DIMS="${HIDDEN_DIMS:-0,32,64}"
GATE_TARGETS="${GATE_TARGETS:-g4_current_sources,g10_current_sources}"
SAVE_POLICY_ARTIFACTS="${SAVE_POLICY_ARTIFACTS:-0}"
SAVE_POLICY_FLAGS=""
if [[ "${SAVE_POLICY_ARTIFACTS}" == "1" ]]; then
  SAVE_POLICY_FLAGS="--save-policy-artifacts"
fi

for path in \
  "${BASE_PARAMS}" \
  "${U6_RUN}/parent_int8.tflite" "${U7_RUN}/parent_int8.tflite" "${U8_RUN}/parent_int8.tflite" "${U10_RUN}/parent_int8.tflite" \
  "${L1_RUN}/parent_int8.tflite" "${L2_RUN}/parent_int8.tflite" "${L3_RUN}/parent_int8.tflite" "${L4_RUN}/parent_int8.tflite" \
  "${U6_PARAMS}" "${U7_PARAMS}" "${U8_PARAMS}" "${C1_PARAMS}" "${C3_PARAMS}" "${C5_PARAMS}" "${L1_PARAMS}" "${L2_PARAMS}" "${L3_PARAMS}" "${L4_PARAMS}" \
  "${U6_STRESS}" "${U7_STRESS}" "${U8_STRESS}" "${C1_STRESS}" "${C3_STRESS}" "${C5_STRESS}" "${L1_STRESS}" "${L2_STRESS}" "${L3_STRESS}" "${L4_STRESS}"; do
  if [[ ! -e "${path}" ]]; then
    echo "missing required path: ${path}" >&2
    exit 1
  fi
done

launch_session() {
  local session="$1"
  local cmd="$2"
  if tmux has-session -t "${session}" 2>/dev/null; then
    echo "exists ${session}"
    return 0
  fi
  printf -v quoted_cmd '%q' "${cmd}"
  tmux new-session -d -s "${session}" "bash -lc ${quoted_cmd}"
  echo "launched ${session}"
}

launch_gate() {
  local name="$1"
  local max_rows="$2"
  local source_args="$3"
  local out_dir="${ROOT}/${name}"
  local session="v8sg_${RUN_TAG}_${name}"
  local cmd
  cmd=$(cat <<EOF
set -euo pipefail
cd ${SCRIPT_DIR}
mkdir -p ${out_dir}
echo synthetic_gate_start ${out_dir}
CUDA_VISIBLE_DEVICES="" OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} venv/bin/python analyze_v8_synthetic_source_event_gate.py \
  ${source_args} \
  --base-params ${BASE_PARAMS} \
  --dataset-dir dataset \
  --output-dir ${out_dir} \
  --perturbs ${PERTURBS} \
  --normal-margin-max 128 \
  --max-train-base-rows ${max_rows} \
  --feature-modes ${FEATURE_MODES} \
  --hidden-dims ${HIDDEN_DIMS} \
  --epochs 90 \
  --learning-rate 0.01 \
  --batch-size 512 \
  --holdout-mode sample_mod \
  --validation-mod 5 \
  --seed 2026052901 \
  ${SAVE_POLICY_FLAGS} 2>&1 | tee -a ${out_dir}/run.log
EOF
)
  launch_session "${session}" "${cmd}"
}

should_launch_target() {
  local target="$1"
  [[ ",${GATE_TARGETS}," == *",${target},"* ]]
}

FOUR_SOURCE_ARGS="\
  --source-tflite u6=${U6_RUN}/parent_int8.tflite \
  --source-tflite c1_m8=${U6_RUN}/parent_int8.tflite \
  --source-tflite c5_raw=${U10_RUN}/parent_int8.tflite \
  --source-tflite l1_sg=${L1_RUN}/parent_int8.tflite \
  --source-params u6=${U6_PARAMS} \
  --source-params c1_m8=${C1_PARAMS} \
  --source-params c5_raw=${C5_PARAMS} \
  --source-params l1_sg=${L1_PARAMS} \
  --source-stress u6=${U6_STRESS} \
  --source-stress c1_m8=${C1_STRESS} \
  --source-stress c5_raw=${C5_STRESS} \
  --source-stress l1_sg=${L1_STRESS}"

FOUR_DIVERSE_L4_ARGS="\
  --source-tflite u6=${U6_RUN}/parent_int8.tflite \
  --source-tflite c5_raw=${U10_RUN}/parent_int8.tflite \
  --source-tflite l1_sg=${L1_RUN}/parent_int8.tflite \
  --source-tflite l4_d24=${L4_RUN}/parent_int8.tflite \
  --source-params u6=${U6_PARAMS} \
  --source-params c5_raw=${C5_PARAMS} \
  --source-params l1_sg=${L1_PARAMS} \
  --source-params l4_d24=${L4_PARAMS} \
  --source-stress u6=${U6_STRESS} \
  --source-stress c5_raw=${C5_STRESS} \
  --source-stress l1_sg=${L1_STRESS} \
  --source-stress l4_d24=${L4_STRESS}"

FIVE_DIVERSE_L4_ARGS="\
  --source-tflite u6=${U6_RUN}/parent_int8.tflite \
  --source-tflite c5_raw=${U10_RUN}/parent_int8.tflite \
  --source-tflite l1_sg=${L1_RUN}/parent_int8.tflite \
  --source-tflite l2_dyn=${L2_RUN}/parent_int8.tflite \
  --source-tflite l4_d24=${L4_RUN}/parent_int8.tflite \
  --source-params u6=${U6_PARAMS} \
  --source-params c5_raw=${C5_PARAMS} \
  --source-params l1_sg=${L1_PARAMS} \
  --source-params l2_dyn=${L2_PARAMS} \
  --source-params l4_d24=${L4_PARAMS} \
  --source-stress u6=${U6_STRESS} \
  --source-stress c5_raw=${C5_STRESS} \
  --source-stress l1_sg=${L1_STRESS} \
  --source-stress l2_dyn=${L2_STRESS} \
  --source-stress l4_d24=${L4_STRESS}"

SIX_DIVERSE_U8_L4_ARGS="\
  --source-tflite u6=${U6_RUN}/parent_int8.tflite \
  --source-tflite u8=${U8_RUN}/parent_int8.tflite \
  --source-tflite c5_raw=${U10_RUN}/parent_int8.tflite \
  --source-tflite l1_sg=${L1_RUN}/parent_int8.tflite \
  --source-tflite l2_dyn=${L2_RUN}/parent_int8.tflite \
  --source-tflite l4_d24=${L4_RUN}/parent_int8.tflite \
  --source-params u6=${U6_PARAMS} \
  --source-params u8=${U8_PARAMS} \
  --source-params c5_raw=${C5_PARAMS} \
  --source-params l1_sg=${L1_PARAMS} \
  --source-params l2_dyn=${L2_PARAMS} \
  --source-params l4_d24=${L4_PARAMS} \
  --source-stress u6=${U6_STRESS} \
  --source-stress u8=${U8_STRESS} \
  --source-stress c5_raw=${C5_STRESS} \
  --source-stress l1_sg=${L1_STRESS} \
  --source-stress l2_dyn=${L2_STRESS} \
  --source-stress l4_d24=${L4_STRESS}"

SIX_SYNTH_TOP_ARGS="\
  --source-tflite u6=${U6_RUN}/parent_int8.tflite \
  --source-tflite u7=${U7_RUN}/parent_int8.tflite \
  --source-tflite u8=${U8_RUN}/parent_int8.tflite \
  --source-tflite c5_raw=${U10_RUN}/parent_int8.tflite \
  --source-tflite l2_dyn=${L2_RUN}/parent_int8.tflite \
  --source-tflite l4_d24=${L4_RUN}/parent_int8.tflite \
  --source-params u6=${U6_PARAMS} \
  --source-params u7=${U7_PARAMS} \
  --source-params u8=${U8_PARAMS} \
  --source-params c5_raw=${C5_PARAMS} \
  --source-params l2_dyn=${L2_PARAMS} \
  --source-params l4_d24=${L4_PARAMS} \
  --source-stress u6=${U6_STRESS} \
  --source-stress u7=${U7_STRESS} \
  --source-stress u8=${U8_STRESS} \
  --source-stress c5_raw=${C5_STRESS} \
  --source-stress l2_dyn=${L2_STRESS} \
  --source-stress l4_d24=${L4_STRESS}"

EIGHT_NO_TABLES_ARGS="\
  --source-tflite u6=${U6_RUN}/parent_int8.tflite \
  --source-tflite u7=${U7_RUN}/parent_int8.tflite \
  --source-tflite u8=${U8_RUN}/parent_int8.tflite \
  --source-tflite c5_raw=${U10_RUN}/parent_int8.tflite \
  --source-tflite l1_sg=${L1_RUN}/parent_int8.tflite \
  --source-tflite l2_dyn=${L2_RUN}/parent_int8.tflite \
  --source-tflite l3_sd=${L3_RUN}/parent_int8.tflite \
  --source-tflite l4_d24=${L4_RUN}/parent_int8.tflite \
  --source-params u6=${U6_PARAMS} \
  --source-params u7=${U7_PARAMS} \
  --source-params u8=${U8_PARAMS} \
  --source-params c5_raw=${C5_PARAMS} \
  --source-params l1_sg=${L1_PARAMS} \
  --source-params l2_dyn=${L2_PARAMS} \
  --source-params l3_sd=${L3_PARAMS} \
  --source-params l4_d24=${L4_PARAMS} \
  --source-stress u6=${U6_STRESS} \
  --source-stress u7=${U7_STRESS} \
  --source-stress u8=${U8_STRESS} \
  --source-stress c5_raw=${C5_STRESS} \
  --source-stress l1_sg=${L1_STRESS} \
  --source-stress l2_dyn=${L2_STRESS} \
  --source-stress l3_sd=${L3_STRESS} \
  --source-stress l4_d24=${L4_STRESS}"

TEN_SOURCE_ARGS="\
  --source-tflite u6=${U6_RUN}/parent_int8.tflite \
  --source-tflite u7=${U7_RUN}/parent_int8.tflite \
  --source-tflite u8=${U8_RUN}/parent_int8.tflite \
  --source-tflite c1_m8=${U6_RUN}/parent_int8.tflite \
  --source-tflite c3_m16=${U6_RUN}/parent_int8.tflite \
  --source-tflite c5_raw=${U10_RUN}/parent_int8.tflite \
  --source-tflite l1_sg=${L1_RUN}/parent_int8.tflite \
  --source-tflite l2_dyn=${L2_RUN}/parent_int8.tflite \
  --source-tflite l3_sd=${L3_RUN}/parent_int8.tflite \
  --source-tflite l4_d24=${L4_RUN}/parent_int8.tflite \
  --source-params u6=${U6_PARAMS} \
  --source-params u7=${U7_PARAMS} \
  --source-params u8=${U8_PARAMS} \
  --source-params c1_m8=${C1_PARAMS} \
  --source-params c3_m16=${C3_PARAMS} \
  --source-params c5_raw=${C5_PARAMS} \
  --source-params l1_sg=${L1_PARAMS} \
  --source-params l2_dyn=${L2_PARAMS} \
  --source-params l3_sd=${L3_PARAMS} \
  --source-params l4_d24=${L4_PARAMS} \
  --source-stress u6=${U6_STRESS} \
  --source-stress u7=${U7_STRESS} \
  --source-stress u8=${U8_STRESS} \
  --source-stress c1_m8=${C1_STRESS} \
  --source-stress c3_m16=${C3_STRESS} \
  --source-stress c5_raw=${C5_STRESS} \
  --source-stress l1_sg=${L1_STRESS} \
  --source-stress l2_dyn=${L2_STRESS} \
  --source-stress l3_sd=${L3_STRESS} \
  --source-stress l4_d24=${L4_STRESS}"

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "purpose": "Train source gates from normal/synthetic rows only and evaluate on frozen high-pressure events.",
  "high_pressure_usage": "evaluation_only",
  "selection_label_usage": "non_highpressure_synthetic_row_winner",
  "perturbs": "${PERTURBS}",
  "feature_modes": "${FEATURE_MODES}",
  "hidden_dims": "${HIDDEN_DIMS}",
  "gate_targets": "${GATE_TARGETS}",
  "save_policy_artifacts": "${SAVE_POLICY_ARTIFACTS}"
}
EOF

should_launch_target "g4_current_sources" && launch_gate "g4_current_sources" 700 "${FOUR_SOURCE_ARGS}"
should_launch_target "g4_diverse_l4" && launch_gate "g4_diverse_l4" 700 "${FOUR_DIVERSE_L4_ARGS}"
should_launch_target "g5_diverse_l4" && launch_gate "g5_diverse_l4" 650 "${FIVE_DIVERSE_L4_ARGS}"
should_launch_target "g6_diverse_u8_l4" && launch_gate "g6_diverse_u8_l4" 600 "${SIX_DIVERSE_U8_L4_ARGS}"
should_launch_target "g6_synth_top" && launch_gate "g6_synth_top" 600 "${SIX_SYNTH_TOP_ARGS}"
should_launch_target "g8_no_tables" && launch_gate "g8_no_tables" 520 "${EIGHT_NO_TABLES_ARGS}"
should_launch_target "g10_current_sources" && launch_gate "g10_current_sources" 420 "${TEN_SOURCE_ARGS}"

echo "root=${ROOT}"
echo "sessions:"
tmux ls | grep -E "v8sg_${RUN_TAG}" | sort || true
