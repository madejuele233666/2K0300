#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_BIN="${SCRIPT_DIR}/visual_element_evidence_test"

source "${SCRIPT_DIR}/common_steering_test_build.sh"

compile_test_binary \
  "${OUT_BIN}" \
  "${REPO_ROOT}/new/verification/tests/visual_element_evidence_test.cpp" \
  "${REPO_ROOT}/new/code/vision/bev/bev_projector.cpp" \
  "${REPO_ROOT}/new/code/vision/bev/single_boundary_offset.cpp" \
  "${REPO_ROOT}/new/code/vision/bev/reference_connectivity.cpp" \
  "${REPO_ROOT}/new/code/vision/bev/bev_simple_perception.cpp" \
  "${REPO_ROOT}/new/code/vision/bev/bev_element_raster.cpp" \
  "${REPO_ROOT}/new/code/vision/elements/circle_element_evidence.cpp" \
  "${REPO_ROOT}/new/code/vision/elements/cross_exit_element_evidence.cpp" \
  "${REPO_ROOT}/new/code/vision/elements/visual_element_pipeline.cpp" \
  "${REPO_ROOT}/new/code/reference/visual_reference_orchestration.cpp"

"${OUT_BIN}"
