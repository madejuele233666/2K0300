#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
ARTIFACT_DIR="${ARTIFACT_DIR:-$(mktemp -d)}"
OUT_BIN="${ARTIFACT_DIR}/scene_overlay_probe_authority_baseline"
FIXTURE_DIR="${REPO_ROOT}/new/verification/test-images/authority-baseline"
PARAMS_PATH="${REPO_ROOT}/new/config/default_params.json"

source "${SCRIPT_DIR}/common_steering_test_build.sh"

mkdir -p "${ARTIFACT_DIR}"

compile_test_binary \
  "${OUT_BIN}" \
  "${REPO_ROOT}/new/user/scene_overlay_probe.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_otsu_threshold.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_bev_projector.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_single_boundary_offset.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_bev_simple_perception.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_bev_element_raster.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_circle_element_evidence.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_cross_exit_element_evidence.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_visual_element_pipeline.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_visual_reference_orchestration.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_reference_usability.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_reference_lateral_error.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_reference_control_readiness.cpp" \
  "${REPO_ROOT}/new/code/runtime/steering_circle_v2_scene.cpp" \
  "${REPO_ROOT}/new/code/runtime/steering_circle_v2_reference_adapter.cpp" \
  "${REPO_ROOT}/new/code/runtime/detail/steering_circle_v2_event_observer.cpp" \
  "${REPO_ROOT}/new/code/runtime/detail/steering_circle_v2_expansion_observer.cpp" \
  "${REPO_ROOT}/new/code/runtime/detail/steering_circle_v2_reducer.cpp" \
  "${REPO_ROOT}/new/code/runtime/detail/steering_circle_v2_geometry_observer.cpp" \
  "${REPO_ROOT}/new/code/runtime/detail/steering_circle_v2_composer.cpp" \
  "${REPO_ROOT}/new/code/port/perf_counter.cpp"

require_token() {
  local log_path="$1"
  local case_name="$2"
  local token="$3"
  if ! grep -Fq "${token}" "${log_path}"; then
    echo "scene_overlay_probe authority-baseline failed: ${case_name} missing token: ${token}" >&2
    echo "log_path=${log_path}" >&2
    tail -n 80 "${log_path}" >&2 || true
    exit 1
  fi
}

run_probe_case() {
  local case_name="$1"
  shift
  local params_path="${PARAMS_PATH}"
  local log_name="${case_name}"
  if [[ "${1:-}" == "--label" ]]; then
    if [[ "$#" -lt 2 ]]; then
      echo "scene_overlay_probe authority-baseline failed: --label requires a value" >&2
      exit 1
    fi
    log_name="$2"
    shift 2
  fi
  if [[ "${1:-}" == "--params" ]]; then
    if [[ "$#" -lt 2 ]]; then
      echo "scene_overlay_probe authority-baseline failed: --params requires a value" >&2
      exit 1
    fi
    params_path="$2"
    shift 2
  fi
  local raw_path="${FIXTURE_DIR}/${case_name}.raw"
  local log_path="${ARTIFACT_DIR}/${log_name}.log"
  local overlay_path="${ARTIFACT_DIR}/${log_name}.bmp"

  if [[ ! -f "${raw_path}" ]]; then
    echo "scene_overlay_probe authority-baseline failed: missing fixture ${raw_path}" >&2
    exit 1
  fi

  "${OUT_BIN}" "${raw_path}" "${overlay_path}" "${params_path}" --bev-only > "${log_path}"
  require_token "${log_path}" "${case_name}" "element_evidence.records.count=0"
  require_token "${log_path}" "${case_name}" "circle_v2.enabled=true"
  if grep -Fq "circle_entry." "${log_path}"; then
    echo "scene_overlay_probe authority-baseline failed: ${case_name} still prints circle_entry diagnostics" >&2
    echo "log_path=${log_path}" >&2
    tail -n 80 "${log_path}" >&2 || true
    exit 1
  fi

  local token
  for token in "$@"; do
    require_token "${log_path}" "${case_name}" "${token}"
  done
  echo "scene_overlay_probe authority-baseline ${log_name} passed"
}

run_probe_case \
  "circle-2" \
  "element_evidence.cross_exit.present=false" \
  "circle_v2.frame_phase=approach" \
  "circle_v2.next_phase=approach" \
  "circle_v2.dir=left" \
  "circle_v2.reference_role=none" \
  "circle_v2.reason=phase1_cue_left" \
  "visual_reference.source=none"

non_object_element_params_path="${ARTIFACT_DIR}/circle-non-object-bev-element.json"
python3 - "${PARAMS_PATH}" "${non_object_element_params_path}" <<'PY'
import json
import sys

source_path, target_path = sys.argv[1:3]
with open(source_path, "r", encoding="utf-8") as file:
    params = json.load(file)
params["BEV_ELEMENT"] = 1
params.setdefault("BEV_ELEMENT_RASTER", {})["ENABLED"] = 0
with open(target_path, "w", encoding="utf-8") as file:
    json.dump(params, file, indent=2)
    file.write("\n")
PY

run_probe_case \
  "circle-2" \
  --label "circle-2-non-object-bev-element-fallback" \
  --params "${non_object_element_params_path}" \
  "element_evidence.cross_exit.present=false" \
  "circle_v2.frame_phase=approach" \
  "circle_v2.dir=left" \
  "circle_v2.reason=phase1_cue_left"

non_object_raster_params_path="${ARTIFACT_DIR}/circle-non-object-bev-element-raster.json"
python3 - "${PARAMS_PATH}" "${non_object_raster_params_path}" <<'PY'
import json
import sys

source_path, target_path = sys.argv[1:3]
with open(source_path, "r", encoding="utf-8") as file:
    params = json.load(file)
params["BEV_ELEMENT_RASTER"] = 1
with open(target_path, "w", encoding="utf-8") as file:
    json.dump(params, file, indent=2)
    file.write("\n")
PY

run_probe_case \
  "circle-2" \
  --label "circle-2-non-object-bev-element-raster-fallback" \
  --params "${non_object_raster_params_path}" \
  "element_evidence.cross_exit.present=false" \
  "circle_v2.frame_phase=approach" \
  "circle_v2.next_phase=approach" \
  "circle_v2.dir=left" \
  "circle_v2.reference_role=none"

default_confirm_log_path="${ARTIFACT_DIR}/circle-2-confirmed-innertrace.log"
"${OUT_BIN}" \
  "${FIXTURE_DIR}/circle-2.raw" \
  "${ARTIFACT_DIR}/circle-2-confirmed-innertrace.bmp" \
  "${PARAMS_PATH}" \
  --bev-only \
  --confirm-cycles 2 > "${default_confirm_log_path}"
require_token "${default_confirm_log_path}" \
  "circle-2-confirmed-innertrace" \
  "circle_v2.frame_phase=inner_trace"
require_token "${default_confirm_log_path}" \
  "circle-2-confirmed-innertrace" \
  "circle_v2.next_phase=inner_trace"
require_token "${default_confirm_log_path}" \
  "circle-2-confirmed-innertrace" \
  "circle_v2.reason=entry_gate_reached"
require_token "${default_confirm_log_path}" \
  "circle-2-confirmed-innertrace" \
  "visual_reference.source=circle_v2_inner"
echo "scene_overlay_probe authority-baseline circle-2-confirmed-innertrace passed"

for case_name in circle-1 circle-3; do
  run_probe_case \
    "${case_name}" \
    "element_evidence.cross_exit.present=false" \
    "circle_v2.frame_phase=approach" \
    "circle_v2.dir=left" \
    "circle_v2.reason=phase1_cue_left"
done

for case_name in cross-1 cross-2 cross-3; do
  run_probe_case \
    "${case_name}" \
    "element_evidence.cross_exit.present=true" \
    "circle_v2.frame_phase=idle" \
    "circle_v2.next_phase=idle" \
    "circle_v2.dir=none"
done

cross_suppresses_active_log_path="${ARTIFACT_DIR}/cross-suppresses-active-circle.log"
"${OUT_BIN}" \
  "${FIXTURE_DIR}/cross-1.raw" \
  "${ARTIFACT_DIR}/cross-suppresses-active-circle.bmp" \
  "${PARAMS_PATH}" \
  --bev-only \
  --warmup "${FIXTURE_DIR}/circle-2.raw" \
  --warmup "${FIXTURE_DIR}/circle-2.raw" > "${cross_suppresses_active_log_path}"
require_token "${cross_suppresses_active_log_path}" \
  "cross-suppresses-active-circle" \
  "element_evidence.cross_exit.present=true"
require_token "${cross_suppresses_active_log_path}" \
  "cross-suppresses-active-circle" \
  "circle_v2.frame_phase=idle"
require_token "${cross_suppresses_active_log_path}" \
  "cross-suppresses-active-circle" \
  "circle_v2.next_phase=idle"
require_token "${cross_suppresses_active_log_path}" \
  "cross-suppresses-active-circle" \
  "circle_v2.dir=none"
require_token "${cross_suppresses_active_log_path}" \
  "cross-suppresses-active-circle" \
  "element_evidence.cross_exit.candidate.reason=line_candidate_absent"
require_token "${cross_suppresses_active_log_path}" \
  "cross-suppresses-active-circle" \
  "visual_reference.source=none"
echo "scene_overlay_probe authority-baseline cross-suppresses-active-circle passed"

for case_name in bend-1 bend-2 bend-3; do
  run_probe_case \
    "${case_name}" \
    "element_evidence.cross_exit.present=false" \
    "circle_v2.frame_phase=idle" \
    "circle_v2.next_phase=idle" \
    "circle_v2.dir=none"
done

echo "scene_overlay_probe authority-baseline passed"
echo "artifact_dir=${ARTIFACT_DIR}"
