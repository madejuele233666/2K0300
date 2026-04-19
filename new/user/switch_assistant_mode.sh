#!/bin/bash
set -euo pipefail

WORK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARAMS_PATH="${LS2K_PARAMS_PATH:-${WORK_DIR}/../config/default_params.json}"
DEFAULT_BOARD_IP="${BOARD_IP:-10.100.170.226}"
DEFAULT_PORT="${ASSISTANT_PORT:-8888}"

usage() {
    cat <<'EOF'
Usage:
  ./switch_assistant_mode.sh status
  ./switch_assistant_mode.sh on [host] [port]
  ./switch_assistant_mode.sh local [port]
  ./switch_assistant_mode.sh off

Behavior:
  - only updates assistant-related fields in the params JSON
  - keeps all motion / PID parameters unchanged
  - `on` defaults host to the routed local IP for BOARD_IP, then falls back to the existing JSON host
  - `local` requires a detectable local IPv4 and enables continuous publish to that host
EOF
}

require_params_file() {
    if [[ ! -f "${PARAMS_PATH}" ]]; then
        echo "params file not found: ${PARAMS_PATH}" >&2
        exit 1
    fi
}

detect_local_ip() {
    if command -v ip >/dev/null 2>&1; then
        local detected
        detected="$(ip route get "${DEFAULT_BOARD_IP}" 2>/dev/null | awk '/ src / { for (i = 1; i <= NF; ++i) if ($i == "src") { print $(i + 1); exit } }')"
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

read_current_field() {
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

print_status() {
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

update_params() {
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

require_params_file

mode="${1:-status}"
case "${mode}" in
    status)
        print_status
        ;;
    on)
        host="${2:-${ASSISTANT_HOST:-}}"
        port="${3:-${DEFAULT_PORT}}"
        if [[ -z "${host}" ]]; then
            host="$(detect_local_ip || true)"
        fi
        if [[ -z "${host}" ]]; then
            host="$(read_current_field host)"
        fi
        if [[ -z "${host}" ]]; then
            echo "unable to resolve assistant host; pass it explicitly: ./switch_assistant_mode.sh on <host> [port]" >&2
            exit 1
        fi
        update_params "1" "${host}" "${port}"
        ;;
    local)
        port="${2:-${DEFAULT_PORT}}"
        host="$(detect_local_ip || true)"
        if [[ -z "${host}" ]]; then
            echo "unable to detect local IPv4 for BOARD_IP=${DEFAULT_BOARD_IP}" >&2
            exit 1
        fi
        update_params "1" "${host}" "${port}"
        ;;
    off)
        update_params "0" "$(read_current_field host)" "$(read_current_field port)"
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        usage >&2
        exit 1
        ;;
esac
