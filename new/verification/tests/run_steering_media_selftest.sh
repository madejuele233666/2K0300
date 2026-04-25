#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUT_BIN="${SCRIPT_DIR}/steering_media_selftest"

c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  -I"${REPO_ROOT}/new/code" \
  -I"${REPO_ROOT}/new/code/port" \
  -I"${REPO_ROOT}/new/code/platform" \
  -I"${REPO_ROOT}/new/code/platform/true_ls2k0300" \
  -I"${REPO_ROOT}/new/code/runtime" \
  -I"${REPO_ROOT}/new/user" \
  "${REPO_ROOT}/new/user/steering_media_selftest.cpp" \
  "${REPO_ROOT}/new/code/platform/steering_media_link.cpp" \
  "${REPO_ROOT}/new/code/platform/steering_media_protocol.cpp" \
  "${REPO_ROOT}/new/code/platform/true_ls2k0300/steering_media_bridge.cpp" \
  "${REPO_ROOT}/new/code/runtime/control_debug_reporter.cpp" \
  "${REPO_ROOT}/new/code/runtime/control_decision.cpp" \
  "${REPO_ROOT}/new/code/runtime/motion_supervisor.cpp" \
  "${REPO_ROOT}/new/code/runtime/steering_media_service.cpp" \
  -o "${OUT_BIN}"

"${OUT_BIN}"
