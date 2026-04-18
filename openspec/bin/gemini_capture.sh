#!/usr/bin/env bash
set -euo pipefail

prompt=""
raw_path=""
report_path=""
input_raw_path=""
max_attempts=1
require_json_response=0
model=""

usage() {
    cat <<'EOF'
Usage:
  gemini_capture.sh --prompt "<prompt>" --raw-path <raw.json> --report-path <report.json> [--model MODEL] [--max-attempts N] [--require-json-response]
  gemini_capture.sh --input-raw-path <raw.json> --report-path <report.json> [--require-json-response]
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prompt)
            prompt="${2:-}"
            shift 2
            ;;
        --raw-path)
            raw_path="${2:-}"
            shift 2
            ;;
        --report-path)
            report_path="${2:-}"
            shift 2
            ;;
        --model)
            model="${2:-}"
            shift 2
            ;;
        --input-raw-path)
            input_raw_path="${2:-}"
            shift 2
            ;;
        --max-attempts)
            max_attempts="${2:-}"
            shift 2
            ;;
        --require-json-response)
            require_json_response=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if ! command -v gemini >/dev/null 2>&1; then
    echo "gemini CLI not found in PATH" >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 not found in PATH" >&2
    exit 1
fi

if ! [[ "$max_attempts" =~ ^[1-9][0-9]*$ ]]; then
    echo "--max-attempts must be a positive integer" >&2
    exit 2
fi

normalize_report() {
    local raw_file="$1"
    local report_file="$2"
    python3 - "$raw_file" "$report_file" "$require_json_response" <<'PY'
import json
import pathlib
import re
import sys

raw_path, report_path, require_json = sys.argv[1], sys.argv[2], sys.argv[3] == "1"
payload = json.loads(pathlib.Path(raw_path).read_text(encoding="utf-8"))
response = payload.get("response")
error = payload.get("error")
if error and (not isinstance(response, str) or not response.strip()):
    raise SystemExit(f"gemini envelope contains error payload: {error}")

def extract_json_candidate(text: str):
    start = text.find("{")
    while start != -1:
        depth = 0
        in_string = False
        escaping = False
        for i in range(start, len(text)):
            ch = text[i]
            if in_string:
                if escaping:
                    escaping = False
                elif ch == "\\":
                    escaping = True
                elif ch == '"':
                    in_string = False
                continue
            if ch == '"':
                in_string = True
            elif ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    candidate = text[start:i + 1]
                    try:
                        return json.loads(candidate)
                    except json.JSONDecodeError:
                        break
        start = text.find("{", start + 1)
    return None

if require_json:
    if not isinstance(response, str) or not response.strip():
        raise SystemExit("raw envelope is missing a JSON response payload")
    text = response.strip()
    fence = re.match(r"^```(?:json)?\s*(.*?)\s*```$", text, re.DOTALL)
    if fence:
        text = fence.group(1).strip()
    try:
        normalized = json.loads(text)
    except json.JSONDecodeError as exc:
        recovered = extract_json_candidate(text)
        if recovered is None:
            preview = text[:200].replace("\n", " ")
            raise SystemExit(
                f"response is not valid JSON and recovery failed: {exc}; preview={preview!r}"
            )
        normalized = recovered
else:
    normalized = response
pathlib.Path(report_path).parent.mkdir(parents=True, exist_ok=True)
tmp_report = pathlib.Path(report_path + ".tmp")
with open(tmp_report, "w", encoding="utf-8") as handle:
    json.dump(normalized, handle, ensure_ascii=False, indent=2)
    handle.write("\n")
tmp_report.replace(report_path)
PY
}

if [[ -n "$input_raw_path" ]]; then
    if [[ -z "$report_path" ]]; then
        echo "--report-path is required with --input-raw-path" >&2
        exit 2
    fi
    normalize_report "$input_raw_path" "$report_path"
    exit 0
fi

if [[ -z "$prompt" || -z "$raw_path" || -z "$report_path" ]]; then
    usage >&2
    exit 2
fi

mkdir -p "$(dirname "$raw_path")" "$(dirname "$report_path")"
tmp_dir="$(mktemp -d)"
cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT

attempt=1
last_error="unknown failure"
while (( attempt <= max_attempts )); do
    cmd=(gemini -y -p "$prompt" --output-format json)
    if [[ -n "$model" ]]; then
        cmd+=(-m "$model")
    fi
    raw_tmp="$tmp_dir/raw-attempt-${attempt}.json"
    gemini_err="$tmp_dir/gemini-attempt-${attempt}.stderr"
    norm_err="$tmp_dir/normalize-attempt-${attempt}.stderr"
    if "${cmd[@]}" > "$raw_tmp" 2>"$gemini_err"; then
        if normalize_report "$raw_tmp" "$report_path" 2>"$norm_err"; then
            cp "$raw_tmp" "$raw_path"
            exit 0
        else
            last_error="normalize failure (attempt ${attempt}): $(tr '\n' ' ' < "$norm_err")"
            echo "$last_error" >&2
        fi
    else
        last_error="gemini invocation failure (attempt ${attempt}): $(tr '\n' ' ' < "$gemini_err")"
        echo "$last_error" >&2
    fi
    if (( attempt < max_attempts )); then
        backoff_seconds=$((attempt * 2))
        sleep "$backoff_seconds"
    fi
    attempt=$((attempt + 1))
done

if [[ -f "$raw_path" ]]; then
    if normalize_report "$raw_path" "$report_path" 2>"$tmp_dir/recover.stderr"; then
        echo "primary attempts failed; report recovered from existing raw envelope" >&2
        exit 0
    else
        last_error="${last_error}; recover-from-existing-raw failed: $(tr '\n' ' ' < "$tmp_dir/recover.stderr")"
    fi
fi

echo "gemini_capture failed after ${max_attempts} attempt(s): $last_error" >&2
exit 1
