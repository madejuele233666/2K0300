#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/new/out-runtime-params-test"
OUT_BIN="${BUILD_DIR}/runtime_params_load_test"

cmake -S "${REPO_ROOT}/new/user" -B "${BUILD_DIR}" -DLS2K_BUILD_VERIFICATION_HELPERS=ON
cmake --build "${BUILD_DIR}" --target runtime_params_load_test -j2

if command -v qemu-loongarch64 >/dev/null 2>&1; then
  qemu-loongarch64 -L / "${OUT_BIN}"
  exit 0
fi

echo "built ${OUT_BIN}, but did not run it: qemu-loongarch64 is unavailable on this host" >&2
