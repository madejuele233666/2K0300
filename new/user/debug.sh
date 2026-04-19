#!/bin/bash
set -euo pipefail

export PATH=/opt/ls_2k0300_env/loongson-gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc1.6/bin:$PATH

WORK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
USER_DIR="${WORK_DIR}"
OUT_DIR="$(cd "${WORK_DIR}/../out" && pwd)"
REPO_ROOT="$(cd "${WORK_DIR}/../.." && pwd)"
TRUE_VENDOR_ROOT="${REPO_ROOT}/true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library"

BOARD_IP="${BOARD_IP:-10.100.170.226}"
BOARD_USER="${BOARD_USER:-root}"
BOARD_PATH="${BOARD_PATH:-/home/root}"
BOARD_BIN="${BOARD_BIN:-${BOARD_PATH}/new}"
LOCAL_BIN="${WORK_DIR}/../out/new"

PARAMS_PATH="${LS2K_PARAMS_PATH:-${REPO_ROOT}/new/config/default_params.json}"
PROFILE_PATH="${LS2K_PROFILE_PATH:-${REPO_ROOT}/new/config/hardware_profile.json}"
REMOTE_LOG="${LS2K_REMOTE_LOG:-${BOARD_PATH}/new_runtime.log}"
REMOTE_PIDFILE="${LS2K_REMOTE_PIDFILE:-${BOARD_PATH}/new_runtime.pid}"
REMOTE_PARAMS="${LS2K_REMOTE_PARAMS_PATH:-${BOARD_PATH}/default_params.json}"
REMOTE_PROFILE_NORMAL="${LS2K_REMOTE_PROFILE_PATH:-${BOARD_PATH}/hardware_profile.json}"
REMOTE_PROFILE_SMOKE="${LS2K_REMOTE_SMOKE_PROFILE_PATH:-${BOARD_PATH}/hardware_profile.smoke.json}"

MAKE_JOBS="${MAKE_JOBS:-$(nproc)}"
RESERVE_FILE="本文件夹作用.txt"
TAIL_LINES="${TAIL_LINES:-80}"
ASSISTANT_PORT="${ASSISTANT_PORT:-8888}"
VERIFY_LOG="${VERIFY_LOG_PATH:-${REPO_ROOT}/new/verification/runtime-smoke.log}"
SMOKE_REMOTE_LOG="${LS2K_REMOTE_SMOKE_LOG:-${BOARD_PATH}/new_runtime_smoke.log}"
SMOKE_REMOTE_PARAMS="${LS2K_REMOTE_SMOKE_PARAMS_PATH:-${BOARD_PATH}/default_params.smoke.json}"
SMOKE_REMOTE_PROFILE="${LS2K_REMOTE_SMOKE_PROFILE_PATH:-${BOARD_PATH}/hardware_profile.smoke.json}"
SMOKE_MAX_FRAMES="${SMOKE_MAX_FRAMES:-400}"
SMOKE_ENABLE_MOTOR="${SMOKE_ENABLE_MOTOR:-0}"
SMOKE_AUTO_START="${SMOKE_AUTO_START:-1}"
SMOKE_AUTO_START_DELAY_MS="${SMOKE_AUTO_START_DELAY_MS:-200}"
SMOKE_AUTO_STOP_AFTER_MS="${SMOKE_AUTO_STOP_AFTER_MS:-0}"
SMOKE_AUTO_RESET_FAULT="${SMOKE_AUTO_RESET_FAULT:-0}"
SMOKE_FAULT_INJECT_DROP_FRAME_EVERY_N="${SMOKE_FAULT_INJECT_DROP_FRAME_EVERY_N:-${LS2K_FAULT_INJECT_DROP_FRAME_EVERY_N:-0}}"
SMOKE_FAULT_INJECT_IMU_INVALID_EVERY_N="${SMOKE_FAULT_INJECT_IMU_INVALID_EVERY_N:-${LS2K_FAULT_INJECT_IMU_INVALID_EVERY_N:-0}}"
SMOKE_FAULT_INJECT_ENCODER_INVALID_EVERY_N="${SMOKE_FAULT_INJECT_ENCODER_INVALID_EVERY_N:-${LS2K_FAULT_INJECT_ENCODER_INVALID_EVERY_N:-0}}"
SMOKE_FORCE_LOW_VOLTAGE="${SMOKE_FORCE_LOW_VOLTAGE:-${LS2K_FORCE_LOW_VOLTAGE:-}}"

log_info() {
    echo "[INFO] $1"
}

log_error() {
    echo "[ERROR] $1" >&2
}

usage() {
    cat <<'EOF'
Usage:
  ./debug.sh build
  ./debug.sh assistant status
  ./debug.sh assistant on [host] [port]
  ./debug.sh assistant local [port]
  ./debug.sh assistant off
  ./debug.sh remote start [normal|smoke]
  ./debug.sh remote stop
  ./debug.sh remote restart [normal|smoke]
  ./debug.sh remote status
  ./debug.sh remote logs
  ./debug.sh smoke run
  ./debug.sh smoke local
  ./debug.sh smoke help

Command groups:
  build      build new/ and upload binary + params + profile to the board
  assistant  switch assistant TCP publish settings in default_params.json
  remote     start/stop/check the runtime process on the board
  smoke      execute the runtime smoke harness and collect a verification log

Common env overrides:
  BOARD_IP=10.100.170.226
  BOARD_USER=root
  BOARD_PATH=/home/root
  LS2K_PARAMS_PATH=/abs/path/to/default_params.json
  LS2K_PROFILE_PATH=/abs/path/to/hardware_profile.json
EOF
}

run_ssh() {
    ssh "${BOARD_USER}@${BOARD_IP}" "$@"
}

require_local_file() {
    local path="$1"
    local label="$2"
    if [[ ! -f "${path}" ]]; then
        log_error "${label} missing: ${path}"
        exit 1
    fi
}

require_remote_file() {
    local path="$1"
    local label="$2"
    if ! run_ssh "[ -f '${path}' ]"; then
        log_error "${label} missing on board: ${path}"
        exit 1
    fi
}

detect_local_ip() {
    if command -v ip >/dev/null 2>&1; then
        local detected
        detected="$(ip route get "${BOARD_IP}" 2>/dev/null | awk '/ src / { for (i = 1; i <= NF; ++i) if ($i == "src") { print $(i + 1); exit } }')"
        if [[ -n "${detected}" ]]; then
            printf '%s\n' "${detected}"
            return 0
        fi
    fi

    if command -v hostname >/dev/null 2>&1; then
        local detected
        detected="$(hostname -I 2>/dev/null | awk '{ print $1 }')"
        if [[ -n "${detected}" ]]; then
            printf '%s\n' "${detected}"
            return 0
        fi
    fi

    return 1
}

assistant_read_field() {
    local field_name="$1"
    python3 - "${PARAMS_PATH}" "${field_name}" <<'PY'
import json
import sys

path, field_name = sys.argv[1:3]
with open(path, "r", encoding="utf-8") as f:
    data = json.load(f)

assistant_tcp = data.get("assistant_tcp", {})
if field_name == "host":
    print(assistant_tcp.get("host", ""))
elif field_name == "port":
    print(int(assistant_tcp.get("port", 8888)))
elif field_name == "enabled":
    print(1 if data.get("assistant_enabled") else 0)
else:
    raise SystemExit(f"unknown field: {field_name}")
PY
}

assistant_print_status() {
    python3 - "${PARAMS_PATH}" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as f:
    data = json.load(f)

assistant_tcp = data.get("assistant_tcp", {})
summary = {
    "params_path": sys.argv[1],
    "assistant_enabled": 1 if data.get("assistant_enabled") else 0,
    "assistant_host": assistant_tcp.get("host", ""),
    "assistant_port": int(assistant_tcp.get("port", 8888)),
    "waveform_interval_ms": data.get("assistant_waveform_publish_interval_ms"),
    "image_interval_ms": data.get("assistant_image_publish_interval_ms"),
}
for key, value in summary.items():
    print(f"{key}={value}")
PY
}

assistant_update_params() {
    local enabled="$1"
    local host="$2"
    local port="$3"

    python3 - "${PARAMS_PATH}" "${enabled}" "${host}" "${port}" <<'PY'
import json
import sys

path, enabled_raw, host, port_raw = sys.argv[1:5]
enabled = enabled_raw == "1"
port = int(port_raw)

with open(path, "r", encoding="utf-8") as f:
    data = json.load(f)

assistant_tcp = data.setdefault("assistant_tcp", {})
data["assistant_enabled"] = 1 if enabled else 0
if host:
    assistant_tcp["host"] = host
assistant_tcp["port"] = port

with open(path, "w", encoding="utf-8") as f:
    json.dump(data, f, indent=2, ensure_ascii=False)
    f.write("\n")

summary = {
    "params_path": path,
    "assistant_enabled": data["assistant_enabled"],
    "assistant_host": assistant_tcp.get("host", ""),
    "assistant_port": assistant_tcp.get("port", 0),
    "waveform_interval_ms": data.get("assistant_waveform_publish_interval_ms"),
    "image_interval_ms": data.get("assistant_image_publish_interval_ms"),
}
for key, value in summary.items():
    print(f"{key}={value}")
PY
}

assistant_usage() {
    cat <<'EOF'
Usage:
  ./debug.sh assistant status
  ./debug.sh assistant on [host] [port]
  ./debug.sh assistant local [port]
  ./debug.sh assistant off
EOF
}

assistant_command() {
    require_local_file "${PARAMS_PATH}" "params file"

    local mode="${1:-status}"
    case "${mode}" in
        status)
            assistant_print_status
            ;;
        on)
            local host="${2:-${ASSISTANT_HOST:-}}"
            local port="${3:-${ASSISTANT_PORT}}"
            if [[ -z "${host}" ]]; then
                host="$(detect_local_ip || true)"
            fi
            if [[ -z "${host}" ]]; then
                host="$(assistant_read_field host)"
            fi
            if [[ -z "${host}" ]]; then
                log_error "unable to resolve assistant host; pass it explicitly"
                exit 1
            fi
            assistant_update_params "1" "${host}" "${port}"
            ;;
        local)
            local port="${2:-${ASSISTANT_PORT}}"
            local host
            host="$(detect_local_ip || true)"
            if [[ -z "${host}" ]]; then
                log_error "unable to detect local IPv4 for BOARD_IP=${BOARD_IP}"
                exit 1
            fi
            assistant_update_params "1" "${host}" "${port}"
            ;;
        off)
            assistant_update_params "0" "$(assistant_read_field host)" "$(assistant_read_field port)"
            ;;
        -h|--help|help)
            assistant_usage
            ;;
        *)
            assistant_usage >&2
            exit 1
            ;;
    esac
}

build_command() {
    if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || "${1:-}" == "help" ]]; then
        cat <<'EOF'
Usage:
  ./debug.sh build

Behavior:
  - build new/ under ../out
  - upload binary + default_params.json + hardware_profile.json to the board
EOF
        return 0
    fi

    if [[ ! -d "${TRUE_VENDOR_ROOT}/project/user" ]]; then
        log_error "true vendor root missing: ${TRUE_VENDOR_ROOT}/project/user"
        exit 1
    fi

    require_local_file "${PARAMS_PATH}" "default params"
    require_local_file "${PROFILE_PATH}" "hardware profile"

    log_info "building migrated new/ workspace from ${USER_DIR}"
    log_info "true vendor root: ${TRUE_VENDOR_ROOT}"
    cd "${OUT_DIR}"

    find . -mindepth 1 ! -name "${RESERVE_FILE}" -exec rm -rf {} +
    cmake "${USER_DIR}"
    make -j"${MAKE_JOBS}"

    local artifact_name
    artifact_name="$(basename "$(cd "${OUT_DIR}/.." && pwd)")"
    local artifact_path="${OUT_DIR}/${artifact_name}"

    if [[ ! -f "${artifact_path}" ]]; then
        log_error "build succeeded but artifact missing: ${artifact_path}"
        exit 1
    fi

    if [[ "${SKIP_UPLOAD:-0}" == "1" ]]; then
        log_info "SKIP_UPLOAD=1, build completed without upload"
        return 0
    fi

    log_info "uploading runtime assets to ${BOARD_USER}@${BOARD_IP}:${BOARD_PATH}"
    scp -O \
        "${artifact_path}" \
        "${PARAMS_PATH}" \
        "${PROFILE_PATH}" \
        "${BOARD_USER}@${BOARD_IP}:${BOARD_PATH}/"
    log_info "upload complete"
}

remote_usage() {
    cat <<'EOF'
Usage:
  ./debug.sh remote start [normal|smoke]
  ./debug.sh remote stop
  ./debug.sh remote restart [normal|smoke]
  ./debug.sh remote status
  ./debug.sh remote logs
EOF
}

remote_start() {
    local mode="${1:-normal}"
    local remote_profile=""
    local degraded="${LS2K_ALLOW_DEGRADED_STARTUP:-}"

    case "${mode}" in
        normal)
            remote_profile="${REMOTE_PROFILE_NORMAL}"
            ;;
        smoke)
            remote_profile="${REMOTE_PROFILE_SMOKE}"
            if [[ -z "${degraded}" ]]; then
                degraded="1"
            fi
            ;;
        *)
            log_error "unknown remote mode: ${mode}"
            remote_usage >&2
            exit 1
            ;;
    esac

    require_remote_file "${BOARD_BIN}" "runtime binary"
    require_remote_file "${REMOTE_PARAMS}" "params file"
    require_remote_file "${remote_profile}" "hardware profile"

    run_ssh \
        "REMOTE_BIN='${BOARD_BIN}' \
REMOTE_LOG='${REMOTE_LOG}' \
REMOTE_PIDFILE='${REMOTE_PIDFILE}' \
REMOTE_PARAMS='${REMOTE_PARAMS}' \
REMOTE_PROFILE='${remote_profile}' \
LS2K_AUTO_START_VALUE='${LS2K_AUTO_START:-}' \
LS2K_AUTO_START_DELAY_MS_VALUE='${LS2K_AUTO_START_DELAY_MS:-}' \
LS2K_AUTO_STOP_AFTER_MS_VALUE='${LS2K_AUTO_STOP_AFTER_MS:-}' \
LS2K_AUTO_RESET_FAULT_VALUE='${LS2K_AUTO_RESET_FAULT:-}' \
LS2K_EMIT_FRAME_PROGRESS_VALUE='${LS2K_EMIT_FRAME_PROGRESS:-}' \
LS2K_ALLOW_DEGRADED_STARTUP_VALUE='${degraded}' \
bash -s" <<'EOF'
set -euo pipefail

if [[ -f "${REMOTE_PIDFILE}" ]]; then
    old_pid="$(cat "${REMOTE_PIDFILE}" 2>/dev/null || true)"
    if [[ -n "${old_pid}" ]] && kill -0 "${old_pid}" 2>/dev/null; then
        kill "${old_pid}" 2>/dev/null || true
        sleep 1
    fi
    rm -f "${REMOTE_PIDFILE}"
fi

pkill -f "^${REMOTE_BIN}$" 2>/dev/null || true
sleep 1

export LS2K_PARAMS_PATH="${REMOTE_PARAMS}"
export LS2K_PROFILE_PATH="${REMOTE_PROFILE}"

if [[ -n "${LS2K_AUTO_START_VALUE}" ]]; then
    export LS2K_AUTO_START="${LS2K_AUTO_START_VALUE}"
fi
if [[ -n "${LS2K_AUTO_START_DELAY_MS_VALUE}" ]]; then
    export LS2K_AUTO_START_DELAY_MS="${LS2K_AUTO_START_DELAY_MS_VALUE}"
fi
if [[ -n "${LS2K_AUTO_STOP_AFTER_MS_VALUE}" ]]; then
    export LS2K_AUTO_STOP_AFTER_MS="${LS2K_AUTO_STOP_AFTER_MS_VALUE}"
fi
if [[ -n "${LS2K_AUTO_RESET_FAULT_VALUE}" ]]; then
    export LS2K_AUTO_RESET_FAULT="${LS2K_AUTO_RESET_FAULT_VALUE}"
fi
if [[ -n "${LS2K_EMIT_FRAME_PROGRESS_VALUE}" ]]; then
    export LS2K_EMIT_FRAME_PROGRESS="${LS2K_EMIT_FRAME_PROGRESS_VALUE}"
fi
if [[ -n "${LS2K_ALLOW_DEGRADED_STARTUP_VALUE}" ]]; then
    export LS2K_ALLOW_DEGRADED_STARTUP="${LS2K_ALLOW_DEGRADED_STARTUP_VALUE}"
fi

nohup "${REMOTE_BIN}" > "${REMOTE_LOG}" 2>&1 < /dev/null &
runtime_pid=$!
echo "${runtime_pid}" > "${REMOTE_PIDFILE}"
sleep 1

if kill -0 "${runtime_pid}" 2>/dev/null; then
    echo "runtime_started pid=${runtime_pid}"
    echo "remote_log=${REMOTE_LOG}"
    echo "params=${REMOTE_PARAMS}"
    echo "profile=${REMOTE_PROFILE}"
else
    echo "runtime failed to stay alive" >&2
    tail -n 80 "${REMOTE_LOG}" 2>/dev/null || true
    exit 1
fi
EOF
}

remote_stop() {
    run_ssh \
        "REMOTE_BIN='${BOARD_BIN}' REMOTE_PIDFILE='${REMOTE_PIDFILE}' bash -s" <<'EOF'
set -euo pipefail

stopped=0
if [[ -f "${REMOTE_PIDFILE}" ]]; then
    runtime_pid="$(cat "${REMOTE_PIDFILE}" 2>/dev/null || true)"
    if [[ -n "${runtime_pid}" ]] && kill -0 "${runtime_pid}" 2>/dev/null; then
        kill "${runtime_pid}" 2>/dev/null || true
        sleep 1
        stopped=1
    fi
    rm -f "${REMOTE_PIDFILE}"
fi

if pkill -f "^${REMOTE_BIN}$" 2>/dev/null; then
    stopped=1
fi

if [[ "${stopped}" -eq 1 ]]; then
    echo "runtime_stopped"
else
    echo "runtime_not_running"
fi
EOF
}

remote_status() {
    run_ssh \
        "REMOTE_BIN='${BOARD_BIN}' REMOTE_PIDFILE='${REMOTE_PIDFILE}' REMOTE_LOG='${REMOTE_LOG}' bash -s" <<'EOF'
set -euo pipefail

if [[ -f "${REMOTE_PIDFILE}" ]]; then
    runtime_pid="$(cat "${REMOTE_PIDFILE}" 2>/dev/null || true)"
    if [[ -n "${runtime_pid}" ]] && kill -0 "${runtime_pid}" 2>/dev/null; then
        echo "runtime_status=running"
        echo "pid=${runtime_pid}"
        echo "log=${REMOTE_LOG}"
        exit 0
    fi
fi

if pgrep -f "^${REMOTE_BIN}$" >/dev/null 2>&1; then
    echo "runtime_status=running_without_pidfile"
    echo "log=${REMOTE_LOG}"
else
    echo "runtime_status=stopped"
    echo "log=${REMOTE_LOG}"
fi
EOF
}

remote_logs() {
    run_ssh "tail -n ${TAIL_LINES} -f '${REMOTE_LOG}'"
}

remote_command() {
    local action="${1:-status}"
    local mode="${2:-normal}"

    case "${action}" in
        start)
            remote_start "${mode}"
            ;;
        stop)
            remote_stop
            ;;
        restart)
            remote_stop
            remote_start "${mode}"
            ;;
        status)
            remote_status
            ;;
        logs)
            remote_logs
            ;;
        -h|--help|help)
            remote_usage
            ;;
        *)
            remote_usage >&2
            exit 1
            ;;
    esac
}

smoke_usage() {
    cat <<'EOF'
Usage:
  ./debug.sh smoke run
  ./debug.sh smoke local

Behavior:
  - `run` uploads smoke params/profile to the board, runs the harness remotely, and copies the smoke log back
  - `local` runs the same harness locally against ../out/new when the host architecture is compatible

Common env overrides:
  VERIFY_LOG_PATH=/abs/path/to/runtime-smoke.log
  SMOKE_ENABLE_MOTOR=0|1
  SMOKE_MAX_FRAMES=400
  SMOKE_AUTO_START=1
  SMOKE_AUTO_START_DELAY_MS=200
  SMOKE_AUTO_STOP_AFTER_MS=0
  SMOKE_AUTO_RESET_FAULT=0
  LS2K_FAULT_INJECT_DROP_FRAME_EVERY_N=0
  LS2K_FAULT_INJECT_IMU_INVALID_EVERY_N=0
  LS2K_FAULT_INJECT_ENCODER_INVALID_EVERY_N=0
  LS2K_FORCE_LOW_VOLTAGE=true|false
EOF
}

smoke_write_header() {
    mkdir -p "$(dirname "${VERIFY_LOG}")"
    {
        echo "== runtime smoke $(date -u +"%Y-%m-%dT%H:%M:%SZ") =="
        echo "board=${BOARD_USER}@${BOARD_IP}"
        echo "binary=${BOARD_BIN}"
        echo "params_source=${PARAMS_PATH}"
        echo "profile_source=${PROFILE_PATH}"
        echo "smoke_enable_motor=${SMOKE_ENABLE_MOTOR}"
        echo "true_vendor_root=${TRUE_VENDOR_ROOT}"
        echo "harness_context_begin"
        echo "LS2K_AUTO_START=${SMOKE_AUTO_START}"
        echo "LS2K_AUTO_START_DELAY_MS=${SMOKE_AUTO_START_DELAY_MS}"
        echo "LS2K_AUTO_STOP_AFTER_MS=${SMOKE_AUTO_STOP_AFTER_MS}"
        echo "LS2K_AUTO_RESET_FAULT=${SMOKE_AUTO_RESET_FAULT}"
        echo "SMOKE_MAX_FRAMES=${SMOKE_MAX_FRAMES}"
        echo "LS2K_FAULT_INJECT_DROP_FRAME_EVERY_N=${SMOKE_FAULT_INJECT_DROP_FRAME_EVERY_N}"
        echo "LS2K_FAULT_INJECT_IMU_INVALID_EVERY_N=${SMOKE_FAULT_INJECT_IMU_INVALID_EVERY_N}"
        echo "LS2K_FAULT_INJECT_ENCODER_INVALID_EVERY_N=${SMOKE_FAULT_INJECT_ENCODER_INVALID_EVERY_N}"
        echo "LS2K_FORCE_LOW_VOLTAGE=${SMOKE_FORCE_LOW_VOLTAGE:-unset}"
        echo "harness_context_end"
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
    local prefix=""

    if [[ "${SMOKE_ENABLE_MOTOR}" != "1" ]]; then
        prefix="LS2K_ALLOW_DEGRADED_STARTUP=1 "
    fi

    printf '%sLS2K_PARAMS_PATH=%s LS2K_PROFILE_PATH=%s LS2K_AUTO_START=%s LS2K_AUTO_START_DELAY_MS=%s LS2K_AUTO_STOP_AFTER_MS=%s LS2K_AUTO_RESET_FAULT=%s' \
        "${prefix}" "${params_path}" "${profile_path}" \
        "${SMOKE_AUTO_START}" "${SMOKE_AUTO_START_DELAY_MS}" "${SMOKE_AUTO_STOP_AFTER_MS}" "${SMOKE_AUTO_RESET_FAULT}"

    if [[ "${SMOKE_MAX_FRAMES}" != "0" ]]; then
        printf ' LS2K_EMIT_FRAME_PROGRESS=1'
    fi
    if [[ "${SMOKE_FAULT_INJECT_DROP_FRAME_EVERY_N}" != "0" ]]; then
        printf ' LS2K_FAULT_INJECT_DROP_FRAME_EVERY_N=%s' "${SMOKE_FAULT_INJECT_DROP_FRAME_EVERY_N}"
    fi
    if [[ "${SMOKE_FAULT_INJECT_IMU_INVALID_EVERY_N}" != "0" ]]; then
        printf ' LS2K_FAULT_INJECT_IMU_INVALID_EVERY_N=%s' "${SMOKE_FAULT_INJECT_IMU_INVALID_EVERY_N}"
    fi
    if [[ "${SMOKE_FAULT_INJECT_ENCODER_INVALID_EVERY_N}" != "0" ]]; then
        printf ' LS2K_FAULT_INJECT_ENCODER_INVALID_EVERY_N=%s' "${SMOKE_FAULT_INJECT_ENCODER_INVALID_EVERY_N}"
    fi
    if [[ -n "${SMOKE_FORCE_LOW_VOLTAGE}" ]]; then
        printf ' LS2K_FORCE_LOW_VOLTAGE=%s' "${SMOKE_FORCE_LOW_VOLTAGE}"
    fi
}

frame_progress_count() {
    local log_path="$1"
    grep -c '^\[INFO\]\[main\.frame\.processed\]' "${log_path}" 2>/dev/null || true
}

monitor_local_runtime() {
    local runtime_pid="$1"
    local log_path="$2"
    local start_ts
    local stop_sent=0
    local timed_out=0
    local force_exit_sent=0

    start_ts="$(date +%s)"
    while kill -0 "${runtime_pid}" 2>/dev/null; do
        local now_ts
        now_ts="$(date +%s)"
        if [[ "${SMOKE_MAX_FRAMES}" -gt 0 && "${stop_sent}" -eq 0 ]]; then
            local frame_count
            frame_count="$(frame_progress_count "${log_path}")"
            if [[ "${frame_count}" -ge "${SMOKE_MAX_FRAMES}" ]]; then
                kill -INT "${runtime_pid}" 2>/dev/null || true
                stop_sent=1
            fi
        fi
        if (( now_ts - start_ts >= 12 )); then
            kill -TERM "${runtime_pid}" 2>/dev/null || true
            timed_out=1
            force_exit_sent=1
            start_ts=$(( now_ts + 2 ))
        fi
        if [[ "${force_exit_sent}" -eq 1 ]] && (( now_ts >= start_ts )); then
            kill -KILL "${runtime_pid}" 2>/dev/null || true
            break
        fi
        sleep 0.05
    done

    set +e
    wait "${runtime_pid}"
    local runtime_status=$?
    set -e
    if [[ "${timed_out}" -eq 1 && "${runtime_status}" -eq 143 ]]; then
        return 124
    fi
    return "${runtime_status}"
}

smoke_run_remote() {
    local tmp_log
    local tmp_profile
    local remote_status=0
    local copy_status=0
    local runtime_invoked=0
    local runtime_env

    tmp_log="$(mktemp)"
    tmp_profile="$(mktemp)"
    trap "rm -f '${tmp_log}' '${tmp_profile}'" RETURN
    runtime_env="$(smoke_runtime_env "${SMOKE_REMOTE_PARAMS}" "${SMOKE_REMOTE_PROFILE}")"

    if ! prepare_smoke_profile "${PROFILE_PATH}" "${tmp_profile}"; then
        echo "profile_prepare_failed=${PROFILE_PATH}" >> "${VERIFY_LOG}"
        return 1
    fi

    if scp -O "${PARAMS_PATH}" "${BOARD_USER}@${BOARD_IP}:${SMOKE_REMOTE_PARAMS}"; then
        if scp -O "${tmp_profile}" "${BOARD_USER}@${BOARD_IP}:${SMOKE_REMOTE_PROFILE}"; then
            runtime_invoked=1
            if ssh "${BOARD_USER}@${BOARD_IP}" \
                "BOARD_BIN='${BOARD_BIN}' REMOTE_LOG='${SMOKE_REMOTE_LOG}' RUNTIME_ENV='${runtime_env}' SMOKE_MAX_FRAMES='${SMOKE_MAX_FRAMES}' bash -s" <<'EOF'
set -euo pipefail
chmod +x "${BOARD_BIN}"
rm -f "${REMOTE_LOG}"
bash -lc "${RUNTIME_ENV} \"${BOARD_BIN}\"" > "${REMOTE_LOG}" 2>&1 &
runtime_pid=$!
start_ts="$(date +%s)"
stop_sent=0
timed_out=0
force_exit_sent=0

while kill -0 "${runtime_pid}" 2>/dev/null; do
    now_ts="$(date +%s)"
    if [[ "${SMOKE_MAX_FRAMES}" -gt 0 && "${stop_sent}" -eq 0 ]]; then
        frame_count="$(grep -c '^\[INFO\]\[main\.frame\.processed\]' "${REMOTE_LOG}" 2>/dev/null || true)"
        if [[ "${frame_count}" -ge "${SMOKE_MAX_FRAMES}" ]]; then
            kill -INT "${runtime_pid}" 2>/dev/null || true
            stop_sent=1
        fi
    fi
    if (( now_ts - start_ts >= 12 )); then
        kill -TERM "${runtime_pid}" 2>/dev/null || true
        timed_out=1
        force_exit_sent=1
        start_ts=$(( now_ts + 2 ))
    fi
    if [[ "${force_exit_sent}" -eq 1 ]] && (( now_ts >= start_ts )); then
        kill -KILL "${runtime_pid}" 2>/dev/null || true
        break
    fi
    sleep 0.05
done

set +e
wait "${runtime_pid}"
runtime_status=$?
set -e
if [[ "${timed_out}" -eq 1 && "${runtime_status}" -eq 143 ]]; then
    exit 124
fi
exit "${runtime_status}"
EOF
            then
                remote_status=0
            else
                remote_status=$?
            fi

            if scp -O "${BOARD_USER}@${BOARD_IP}:${SMOKE_REMOTE_LOG}" "${tmp_log}"; then
                copy_status=0
            else
                copy_status=$?
                {
                    echo "remote_log_copy_exit=${copy_status}"
                    echo "remote_log_copy_failed=${SMOKE_REMOTE_LOG}"
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
                echo "remote_upload_failed=${SMOKE_REMOTE_PROFILE}"
                echo "remote_log_copy_exit=${copy_status}"
                echo "remote_log_copy_skipped=runtime_not_invoked"
            } >> "${VERIFY_LOG}"
        fi
    else
        remote_status=$?
        copy_status=255
        {
            echo "remote_upload_exit=${remote_status}"
            echo "remote_upload_failed=${SMOKE_REMOTE_PARAMS}"
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
    if [[ "${copy_status}" -ne 0 ]]; then
        return "${copy_status}"
    fi
}

smoke_run_local() {
    local tmp_profile
    local runtime_env
    local host_arch
    local file_desc

    tmp_profile="$(mktemp)"
    trap "rm -f '${tmp_profile}'" RETURN
    {
        echo "remote smoke unavailable, executed local fallback"
        echo "local_binary=${LOCAL_BIN}"
    } >> "${VERIFY_LOG}"

    host_arch="$(uname -m || echo unknown)"
    file_desc="$(file -b "${LOCAL_BIN}" 2>/dev/null || true)"
    if [[ "${file_desc}" == *"LoongArch"* && "${host_arch}" != "loongarch64" ]]; then
        {
            echo "local execution skipped: binary architecture incompatible with host"
            echo "host_arch=${host_arch}"
            echo "binary_desc=${file_desc}"
        } >> "${VERIFY_LOG}"
        return 0
    fi

    if ! prepare_smoke_profile "${PROFILE_PATH}" "${tmp_profile}"; then
        echo "profile_prepare_failed=${PROFILE_PATH}" >> "${VERIFY_LOG}"
        return 1
    fi

    runtime_env="$(smoke_runtime_env "${PARAMS_PATH}" "${tmp_profile}")"
    bash -lc "${runtime_env} \"${LOCAL_BIN}\"" >> "${VERIFY_LOG}" 2>&1 &
    local runtime_pid=$!
    monitor_local_runtime "${runtime_pid}" "${VERIFY_LOG}"
}

smoke_command() {
    local action="${1:-run}"

    case "${action}" in
        run)
            smoke_write_header
            smoke_run_remote
            ;;
        local)
            smoke_write_header
            smoke_run_local
            ;;
        -h|--help|help)
            smoke_usage
            ;;
        *)
            smoke_usage >&2
            exit 1
            ;;
    esac
}

main() {
    local group="${1:-help}"
    case "${group}" in
        build)
            shift
            build_command "$@"
            ;;
        assistant)
            shift
            assistant_command "$@"
            ;;
        remote)
            shift
            remote_command "$@"
            ;;
        smoke)
            shift
            smoke_command "$@"
            ;;
        -h|--help|help)
            usage
            ;;
        *)
            usage >&2
            exit 1
            ;;
    esac
}

main "$@"
