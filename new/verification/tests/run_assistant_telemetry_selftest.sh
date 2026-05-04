#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUT_BIN="${SCRIPT_DIR}/assistant_telemetry_selftest"
OPENCV_ROOT="${OPENCV_ROOT:-/opt/ls_2k0300_env/opencv_4_10_build}"

c++ -std=c++17 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -pthread \
  -I"${REPO_ROOT}/new/code" \
  -I"${REPO_ROOT}/new/code/port" \
  -I"${REPO_ROOT}/new/code/platform" \
  -I"${REPO_ROOT}/new/code/runtime" \
  -I"${OPENCV_ROOT}/include/opencv4" \
  "${REPO_ROOT}/new/verification/tests/assistant_telemetry_selftest.cpp" \
  "${REPO_ROOT}/new/code/platform/assistant_protocol.cpp" \
  "${REPO_ROOT}/new/code/runtime/motion_supervisor.cpp" \
  -Wl,--gc-sections \
  -o "${OUT_BIN}"

"${OUT_BIN}"
