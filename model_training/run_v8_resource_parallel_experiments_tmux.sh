#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

RUN_ID="${1:-v8_resource_parallel_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
mkdir -p "${ROOT}"
printf '%s\n' "${RUN_ID}" > experiments/v8_resource_parallel_active_run.txt

RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-34)"
OMP_THREADS="${OMP_THREADS:-2}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-2}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"
LAUNCH_EXTRA="${LAUNCH_EXTRA:-0}"

BASE_PARAMS="experiments/v8_parent_c2612_d4_multiteacher_20260520_0001/s2d_c2_6_12_d4_shadow_init_compile/best_parent_logits_memory_params.npz"
SYNTH_SOURCE_GATE="experiments/v8_synthetic_source_gate_teacher_20260521_0001/cleanrow_all_margin_all41_extra29/source_gate_teacher.npz"
DYNAMIC_QPAIR="experiments/v8_pair_margin_teacher_20260520_0001/d4_c2612_qanchor_neigh32_dynamic/pair_margin_teacher.npz"
SOURCE_DECISION="experiments/v8_source_decision_margin_teacher_20260521_0002/d4base_c248pool_c248d4_d8q_d6sm_d7sm_t8/source_decision_margin_teacher.npz"

DYNAMIC_NORMAL_STRESS="rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,cam_blur3a135,hblur5_noise_0p06,cam_blur5a135,cam_noise0p04,cam_blur5a45_noise0p04,cam_blur3a135_noise0p02,cam_blur3a0_noise0p02,diagblur5,cam_blur5a45,cam_bright0p04_contrast0p10,cam_noise0p02,vblur5,cam_contrast0p12,noise_0p10,diagblur5_noise_0p08,cam_blur2a0,cam_blur3a45,cam_bright0p06,cam_noise0p03,cam_blur3a90,noise_0p06,cam_blur3a45_noise0p02"
SYNTH_EXTRA_STRESS="cam_noise0p06,cam_noise0p08,cam_noise0p10,cam_blur3a0,cam_blur5a0,cam_blur5a90,cam_blur7a45,cam_blur7a135,cam_blur5a135_noise0p04,cam_blur7a45_noise0p04,cam_brightp0p04,cam_brightp0p08,cam_brightp0p12,cam_brightm0p04,cam_brightm0p08,cam_brightm0p12,cam_contrastp0p10,cam_contrastp0p20,cam_contrastm0p10,cam_contrastm0p20,cam_shiftu1,cam_shiftd1,cam_shiftl1,cam_shiftr1,cam_shiftul1,cam_shiftdr1,cam_shiftu2,cam_shiftd2,cam_shiftl2,cam_shiftr2,cam_shiftu1_noise0p04,cam_shiftl1_noise0p04"
TRAIN_UNION_STRESS="${DYNAMIC_NORMAL_STRESS},${SYNTH_EXTRA_STRESS}"

required_paths=(
  "${BASE_PARAMS}"
  "${SYNTH_SOURCE_GATE}"
  "${DYNAMIC_QPAIR}"
  "${SOURCE_DECISION}"
)
for path in "${required_paths[@]}"; do
  if [[ ! -f "${path}" ]]; then
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

launch_pipeline() {
  local name="$1"
  local train_flags="$2"
  local out_dir="${ROOT}/${name}"
  local compile_dir="${out_dir}_compile_dynamic_m4"
  local stress_dir="${out_dir}_highstress_canonical"
  local session="v8res_${RUN_TAG}_${name}"
  local cmd
  cmd=$(cat <<EOF
set -euo pipefail
cd ${SCRIPT_DIR}
mkdir -p ${out_dir} ${compile_dir} ${stress_dir}
OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} ./run_gpu.sh venv/bin/python train_v8_parent_classifier.py \
  --dataset-dir dataset \
  --output-dir ${out_dir} \
  --stress ${TRAIN_UNION_STRESS} \
  --sample-weight-mode parent_balanced \
  --log-every 25 \
  ${train_flags} 2>&1 | tee -a ${out_dir}/run.log
CUDA_VISIBLE_DEVICES="" OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} venv/bin/python compile_v8_parent_logits_memory.py \
  --parent-run-dir ${out_dir} \
  --dataset-dir dataset \
  --output-dir ${compile_dir} \
  --stress ${DYNAMIC_NORMAL_STRESS} \
  --prototype-subsets clean,clean_rotmirror,all \
  --residual-bases clean,clean_rotmirror \
  --residual-target-int8-margin 4 \
  --max-residual-iterations 12 2>&1 | tee -a ${compile_dir}/run.log
CUDA_VISIBLE_DEVICES="" OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} venv/bin/python stress_test_v8_low_margin.py \
  --tflite ${out_dir}/parent_int8.tflite \
  --params-npz ${compile_dir}/best_parent_logits_memory_params.npz \
  --selection-params-npz ${BASE_PARAMS} \
  --dataset-dir dataset \
  --output-dir ${stress_dir} \
  --perturbs all \
  --low-margin-threshold 8 \
  --control-margin-min 128 \
  --batch-size 512 2>&1 | tee -a ${stress_dir}/run.log
EOF
)
  launch_session "${session}" "${cmd}"
}

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "purpose": "Resource-parallel V8 robustness experiments: union normal/synthetic training views, larger code capacity, source-gate/source-decision combinations.",
  "high_pressure_usage": "evaluation_only",
  "train_stress": "${TRAIN_UNION_STRESS}",
  "compile_stress": "${DYNAMIC_NORMAL_STRESS}",
  "base_params": "${BASE_PARAMS}",
  "source_gate_teacher": "${SYNTH_SOURCE_GATE}",
  "dynamic_qpair_teacher": "${DYNAMIC_QPAIR}",
  "source_decision_teacher": "${SOURCE_DECISION}"
}
EOF

COMMON_SG="--backbone-architecture spacetodepth_conv --activation relu6 --pool max --source-gate-teacher-npz ${SYNTH_SOURCE_GATE} --source-gate-start 3 --source-gate-weight 0.002 --source-gate-margin-weight 0.0004 --source-gate-margin-target 24 --source-gate-balance-weight 0.03 --source-gate-rank-weight 0.008 --source-gate-rank-min-gap 16 --source-gate-rank-max-target 24 --source-gate-center-weight 0.001 --source-gate-center-target 24"

launch_pipeline "u1_d12_identity_sgrank" \
  "--filters 2,6,12 --code-dim 12 --seed 2026052601 --epochs 180 --learning-rate 0.0018 --input-transform identity ${COMMON_SG} --source-cluster-weight 0.0002 --source-cluster-target 64"

launch_pipeline "u2_d16_identity_sgrank" \
  "--filters 2,6,12 --code-dim 16 --seed 2026052602 --epochs 180 --learning-rate 0.0018 --input-transform identity ${COMMON_SG} --source-cluster-weight 0.0002 --source-cluster-target 64"

launch_pipeline "u3_d12_rawlowedge_sgrank" \
  "--filters 2,6,12 --code-dim 12 --seed 2026052603 --epochs 180 --learning-rate 0.0018 --input-transform raw_low_edge ${COMMON_SG} --source-cluster-weight 0.0002 --source-cluster-target 64"

launch_pipeline "u4_d16_rawlowedge_sgrank" \
  "--filters 2,6,12 --code-dim 16 --seed 2026052604 --epochs 180 --learning-rate 0.0018 --input-transform raw_low_edge ${COMMON_SG} --source-cluster-weight 0.0002 --source-cluster-target 64"

launch_pipeline "u5_wide_d16_rawlowedge_sgrank" \
  "--filters 4,8,16 --code-dim 16 --seed 2026052605 --epochs 180 --learning-rate 0.0016 --input-transform raw_low_edge ${COMMON_SG} --source-cluster-weight 0.0002 --source-cluster-target 64"

launch_pipeline "u6_wide_d20_identity_sgrank" \
  "--filters 4,8,16 --code-dim 20 --seed 2026052606 --epochs 200 --learning-rate 0.0016 --input-transform identity ${COMMON_SG} --source-cluster-weight 0.0002 --source-cluster-target 64"

launch_pipeline "u7_d16_identity_sg_sourcedecision" \
  "--filters 2,6,12 --code-dim 16 --seed 2026052607 --epochs 200 --learning-rate 0.0016 --input-transform identity ${COMMON_SG} --source-decision-teacher-npz ${SOURCE_DECISION} --source-decision-margin-weight 0.0003 --source-decision-margin-alpha 0.05 --source-decision-center-weight 0.00005 --source-decision-center-target 4096 --source-decision-center-alpha 0.01"

launch_pipeline "u8_d16_rawlowedge_sg_dynqpair" \
  "--filters 2,6,12 --code-dim 16 --seed 2026052608 --epochs 200 --learning-rate 0.0016 --input-transform raw_low_edge ${COMMON_SG} --dynamic-qpair-teacher-npz ${DYNAMIC_QPAIR} --dynamic-qpair-margin-weight 0.0001 --dynamic-qpair-margin-target 64"

launch_pipeline "u9_d20_rawlowedge_sgrank" \
  "--filters 2,6,12 --code-dim 20 --seed 2026052609 --epochs 200 --learning-rate 0.0016 --input-transform raw_low_edge ${COMMON_SG} --source-cluster-weight 0.0002 --source-cluster-target 64"

launch_pipeline "u10_wide_d20_rawlowedge_sgrank" \
  "--filters 4,8,16 --code-dim 20 --seed 2026052610 --epochs 200 --learning-rate 0.0015 --input-transform raw_low_edge ${COMMON_SG} --source-cluster-weight 0.0002 --source-cluster-target 64"

launch_pipeline "u11_d16_rawlowedge_sg_sourcedecision" \
  "--filters 2,6,12 --code-dim 16 --seed 2026052611 --epochs 200 --learning-rate 0.0016 --input-transform raw_low_edge ${COMMON_SG} --source-decision-teacher-npz ${SOURCE_DECISION} --source-decision-margin-weight 0.0003 --source-decision-margin-alpha 0.05 --source-decision-center-weight 0.00005 --source-decision-center-target 4096 --source-decision-center-alpha 0.01"

launch_pipeline "u12_d20_identity_sg_sourcedecision" \
  "--filters 2,6,12 --code-dim 20 --seed 2026052612 --epochs 200 --learning-rate 0.0016 --input-transform identity ${COMMON_SG} --source-decision-teacher-npz ${SOURCE_DECISION} --source-decision-margin-weight 0.0003 --source-decision-margin-alpha 0.05 --source-decision-center-weight 0.00005 --source-decision-center-target 4096 --source-decision-center-alpha 0.01"

if [[ "${LAUNCH_EXTRA}" == "1" ]]; then
launch_pipeline "u13_d12_identity_sg_dynqpair" \
  "--filters 2,6,12 --code-dim 12 --seed 2026052613 --epochs 200 --learning-rate 0.0016 --input-transform identity ${COMMON_SG} --dynamic-qpair-teacher-npz ${DYNAMIC_QPAIR} --dynamic-qpair-margin-weight 0.0001 --dynamic-qpair-margin-target 64"

launch_pipeline "u14_d12_rawlowedge_sg_sd_dynqpair" \
  "--filters 2,6,12 --code-dim 12 --seed 2026052614 --epochs 220 --learning-rate 0.0015 --input-transform raw_low_edge ${COMMON_SG} --source-decision-teacher-npz ${SOURCE_DECISION} --source-decision-margin-weight 0.00025 --source-decision-margin-alpha 0.05 --dynamic-qpair-teacher-npz ${DYNAMIC_QPAIR} --dynamic-qpair-margin-weight 0.00008 --dynamic-qpair-margin-target 64"

STRONG_SG="--backbone-architecture spacetodepth_conv --activation relu6 --pool max --source-gate-teacher-npz ${SYNTH_SOURCE_GATE} --source-gate-start 3 --source-gate-weight 0.0015 --source-gate-margin-weight 0.0006 --source-gate-margin-target 32 --source-gate-balance-weight 0.02 --source-gate-rank-weight 0.012 --source-gate-rank-min-gap 16 --source-gate-rank-max-target 32 --source-gate-center-weight 0.0015 --source-gate-center-target 32"

launch_pipeline "u15_d16_identity_strong_sg" \
  "--filters 2,6,12 --code-dim 16 --seed 2026052615 --epochs 220 --learning-rate 0.0015 --input-transform identity ${STRONG_SG} --source-cluster-weight 0.0003 --source-cluster-target 96"

launch_pipeline "u16_wide_d16_rawlowedge_strong_sg" \
  "--filters 4,8,16 --code-dim 16 --seed 2026052616 --epochs 220 --learning-rate 0.0014 --input-transform raw_low_edge ${STRONG_SG} --source-cluster-weight 0.0003 --source-cluster-target 96"
fi

echo "root=${ROOT}"
echo "sessions:"
tmux ls | grep -E "v8res_${RUN_TAG}" | sort || true
