#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common_steering_test_build.sh"

OUT_BIN="${SCRIPT_DIR}/perf_counter_test.bin"
compile_test_binary \
  "${OUT_BIN}" \
  -DLS2K_PERF_ENABLED=1 \
  -DLS2K_PERF_USE_CYCLE_COUNTER=0 \
  "${REPO_ROOT}/new/verification/tests/perf_counter_test.cpp" \
  "${REPO_ROOT}/new/code/port/perf_counter.cpp"

"${OUT_BIN}"
echo "perf_counter_test passed"
