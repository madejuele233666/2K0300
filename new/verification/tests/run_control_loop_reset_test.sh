#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUT_BIN="${SCRIPT_DIR}/control_loop_reset_test"

g++ -std=c++17 -Wall -Wextra -pedantic \
  -I"${REPO_ROOT}/new/code" \
  "${REPO_ROOT}/new/verification/tests/control_loop_reset_test.cpp" \
  "${REPO_ROOT}/new/code/runtime/control_loop.cpp" \
  "${REPO_ROOT}/new/code/runtime/control_decision.cpp" \
  "${REPO_ROOT}/new/code/runtime/control_debug_reporter.cpp" \
  "${REPO_ROOT}/new/code/runtime/motion_supervisor.cpp" \
  "${REPO_ROOT}/new/code/runtime/tuning_state.cpp" \
  "${REPO_ROOT}/new/code/legacy/attitude_logic.cpp" \
  "${REPO_ROOT}/new/code/legacy/fuzzy_pid_ucas.cpp" \
  "${REPO_ROOT}/new/code/legacy/motor_logic.cpp" \
  "${REPO_ROOT}/new/code/legacy/pid_control.cpp" \
  "${REPO_ROOT}/new/code/legacy/wheel_pid.cpp" \
  "${REPO_ROOT}/new/code/legacy/wheel_target_mixer.cpp" \
  -o "${OUT_BIN}"

"${OUT_BIN}"
