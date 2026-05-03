#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_BIN="${SCRIPT_DIR}/authority_baseline_validate"

source "${SCRIPT_DIR}/common_steering_test_build.sh"

DATASET_DIR="${1:-${REPO_ROOT}/new/verification/test-images/authority-baseline}"

compile_test_binary \
  "${OUT_BIN}" \
  "${REPO_ROOT}/new/verification/tests/authority_baseline_validate.cpp" \
  "${CAMERA_PIPELINE_SOURCES[@]}"

"${OUT_BIN}" "${DATASET_DIR}"
