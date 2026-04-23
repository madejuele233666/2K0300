#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUT_BIN="${SCRIPT_DIR}/assistant_service_publish_policy_test"

c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  -I"${REPO_ROOT}/new/code" \
  -I"${REPO_ROOT}/new/code/port" \
  -I"${REPO_ROOT}/new/code/platform" \
  -I"${REPO_ROOT}/new/code/runtime" \
  -I"${REPO_ROOT}/new/user" \
  "${SCRIPT_DIR}/assistant_service_publish_policy_test.cpp" \
  "${REPO_ROOT}/new/code/runtime/assistant_service.cpp" \
  "${REPO_ROOT}/new/code/runtime/tuning_state.cpp" \
  -o "${OUT_BIN}"

"${OUT_BIN}"
