#!/usr/bin/env python3
"""
Run cross-caller transition-resolver sequence fixtures.

These scenarios model realistic multi-step orchestration chains rather than
isolated one-shot inputs.
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
SEQUENCES_PATH = FIXTURES_DIR / "sequences.json"
RESOLVER_PATH = RESOLVER_ROOT / "bin" / "transition_resolver_resolve.py"
VALIDATOR_PATH = RESOLVER_ROOT / "bin" / "transition_resolver_validate.py"


def load_sequences() -> list[dict[str, Any]]:
    data = json.loads(SEQUENCES_PATH.read_text())
    if not isinstance(data, list):
        raise ValueError(f"Expected array in {SEQUENCES_PATH}")
    return data


def compare_expected(actual: dict[str, Any], expected: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    for key, expected_value in expected.items():
        actual_value = actual.get(key)
        if actual_value != expected_value:
            errors.append(f"Expected {key}={expected_value!r}; got {actual_value!r}")
    return errors


def resolve_input(input_path: Path, python_executable: str) -> tuple[dict[str, Any] | None, list[str]]:
    proc = subprocess.run(
        [python_executable, str(RESOLVER_PATH), "--input", str(input_path)],
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        return None, [f"resolver failed for {input_path}\n{(proc.stdout + proc.stderr).strip()}"]
    try:
        resolved = json.loads(proc.stdout)
    except Exception as exc:
        return None, [f"could not parse resolver JSON output for {input_path}: {exc}"]

    with tempfile.TemporaryDirectory() as temp_dir:
        decision_path = Path(temp_dir) / "decision.json"
        decision_path.write_text(json.dumps(resolved, ensure_ascii=True, indent=2) + "\n")
        validator = subprocess.run(
            [python_executable, str(VALIDATOR_PATH), "--input", str(decision_path)],
            text=True,
            capture_output=True,
        )
    if validator.returncode != 0:
        return None, [f"validator rejected resolver output for {input_path}\n{validator.stdout}{validator.stderr}".rstrip()]
    return resolved, []


def run_sequence(sequence: dict[str, Any], python_executable: str) -> tuple[bool, list[str]]:
    errors: list[str] = []
    for index, step in enumerate(sequence["steps"], start=1):
        input_path = FIXTURES_DIR / step["input"]
        resolved, step_errors = resolve_input(input_path, python_executable)
        if step_errors:
            errors.extend(f"{sequence['id']} step {index}: {error}" for error in step_errors)
            continue
        assert resolved is not None
        expectation_errors = compare_expected(resolved, step["expect"])
        errors.extend(f"{sequence['id']} step {index}: {error}" for error in expectation_errors)
    return not errors, errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--filter", help="Only run sequences whose id contains this substring")
    parser.add_argument("--list", action="store_true", help="List sequence ids without executing them")
    args = parser.parse_args()

    sequences = load_sequences()
    if args.filter:
        sequences = [sequence for sequence in sequences if args.filter in sequence["id"]]

    if args.list:
        for sequence in sequences:
            print(sequence["id"])
        return 0

    if not sequences:
        print("No matching sequences found.")
        return 1

    passed = 0
    failed = 0
    all_errors: list[str] = []

    for sequence in sequences:
        ok, errors = run_sequence(sequence, sys.executable)
        if ok:
            passed += 1
            print(f"PASS {sequence['id']}")
            continue
        failed += 1
        print(f"FAIL {sequence['id']}")
        all_errors.extend(errors)

    print(f"\nSummary: {passed} passed, {failed} failed, {len(sequences)} total")
    if all_errors:
        print("\nFailures:")
        for error in all_errors:
            print(f"- {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
