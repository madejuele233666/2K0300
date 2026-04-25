#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
OUT_BIN="${SCRIPT_DIR}/steering_alignment_test"

c++ -std=c++17 -Wall -Wextra -Werror -pthread \
  -I"${REPO_ROOT}/new/code" \
  -I"${REPO_ROOT}/new/code/port" \
  -I"${REPO_ROOT}/new/code/legacy" \
  "${REPO_ROOT}/new/verification/tests/steering_alignment_test.cpp" \
  "${REPO_ROOT}/new/code/legacy/camera_logic.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_bottom_tracker.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_gyro_continuity.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_scene_common.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_scene_orchestrator.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_scene_straight.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_scene_bend.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_scene_circle_entry.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_scene_circle_interior.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_scene_circle_exit.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_scene_zebra.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_scene_cross.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_scene_special_wide.cpp" \
  "${REPO_ROOT}/new/code/legacy/steering_scene_roadblock_stub.cpp" \
  -o "${OUT_BIN}"

"${OUT_BIN}"
