#!/bin/bash

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  echo "common_steering_test_build.sh must be sourced by a test runner" >&2
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[1]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
CXX_BIN="${CXX:-c++}"

COMMON_CXXFLAGS=(
  -std=c++17
  -Wall
  -Wextra
  -Werror
  -pthread
)

COMMON_INCLUDES=(
  -I"${REPO_ROOT}/new/code"
  -I"${REPO_ROOT}/new/code/port"
  -I"${REPO_ROOT}/new/code/legacy"
  -I"${REPO_ROOT}/new/code/platform"
  -I"${REPO_ROOT}/new/code/platform/true_ls2k0300"
  -I"${REPO_ROOT}/new/code/runtime"
  -I"${REPO_ROOT}/new/user"
)

STEERING_MEDIA_SOURCES=(
  "${REPO_ROOT}/new/code/platform/steering_media_link.cpp"
  "${REPO_ROOT}/new/code/platform/steering_media_protocol.cpp"
  "${REPO_ROOT}/new/code/platform/true_ls2k0300/steering_media_bridge.cpp"
  "${REPO_ROOT}/new/code/port/perf_counter.cpp"
  "${REPO_ROOT}/new/code/runtime/control_debug_reporter.cpp"
  "${REPO_ROOT}/new/code/runtime/control_decision.cpp"
  "${REPO_ROOT}/new/code/runtime/motion_supervisor.cpp"
  "${REPO_ROOT}/new/code/runtime/steering_media_service.cpp"
)

compile_test_binary() {
  local out_bin="$1"
  shift
  "${CXX_BIN}" "${COMMON_CXXFLAGS[@]}" "${COMMON_INCLUDES[@]}" "$@" -o "${out_bin}"
}
