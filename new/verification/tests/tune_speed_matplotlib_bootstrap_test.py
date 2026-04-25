#!/usr/bin/env python3
"""Regression tests for tune_speed matplotlib bootstrap behavior."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch


REPO_ROOT = Path(__file__).resolve().parents[2]
USER_DIR = REPO_ROOT / "user"
if str(USER_DIR) not in sys.path:
    sys.path.insert(0, str(USER_DIR))

import tune_speed  # noqa: E402


class TuneSpeedMatplotlibBootstrapTest(unittest.TestCase):
    def test_pip_install_args_use_current_virtualenv(self) -> None:
        with patch.object(tune_speed.sys, "executable", "/tmp/venv/bin/python"), \
             patch.object(tune_speed.sys, "prefix", "/tmp/venv"), \
             patch.object(tune_speed.sys, "base_prefix", "/usr"):
            self.assertEqual(
                tune_speed.pip_install_args_for_current_python(),
                ["/tmp/venv/bin/python", "-m", "pip", "install", "matplotlib"],
            )

    def test_pip_install_args_use_user_flag_outside_virtualenv(self) -> None:
        with patch.object(tune_speed.sys, "executable", "/usr/bin/python3"), \
             patch.object(tune_speed.sys, "prefix", "/usr"), \
             patch.object(tune_speed.sys, "base_prefix", "/usr"):
            self.assertEqual(
                tune_speed.pip_install_args_for_current_python(),
                ["/usr/bin/python3", "-m", "pip", "install", "--user", "matplotlib"],
            )

    def test_bootstrap_matplotlib_returns_none_when_disabled(self) -> None:
        with patch.object(tune_speed, "import_matplotlib_pyplot") as import_mock, \
             patch.object(tune_speed.subprocess, "run") as run_mock:
            self.assertIsNone(tune_speed.bootstrap_matplotlib(True))
            import_mock.assert_not_called()
            run_mock.assert_not_called()

    def test_bootstrap_matplotlib_installs_inside_virtualenv_after_import_failure(self) -> None:
        fake_pyplot = object()
        fake_install = SimpleNamespace(returncode=0, stdout="installed")
        with patch.object(tune_speed.sys, "executable", "/tmp/venv/bin/python"), \
             patch.object(tune_speed.sys, "prefix", "/tmp/venv"), \
             patch.object(tune_speed.sys, "base_prefix", "/usr"), \
             patch.object(
                 tune_speed,
                 "import_matplotlib_pyplot",
                 side_effect=[ModuleNotFoundError("No module named 'matplotlib'"), fake_pyplot],
             ) as import_mock, \
             patch.object(tune_speed.subprocess, "run", return_value=fake_install) as run_mock:
            pyplot, install_log = tune_speed.bootstrap_matplotlib(False)

        self.assertIs(pyplot, fake_pyplot)
        self.assertEqual(install_log, "installed")
        self.assertEqual(import_mock.call_count, 2)
        run_mock.assert_called_once_with(
            ["/tmp/venv/bin/python", "-m", "pip", "install", "matplotlib"],
            stdout=tune_speed.subprocess.PIPE,
            stderr=tune_speed.subprocess.STDOUT,
            text=True,
            check=False,
        )

    def test_bootstrap_matplotlib_uses_user_install_outside_virtualenv(self) -> None:
        fake_install = SimpleNamespace(returncode=1, stdout="install failed")
        with patch.object(tune_speed.sys, "executable", "/usr/bin/python3"), \
             patch.object(tune_speed.sys, "prefix", "/usr"), \
             patch.object(tune_speed.sys, "base_prefix", "/usr"), \
             patch.object(
                 tune_speed,
                 "import_matplotlib_pyplot",
                 side_effect=ModuleNotFoundError("No module named 'matplotlib'"),
             ), \
             patch.object(tune_speed.subprocess, "run", return_value=fake_install) as run_mock:
            self.assertIsNone(tune_speed.bootstrap_matplotlib(False))

        run_mock.assert_called_once_with(
            ["/usr/bin/python3", "-m", "pip", "install", "--user", "matplotlib"],
            stdout=tune_speed.subprocess.PIPE,
            stderr=tune_speed.subprocess.STDOUT,
            text=True,
            check=False,
        )


if __name__ == "__main__":
    unittest.main()
