#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUT_BIN="${SCRIPT_DIR}/legacy_pid_control_cycle_test"

g++ -std=c++17 -Wall -Wextra -pedantic \
  -I"${REPO_ROOT}/new/code" \
  "${REPO_ROOT}/new/verification/tests/legacy_pid_control_cycle_test.cpp" \
  "${REPO_ROOT}/new/code/legacy/pid_control.cpp" \
  "${REPO_ROOT}/new/code/legacy/fuzzy_pid_ucas.cpp" \
  -o "${OUT_BIN}"

"${OUT_BIN}"
