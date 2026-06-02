#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUT_DIR="${REPO_ROOT}/new/out/tests"
OUT_BIN="${OUT_DIR}/param_store_load_runtime_parameters_test"

CXX_BIN="${CXX:-/opt/ls_2k0300_env/loongson-gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc1.6/bin/loongarch64-linux-gnu-g++}"
OPENCV_ROOT="${OPENCV_ROOT:-/opt/ls_2k0300_env/opencv_4_10_build}"
BOARD_IP="${BOARD_IP:-10.100.170.226}"
BOARD_USER="${BOARD_USER:-root}"
REMOTE_BIN="${REMOTE_BIN:-/home/root/param_store_load_runtime_parameters_test}"
REMOTE_OPENCV_LIB="${REMOTE_OPENCV_LIB:-/home/root/opencv_4_10_build/lib}"

mkdir -p "${OUT_DIR}"

"${CXX_BIN}" -std=c++17 -Wall -Wextra -Werror -pthread \
  -I"${REPO_ROOT}/new/code" \
  -I"${REPO_ROOT}/new/code/port" \
  -I"${REPO_ROOT}/new/code/platform" \
  -I"${OPENCV_ROOT}/include/opencv4" \
  "${REPO_ROOT}/new/verification/tests/param_store_load_runtime_parameters_test.cpp" \
  "${REPO_ROOT}/new/code/platform/param_store.cpp" \
  -L"${OPENCV_ROOT}/lib" \
  -lopencv_core \
  -o "${OUT_BIN}"

scp -O -o StrictHostKeyChecking=accept-new "${OUT_BIN}" "${BOARD_USER}@${BOARD_IP}:${REMOTE_BIN}"
ssh -o StrictHostKeyChecking=accept-new "${BOARD_USER}@${BOARD_IP}" \
  "chmod +x '${REMOTE_BIN}' && LD_LIBRARY_PATH='${REMOTE_OPENCV_LIB}':\$LD_LIBRARY_PATH '${REMOTE_BIN}'"
