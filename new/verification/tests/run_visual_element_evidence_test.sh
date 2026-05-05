#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_BIN="${SCRIPT_DIR}/visual_element_evidence_test"

source "${SCRIPT_DIR}/common_steering_test_build.sh"

compile_test_binary \
  "${OUT_BIN}" \
  "${REPO_ROOT}/new/verification/tests/visual_element_evidence_test.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_visual_element_evidence.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_visual_reference_orchestration.cpp"

"${OUT_BIN}"
