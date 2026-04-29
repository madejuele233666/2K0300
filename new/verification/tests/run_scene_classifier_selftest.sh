#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_BIN="${SCRIPT_DIR}/scene_classifier_selftest"

source "${SCRIPT_DIR}/common_steering_test_build.sh"

compile_test_binary \
  "${OUT_BIN}" \
  "${REPO_ROOT}/new/verification/tests/scene_classifier_selftest.cpp" \
  "${CAMERA_PIPELINE_SOURCES[@]}" \
  "${PID_SOURCES[@]}"

"${OUT_BIN}"
