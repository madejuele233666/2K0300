#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUT_BIN="${SCRIPT_DIR}/reference_policy_control_error_test"

c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  -I"${REPO_ROOT}/new/code" \
  -I"${REPO_ROOT}/new/code/port" \
  -I"${REPO_ROOT}/new/code/legacy" \
  "${REPO_ROOT}/new/verification/tests/reference_policy_control_error_test.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_topology_hypotheses.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_reference_policy.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_control_error_model.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_bev_geometry.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_bev_projector.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_bev_sparse_sampler.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_bev_element_evidence.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_corridor_intervals.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_corridor_graph.cpp" \
  -o "${OUT_BIN}"

"${OUT_BIN}"
