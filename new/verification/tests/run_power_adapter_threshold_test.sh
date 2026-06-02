#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
source "${SCRIPT_DIR}/common_steering_test_build.sh"

OUT_DIR="${REPO_ROOT}/new/out/tests"
mkdir -p "${OUT_DIR}"

compile_test_binary \
  "${OUT_DIR}/power_adapter_threshold_test" \
  "${REPO_ROOT}/new/verification/tests/power_adapter_threshold_test.cpp" \
  "${REPO_ROOT}/new/code/platform/power_adapter.cpp" \
  "${REPO_ROOT}/new/code/platform/true_ls2k0300/adc_bridge.cpp"

"${OUT_DIR}/power_adapter_threshold_test"
