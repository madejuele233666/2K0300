#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUT_BIN="${SCRIPT_DIR}/wheel_target_mixer_test"

c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  -I"${REPO_ROOT}/new/code" \
  -I"${REPO_ROOT}/new/code/port" \
  -I"${REPO_ROOT}/new/code/legacy" \
  "${REPO_ROOT}/new/verification/tests/wheel_target_mixer_test.cpp" \
  "${REPO_ROOT}/new/code/legacy/wheel_target_mixer.cpp" \
  -o "${OUT_BIN}"

"${OUT_BIN}"
