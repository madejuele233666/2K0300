#!/usr/bin/env python3
"""Regression tests for delayed steering listener connections."""

from __future__ import annotations

import json
import socket
import struct
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
USER_DIR = REPO_ROOT / "user"
if str(USER_DIR) not in sys.path:
    sys.path.insert(0, str(USER_DIR))

from steering_media_capture import SteeringMediaListener  # noqa: E402
from tune_steering import PassiveAssistantListener  # noqa: E402


class TuneSteeringListenerTimeoutTest(unittest.TestCase):
    def _listener_port(self, listener: object) -> int:
        server = getattr(listener, "_server")
        self.assertIsNotNone(server)
        return int(server.getsockname()[1])

    def test_assistant_listener_accepts_connection_after_timeout(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            output_dir = Path(tmp_dir)
            listener = PassiveAssistantListener(
                "127.0.0.1",
                0,
                output_dir / "assistant_control.csv",
                accept_timeout_s=0.2,
            )
            listener.start()
            port = self._listener_port(listener)

            def delayed_client() -> None:
                time.sleep(0.45)
                with socket.create_connection(("127.0.0.1", port), timeout=5.0) as conn:
                    conn.sendall(
                        b'{"type":"telemetry","motion_phase":"RUNNING","active_module":"straight",'
                        b'"scene_phase":"idle","reference_mode":"centerline",'
                        b'"lookahead_distance_m":1.4,"curvature_command":0.02,'
                        b'"yaw_rate_target":0,"raw_turn_output":1.5,"applied_turn_output":1.25}\n'
                    )
                    time.sleep(0.05)

            thread = threading.Thread(target=delayed_client, daemon=True)
            thread.start()
            time.sleep(0.8)
            summary = listener.close()
            thread.join(timeout=1.0)

            self.assertTrue(summary["connected"])
            self.assertEqual(summary["telemetry_frames"], 1)
            self.assertIsNone(summary["receiver_error"])
            rows = (output_dir / "assistant_control.csv").read_text(encoding="utf-8")
            self.assertIn("lookahead_distance_m", rows)
            self.assertIn("curvature_command", rows)
            self.assertIn("reference_mode", rows)
            self.assertIn("0.02", rows)

    def test_media_listener_accepts_connection_after_timeout(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            output_dir = Path(tmp_dir)
            listener = SteeringMediaListener(
                "127.0.0.1",
                0,
                output_dir,
                accept_timeout_s=0.2,
            )
            listener.start()
            port = self._listener_port(listener)

            def delayed_client() -> None:
                time.sleep(0.45)
                header = json.dumps(
                    {
                        "type": "config_snapshot",
                        "media_publish_interval_ms": 80,
                        "param_snapshot": {"pid_turn_camera_d": 0.0},
                    }
                ).encode("utf-8")
                frame = struct.pack(">II", len(header), 0) + header
                with socket.create_connection(("127.0.0.1", port), timeout=5.0) as conn:
                    conn.sendall(frame)
                    time.sleep(0.05)

            thread = threading.Thread(target=delayed_client, daemon=True)
            thread.start()
            time.sleep(0.8)
            summary = listener.close()
            thread.join(timeout=1.0)

            self.assertTrue(summary["connected"])
            self.assertIsNone(summary["receiver_error"])
            self.assertIsNotNone(summary["config_snapshot_path"])


if __name__ == "__main__":
    unittest.main()
