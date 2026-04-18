#!/bin/bash
set -euo pipefail

export PATH=/opt/ls_2k0300_env/loongson-gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc1.6/bin:$PATH

WORK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="$(cd "${WORK_DIR}/../out" && pwd)"
USER_DIR="${WORK_DIR}"
REPO_ROOT="$(cd "${WORK_DIR}/../.." && pwd)"
TRUE_VENDOR_ROOT="${REPO_ROOT}/true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library"
REMOTE_IP="${BOARD_IP:-10.236.192.226}"
REMOTE_USER="${BOARD_USER:-root}"
REMOTE_PATH="${BOARD_PATH:-/home/root/}"
MAKE_JOBS="${MAKE_JOBS:-$(nproc)}"
RESERVE_FILE="本文件夹作用.txt"

log_info() {
    echo "[INFO] $1"
}

log_error() {
    echo "[ERROR] $1" >&2
}

if [[ ! -d "${TRUE_VENDOR_ROOT}/project/user" ]]; then
    log_error "true vendor root missing: ${TRUE_VENDOR_ROOT}/project/user"
    exit 1
fi

log_info "building migrated new/ workspace from ${USER_DIR}"
log_info "true vendor root: ${TRUE_VENDOR_ROOT}"
cd "${OUT_DIR}"

find . -mindepth 1 ! -name "${RESERVE_FILE}" -exec rm -rf {} +
cmake "${USER_DIR}"
make -j"${MAKE_JOBS}"

artifact_name="$(basename "$(cd "${OUT_DIR}/.." && pwd)")"
artifact_path="${OUT_DIR}/${artifact_name}"

if [[ ! -f "${artifact_path}" ]]; then
    log_error "build succeeded but artifact missing: ${artifact_path}"
    exit 1
fi

if [[ "${SKIP_UPLOAD:-0}" == "1" ]]; then
    log_info "SKIP_UPLOAD=1, build completed without upload"
    exit 0
fi

log_info "uploading ${artifact_path} to ${REMOTE_USER}@${REMOTE_IP}:${REMOTE_PATH}"
if scp -O "${artifact_path}" "${REMOTE_USER}@${REMOTE_IP}:${REMOTE_PATH}"; then
    log_info "upload complete"
else
    log_error "upload failed"
    exit 1
fi
