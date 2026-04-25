#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUT_BIN="${SCRIPT_DIR}/steering_track_fusion_selftest"

c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  -I"${REPO_ROOT}/new/code" \
  -I"${REPO_ROOT}/new/code/port" \
  -I"${REPO_ROOT}/new/code/legacy" \
  -I"${REPO_ROOT}/new/code/runtime" \
  "${REPO_ROOT}/new/verification/tests/steering_track_fusion_selftest.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_bottom_tracker.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_gyro_continuity.cpp" \
  "${REPO_ROOT}/new/code/runtime/control_decision.cpp" \
  -o "${OUT_BIN}"

"${OUT_BIN}"
