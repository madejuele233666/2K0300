#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_BIN="${SCRIPT_DIR}/steering_media_selftest"

source "${SCRIPT_DIR}/common_steering_test_build.sh"

compile_test_binary \
  "${OUT_BIN}" \
  "${REPO_ROOT}/new/user/steering_media_selftest.cpp" \
  "${STEERING_MEDIA_SOURCES[@]}"

"${OUT_BIN}"
