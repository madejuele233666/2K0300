#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

RUN_ID="${1:-v8_low_focus_parallel_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
mkdir -p "${ROOT}"
printf '%s\n' "${RUN_ID}" > experiments/v8_low_focus_active_run.txt

RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-34)"
OMP_THREADS="${OMP_THREADS:-2}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-2}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"
TRAIN_LAUNCH="${TRAIN_LAUNCH:-1}"
COMPILE_LAUNCH="${COMPILE_LAUNCH:-1}"

BASE_PARAMS="experiments/v8_parent_c2612_d4_multiteacher_20260520_0001/s2d_c2_6_12_d4_shadow_init_compile/best_parent_logits_memory_params.npz"
SYNTH_SOURCE_GATE="experiments/v8_synthetic_source_gate_teacher_20260521_0001/cleanrow_all_margin_all41_extra29/source_gate_teacher.npz"
DYNAMIC_QPAIR="experiments/v8_pair_margin_teacher_20260520_0001/d4_c2612_qanchor_neigh32_dynamic/pair_margin_teacher.npz"
SOURCE_DECISION="experiments/v8_source_decision_margin_teacher_20260521_0002/d4base_c248pool_c248d4_d8q_d6sm_d7sm_t8/source_decision_margin_teacher.npz"

RESOURCE_ROOT="experiments/v8_resource_parallel_20260521_0001"
U6_RUN="${RESOURCE_ROOT}/u6_wide_d20_identity_sgrank"
U9_RUN="${RESOURCE_ROOT}/u9_d20_rawlowedge_sgrank"
U10_RUN="${RESOURCE_ROOT}/u10_wide_d20_rawlowedge_sgrank"
U11_RUN="${RESOURCE_ROOT}/u11_d16_rawlowedge_sg_sourcedecision"
U6_INIT="${U6_RUN}/parent_model.keras"

DYNAMIC_NORMAL_STRESS="rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,cam_blur3a135,hblur5_noise_0p06,cam_blur5a135,cam_noise0p04,cam_blur5a45_noise0p04,cam_blur3a135_noise0p02,cam_blur3a0_noise0p02,diagblur5,cam_blur5a45,cam_bright0p04_contrast0p10,cam_noise0p02,vblur5,cam_contrast0p12,noise_0p10,diagblur5_noise_0p08,cam_blur2a0,cam_blur3a45,cam_bright0p06,cam_noise0p03,cam_blur3a90,noise_0p06,cam_blur3a45_noise0p02"
SYNTH_EXTRA_STRESS="cam_noise0p06,cam_noise0p08,cam_noise0p10,cam_blur3a0,cam_blur5a0,cam_blur5a90,cam_blur7a45,cam_blur7a135,cam_blur5a135_noise0p04,cam_blur7a45_noise0p04,cam_brightp0p04,cam_brightp0p08,cam_brightp0p12,cam_brightm0p04,cam_brightm0p08,cam_brightm0p12,cam_contrastp0p10,cam_contrastp0p20,cam_contrastm0p10,cam_contrastm0p20,cam_shiftu1,cam_shiftd1,cam_shiftl1,cam_shiftr1,cam_shiftul1,cam_shiftdr1,cam_shiftu2,cam_shiftd2,cam_shiftl2,cam_shiftr2,cam_shiftu1_noise0p04,cam_shiftl1_noise0p04"
TRAIN_UNION_STRESS="${DYNAMIC_NORMAL_STRESS},${SYNTH_EXTRA_STRESS}"

required_paths=(
  "${BASE_PARAMS}"
  "${SYNTH_SOURCE_GATE}"
  "${DYNAMIC_QPAIR}"
  "${SOURCE_DECISION}"
  "${U6_RUN}/parent_int8.tflite"
  "${U6_INIT}"
)
for path in "${required_paths[@]}"; do
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

compile_eval_body() {
  local parent_dir="$1"
  local compile_dir="$2"
  local stress_dir="$3"
  local target_margin="$4"
  local source_decision_flag="$5"
  local max_iterations="$6"
  cat <<EOF
mkdir -p ${compile_dir} ${stress_dir}
(
  flock 9
  echo compile_start ${compile_dir}
  CUDA_VISIBLE_DEVICES="" OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} venv/bin/python compile_v8_parent_logits_memory.py \
    --parent-run-dir ${parent_dir} \
    --dataset-dir dataset \
    --output-dir ${compile_dir} \
    --stress ${DYNAMIC_NORMAL_STRESS} \
    --feature-sources int8_tflite,float_tflite \
    --prototype-subsets clean,clean_rotmirror,all \
    --residual-bases clean,clean_rotmirror \
    --residual-target-int8-margin ${target_margin} \
    --max-residual-iterations ${max_iterations} \
    ${source_decision_flag} 2>&1 | tee -a ${compile_dir}/run.log
  echo highstress_start ${stress_dir}
  CUDA_VISIBLE_DEVICES="" OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} venv/bin/python stress_test_v8_low_margin.py \
    --tflite ${parent_dir}/parent_int8.tflite \
    --params-npz ${compile_dir}/best_parent_logits_memory_params.npz \
    --selection-params-npz ${BASE_PARAMS} \
    --dataset-dir dataset \
    --output-dir ${stress_dir} \
    --perturbs all \
    --low-margin-threshold 8 \
    --control-margin-min 128 \
    --batch-size 512 2>&1 | tee -a ${stress_dir}/run.log
  echo compile_eval_done ${stress_dir}
) 9>${ROOT}/compile.lock
EOF
}

launch_compile_eval() {
  local name="$1"
  local parent_dir="$2"
  local target_margin="$3"
  local source_decision_flag="$4"
  local max_iterations="${5:-16}"
  local compile_dir="${ROOT}/${name}_compile_m${target_margin}"
  local stress_dir="${ROOT}/${name}_highstress_canonical"
  local session="v8lf_${RUN_TAG}_${name}"
  local cmd
  cmd=$(cat <<EOF
set -euo pipefail
cd ${SCRIPT_DIR}
$(compile_eval_body "${parent_dir}" "${compile_dir}" "${stress_dir}" "${target_margin}" "${source_decision_flag}" "${max_iterations}")
EOF
)
  launch_session "${session}" "${cmd}"
}

launch_train_pipeline() {
  local name="$1"
  local train_flags="$2"
  local target_margin="${3:-8}"
  local source_decision_flag="${4:---source-decision-teacher-npz ${SOURCE_DECISION} --source-decision-compiler-margin 8}"
  local out_dir="${ROOT}/${name}"
  local compile_dir="${out_dir}_compile_m${target_margin}"
  local stress_dir="${out_dir}_highstress_canonical"
  local session="v8lf_${RUN_TAG}_${name}"
  local cmd
  cmd=$(cat <<EOF
set -euo pipefail
cd ${SCRIPT_DIR}
mkdir -p ${out_dir}
echo train_start ${out_dir}
OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} ./run_gpu.sh venv/bin/python train_v8_parent_classifier.py \
  --dataset-dir dataset \
  --output-dir ${out_dir} \
  --stress ${TRAIN_UNION_STRESS} \
  --sample-weight-mode parent_balanced \
  --log-every 25 \
  ${train_flags} 2>&1 | tee -a ${out_dir}/run.log
$(compile_eval_body "${out_dir}" "${compile_dir}" "${stress_dir}" "${target_margin}" "${source_decision_flag}" "16")
EOF
)
  launch_session "${session}" "${cmd}"
}

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "purpose": "Low-margin focused V8 experiments: keep high-pressure samples evaluation-only, amplify normal D4 boundary rows through source-gate, source-decision, dynamic qpair, and higher normal residual compile margins.",
  "high_pressure_usage": "evaluation_only",
  "train_stress": "${TRAIN_UNION_STRESS}",
  "compile_stress": "${DYNAMIC_NORMAL_STRESS}",
  "selection_params": "${BASE_PARAMS}",
  "source_gate_teacher": "${SYNTH_SOURCE_GATE}",
  "source_decision_teacher": "${SOURCE_DECISION}",
  "dynamic_qpair_teacher": "${DYNAMIC_QPAIR}",
  "compile_lock": "${ROOT}/compile.lock"
}
EOF

COMMON_SG="--backbone-architecture spacetodepth_conv --activation relu6 --pool max --source-gate-teacher-npz ${SYNTH_SOURCE_GATE} --source-gate-start 3 --source-gate-weight 0.002 --source-gate-margin-weight 0.0004 --source-gate-margin-target 24 --source-gate-balance-weight 0.03 --source-gate-rank-weight 0.008 --source-gate-rank-min-gap 16 --source-gate-rank-max-target 24 --source-gate-center-weight 0.001 --source-gate-center-target 24"
STRONG_SG="--backbone-architecture spacetodepth_conv --activation relu6 --pool max --source-gate-teacher-npz ${SYNTH_SOURCE_GATE} --source-gate-start 3 --source-gate-weight 0.0015 --source-gate-margin-weight 0.0007 --source-gate-margin-target 40 --source-gate-balance-weight 0.02 --source-gate-rank-weight 0.014 --source-gate-rank-min-gap 16 --source-gate-rank-max-target 40 --source-gate-center-weight 0.0015 --source-gate-center-target 40"

if [[ "${COMPILE_LAUNCH}" == "1" ]]; then
  launch_compile_eval "c1_u6_t8" "${U6_RUN}" "8" "" "16"
  launch_compile_eval "c2_u6_t8_sd" "${U6_RUN}" "8" "--source-decision-teacher-npz ${SOURCE_DECISION} --source-decision-compiler-margin 8" "16"
  launch_compile_eval "c3_u6_t16_sd" "${U6_RUN}" "16" "--source-decision-teacher-npz ${SOURCE_DECISION} --source-decision-compiler-margin 16" "20"
  [[ -e "${U9_RUN}/parent_int8.tflite" ]] && launch_compile_eval "c4_u9_t8_sd" "${U9_RUN}" "8" "--source-decision-teacher-npz ${SOURCE_DECISION} --source-decision-compiler-margin 8" "16"
  [[ -e "${U10_RUN}/parent_int8.tflite" ]] && launch_compile_eval "c5_u10_t8_sd" "${U10_RUN}" "8" "--source-decision-teacher-npz ${SOURCE_DECISION} --source-decision-compiler-margin 8" "16"
  [[ -e "${U11_RUN}/parent_int8.tflite" ]] && launch_compile_eval "c6_u11_t8_sd" "${U11_RUN}" "8" "--source-decision-teacher-npz ${SOURCE_DECISION} --source-decision-compiler-margin 8" "16"
fi

if [[ "${TRAIN_LAUNCH}" == "1" ]]; then
  launch_train_pipeline "l1_u6_ft_strong_sg" \
    "--filters 4,8,16 --code-dim 20 --seed 2026052701 --epochs 120 --learning-rate 0.0007 --input-transform identity --init-model ${U6_INIT} ${STRONG_SG} --source-cluster-weight 0.00035 --source-cluster-target 128" \
    "8" "--source-decision-teacher-npz ${SOURCE_DECISION} --source-decision-compiler-margin 8"

  launch_train_pipeline "l2_u6_ft_dynqpair_axis" \
    "--filters 4,8,16 --code-dim 20 --seed 2026052702 --epochs 120 --learning-rate 0.0007 --input-transform identity --init-model ${U6_INIT} ${COMMON_SG} --source-cluster-weight 0.0002 --source-cluster-target 64 --dynamic-qpair-teacher-npz ${DYNAMIC_QPAIR} --dynamic-qpair-margin-weight 0.00014 --dynamic-qpair-margin-target 96 --dynamic-qpair-axis-weight 0.00004 --dynamic-qpair-axis-target 0" \
    "8" "--source-decision-teacher-npz ${SOURCE_DECISION} --source-decision-compiler-margin 8"

  launch_train_pipeline "l3_u6_ft_source_decision_margin" \
    "--filters 4,8,16 --code-dim 20 --seed 2026052703 --epochs 120 --learning-rate 0.0007 --input-transform identity --init-model ${U6_INIT} ${COMMON_SG} --source-cluster-weight 0.0002 --source-cluster-target 64 --source-decision-teacher-npz ${SOURCE_DECISION} --source-decision-margin-weight 0.00016 --source-decision-margin-alpha 0.05" \
    "8" "--source-decision-teacher-npz ${SOURCE_DECISION} --source-decision-compiler-margin 8"

  launch_train_pipeline "l4_d24_partial_strong_sg" \
    "--filters 4,8,16 --code-dim 24 --seed 2026052704 --epochs 170 --learning-rate 0.0009 --input-transform identity --init-model ${U6_INIT} --allow-partial-init-output ${STRONG_SG} --source-cluster-weight 0.0003 --source-cluster-target 128" \
    "8" "--source-decision-teacher-npz ${SOURCE_DECISION} --source-decision-compiler-margin 8"
fi

echo "root=${ROOT}"
echo "sessions:"
tmux ls | grep -E "v8lf_${RUN_TAG}" | sort || true
