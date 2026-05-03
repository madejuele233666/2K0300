#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUT_BIN="${SCRIPT_DIR}/reference_usability_curvature_test"

c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  -I"${REPO_ROOT}/new/code" \
  -I"${REPO_ROOT}/new/code/port" \
  -I"${REPO_ROOT}/new/code/legacy" \
  "${REPO_ROOT}/new/verification/tests/reference_usability_curvature_test.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_reference_usability.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_reference_curvature.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_reference_control_readiness.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_yaw_controller.cpp" \
  "${REPO_ROOT}/new/code/runtime/control_decision.cpp" \
  -o "${OUT_BIN}"

"${OUT_BIN}"
