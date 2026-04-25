#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUT_BIN="${SCRIPT_DIR}/control_debug_snapshot_contract_test"

g++ -std=c++17 -Wall -Wextra -pedantic \
  -I"${REPO_ROOT}/new/code" \
  "${REPO_ROOT}/new/verification/tests/control_debug_snapshot_contract_test.cpp" \
  "${REPO_ROOT}/new/code/runtime/control_debug_reporter.cpp" \
  "${REPO_ROOT}/new/code/runtime/control_decision.cpp" \
  "${REPO_ROOT}/new/code/runtime/motion_supervisor.cpp" \
  -o "${OUT_BIN}"

"${OUT_BIN}"
