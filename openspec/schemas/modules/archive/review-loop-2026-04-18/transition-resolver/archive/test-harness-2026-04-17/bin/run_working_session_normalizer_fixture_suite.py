#!/usr/bin/env python3
"""
Run fixture cases for working-session normalization.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

SCRIPT = Path(__file__).resolve()
MODULE_ROOT = SCRIPT.parents[1]
RESOLVER_ROOT = SCRIPT.parents[3]
FIXTURES_DIR = MODULE_ROOT / "fixtures"
CASES_PATH = FIXTURES_DIR / "normalizer-cases.json"
NORMALIZER_PATH = RESOLVER_ROOT / "bin" / "normalize_working_session.py"


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
    proc = subprocess.run(
        [python_executable, str(NORMALIZER_PATH), "--input", str(input_path)],
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        return False, [f"{case_id}: normalizer failed unexpectedly\n{(proc.stdout + proc.stderr).strip()}"]
    try:
        resolved = json.loads(proc.stdout)
    except Exception as exc:
        return False, [f"{case_id}: could not parse normalizer JSON output: {exc}"]
    errors = [f"{case_id}: {error}" for error in compare_expected(resolved, expect["fields"])]
    return not errors, errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--filter", help="Only run cases whose id contains this substring")
    parser.add_argument("--list", action="store_true", help="List case ids without executing them")
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
