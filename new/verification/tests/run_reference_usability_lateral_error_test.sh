#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUT_BIN="${SCRIPT_DIR}/reference_usability_lateral_error_test"

c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  -I"${REPO_ROOT}/new/code" \
  -I"${REPO_ROOT}/new/code/port" \
  "${REPO_ROOT}/new/verification/tests/reference_usability_lateral_error_test.cpp" \
  "${REPO_ROOT}/new/code/reference/reference_usability.cpp" \
  "${REPO_ROOT}/new/code/reference/reference_lateral_error.cpp" \
  "${REPO_ROOT}/new/code/reference/reference_tracking_geometry.cpp" \
  "${REPO_ROOT}/new/code/reference/reference_control_readiness.cpp" \
  "${REPO_ROOT}/new/code/control/steering_yaw_controller.cpp" \
  "${REPO_ROOT}/new/code/safety/control_gate.cpp" \
  "${REPO_ROOT}/new/code/safety/control_apply_observation.cpp" \
  -o "${OUT_BIN}"

"${OUT_BIN}"
