#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
DATASET_DIR="${1:-${REPO_ROOT}/new/verification/test-images/authority-baseline}"
OUT_BIN="${SCRIPT_DIR}/authority_baseline_validate"

c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  -I"${REPO_ROOT}/new/code" \
  -I"${REPO_ROOT}/new/code/port" \
  -I"${REPO_ROOT}/new/code/legacy" \
  "${REPO_ROOT}/new/verification/tests/authority_baseline_validate.cpp" \
  "${REPO_ROOT}/new/code/legacy/camera_logic.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_bev_projector.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_bev_sparse_sampler.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_corridor_intervals.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_corridor_graph.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_topology_hypotheses.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_topology_evidence.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_bev_geometry.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_observation_assembly.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_scene_fsm.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_reference_policy.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_control_error_model.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_gyro_continuity.cpp" \
  -o "${OUT_BIN}"

"${OUT_BIN}" "${DATASET_DIR}"
