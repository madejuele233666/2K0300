#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

RUN_ID="${1:-v8_combo_focus_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
mkdir -p "${ROOT}"
printf '%s\n' "${RUN_ID}" > experiments/v8_combo_focus_active_run.txt

RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-34)"
OMP_THREADS="${OMP_THREADS:-2}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-2}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"
GLOBAL_COMPILE_LOCK="${GLOBAL_COMPILE_LOCK:-experiments/v8_low_focus_parallel_20260521_0001/compile.lock}"

BASE_PARAMS="experiments/v8_parent_c2612_d4_multiteacher_20260520_0001/s2d_c2_6_12_d4_shadow_init_compile/best_parent_logits_memory_params.npz"
SYNTH_SOURCE_GATE="experiments/v8_synthetic_source_gate_teacher_20260521_0001/cleanrow_all_margin_all41_extra29/source_gate_teacher.npz"
DYNAMIC_QPAIR="experiments/v8_pair_margin_teacher_20260520_0001/d4_c2612_qanchor_neigh32_dynamic/pair_margin_teacher.npz"
SOURCE_DECISION="experiments/v8_source_decision_margin_teacher_20260521_0002/d4base_c248pool_c248d4_d8q_d6sm_d7sm_t8/source_decision_margin_teacher.npz"
U6_INIT="experiments/v8_resource_parallel_20260521_0001/u6_wide_d20_identity_sgrank/parent_model.keras"

DYNAMIC_NORMAL_STRESS="rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,cam_blur3a135,hblur5_noise_0p06,cam_blur5a135,cam_noise0p04,cam_blur5a45_noise0p04,cam_blur3a135_noise0p02,cam_blur3a0_noise0p02,diagblur5,cam_blur5a45,cam_bright0p04_contrast0p10,cam_noise0p02,vblur5,cam_contrast0p12,noise_0p10,diagblur5_noise_0p08,cam_blur2a0,cam_blur3a45,cam_bright0p06,cam_noise0p03,cam_blur3a90,noise_0p06,cam_blur3a45_noise0p02"
SYNTH_EXTRA_STRESS="cam_noise0p06,cam_noise0p08,cam_noise0p10,cam_blur3a0,cam_blur5a0,cam_blur5a90,cam_blur7a45,cam_blur7a135,cam_blur5a135_noise0p04,cam_blur7a45_noise0p04,cam_brightp0p04,cam_brightp0p08,cam_brightp0p12,cam_brightm0p04,cam_brightm0p08,cam_brightm0p12,cam_contrastp0p10,cam_contrastp0p20,cam_contrastm0p10,cam_contrastm0p20,cam_shiftu1,cam_shiftd1,cam_shiftl1,cam_shiftr1,cam_shiftul1,cam_shiftdr1,cam_shiftu2,cam_shiftd2,cam_shiftl2,cam_shiftr2,cam_shiftu1_noise0p04,cam_shiftl1_noise0p04"
COMBO_STRESS="cam_rot270_shiftl2_blur3a45,cam_rot270_shiftd1_blur3a45,cam_rot270_mirror_lr_shiftl2_blur3a45,cam_rot270_mirror_lr_shiftl1_blur3a45,cam_rot180_mirror_lr_shiftl1_blur3a45,cam_rot90_shiftl1_blur3a45,cam_rot90_shiftd1_blur3a135_noise0p02,cam_rot90_mirror_lr_shiftl1_blur3a45,cam_rot270_contrastm0p10_blur3a45,cam_rot270_mirror_lr_contrastm0p10_blur3a45,cam_rot270_brightm0p08_blur3a45,cam_rot90_brightp0p08_blur3a135"
TRAIN_STRESS="${DYNAMIC_NORMAL_STRESS},${SYNTH_EXTRA_STRESS},${COMBO_STRESS}"

for path in "${BASE_PARAMS}" "${SYNTH_SOURCE_GATE}" "${DYNAMIC_QPAIR}" "${SOURCE_DECISION}" "${U6_INIT}"; do
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

launch_pipeline() {
  local name="$1"
  local flags="$2"
  local out_dir="${ROOT}/${name}"
  local compile_dir="${out_dir}_compile_m4"
  local stress_dir="${out_dir}_highstress_canonical"
  local session="v8cf_${RUN_TAG}_${name}"
  local cmd
  cmd=$(cat <<EOF
set -euo pipefail
cd ${SCRIPT_DIR}
mkdir -p ${out_dir} ${compile_dir} ${stress_dir}
echo train_start ${out_dir}
OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} ./run_gpu.sh venv/bin/python train_v8_parent_classifier.py \
  --dataset-dir dataset \
  --output-dir ${out_dir} \
  --stress ${TRAIN_STRESS} \
  --sample-weight-mode parent_balanced \
  --log-every 25 \
  ${flags} 2>&1 | tee -a ${out_dir}/run.log
(
  flock 9
  echo compile_start ${compile_dir}
  CUDA_VISIBLE_DEVICES="" OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} venv/bin/python compile_v8_parent_logits_memory.py \
    --parent-run-dir ${out_dir} \
    --dataset-dir dataset \
    --output-dir ${compile_dir} \
    --stress ${DYNAMIC_NORMAL_STRESS} \
    --feature-sources int8_tflite,float_tflite \
    --prototype-subsets clean,clean_rotmirror,all \
    --residual-bases clean,clean_rotmirror \
    --residual-target-int8-margin 4 \
    --max-residual-iterations 12 2>&1 | tee -a ${compile_dir}/run.log
  echo highstress_start ${stress_dir}
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
  echo compile_eval_done ${stress_dir}
) 9>${GLOBAL_COMPILE_LOCK}
EOF
)
  launch_session "${session}" "${cmd}"
}

cat > "${ROOT}/launch_config.json" <<EOF
{
  "run_id": "${RUN_ID}",
  "purpose": "Normal-only composite augmentation around D4 orientation plus camera perturbations; high-pressure events remain evaluation-only.",
  "high_pressure_usage": "evaluation_only",
  "train_stress": "${TRAIN_STRESS}",
  "combo_stress": "${COMBO_STRESS}",
  "compile_stress": "${DYNAMIC_NORMAL_STRESS}",
  "selection_params": "${BASE_PARAMS}",
  "global_compile_lock": "${GLOBAL_COMPILE_LOCK}"
}
EOF

COMMON_SG="--backbone-architecture spacetodepth_conv --activation relu6 --pool max --source-gate-teacher-npz ${SYNTH_SOURCE_GATE} --source-gate-start 3 --source-gate-weight 0.002 --source-gate-margin-weight 0.0004 --source-gate-margin-target 24 --source-gate-balance-weight 0.03 --source-gate-rank-weight 0.008 --source-gate-rank-min-gap 16 --source-gate-rank-max-target 24 --source-gate-center-weight 0.001 --source-gate-center-target 24"

launch_pipeline "k1_combo_ce_sourcegate" \
  "--filters 4,8,16 --code-dim 20 --seed 2026052801 --epochs 110 --learning-rate 0.00055 --input-transform identity --init-model ${U6_INIT} ${COMMON_SG} --source-cluster-weight 0.0002 --source-cluster-target 64"

launch_pipeline "k2_combo_ce_only" \
  "--filters 4,8,16 --code-dim 20 --seed 2026052802 --epochs 90 --learning-rate 0.00045 --input-transform identity --init-model ${U6_INIT} --backbone-architecture spacetodepth_conv --activation relu6 --pool max"

launch_pipeline "k3_combo_dynqpair_weak" \
  "--filters 4,8,16 --code-dim 20 --seed 2026052803 --epochs 110 --learning-rate 0.00055 --input-transform identity --init-model ${U6_INIT} ${COMMON_SG} --source-cluster-weight 0.00015 --source-cluster-target 64 --dynamic-qpair-teacher-npz ${DYNAMIC_QPAIR} --dynamic-qpair-margin-weight 0.00006 --dynamic-qpair-margin-target 64 --dynamic-qpair-axis-weight 0.000015 --dynamic-qpair-axis-target 0"

echo "root=${ROOT}"
echo "sessions:"
tmux ls | grep -E "v8cf_${RUN_TAG}" | sort || true
