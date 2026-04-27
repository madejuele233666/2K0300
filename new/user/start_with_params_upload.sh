#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BOARD_IP="${BOARD_IP:-10.100.170.226}"
BOARD_USER="${BOARD_USER:-root}"
BOARD_PATH="${BOARD_PATH:-/home/root}"
PARAMS_PATH="${LS2K_PARAMS_PATH:-${REPO_ROOT}/new/config/default_params.json}"
REMOTE_PARAMS="${LS2K_REMOTE_PARAMS_PATH:-${BOARD_PATH}/default_params.json}"
SSH_CONNECT_TIMEOUT="${SSH_CONNECT_TIMEOUT:-5}"
SSH_SERVER_ALIVE_INTERVAL="${SSH_SERVER_ALIVE_INTERVAL:-5}"
SSH_SERVER_ALIVE_COUNT_MAX="${SSH_SERVER_ALIVE_COUNT_MAX:-2}"

usage() {
    cat <<'EOF'
Usage:
  ./start_with_params_upload.sh [no-motion|drive]

Modes:
  no-motion  Default. Stop runtime, upload params, then start normal runtime with LS2K_AUTO_START=0.
  drive      Powered start. Requires CONFIRM_POWERED_START=1 and starts with LS2K_AUTO_START=1.

Examples:
  ./start_with_params_upload.sh
  ./start_with_params_upload.sh no-motion
  CONFIRM_POWERED_START=1 ./start_with_params_upload.sh drive
EOF
}

mode="${1:-no-motion}"
case "${mode}" in
    no-motion|--no-motion|safe|--safe)
        if [[ "$#" -gt 0 ]]; then
            shift
        fi
        auto_start="0"
        auto_start_delay_ms="${LS2K_AUTO_START_DELAY_MS:-0}"
        ;;
    drive|--drive|auto-start|--auto-start)
        shift
        if [[ "${CONFIRM_POWERED_START:-0}" != "1" ]]; then
            echo "[ERROR] drive mode requires CONFIRM_POWERED_START=1" >&2
            exit 1
        fi
        auto_start="1"
        auto_start_delay_ms="${LS2K_AUTO_START_DELAY_MS:-200}"
        ;;
    -h|--help|help)
        usage
        exit 0
        ;;
    *)
        usage >&2
        exit 1
        ;;
esac

if [[ "$#" -ne 0 ]]; then
    echo "[ERROR] unexpected arguments: $*" >&2
    usage >&2
    exit 1
fi

if [[ ! -f "${PARAMS_PATH}" ]]; then
    echo "[ERROR] params file missing: ${PARAMS_PATH}" >&2
    exit 1
fi

SSH_COMMON_OPTS=(
    -o "ConnectTimeout=${SSH_CONNECT_TIMEOUT}"
    -o "ServerAliveInterval=${SSH_SERVER_ALIVE_INTERVAL}"
    -o "ServerAliveCountMax=${SSH_SERVER_ALIVE_COUNT_MAX}"
    -o "StrictHostKeyChecking=accept-new"
)

# Stop the runtime before updating parameters so the next launch definitely
# comes up on the just-uploaded config set.
"${SCRIPT_DIR}/debug.sh" remote stop
scp -O "${SSH_COMMON_OPTS[@]}" "${PARAMS_PATH}" "${BOARD_USER}@${BOARD_IP}:${REMOTE_PARAMS}"

exec env \
    LS2K_AUTO_START="${auto_start}" \
    LS2K_AUTO_START_DELAY_MS="${auto_start_delay_ms}" \
    CONFIRM_POWERED_START="${CONFIRM_POWERED_START:-0}" \
    "${SCRIPT_DIR}/debug.sh" remote restart normal
