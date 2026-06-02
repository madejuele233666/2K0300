#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_BIN="${SCRIPT_DIR}/camera_frame_store_test"

source "${SCRIPT_DIR}/common_steering_test_build.sh"

compile_test_binary \
  "${OUT_BIN}" \
  "${REPO_ROOT}/new/verification/tests/camera_frame_store_test.cpp" \
  "${REPO_ROOT}/new/code/runtime/camera_frame_store.cpp"

"${OUT_BIN}"
