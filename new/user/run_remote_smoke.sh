#!/bin/bash
set -euo pipefail

WORK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${WORK_DIR}/../.." && pwd)"
TRUE_VENDOR_ROOT="${REPO_ROOT}/true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library"
VERIFY_LOG="${VERIFY_LOG_PATH:-${REPO_ROOT}/openspec/changes/fix-board-runtime-hardware-detection/verification/runtime-smoke.log}"

BOARD_IP="${BOARD_IP:-10.236.192.226}"
BOARD_USER="${BOARD_USER:-root}"
BOARD_PATH="${BOARD_PATH:-/home/root}"
BOARD_BIN="${BOARD_BIN:-${BOARD_PATH}/new}"
REMOTE_LOG="${BOARD_PATH}/new_runtime_smoke.log"
REMOTE_PARAMS="${BOARD_PATH}/default_params.smoke.json"
REMOTE_PROFILE="${BOARD_PATH}/hardware_profile.smoke.json"
LOCAL_BIN="${WORK_DIR}/../out/new"
PARAMS_SOURCE="${LS2K_PARAMS_PATH:-${WORK_DIR}/../config/default_params.json}"
PROFILE_SOURCE="${LS2K_PROFILE_PATH:-${WORK_DIR}/../config/hardware_profile.json}"
SMOKE_MAX_FRAMES="${SMOKE_MAX_FRAMES:-400}"
SMOKE_ENABLE_MOTOR="${SMOKE_ENABLE_MOTOR:-0}"

mkdir -p "$(dirname "${VERIFY_LOG}")"

write_header() {
    {
        echo "== runtime smoke $(date -u +"%Y-%m-%dT%H:%M:%SZ") =="
        echo "board=${BOARD_USER}@${BOARD_IP}"
        echo "binary=${BOARD_BIN}"
        echo "params_source=${PARAMS_SOURCE}"
        echo "profile_source=${PROFILE_SOURCE}"
        echo "smoke_enable_motor=${SMOKE_ENABLE_MOTOR}"
        echo "true_vendor_root=${TRUE_VENDOR_ROOT}"
    } >"${VERIFY_LOG}"
}

prepare_smoke_profile() {
    local source_path="$1"
    local target_path="$2"

    if [[ "${SMOKE_ENABLE_MOTOR}" == "1" ]]; then
        cp "${source_path}" "${target_path}"
        return 0
    fi

    sed '/"motor": {/,/},/s/"mode": "[^"]*"/"mode": "disabled"/; /"motor": {/,/},/s/"hook": "[^"]*"/"hook": "smoke-motor-disabled"/' \
        "${source_path}" > "${target_path}"
}

smoke_runtime_env() {
    local params_path="$1"
    local profile_path="$2"
    if [[ "${SMOKE_ENABLE_MOTOR}" == "1" ]]; then
        printf 'LS2K_PARAMS_PATH=%s LS2K_PROFILE_PATH=%s LS2K_MAX_FRAMES=%s' \
            "${params_path}" "${profile_path}" "${SMOKE_MAX_FRAMES}"
        return 0
    fi

    printf 'LS2K_ALLOW_DEGRADED_STARTUP=1 LS2K_PARAMS_PATH=%s LS2K_PROFILE_PATH=%s LS2K_MAX_FRAMES=%s' \
        "${params_path}" "${profile_path}" "${SMOKE_MAX_FRAMES}"
}

run_remote() {
    local tmp_log
    local tmp_profile
    local remote_status
    local copy_status
    local runtime_invoked
    local runtime_env
    tmp_log="$(mktemp)"
    tmp_profile="$(mktemp)"
    trap 'rm -f "${tmp_log}" "${tmp_profile}"' RETURN
    copy_status=0
    remote_status=0
    runtime_invoked=0
    runtime_env="$(smoke_runtime_env "${REMOTE_PARAMS}" "${REMOTE_PROFILE}")"

    if ! prepare_smoke_profile "${PROFILE_SOURCE}" "${tmp_profile}"; then
        {
            echo "profile_prepare_failed=${PROFILE_SOURCE}"
        } >> "${VERIFY_LOG}"
        return 1
    fi

    if scp -O "${PARAMS_SOURCE}" "${BOARD_USER}@${BOARD_IP}:${REMOTE_PARAMS}"; then
        if scp -O "${tmp_profile}" "${BOARD_USER}@${BOARD_IP}:${REMOTE_PROFILE}"; then
            runtime_invoked=1
            if ssh "${BOARD_USER}@${BOARD_IP}" \
                "chmod +x ${BOARD_BIN}; ${runtime_env} timeout 12s ${BOARD_BIN} > ${REMOTE_LOG} 2>&1"; then
                remote_status=0
            else
                remote_status=$?
            fi

            if scp -O "${BOARD_USER}@${BOARD_IP}:${REMOTE_LOG}" "${tmp_log}"; then
                copy_status=0
            else
                copy_status=$?
                {
                    echo "remote_log_copy_exit=${copy_status}"
                    echo "remote_log_copy_failed=${REMOTE_LOG}"
                } >> "${VERIFY_LOG}"
            fi

            if [[ "${copy_status}" -eq 0 ]]; then
                cat "${tmp_log}" >> "${VERIFY_LOG}"
            fi
        else
            remote_status=$?
            copy_status=255
            {
                echo "remote_upload_exit=${remote_status}"
                echo "remote_upload_failed=${REMOTE_PROFILE}"
                echo "remote_log_copy_exit=${copy_status}"
                echo "remote_log_copy_skipped=runtime_not_invoked"
            } >> "${VERIFY_LOG}"
        fi
    else
        remote_status=$?
        copy_status=255
        {
            echo "remote_upload_exit=${remote_status}"
            echo "remote_upload_failed=${REMOTE_PARAMS}"
            echo "remote_log_copy_exit=${copy_status}"
            echo "remote_log_copy_skipped=runtime_not_invoked"
        } >> "${VERIFY_LOG}"
    fi

    {
        echo "remote_runtime_exit=${remote_status}"
        echo "remote_log_copy_exit=${copy_status}"
        echo "remote_runtime_invoked=${runtime_invoked}"
    } >> "${VERIFY_LOG}"

    if [[ "${remote_status}" -ne 0 ]]; then
        return "${remote_status}"
    fi
    return 0
}

run_local() {
    local tmp_profile
    local runtime_env
    tmp_profile="$(mktemp)"
    trap 'rm -f "${tmp_profile}"' RETURN

    {
        echo "remote smoke unavailable, executed local fallback"
        echo "local_binary=${LOCAL_BIN}"
    } >>"${VERIFY_LOG}"

    local host_arch
    host_arch="$(uname -m || echo unknown)"
    local file_desc
    file_desc="$(file -b "${LOCAL_BIN}" 2>/dev/null || true)"
    if [[ "${file_desc}" == *"LoongArch"* && "${host_arch}" != "loongarch64" ]]; then
        {
            echo "local execution skipped: binary architecture incompatible with host"
            echo "host_arch=${host_arch}"
            echo "binary_desc=${file_desc}"
        } >>"${VERIFY_LOG}"
        return 0
    fi

    if ! prepare_smoke_profile "${PROFILE_SOURCE}" "${tmp_profile}"; then
        {
            echo "profile_prepare_failed=${PROFILE_SOURCE}"
        } >> "${VERIFY_LOG}"
        return 1
    fi

    runtime_env="$(smoke_runtime_env "${PARAMS_SOURCE}" "${tmp_profile}")"
    eval "${runtime_env} timeout 12s \"${LOCAL_BIN}\" >> \"${VERIFY_LOG}\" 2>&1" || true
}

write_header

if [[ "${SMOKE_LOCAL_ONLY:-0}" == "1" ]]; then
    run_local
    exit 0
fi

run_remote
