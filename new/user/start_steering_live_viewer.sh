#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARAMS_PATH="${LS2K_PARAMS_PATH:-${SCRIPT_DIR}/../config/default_params.json}"

live_host="${LS2K_LIVE_HOST:-127.0.0.1}"
live_port="${LS2K_LIVE_PORT:-8765}"
duration_s="${LS2K_LIVE_DURATION_S:-86400}"
open_browser="${LS2K_OPEN_LIVE_BROWSER:-1}"
auto_ports="${LS2K_LIVE_AUTO_PORTS:-1}"
upload_params="${LS2K_LIVE_UPLOAD_PARAMS:-1}"
live_advertise_host="${LS2K_LIVE_ADVERTISE_HOST:-}"
capture_bind_host="${LS2K_LIVE_CAPTURE_BIND_HOST:-}"
media_record_mode="${LS2K_LIVE_MEDIA_RECORD_MODE:-none}"
display_mode="${LS2K_LIVE_DISPLAY_MODE:-bev}"
view_mode="${LS2K_LIVE_VIEW_MODE:-camera}"
media_interval_ms="${LS2K_LIVE_MEDIA_INTERVAL_MS:-}"
media_downsample="${LS2K_LIVE_MEDIA_DOWNSAMPLE:-}"
media_gray_bits="${LS2K_LIVE_MEDIA_GRAY_BITS:-}"
media_latest_frame="${LS2K_LIVE_MEDIA_LATEST_FRAME:-}"
capture_args=()
capture_port_pairs=()
retry_capture_ports=0

usage() {
    cat <<'EOF'
Usage:
  ./start_steering_live_viewer.sh [options] [-- host_capture.py args...]

Starts a long-lived host-side steering/media capture with the read-only local web viewer.
The board still uses the existing assistant control and steering media TCP links.

Options:
  --duration-s <seconds>    Capture duration. Default: LS2K_LIVE_DURATION_S or 86400.
  --live-host <host>        Viewer bind host. Default: LS2K_LIVE_HOST or 127.0.0.1.
  --live-port <port>        Viewer TCP port. Default: LS2K_LIVE_PORT or 8765.
  --open-browser           Open the viewer URL automatically. Default.
  --no-open-browser        Print the viewer URL only.
  --auto-ports             Select capture ports and retry bind failures. Default.
  --no-auto-ports          Use configured ports exactly.
  --upload-params          Upload any selected control/media ports to the board. Default.
  --no-upload-params       Update local capture only; do not upload params.
  --advertise-host <ip>    Host/IP written to board params. Default: auto-detect Windows source IP.
  --capture-bind-host <ip> Local host_capture bind address. Default: 0.0.0.0 for Windows capture.
  --media-record-mode <m>  Host evidence mode for media frames: none, metadata, or all.
                           Default: LS2K_LIVE_MEDIA_RECORD_MODE or none.
  --display-mode <raw|bev> Initial viewer image mode. Default: LS2K_LIVE_DISPLAY_MODE or bev.
  --view-mode <camera|waveform>
                           Initial viewer surface. camera shows image frames; waveform shows motor target/actual speed.
                           Default: LS2K_LIVE_VIEW_MODE or camera.
  --media-interval-ms <ms> Override steering_media_publish_interval_ms before upload.
  --media-downsample <n>   Override steering_media_downsample before upload.
  --media-gray-bits <1|2|4|8>
                           Override steering_media_gray_bits before upload.
  --media-latest-frame     Diagnostic mode: publish latest camera frame, not the snapshot-aligned frame.
  --no-media-latest-frame  Force strict snapshot-aligned media publication.
  --high-fps-320x240      Shortcut for 320x240 gray2 snapshot-aligned live mode at 20ms.
  -h, --help               Show this help.

Examples:
  ./start_steering_live_viewer.sh
  ./start_steering_live_viewer.sh --duration-s 120
  ./start_steering_live_viewer.sh --live-host 0.0.0.0 --live-port 8765 -- --output-dir ../verification/live-test
EOF
}

require_value() {
    local option="$1"
    local value="${2:-}"
    if [[ -z "${value}" ]]; then
        echo "[ERROR] ${option} requires a value" >&2
        usage >&2
        exit 1
    fi
}

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --duration-s)
            require_value "$1" "${2:-}"
            duration_s="$2"
            shift 2
            ;;
        --duration-s=*)
            duration_s="${1#*=}"
            shift
            ;;
        --live-host)
            require_value "$1" "${2:-}"
            live_host="$2"
            shift 2
            ;;
        --live-host=*)
            live_host="${1#*=}"
            shift
            ;;
        --live-port)
            require_value "$1" "${2:-}"
            live_port="$2"
            shift 2
            ;;
        --live-port=*)
            live_port="${1#*=}"
            shift
            ;;
        --open-browser)
            open_browser=1
            shift
            ;;
        --no-open-browser)
            open_browser=0
            shift
            ;;
        --auto-ports)
            auto_ports=1
            shift
            ;;
        --no-auto-ports)
            auto_ports=0
            shift
            ;;
        --upload-params)
            upload_params=1
            shift
            ;;
        --no-upload-params)
            upload_params=0
            shift
            ;;
        --advertise-host)
            require_value "$1" "${2:-}"
            live_advertise_host="$2"
            shift 2
            ;;
        --advertise-host=*)
            live_advertise_host="${1#*=}"
            shift
            ;;
        --capture-bind-host)
            require_value "$1" "${2:-}"
            capture_bind_host="$2"
            shift 2
            ;;
        --capture-bind-host=*)
            capture_bind_host="${1#*=}"
            shift
            ;;
        --media-record-mode)
            require_value "$1" "${2:-}"
            media_record_mode="$2"
            shift 2
            ;;
        --media-record-mode=*)
            media_record_mode="${1#*=}"
            shift
            ;;
        --display-mode)
            require_value "$1" "${2:-}"
            display_mode="$2"
            shift 2
            ;;
        --display-mode=*)
            display_mode="${1#*=}"
            shift
            ;;
        --view-mode)
            require_value "$1" "${2:-}"
            view_mode="$2"
            shift 2
            ;;
        --view-mode=*)
            view_mode="${1#*=}"
            shift
            ;;
        --media-interval-ms)
            require_value "$1" "${2:-}"
            media_interval_ms="$2"
            shift 2
            ;;
        --media-interval-ms=*)
            media_interval_ms="${1#*=}"
            shift
            ;;
        --media-downsample)
            require_value "$1" "${2:-}"
            media_downsample="$2"
            shift 2
            ;;
        --media-downsample=*)
            media_downsample="${1#*=}"
            shift
            ;;
        --media-gray-bits)
            require_value "$1" "${2:-}"
            media_gray_bits="$2"
            shift 2
            ;;
        --media-gray-bits=*)
            media_gray_bits="${1#*=}"
            shift
            ;;
        --media-latest-frame)
            media_latest_frame=1
            shift
            ;;
        --no-media-latest-frame)
            media_latest_frame=0
            shift
            ;;
        --high-fps-320x240)
            media_downsample=1
            media_interval_ms=20
            media_gray_bits=2
            media_latest_frame=0
            shift
            ;;
        -h|--help|help)
            usage
            exit 0
            ;;
        --)
            shift
            capture_args+=("$@")
            break
            ;;
        *)
            capture_args+=("$1")
            shift
            ;;
    esac
done

truthy() {
    case "${1:-}" in
        1|true|TRUE|yes|YES|on|ON)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

windows_python_wsl_path() {
    if [[ -n "${LS2K_WINDOWS_PYTHON:-}" ]]; then
        if [[ "${LS2K_WINDOWS_PYTHON}" == /* ]]; then
            printf '%s\n' "${LS2K_WINDOWS_PYTHON}"
            return 0
        fi
        wslpath -u "${LS2K_WINDOWS_PYTHON}"
        return 0
    fi
    if [[ -x "/mnt/d/install_software/py3/python.exe" ]]; then
        printf '%s\n' "/mnt/d/install_software/py3/python.exe"
        return 0
    fi
    if command -v python.exe >/dev/null 2>&1; then
        command -v python.exe
        return 0
    fi
    if [[ -x "/mnt/c/Windows/py.exe" ]]; then
        printf '%s\n' "/mnt/c/Windows/py.exe"
        return 0
    fi
    return 1
}

capture_arg_present() {
    local flag="$1"
    local token
    for token in "${capture_args[@]}"; do
        if [[ "${token}" == "${flag}" || "${token}" == "${flag}="* ]]; then
            return 0
        fi
    done
	return 1
}

is_ipv4_literal() {
    [[ "${1:-}" =~ ^[0-9]{1,3}(\.[0-9]{1,3}){3}$ ]]
}

board_ip_hint() {
    printf '%s\n' "${BOARD_IP:-${LS2K_DEFAULT_BOARD_IP:-10.100.170.226}}"
}

windows_has_ipv4() {
    local ip="$1"
    is_ipv4_literal "${ip}" || return 1
    command -v powershell.exe >/dev/null 2>&1 || return 1
    powershell.exe -NoProfile -Command "\
\$ErrorActionPreference='SilentlyContinue'; \
\$ip='${ip}'; \
\$match=Get-NetIPAddress -AddressFamily IPv4 -IPAddress \$ip; \
if (\$null -eq \$match) { exit 1 }; \
Write-Output \$ip" 2>/dev/null |
        tr -d '\r' |
        awk '/^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$/ { print; found=1; exit } END { exit found ? 0 : 1 }'
}

windows_route_source_ip() {
    local target="$1"
    is_ipv4_literal "${target}" || return 1
    command -v powershell.exe >/dev/null 2>&1 || return 1
    powershell.exe -NoProfile -Command "\
\$ErrorActionPreference='SilentlyContinue'; \
\$target='${target}'; \
\$route=Find-NetRoute -RemoteIPAddress \$target | Sort-Object RouteMetric,InterfaceMetric | Select-Object -First 1; \
if (\$null -eq \$route) { exit 1 }; \
\$ip=Get-NetIPAddress -InterfaceIndex \$route.InterfaceIndex -AddressFamily IPv4 | \
    Where-Object { \$_.IPAddress -ne '127.0.0.1' -and \$_.IPAddress -notlike '169.254.*' -and \$_.IPAddress -notlike '198.18.*' } | \
    Select-Object -First 1; \
if (\$null -eq \$ip) { exit 1 }; \
Write-Output \$ip.IPAddress" 2>/dev/null |
        tr -d '\r' |
        awk '/^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$/ { print; found=1; exit } END { exit found ? 0 : 1 }'
}

windows_default_source_ip() {
    command -v powershell.exe >/dev/null 2>&1 || return 1
    powershell.exe -NoProfile -Command '\
$ErrorActionPreference="SilentlyContinue"; \
function ValidIpForInterface($ifIndex) { \
    Get-NetIPAddress -InterfaceIndex $ifIndex -AddressFamily IPv4 | \
        Where-Object { $_.IPAddress -ne "127.0.0.1" -and $_.IPAddress -notlike "169.254.*" -and $_.IPAddress -notlike "198.18.*" } | \
        Select-Object -First 1 -ExpandProperty IPAddress; \
}; \
$hotspot=Get-NetAdapter | Where-Object { $_.InterfaceDescription -like "*Wi-Fi Direct Virtual Adapter*" -and $_.Status -eq "Up" } | Select-Object -First 1; \
if ($null -ne $hotspot) { $ip=ValidIpForInterface $hotspot.ifIndex; if ($ip) { Write-Output $ip; exit 0 } }; \
$routes=Get-NetRoute -DestinationPrefix "0.0.0.0/0" | Sort-Object RouteMetric,InterfaceMetric; \
foreach ($route in $routes) { $ip=ValidIpForInterface $route.InterfaceIndex; if ($ip) { Write-Output $ip; exit 0 } }; \
exit 1' 2>/dev/null |
        tr -d '\r' |
        awk '/^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$/ { print; found=1; exit } END { exit found ? 0 : 1 }'
}

wsl_route_source_ip() {
    local target="$1"
    is_ipv4_literal "${target}" || return 1
    command -v ip >/dev/null 2>&1 || return 1
    ip route get "${target}" 2>/dev/null |
        awk '/ src / { for (i = 1; i <= NF; ++i) if ($i == "src") { print $(i + 1); exit } }'
}

resolve_advertise_host() {
    local configured_host="$1"
    local board_hint
    local detected

    if [[ -n "${live_advertise_host}" ]]; then
        printf '%s\n' "${live_advertise_host}"
        return 0
    fi

    if [[ -n "${configured_host}" && "${configured_host}" != "0.0.0.0" ]]; then
        if [[ "${host_capture_backend}" == "windows" ]]; then
            if windows_has_ipv4 "${configured_host}" >/dev/null 2>&1; then
                printf '%s\n' "${configured_host}"
                return 0
            fi
            echo "[WARN] configured assistant host ${configured_host} is not assigned on Windows; detecting current host IP" >&2
        else
            printf '%s\n' "${configured_host}"
            return 0
        fi
    fi

    board_hint="$(board_ip_hint)"
    if [[ "${host_capture_backend}" == "windows" ]]; then
        detected="$(windows_route_source_ip "${board_hint}" || true)"
        if [[ -n "${detected}" ]]; then
            printf '%s\n' "${detected}"
            return 0
        fi
        detected="$(windows_default_source_ip || true)"
        if [[ -n "${detected}" ]]; then
            printf '%s\n' "${detected}"
            return 0
        fi
    fi

    detected="$(wsl_route_source_ip "${board_hint}" || true)"
    if [[ -n "${detected}" ]]; then
        printf '%s\n' "${detected}"
        return 0
    fi

    printf '%s\n' "${configured_host:-192.168.137.1}"
}

if ! capture_arg_present "--media-record-mode"; then
    capture_args=(--media-record-mode "${media_record_mode}" "${capture_args[@]}")
fi

read_assistant_config() {
    python3 - "${PARAMS_PATH}" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
assistant = data.get("assistant_tcp") or {}
print(assistant.get("host") or "")
print(int(assistant.get("port") or 0))
print(int(data.get("steering_media_port") or 0))
print(int(data.get("steering_media_enabled") or 0))
PY
}

update_live_media_params() {
    local interval_ms="$1"
    local downsample="$2"
    local gray_bits="$3"
    local latest_frame="$4"
    if [[ -z "${interval_ms}" && -z "${downsample}" && -z "${gray_bits}" && -z "${latest_frame}" ]]; then
        return 0
    fi
    python3 - "${PARAMS_PATH}" "${interval_ms}" "${downsample}" "${gray_bits}" "${latest_frame}" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
interval_raw = sys.argv[2]
downsample_raw = sys.argv[3]
gray_bits_raw = sys.argv[4]
latest_frame_raw = sys.argv[5]
data = json.loads(path.read_text(encoding="utf-8"))

if interval_raw:
    interval = int(interval_raw)
    if interval < 0:
        raise SystemExit("steering_media_publish_interval_ms must be >= 0")
    data["steering_media_publish_interval_ms"] = interval
if downsample_raw:
    downsample = int(downsample_raw)
    if downsample < 1 or downsample > 8:
        raise SystemExit("steering_media_downsample must be in [1, 8]")
    data["steering_media_downsample"] = downsample
if gray_bits_raw:
    gray_bits = int(gray_bits_raw)
    if gray_bits not in (1, 2, 4, 8):
        raise SystemExit("steering_media_gray_bits must be 1, 2, 4, or 8")
    data["steering_media_gray_bits"] = gray_bits
if latest_frame_raw:
    data["steering_media_publish_latest_frame"] = 0 if latest_frame_raw in ("0", "false", "False", "off") else 1

path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
print(f"steering_media_publish_interval_ms={data.get('steering_media_publish_interval_ms')}")
print(f"steering_media_downsample={data.get('steering_media_downsample')}")
print(f"steering_media_gray_bits={data.get('steering_media_gray_bits')}")
print(f"steering_media_publish_latest_frame={data.get('steering_media_publish_latest_frame')}")
PY
}

candidate_port_pairs() {
    local current_control="$1"
    local current_media="$2"

    python3 - "${current_control}" "${current_media}" <<'PY'
import sys

current_control = int(sys.argv[1])
current_media = int(sys.argv[2])
candidates = []

def add(control, media):
    if control <= 0 or media <= 0 or control == media:
        return
    pair = (control, media)
    if pair not in candidates:
        candidates.append(pair)

if current_control >= 40000 and current_media >= 40000:
    add(current_control, current_media)
for base in (47011, 48011, 43011, 44011, 42011, 45011, 41011, 40011, 39111, 39011):
    add(base, base + 1)

for control, media in candidates:
    print(f"{control} {media}")
PY
}

assistant_host=""
assistant_port=""
media_port=""
media_enabled=""
if [[ -f "${PARAMS_PATH}" ]]; then
    mapfile -t assistant_config < <(read_assistant_config)
    assistant_host="${assistant_config[0]:-}"
    assistant_port="${assistant_config[1]:-0}"
    media_port="${assistant_config[2]:-0}"
    media_enabled="${assistant_config[3]:-0}"
fi

has_capture_listen_host=0
has_capture_listen_port=0
has_capture_media_host=0
has_capture_media_port=0
capture_arg_present "--listen-host" && has_capture_listen_host=1
capture_arg_present "--listen-port" && has_capture_listen_port=1
capture_arg_present "--media-listen-host" && has_capture_media_host=1
capture_arg_present "--media-listen-port" && has_capture_media_port=1

host_capture_backend="${LS2K_HOST_CAPTURE_BACKEND:-auto}"
if [[ "${host_capture_backend}" == "auto" ]] &&
    command -v cmd.exe >/dev/null 2>&1 &&
    windows_python_wsl_path >/dev/null 2>&1; then
    host_capture_backend="windows"
fi
export LS2K_HOST_CAPTURE_BACKEND="${host_capture_backend}"

advertise_host="$(resolve_advertise_host "${assistant_host:-}")"
if [[ -z "${capture_bind_host}" ]]; then
    if [[ "${host_capture_backend}" == "windows" ]]; then
        capture_bind_host="0.0.0.0"
    else
        capture_bind_host="${advertise_host}"
    fi
fi

if truthy "${auto_ports}"; then
    if [[ "${has_capture_listen_host}" -eq 0 && "${has_capture_listen_port}" -eq 0 &&
          "${has_capture_media_host}" -eq 0 && "${has_capture_media_port}" -eq 0 ]]; then
        mapfile -t capture_port_pairs < <(candidate_port_pairs "${assistant_port:-0}" "${media_port:-0}")
        retry_capture_ports=1
    else
        echo "[live] explicit capture endpoint args detected; skipping assistant/media auto-port rewrite"
    fi
fi

viewer_host="${live_host}"
case "${viewer_host}" in
    ""|0.0.0.0|::)
        viewer_host="127.0.0.1"
        ;;
esac
viewer_url="http://${viewer_host}:${live_port}/"

open_viewer_later() {
    local url="$1"
    {
        sleep 1.5
        if command -v cmd.exe >/dev/null 2>&1; then
            cmd.exe /C start "" "${url}" >/dev/null 2>&1 || true
        elif command -v xdg-open >/dev/null 2>&1; then
            xdg-open "${url}" >/dev/null 2>&1 || true
        fi
    } &
}

capture_bind_error() {
    local log_path="$1"
    grep -E -q 'WinError 10013|WinError 10048|WinError 10049|PermissionError|Address already in use|address already in use|failed to start viewer' "${log_path}"
}

run_capture_attempt() {
    local attempt_control_port="$1"
    local attempt_media_port="$2"
    local -a attempt_args=(
        --live-web
        --live-host "${live_host}"
        --live-port "${live_port}"
        --duration-s "${duration_s}"
        --live-display-mode "${display_mode}"
        --live-view-mode "${view_mode}"
        "${capture_args[@]}"
        --listen-host "${capture_bind_host}"
        --listen-port "${attempt_control_port}"
        --media-listen-host "${capture_bind_host}"
        --media-listen-port "${attempt_media_port}"
    )

    echo "[live] advertise_host=${advertise_host}"
    echo "[live] capture_endpoint=${capture_bind_host}:${attempt_control_port} media=${capture_bind_host}:${attempt_media_port}"
    "${SCRIPT_DIR}/debug.sh" assistant on "${advertise_host}" "${attempt_control_port}" "${attempt_media_port}" >/dev/null
    update_live_media_params "${media_interval_ms}" "${media_downsample}" "${media_gray_bits}" "${media_latest_frame}"
    if truthy "${upload_params}"; then
        if ! "${SCRIPT_DIR}/debug.sh" remote upload-params; then
            echo "[WARN] params upload failed; restart/upload params before expecting board connections" >&2
        fi
    fi

    "${SCRIPT_DIR}/debug.sh" steering host-capture "${attempt_args[@]}"
}

echo "[live] viewer_url=${viewer_url}"
echo "[live] duration_s=${duration_s}"
echo "[live] media_record_mode=${media_record_mode}"
echo "[live] display_mode=${display_mode}"
echo "[live] view_mode=${view_mode}"
echo "[live] host_capture_backend=${host_capture_backend}"
if [[ -n "${media_interval_ms}" || -n "${media_downsample}" || -n "${media_gray_bits}" || -n "${media_latest_frame}" ]]; then
    echo "[live] media_param_override interval_ms=${media_interval_ms:-keep} downsample=${media_downsample:-keep} gray_bits=${media_gray_bits:-keep} latest_frame=${media_latest_frame:-keep}"
fi
echo "[live] stop with Ctrl+C when finished"

case "${open_browser}" in
    1|true|TRUE|yes|YES|on|ON)
        open_viewer_later "${viewer_url}"
        ;;
esac

if [[ "${retry_capture_ports}" -eq 1 ]]; then
    last_rc=1
    pair_index=0
    pair_count="${#capture_port_pairs[@]}"
    for pair in "${capture_port_pairs[@]}"; do
        pair_index=$((pair_index + 1))
        read -r assistant_port media_port <<<"${pair}"
        attempt_log="$(mktemp)"
        set +e
        run_capture_attempt "${assistant_port}" "${media_port}" 2>&1 | tee "${attempt_log}"
        last_rc="${PIPESTATUS[0]}"
        set -e
        if [[ "${last_rc}" -eq 0 ]]; then
            rm -f "${attempt_log}"
            exit 0
        fi
        if [[ "${pair_index}" -lt "${pair_count}" ]] && capture_bind_error "${attempt_log}"; then
            echo "[WARN] capture bind failed on ${capture_bind_host}:${assistant_port}/${media_port}; retrying next port pair" >&2
            rm -f "${attempt_log}"
            continue
        fi
        rm -f "${attempt_log}"
        exit "${last_rc}"
    done
    exit "${last_rc}"
fi

final_control_port="${assistant_port:-0}"
final_media_port="${media_port:-0}"
params_updated=0
final_capture_args=(
    --live-web
    --live-host "${live_host}"
    --live-port "${live_port}"
    --duration-s "${duration_s}"
    --live-display-mode "${display_mode}"
    --live-view-mode "${view_mode}"
    "${capture_args[@]}"
)
if [[ "${has_capture_listen_host}" -eq 0 ]]; then
    final_capture_args+=(--listen-host "${capture_bind_host}")
fi
if [[ "${has_capture_listen_port}" -eq 0 && "${final_control_port}" -gt 0 ]]; then
    final_capture_args+=(--listen-port "${final_control_port}")
fi
if [[ "${has_capture_media_host}" -eq 0 ]]; then
    final_capture_args+=(--media-listen-host "${capture_bind_host}")
fi
if [[ "${has_capture_media_port}" -eq 0 && "${media_enabled}" == "1" && "${final_media_port}" -gt 0 ]]; then
    final_capture_args+=(--media-listen-port "${final_media_port}")
fi

if [[ "${has_capture_listen_host}" -eq 0 && "${has_capture_listen_port}" -eq 0 &&
      "${has_capture_media_host}" -eq 0 && "${has_capture_media_port}" -eq 0 &&
      "${final_control_port}" -gt 0 && "${final_media_port}" -gt 0 ]]; then
    echo "[live] advertise_host=${advertise_host}"
    echo "[live] capture_endpoint=${capture_bind_host}:${final_control_port} media=${capture_bind_host}:${final_media_port}"
    "${SCRIPT_DIR}/debug.sh" assistant on "${advertise_host}" "${final_control_port}" "${final_media_port}" >/dev/null
    params_updated=1
fi

update_live_media_params "${media_interval_ms}" "${media_downsample}" "${media_gray_bits}" "${media_latest_frame}"
if [[ -n "${media_interval_ms}${media_downsample}${media_gray_bits}${media_latest_frame}" ]]; then
    params_updated=1
fi
if truthy "${upload_params}" && [[ "${params_updated}" -eq 1 ]]; then
    if ! "${SCRIPT_DIR}/debug.sh" remote upload-params; then
        echo "[WARN] params upload failed; restart/upload params before expecting board connections" >&2
    fi
fi

exec "${SCRIPT_DIR}/debug.sh" steering host-capture "${final_capture_args[@]}"
