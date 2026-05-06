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
  "${REPO_ROOT}/new/code/legacy/steering_bev_simple_perception.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_bev_element_raster.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_circle_element_evidence.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_cross_exit_element_evidence.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_visual_element_pipeline.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_visual_reference_orchestration.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_reference_usability.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_reference_lateral_error.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_reference_control_readiness.cpp" \
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
  require_token "${log_path}" "${case_name}" "element_evidence.records.count=4"
  require_token "${log_path}" "${case_name}" "element_evidence.records[0].id=circle_left_raw"
  require_token "${log_path}" "${case_name}" "element_evidence.records[1].id=circle_right_raw"
  require_token "${log_path}" "${case_name}" "element_evidence.records[2].id=circle_left"
  require_token "${log_path}" "${case_name}" "element_evidence.records[3].id=circle_right"
  require_token "${log_path}" "${case_name}" "element_evidence.records[0].candidate.reason=evidence_only"
  require_token "${log_path}" "${case_name}" "element_evidence.records[1].candidate.reason=evidence_only"
  require_token "${log_path}" "${case_name}" "element_evidence.records[2].candidate.reason=evidence_only"
  require_token "${log_path}" "${case_name}" "element_evidence.records[3].candidate.reason=evidence_only"

  local token
  for token in "$@"; do
    require_token "${log_path}" "${case_name}" "${token}"
  done
  echo "scene_overlay_probe authority-baseline ${log_name} passed"
}

run_probe_case \
  "circle-2" \
  "element_evidence.cross_exit.present=false" \
  "element_evidence.records[0].present=true" \
  "element_evidence.records[0].reason=present" \
  "element_evidence.records[1].present=false" \
  "element_evidence.records[2].present=true" \
  "element_evidence.records[2].reason=present" \
  "element_evidence.records[3].present=false"

disabled_params_path="${ARTIFACT_DIR}/circle-evidence-disabled.json"
python3 - "${PARAMS_PATH}" "${disabled_params_path}" <<'PY'
import json
import sys

source_path, target_path = sys.argv[1:3]
with open(source_path, "r", encoding="utf-8") as file:
    params = json.load(file)
params.setdefault("BEV_ELEMENT", {})["CIRCLE_EVIDENCE_ENABLED"] = 0
with open(target_path, "w", encoding="utf-8") as file:
    json.dump(params, file, indent=2)
    file.write("\n")
PY

run_probe_case \
  "circle-2" \
  --label "circle-2-disabled" \
  --params "${disabled_params_path}" \
  "element_evidence.cross_exit.present=false" \
  "element_evidence.records[0].present=false" \
  "element_evidence.records[0].reason=circle_evidence_disabled" \
  "element_evidence.records[1].present=false" \
  "element_evidence.records[1].reason=circle_evidence_disabled" \
  "element_evidence.records[2].present=false" \
  "element_evidence.records[2].reason=circle_evidence_disabled" \
  "element_evidence.records[3].present=false" \
  "element_evidence.records[3].reason=circle_evidence_disabled"

invalid_confidence_params_path="${ARTIFACT_DIR}/circle-invalid-confidence.json"
python3 - "${PARAMS_PATH}" "${invalid_confidence_params_path}" <<'PY'
import json
import sys

source_path, target_path = sys.argv[1:3]
with open(source_path, "r", encoding="utf-8") as file:
    params = json.load(file)
params.setdefault("BEV_ELEMENT", {})["CIRCLE_PRESENT_CONFIDENCE_MIN"] = 1.5
with open(target_path, "w", encoding="utf-8") as file:
    json.dump(params, file, indent=2)
    file.write("\n")
PY

run_probe_case \
  "circle-2" \
  --label "circle-2-invalid-confidence-fallback" \
  --params "${invalid_confidence_params_path}" \
  "element_evidence.cross_exit.present=false" \
  "element_evidence.records[0].present=true" \
  "element_evidence.records[0].reason=present" \
  "element_evidence.records[1].present=false" \
  "element_evidence.records[2].present=true" \
  "element_evidence.records[2].reason=present" \
  "element_evidence.records[3].present=false"

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
  "element_evidence.records[0].present=true" \
  "element_evidence.records[0].reason=present" \
  "element_evidence.records[1].present=false" \
  "element_evidence.records[2].present=true" \
  "element_evidence.records[2].reason=present" \
  "element_evidence.records[3].present=false"

non_object_raster_params_path="${ARTIFACT_DIR}/circle-non-object-bev-element-raster.json"
python3 - "${PARAMS_PATH}" "${non_object_raster_params_path}" <<'PY'
import json
import sys

source_path, target_path = sys.argv[1:3]
with open(source_path, "r", encoding="utf-8") as file:
    params = json.load(file)
params.setdefault("BEV_ELEMENT", {})["CIRCLE_EVIDENCE_ENABLED"] = 0
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
  "element_evidence.records[0].present=true" \
  "element_evidence.records[0].reason=present" \
  "element_evidence.records[1].present=false" \
  "element_evidence.records[2].present=true" \
  "element_evidence.records[2].reason=present" \
  "element_evidence.records[3].present=false"

for case_name in circle-1 circle-3; do
  run_probe_case \
    "${case_name}" \
    "element_evidence.cross_exit.present=false" \
    "element_evidence.records[0].present=true" \
    "element_evidence.records[0].reason=present" \
    "element_evidence.records[1].present=false" \
    "element_evidence.records[2].present=true" \
    "element_evidence.records[2].reason=present" \
    "element_evidence.records[3].present=false"
done

for case_name in cross-1 cross-2 cross-3; do
  run_probe_case \
    "${case_name}" \
    "element_evidence.cross_exit.present=true" \
    "element_evidence.records[0].present=false" \
    "element_evidence.records[1].present=false" \
    "element_evidence.records[2].present=false" \
    "element_evidence.records[3].present=false"
done

for case_name in bend-1 bend-2 bend-3; do
  run_probe_case \
    "${case_name}" \
    "element_evidence.cross_exit.present=false" \
    "element_evidence.records[0].present=false" \
    "element_evidence.records[1].present=false" \
    "element_evidence.records[2].present=false" \
    "element_evidence.records[3].present=false"
done

echo "scene_overlay_probe authority-baseline passed"
echo "artifact_dir=${ARTIFACT_DIR}"
