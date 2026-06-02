#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

RUN_ID="${1:-v8_divergent_experiments_$(date +%Y%m%d_%H%M%S)}"
ROOT="experiments/${RUN_ID}"
mkdir -p "${ROOT}"
printf '%s\n' "${RUN_ID}" > experiments/v8_divergent_experiments_active_run.txt

RUN_TAG="$(printf '%s' "${RUN_ID}" | tr -c 'A-Za-z0-9_' '_' | cut -c1-34)"
OMP_THREADS="${OMP_THREADS:-2}"
TF_INTRA_THREADS="${TF_INTRA_THREADS:-2}"
TF_INTER_THREADS="${TF_INTER_THREADS:-1}"

BASE_TFLITE="experiments/v8_parent_c2612_d4_multiteacher_20260520_0001/s2d_c2_6_12_d4_shadow_init/parent_int8.tflite"
BASE_PARAMS="experiments/v8_parent_c2612_d4_multiteacher_20260520_0001/s2d_c2_6_12_d4_shadow_init_compile/best_parent_logits_memory_params.npz"
BASE_STRESS="experiments/v8_parent_c2612_d4_multiteacher_20260520_0001/s2d_c2_6_12_d4_shadow_init_highstress/stress_events.csv"

C248_D4_TFLITE="experiments/v8_parent_d4_qanchor_20260520_0001/s2d_c2_4_8_d4_shadow_init/parent_int8.tflite"
C248_D4_PARAMS="experiments/v8_parent_d4_qanchor_20260520_0001/s2d_c2_4_8_d4_shadow_init_compile/best_parent_logits_memory_params.npz"
C248_D4_STRESS="experiments/v8_parent_d4_qanchor_20260520_0001/s2d_c2_4_8_d4_shadow_init_highstress/stress_events.csv"

D8Q_TFLITE="experiments/v8_qlmpl_c248_d8_qpair_20260520_0001/s2d_c2_4_8_d8_shadow_metric_raw/tflite_export/embedding_int8.tflite"
D8Q_PARAMS="experiments/v8_qlmpl_c248_d8_qpair_20260520_0001/s2d_c2_4_8_d8_shadow_metric_compile/best_parent_logits_memory_params.npz"
D8Q_STRESS="experiments/v8_qlmpl_c248_d8_qpair_20260520_0001/s2d_c2_4_8_d8_shadow_metric_highstress/stress_events.csv"

D6SM_TFLITE="experiments/v8_parent_c2612_d6_sourcemargin_qanchor_20260521_0001/s2d_c2_6_12_d6_shadow_init/parent_int8.tflite"
D6SM_PARAMS="experiments/v8_parent_c2612_d6_sourcemargin_qanchor_20260521_0001/s2d_c2_6_12_d6_shadow_init_compile/best_parent_logits_memory_params.npz"
D6SM_STRESS="experiments/v8_parent_c2612_d6_sourcemargin_qanchor_20260521_0001/s2d_c2_6_12_d6_shadow_init_highstress/stress_events.csv"

D7SM_TFLITE="experiments/v8_parent_c2612_d7_sourcemargin_qanchor_20260521_0001/s2d_c2_6_12_d7_shadow_init/parent_int8.tflite"
D7SM_PARAMS="experiments/v8_parent_c2612_d7_sourcemargin_qanchor_20260521_0001/s2d_c2_6_12_d7_shadow_init_prune_under2_m0/best_pruned_merged_parent_logits_params.npz"
D7SM_STRESS="experiments/v8_parent_c2612_d7_sourcemargin_qanchor_20260521_0001/s2d_c2_6_12_d7_shadow_init_prune_under2_m0_highstress/stress_events.csv"

SYNTH_SOURCE_GATE="experiments/v8_synthetic_source_gate_teacher_20260521_0001/cleanrow_all_margin_all41_extra29/source_gate_teacher.npz"
DYNAMIC_QPAIR="experiments/v8_pair_margin_teacher_20260520_0001/d4_c2612_qanchor_neigh32_dynamic/pair_margin_teacher.npz"

HP_PERTURBS="identity,noise_0p02,noise_0p04,noise_0p06,noise_0p08,noise_0p10,blur3a0,blur3a45,blur3a90,blur3a135,blur5a0,blur5a45,blur5a90,blur5a135,blur7a45,blur7a135,blur5a45_noise0p04,blur5a135_noise0p04,blur7a45_noise0p04,bright_p0p04,bright_p0p08,bright_p0p12,bright_m0p04,bright_m0p08,bright_m0p12,contrast_p0p10,contrast_p0p20,contrast_m0p10,contrast_m0p20,shift_u1,shift_d1,shift_l1,shift_r1,shift_ul1,shift_dr1,shift_u2,shift_d2,shift_l2,shift_r2,shift_u1_noise0p04,shift_l1_noise0p04"
DYNAMIC_NORMAL_STRESS="rot90,rot180,rot270,mirror_lr,mirror_lr_rot90,mirror_lr_rot180,mirror_lr_rot270,cam_blur3a135,hblur5_noise_0p06,cam_blur5a135,cam_noise0p04,cam_blur5a45_noise0p04,cam_blur3a135_noise0p02,cam_blur3a0_noise0p02,diagblur5,cam_blur5a45,cam_bright0p04_contrast0p10,cam_noise0p02,vblur5,cam_contrast0p12,noise_0p10,diagblur5_noise_0p08,cam_blur2a0,cam_blur3a45,cam_bright0p06,cam_noise0p03,cam_blur3a90,noise_0p06,cam_blur3a45_noise0p02"
SYNTH_SOURCE_STRESS="cam_blur3a90,cam_blur5a45,cam_blur5a135,cam_noise0p02,cam_noise0p04,cam_blur5a45_noise0p04,cam_blur3a45,cam_blur3a135,cam_noise0p06,cam_noise0p08,cam_noise0p10,cam_blur3a0,cam_blur5a0,cam_blur5a90,cam_blur7a45,cam_blur7a135,cam_blur5a135_noise0p04,cam_blur7a45_noise0p04,cam_brightp0p04,cam_brightp0p08,cam_brightp0p12,cam_brightm0p04,cam_brightm0p08,cam_brightm0p12,cam_contrastp0p10,cam_contrastp0p20,cam_contrastm0p10,cam_contrastm0p20,cam_shiftu1,cam_shiftd1,cam_shiftl1,cam_shiftr1,cam_shiftul1,cam_shiftdr1,cam_shiftu2,cam_shiftd2,cam_shiftl2,cam_shiftr2,cam_shiftu1_noise0p04,cam_shiftl1_noise0p04"

required_paths=(
  "${BASE_TFLITE}" "${BASE_PARAMS}" "${BASE_STRESS}"
  "${C248_D4_TFLITE}" "${C248_D4_PARAMS}" "${C248_D4_STRESS}"
  "${D8Q_TFLITE}" "${D8Q_PARAMS}" "${D8Q_STRESS}"
  "${D6SM_TFLITE}" "${D6SM_PARAMS}" "${D6SM_STRESS}"
  "${D7SM_TFLITE}" "${D7SM_PARAMS}" "${D7SM_STRESS}"
  "${SYNTH_SOURCE_GATE}" "${DYNAMIC_QPAIR}"
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

source_gate_common_flags() {
  cat <<EOF
--source-tflite d4best=${BASE_TFLITE} --source-params d4best=${BASE_PARAMS} --source-stress d4best=${BASE_STRESS} \
--source-tflite c248d4=${C248_D4_TFLITE} --source-params c248d4=${C248_D4_PARAMS} --source-stress c248d4=${C248_D4_STRESS} \
--source-tflite d8q=${D8Q_TFLITE} --source-params d8q=${D8Q_PARAMS} --source-stress d8q=${D8Q_STRESS} \
--source-tflite d6sm=${D6SM_TFLITE} --source-params d6sm=${D6SM_PARAMS} --source-stress d6sm=${D6SM_STRESS} \
--source-tflite d7sm=${D7SM_TFLITE} --source-params d7sm=${D7SM_PARAMS} --source-stress d7sm=${D7SM_STRESS}
EOF
}

launch_synthetic_gate_probe() {
  local name="$1"
  local extra_flags="$2"
  local out_dir="${ROOT}/e0_synthetic_gate_${name}"
  local session="v8e0_${RUN_TAG}_${name}"
  local flags
  flags="$(source_gate_common_flags)"
  local cmd
  cmd=$(cat <<EOF
set -euo pipefail
cd ${SCRIPT_DIR}
mkdir -p ${out_dir}
CUDA_VISIBLE_DEVICES="" OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} venv/bin/python analyze_v8_synthetic_source_event_gate.py \
  ${flags} \
  --base-params ${BASE_PARAMS} \
  --dataset-dir dataset \
  --output-dir ${out_dir} \
  --perturbs ${HP_PERTURBS} \
  --normal-margin-max 128 \
  --max-train-base-rows 128 \
  --feature-modes code+dist+margin+pred+wrong_parent+family \
  --hidden-dims 32,64 \
  --epochs 100 \
  --batch-size 512 \
  --seed 20260525 \
  ${extra_flags} 2>&1 | tee -a ${out_dir}/run.log
EOF
)
  launch_session "${session}" "${cmd}"
}

launch_parent_pipeline() {
  local name="$1"
  local out_dir="${ROOT}/${name}"
  local compile_dir="${out_dir}_compile_m4"
  local stress_dir="${out_dir}_highstress_canonical"
  local session="v8gpu_${RUN_TAG}_${name}"
  shift
  local train_flags="$*"
  local cmd
  cmd=$(cat <<EOF
set -euo pipefail
cd ${SCRIPT_DIR}
mkdir -p ${out_dir} ${compile_dir} ${stress_dir}
OMP_NUM_THREADS=1 TF_NUM_INTRAOP_THREADS=1 TF_NUM_INTEROP_THREADS=1 ./run_gpu.sh venv/bin/python train_v8_parent_classifier.py \
  --dataset-dir dataset \
  --output-dir ${out_dir} \
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

launch_sourceblock_pipeline() {
  local name="e1_sourceblock_d13"
  local teacher_dir="${ROOT}/e1_sourceblock_teacher"
  local out_dir="${ROOT}/${name}"
  local compile_dir="${out_dir}_compile_m4"
  local stress_dir="${out_dir}_highstress_canonical"
  local session="v8gpu_${RUN_TAG}_${name}"
  local cmd
  cmd=$(cat <<EOF
set -euo pipefail
cd ${SCRIPT_DIR}
mkdir -p ${teacher_dir} ${out_dir} ${compile_dir} ${stress_dir}
CUDA_VISIBLE_DEVICES="" OMP_NUM_THREADS=${OMP_THREADS} TF_NUM_INTRAOP_THREADS=${TF_INTRA_THREADS} TF_NUM_INTEROP_THREADS=${TF_INTER_THREADS} venv/bin/python build_v8_source_block_qanchor_teacher.py \
  --base-npz ${BASE_PARAMS} \
  --source-npz ${C248_D4_PARAMS},${D6SM_PARAMS},${D7SM_PARAMS} \
  --output-dir ${teacher_dir} \
  --target-abs-p99 48 \
  --margin-scale-percentile 90 \
  --base-low-margin-threshold 16 \
  --base-low-margin-extra-weight 1.0 2>&1 | tee -a ${teacher_dir}/run.log
OMP_NUM_THREADS=1 TF_NUM_INTRAOP_THREADS=1 TF_NUM_INTEROP_THREADS=1 ./run_gpu.sh venv/bin/python train_v8_parent_classifier.py \
  --dataset-dir dataset \
  --output-dir ${out_dir} \
  --stress ${DYNAMIC_NORMAL_STRESS} \
  --filters 2,6,12 \
  --code-dim 13 \
  --seed 2026052501 \
  --epochs 180 \
  --learning-rate 0.002 \
  --backbone-architecture spacetodepth_conv \
  --activation relu6 \
  --pool max \
  --sample-weight-mode parent_balanced \
  --qanchor-teacher-npz ${teacher_dir}/qanchor_teacher.npz \
  --qanchor-weight 0.0001 \
  --qanchor-start-epoch 1 \
  --source-block-margin-weight 0.001 \
  --source-block-margin-target 16 \
  --source-block-margin-alpha 0.05 \
  --source-block-margin-start-epoch 3 \
  --dynamic-qpair-teacher-npz ${DYNAMIC_QPAIR} \
  --dynamic-qpair-margin-weight 0.0001 \
  --dynamic-qpair-margin-target 64 \
  --log-every 25 2>&1 | tee -a ${out_dir}/run.log
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
  "purpose": "Divergent V8 robustness experiments: E0 synthetic-gate holdouts, E1 source-block budgeted decision, E2/E3/E7 single-encoder probes.",
  "high_pressure_usage": "evaluation_only",
  "base_params": "${BASE_PARAMS}",
  "source_gate_teacher": "${SYNTH_SOURCE_GATE}",
  "dynamic_qpair_teacher": "${DYNAMIC_QPAIR}",
  "hp_perturbs": "${HP_PERTURBS}"
}
EOF

launch_synthetic_gate_probe "base_mod" "--holdout-mode base_mod --validation-mod 5"
launch_synthetic_gate_probe "sample_mod" "--holdout-mode sample_mod --validation-mod 5"
launch_synthetic_gate_probe "family_shift_blur" "--holdout-mode family --holdout-families shift,blur,blur_noise"
launch_synthetic_gate_probe "perturb_combo" "--holdout-mode perturb --holdout-perturbs blur7a45_noise0p04,shift_u1_noise0p04,bright_m0p12,contrast_m0p20"

launch_parent_pipeline "e3_rawlowedge_d8_synthrank" \
  --filters 2,6,12 \
  --code-dim 8 \
  --seed 2026052502 \
  --epochs 160 \
  --learning-rate 0.002 \
  --backbone-architecture spacetodepth_conv \
  --activation relu6 \
  --pool max \
  --stress ${SYNTH_SOURCE_STRESS} \
  --input-transform raw_low_edge \
  --source-gate-teacher-npz ${SYNTH_SOURCE_GATE} \
  --source-gate-start 3 \
  --source-gate-weight 0.002 \
  --source-gate-margin-weight 0.0002 \
  --source-gate-margin-target 16 \
  --source-gate-balance-weight 0.05 \
  --source-gate-rank-weight 0.005 \
  --source-gate-rank-min-gap 16 \
  --source-gate-rank-max-target 16 \
  --source-gate-center-weight 0.001 \
  --source-gate-center-target 16

launch_parent_pipeline "e3_lowedge_d8_synthrank" \
  --filters 2,6,12 \
  --code-dim 8 \
  --seed 2026052503 \
  --epochs 160 \
  --learning-rate 0.002 \
  --backbone-architecture spacetodepth_conv \
  --activation relu6 \
  --pool max \
  --stress ${SYNTH_SOURCE_STRESS} \
  --input-transform low_edge \
  --source-gate-teacher-npz ${SYNTH_SOURCE_GATE} \
  --source-gate-start 3 \
  --source-gate-weight 0.002 \
  --source-gate-margin-weight 0.0002 \
  --source-gate-margin-target 16 \
  --source-gate-balance-weight 0.05 \
  --source-gate-rank-weight 0.005 \
  --source-gate-rank-min-gap 16 \
  --source-gate-rank-max-target 16 \
  --source-gate-center-weight 0.001 \
  --source-gate-center-target 16

launch_parent_pipeline "e2_robustprimary_d12_synthrank" \
  --filters 2,6,12 \
  --code-dim 12 \
  --seed 2026052504 \
  --epochs 180 \
  --learning-rate 0.0018 \
  --backbone-architecture spacetodepth_conv \
  --activation relu6 \
  --pool max \
  --stress ${SYNTH_SOURCE_STRESS} \
  --input-transform identity \
  --source-gate-teacher-npz ${SYNTH_SOURCE_GATE} \
  --source-gate-start 3 \
  --source-gate-weight 0.002 \
  --source-gate-margin-weight 0.0002 \
  --source-gate-margin-target 16 \
  --source-gate-balance-weight 0.05 \
  --source-gate-rank-weight 0.006 \
  --source-gate-rank-min-gap 16 \
  --source-gate-rank-max-target 20 \
  --source-gate-center-weight 0.001 \
  --source-gate-center-target 16 \
  --source-cluster-weight 0.0002 \
  --source-cluster-target 64

launch_parent_pipeline "e7_flatness_d8_orbit_vicreg" \
  --filters 2,6,12 \
  --code-dim 8 \
  --seed 2026052505 \
  --epochs 160 \
  --learning-rate 0.0018 \
  --backbone-architecture spacetodepth_conv \
  --activation relu6 \
  --pool max \
  --stress ${DYNAMIC_NORMAL_STRESS} \
  --input-transform identity \
  --orbit-consistency-weight 0.0005 \
  --vicreg-var-weight 0.001 \
  --vicreg-cov-weight 0.0005 \
  --vicreg-variance-floor 16 \
  --dynamic-qpair-teacher-npz ${DYNAMIC_QPAIR} \
  --dynamic-qpair-margin-weight 0.00015 \
  --dynamic-qpair-margin-target 64

launch_sourceblock_pipeline

echo "root=${ROOT}"
echo "sessions:"
tmux ls | grep -E "v8(e0|gpu)_${RUN_TAG}" || true
