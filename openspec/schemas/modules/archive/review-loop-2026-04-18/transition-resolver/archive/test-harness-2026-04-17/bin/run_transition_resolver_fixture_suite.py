#!/usr/bin/env python3
"""
Run the transition-resolver fixture suite.

The suite resolves each example input through the real CLI, validates successful
decisions through the shared validator, and checks malformed inputs for the
expected failure text.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

SCRIPT = Path(__file__).resolve()
MODULE_ROOT = SCRIPT.parents[1]
RESOLVER_ROOT = SCRIPT.parents[3]
FIXTURES_DIR = MODULE_ROOT / "fixtures"
CASES_PATH = FIXTURES_DIR / "cases.json"
RESOLVER_PATH = RESOLVER_ROOT / "bin" / "transition_resolver_resolve.py"
VALIDATOR_PATH = RESOLVER_ROOT / "bin" / "transition_resolver_validate.py"


def load_cases() -> list[dict[str, Any]]:
    data = json.loads(CASES_PATH.read_text())
    if not isinstance(data, list):
        raise ValueError(f"Expected array in {CASES_PATH}")
    return data


def compare_expected(actual: dict[str, Any], expected: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    for key, expected_value in expected.items():
        actual_value = actual.get(key)
        if actual_value != expected_value:
            errors.append(f"Expected {key}={expected_value!r}; got {actual_value!r}")
    return errors


def run_case(case: dict[str, Any], python_executable: str) -> tuple[bool, list[str]]:
    case_id = case["id"]
    input_path = FIXTURES_DIR / case["input"]
    expect = case["expect"]
    mode = expect["mode"]

    proc = subprocess.run(
        [python_executable, str(RESOLVER_PATH), "--input", str(input_path)],
        text=True,
        capture_output=True,
    )
    combined_output = f"{proc.stdout}{proc.stderr}"
    errors: list[str] = []

    if mode == "error":
        if proc.returncode == 0:
            return False, [f"{case_id}: expected resolver failure but it succeeded"]
        for fragment in expect.get("contains", []):
            if fragment not in combined_output:
                errors.append(f"{case_id}: missing expected output fragment {fragment!r}")
        return not errors, errors

    if proc.returncode != 0:
        return False, [f"{case_id}: resolver failed unexpectedly\n{combined_output.strip()}"]

    try:
        resolved = json.loads(proc.stdout)
    except Exception as exc:
        return False, [f"{case_id}: could not parse resolver JSON output: {exc}"]

    errors.extend(f"{case_id}: {error}" for error in compare_expected(resolved, expect["fields"]))

    with tempfile.TemporaryDirectory() as temp_dir:
        decision_path = Path(temp_dir) / "decision.json"
        decision_path.write_text(json.dumps(resolved, ensure_ascii=True, indent=2) + "\n")
        validator = subprocess.run(
            [python_executable, str(VALIDATOR_PATH), "--input", str(decision_path)],
            text=True,
            capture_output=True,
        )
    if validator.returncode != 0:
        errors.append(
            f"{case_id}: validator rejected resolver output\n{validator.stdout}{validator.stderr}".rstrip()
        )

    return not errors, errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--filter",
        help="Only run cases whose id contains this substring",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List case ids without executing them",
    )
    args = parser.parse_args()

    cases = load_cases()
    if args.filter:
        cases = [case for case in cases if args.filter in case["id"]]

    if args.list:
        for case in cases:
            print(case["id"])
        return 0

    if not cases:
        print("No matching cases found.")
        return 1

    passed = 0
    failed = 0
    all_errors: list[str] = []

    for case in cases:
        ok, errors = run_case(case, sys.executable)
        if ok:
            passed += 1
            print(f"PASS {case['id']}")
            continue
        failed += 1
        print(f"FAIL {case['id']}")
        all_errors.extend(errors)

    print(f"\nSummary: {passed} passed, {failed} failed, {len(cases)} total")
    if all_errors:
        print("\nFailures:")
        for error in all_errors:
            print(f"- {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
