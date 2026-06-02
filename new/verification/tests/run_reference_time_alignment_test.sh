#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_BIN="${SCRIPT_DIR}/reference_time_alignment_test"

source "${SCRIPT_DIR}/common_steering_test_build.sh"

compile_test_binary \
  "${OUT_BIN}" \
  "${REPO_ROOT}/new/verification/tests/reference_time_alignment_test.cpp" \
  "${REPO_ROOT}/new/code/runtime/steering_reference_time_alignment.cpp" \
  "${REPO_ROOT}/new/code/port/perf_counter.cpp"

"${OUT_BIN}"
