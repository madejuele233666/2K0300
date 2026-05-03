#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUT_BIN="${SCRIPT_DIR}/bev_element_evidence_test"

c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  -I"${REPO_ROOT}/new/code" \
  -I"${REPO_ROOT}/new/code/port" \
  -I"${REPO_ROOT}/new/code/legacy" \
  "${REPO_ROOT}/new/verification/tests/bev_element_evidence_test.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_bev_projector.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_bev_sparse_sampler.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_bev_element_evidence.cpp" \
  -o "${OUT_BIN}"

"${OUT_BIN}"
