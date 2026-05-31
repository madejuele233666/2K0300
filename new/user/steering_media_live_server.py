#!/usr/bin/env python3
"""Host-side read-only live web viewer for steering media frames."""

from __future__ import annotations

import base64
import hashlib
import http.server
import json
import re
import select
import socket
import socketserver
import struct
import threading
from typing import Any, Dict, Optional, Tuple

WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def _json_bytes(value: Dict[str, Any]) -> bytes:
    return json.dumps(value, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def _encode_media_envelope(header: Dict[str, Any], payload: bytes) -> bytes:
    header_bytes = _json_bytes(header)
    return (
        len(header_bytes).to_bytes(4, "big")
        + len(payload).to_bytes(4, "big")
        + header_bytes
        + payload
    )


def _encode_ws_frame(payload: bytes, *, opcode: int = 0x2) -> bytes:
    first = 0x80 | (opcode & 0x0F)
    length = len(payload)
    if length < 126:
        return bytes([first, length]) + payload
    if length <= 0xFFFF:
        return bytes([first, 126]) + struct.pack("!H", length) + payload
    return bytes([first, 127]) + struct.pack("!Q", length) + payload


def _viewer_html(display_mode: str = "bev") -> bytes:
    normalized_display_mode = "raw" if display_mode == "raw" else "bev"
    html = b"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Steering Media Live</title>
  <style>
    :root {
      color-scheme: light dark;
      --app-bg: #f5f5f7;
      --surface: rgba(255, 255, 255, 0.78);
      --surface-strong: rgba(255, 255, 255, 0.94);
      --text: #1d1d1f;
      --muted: #6e6e73;
      --hairline: rgba(60, 60, 67, 0.18);
      --accent: #0071e3;
      --blue: #007aff;
      --ok: #34c759;
      --warn: #ff9f0a;
      --danger: #ff3b30;
      --cyan: #32ade6;
      --purple: #af52de;
      --pink: #ff2d55;
      --yellow: #ffd60a;
      --surface-blue: rgba(0, 122, 255, 0.10);
      --surface-green: rgba(52, 199, 89, 0.10);
      --surface-orange: rgba(255, 159, 10, 0.12);
      --surface-purple: rgba(175, 82, 222, 0.11);
      --surface-red: rgba(255, 59, 48, 0.10);
      --shadow: 0 18px 45px rgba(0, 0, 0, 0.10);
      --shadow-soft: 0 7px 24px rgba(0, 0, 0, 0.08);
      --canvas-bg: #030303;
    }
    * { box-sizing: border-box; }
    html { min-height: 100%; }
    body {
      margin: 0;
      font-family: -apple-system, BlinkMacSystemFont, "SF Pro Text", "Segoe UI", system-ui, sans-serif;
      background:
        linear-gradient(180deg, rgba(0, 122, 255, 0.08), transparent 260px),
        linear-gradient(90deg, rgba(52, 199, 89, 0.06), rgba(255, 45, 85, 0.04), rgba(50, 173, 230, 0.06)),
        var(--app-bg);
      color: var(--text);
      letter-spacing: 0;
      -webkit-font-smoothing: antialiased;
      text-rendering: geometricPrecision;
    }
    main {
      display: grid;
      grid-template-columns: minmax(0, 1fr) minmax(344px, 408px);
      gap: 14px;
      min-height: 100vh;
      padding: 14px;
    }
    .workspace {
      display: grid;
      grid-template-rows: auto auto auto minmax(0, 1fr);
      gap: 12px;
      min-width: 0;
      min-height: calc(100vh - 28px);
    }
    .topbar,
    .viewer-panel,
    .metric-card,
    .control-strip,
    .panel {
      border: 1px solid var(--hairline);
      border-radius: 8px;
      background: var(--surface);
      box-shadow: var(--shadow);
      backdrop-filter: blur(22px) saturate(1.35);
      -webkit-backdrop-filter: blur(22px) saturate(1.35);
    }
    .topbar {
      display: grid;
      grid-template-columns: auto minmax(0, 1fr) auto;
      align-items: center;
      gap: 14px;
      min-width: 0;
      padding: 12px 14px;
      background:
        linear-gradient(90deg, rgba(255, 255, 255, 0.82), rgba(255, 255, 255, 0.64)),
        var(--surface);
    }
    .traffic-lights {
      display: flex;
      align-items: center;
      gap: 7px;
    }
    .traffic-lights span {
      width: 12px;
      height: 12px;
      border-radius: 50%;
      box-shadow: inset 0 0 0 1px rgba(0, 0, 0, 0.12);
    }
    .traffic-lights span:nth-child(1) { background: #ff5f57; }
    .traffic-lights span:nth-child(2) { background: #febc2e; }
    .traffic-lights span:nth-child(3) { background: #28c840; }
    .title-block {
      min-width: 0;
      text-align: center;
    }
    .eyebrow {
      margin: 0 0 3px;
      color: var(--muted);
      font-size: 12px;
      line-height: 1.2;
      font-weight: 650;
    }
    h1 {
      color: var(--text);
      font-size: 24px;
      line-height: 1.08;
      margin: 0;
      font-weight: 720;
    }
    .connection {
      display: flex;
      align-items: center;
      justify-content: flex-end;
      flex-wrap: wrap;
      gap: 8px;
      min-width: 0;
    }
    .status-pill {
      display: inline-flex;
      align-items: center;
      gap: 7px;
      min-height: 28px;
      max-width: 100%;
      padding: 5px 9px;
      border: 1px solid var(--hairline);
      border-radius: 8px;
      background: var(--surface-strong);
      color: var(--text);
      font-size: 13px;
      line-height: 1.2;
      font-weight: 650;
      font-variant-numeric: tabular-nums;
      box-shadow: 0 1px 0 rgba(255, 255, 255, 0.65) inset;
    }
    .status-dot {
      width: 8px;
      height: 8px;
      flex: 0 0 8px;
      border-radius: 50%;
      background: var(--warn);
      box-shadow: 0 0 0 3px rgba(255, 159, 10, 0.16);
    }
    body[data-status-tone="ok"] .status-dot {
      background: var(--ok);
      box-shadow: 0 0 0 3px rgba(52, 199, 89, 0.16);
    }
    body[data-status-tone="error"] .status-dot {
      background: var(--danger);
      box-shadow: 0 0 0 3px rgba(255, 59, 48, 0.16);
    }
    body[data-status-tone="ok"] .status-pill:first-child {
      background: linear-gradient(180deg, var(--surface-green), var(--surface-strong));
    }
    body[data-status-tone="warn"] .status-pill:first-child {
      background: linear-gradient(180deg, var(--surface-orange), var(--surface-strong));
    }
    body[data-status-tone="error"] .status-pill:first-child {
      background: linear-gradient(180deg, var(--surface-red), var(--surface-strong));
    }
    .viewer-panel {
      min-width: 0;
      overflow: hidden;
      background: var(--surface-strong);
      position: relative;
    }
    .viewer-panel::before {
      content: "";
      display: block;
      height: 4px;
      background: linear-gradient(90deg, var(--blue), var(--cyan), var(--ok), var(--yellow), var(--warn), var(--pink), var(--purple));
    }
    .viewer-caption {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 1px;
      border-bottom: 1px solid var(--hairline);
      background: var(--hairline);
    }
    .viewer-caption div {
      min-width: 0;
      padding: 9px 11px;
      background: var(--surface-strong);
    }
    .metric-label,
    .viewer-caption span {
      display: block;
      color: var(--muted);
      font-size: 11px;
      line-height: 1.2;
      font-weight: 650;
    }
    .metric-value,
    .viewer-caption strong {
      display: block;
      margin-top: 3px;
      color: var(--text);
      font-size: 13px;
      line-height: 1.25;
      font-weight: 650;
      font-variant-numeric: tabular-nums;
      overflow-wrap: anywhere;
    }
    canvas {
      width: 100%;
      height: auto;
      display: block;
      image-rendering: pixelated;
      background: var(--canvas-bg);
      box-shadow: 0 1px 0 rgba(255, 255, 255, 0.08) inset;
    }
    .control-strip {
      display: grid;
      grid-template-columns: repeat(4, minmax(0, 1fr));
      gap: 1px;
      overflow: hidden;
      background: var(--hairline);
      box-shadow: var(--shadow-soft);
    }
    .control-chip {
      min-width: 0;
      padding: 11px 12px;
      background: var(--surface-strong);
      border-left: 4px solid var(--blue);
    }
    .control-chip:nth-child(2) { border-left-color: var(--ok); }
    .control-chip:nth-child(3) { border-left-color: var(--warn); }
    .control-chip:nth-child(4) { border-left-color: var(--purple); }
    .control-chip:nth-child(1) .metric-value { color: var(--blue); }
    .control-chip:nth-child(2) .metric-value { color: var(--ok); }
    .control-chip:nth-child(3) .metric-value { color: var(--warn); }
    .control-chip:nth-child(4) .metric-value { color: var(--purple); }
    .metric-grid {
      display: grid;
      grid-template-columns: repeat(4, minmax(0, 1fr));
      gap: 10px;
      min-width: 0;
      align-self: start;
    }
    .metric-card {
      min-width: 0;
      padding: 12px;
      position: relative;
      overflow: hidden;
      box-shadow: var(--shadow-soft);
    }
    .metric-card::before,
    .panel::before {
      content: "";
      position: absolute;
      top: 0;
      left: 0;
      right: 0;
      height: 3px;
      background: var(--blue);
    }
    .metric-card:nth-child(1)::before { background: var(--ok); }
    .metric-card:nth-child(2)::before { background: var(--cyan); }
    .metric-card:nth-child(3)::before { background: var(--warn); }
    .metric-card:nth-child(4)::before { background: var(--purple); }
    .metric-card:nth-child(1) { background: linear-gradient(180deg, var(--surface-green), var(--surface-strong)); }
    .metric-card:nth-child(2) { background: linear-gradient(180deg, var(--surface-blue), var(--surface-strong)); }
    .metric-card:nth-child(3) { background: linear-gradient(180deg, var(--surface-orange), var(--surface-strong)); }
    .metric-card:nth-child(4) { background: linear-gradient(180deg, var(--surface-purple), var(--surface-strong)); }
    .metric-card .metric-value {
      font-size: 15px;
    }
    .metric-card:nth-child(1) .metric-value { color: var(--ok); }
    .metric-card:nth-child(2) .metric-value { color: var(--cyan); }
    .metric-card:nth-child(3) .metric-value { color: var(--warn); }
    .metric-card:nth-child(4) .metric-value { color: var(--purple); }
    h2 {
      color: var(--muted);
      font-size: 11px;
      line-height: 1.2;
      margin: 0 0 8px;
      text-transform: uppercase;
      font-weight: 700;
    }
    aside {
      display: flex;
      flex-direction: column;
      gap: 10px;
      min-width: 0;
      max-height: calc(100vh - 28px);
      overflow-y: auto;
      padding-right: 2px;
    }
    .panel {
      padding: 12px;
      background: var(--surface-strong);
      box-shadow: none;
      position: relative;
      overflow: hidden;
    }
    .panel:nth-child(1)::before { background: var(--purple); }
    .panel:nth-child(2)::before { background: var(--danger); }
    .panel:nth-child(3)::before { background: var(--blue); }
    .panel:nth-child(4)::before { background: var(--warn); }
    .panel:nth-child(5)::before { background: var(--cyan); }
    .panel:nth-child(1) h2 { color: var(--purple); }
    .panel:nth-child(2) h2 { color: var(--danger); }
    .panel:nth-child(3) h2 { color: var(--blue); }
    .panel:nth-child(4) h2 { color: var(--warn); }
    .panel:nth-child(5) h2 { color: var(--cyan); }
    .panel h2 {
      display: flex;
      align-items: center;
      gap: 7px;
    }
    .panel h2::before {
      content: "";
      width: 7px;
      height: 7px;
      border-radius: 50%;
      background: currentColor;
    }
    dl {
      display: grid;
      grid-template-columns: minmax(92px, 0.42fr) minmax(0, 1fr);
      gap: 7px 10px;
      margin: 0;
      font-size: 13px;
      line-height: 1.28;
    }
    dt {
      color: var(--muted);
      font-weight: 520;
    }
    dd {
      margin: 0;
      color: var(--text);
      overflow-wrap: anywhere;
      font-variant-numeric: tabular-nums;
    }
    #status,
    #displayMode,
    #displayFps {
      color: var(--accent);
      font-weight: 650;
    }
    #gate,
    #referenceControl {
      font-weight: 650;
    }
    body[data-gate-tone="ok"] #gate,
    body[data-reference-tone="ok"] #referenceControl {
      color: var(--ok);
    }
    body[data-gate-tone="warn"] #gate,
    body[data-reference-tone="warn"] #referenceControl {
      color: var(--warn);
    }
    body[data-gate-tone="error"] #gate,
    body[data-reference-tone="error"] #referenceControl {
      color: var(--danger);
    }
    @media (max-width: 1080px) {
      main {
        grid-template-columns: 1fr;
        min-height: 100vh;
      }
      .workspace { min-height: 0; }
      aside {
        max-height: none;
        display: grid;
        grid-template-columns: repeat(2, minmax(0, 1fr));
      }
    }
    @media (max-width: 760px) {
      main { padding: 10px; }
      .topbar {
        align-items: flex-start;
        grid-template-columns: 1fr;
        text-align: left;
      }
      .title-block { text-align: left; }
      .connection { justify-content: flex-start; }
      .viewer-caption,
      .control-strip,
      .metric-grid,
      aside {
        grid-template-columns: 1fr;
      }
    }
    @media (prefers-color-scheme: dark) {
      :root {
        --app-bg: #111113;
        --surface: rgba(28, 28, 30, 0.78);
        --surface-strong: rgba(44, 44, 46, 0.92);
        --text: #f5f5f7;
        --muted: #a1a1a6;
        --hairline: rgba(255, 255, 255, 0.14);
        --accent: #0a84ff;
        --shadow: 0 18px 45px rgba(0, 0, 0, 0.30);
        --shadow-soft: 0 7px 24px rgba(0, 0, 0, 0.22);
        --surface-blue: rgba(10, 132, 255, 0.18);
        --surface-green: rgba(48, 209, 88, 0.16);
        --surface-orange: rgba(255, 159, 10, 0.18);
        --surface-purple: rgba(191, 90, 242, 0.16);
        --surface-red: rgba(255, 69, 58, 0.16);
      }
      .topbar {
        background:
          linear-gradient(90deg, rgba(44, 44, 46, 0.88), rgba(28, 28, 30, 0.74)),
          var(--surface);
      }
    }
  </style>
</head>
<body>
<main>
  <section class="workspace">
    <header class="topbar">
      <div class="traffic-lights" aria-hidden="true"><span></span><span></span><span></span></div>
      <div class="title-block">
        <p class="eyebrow">Live Viewer</p>
        <h1>Steering Media</h1>
      </div>
      <div class="connection">
        <span class="status-pill"><span class="status-dot" id="statusDot"></span><span id="status">connecting</span></span>
        <span class="status-pill" id="displayMode">BEV</span>
      </div>
    </header>
    <section class="control-strip">
      <div class="control-chip"><span class="metric-label">Transport</span><strong class="metric-value" id="transportSummary">-</strong></div>
      <div class="control-chip"><span class="metric-label">Reference</span><strong class="metric-value" id="referenceSummary">-</strong></div>
      <div class="control-chip"><span class="metric-label">Safety</span><strong class="metric-value" id="safetySummary">-</strong></div>
      <div class="control-chip"><span class="metric-label">Camera</span><strong class="metric-value" id="cameraSummary">-</strong></div>
    </section>
    <section class="viewer-panel">
      <div class="viewer-caption">
        <div><span>Frame</span><strong id="frameId">-</strong></div>
        <div><span>Size</span><strong id="size">-</strong></div>
        <div><span>Source</span><strong id="source">-</strong></div>
      </div>
      <canvas id="frame"></canvas>
    </section>
    <section class="metric-grid">
      <article class="metric-card"><span class="metric-label">Display FPS</span><strong class="metric-value" id="displayFps">-</strong></article>
      <article class="metric-card"><span class="metric-label">Times</span><strong class="metric-value" id="latency">-</strong></article>
      <article class="metric-card"><span class="metric-label">Motion</span><strong class="metric-value" id="motionFsm">-</strong></article>
      <article class="metric-card"><span class="metric-label">Turn</span><strong class="metric-value" id="turn">-</strong></article>
    </section>
  </section>
  <aside>
    <section class="panel">
      <h2>FSM</h2>
      <dl>
        <dt>Circle</dt><dd id="circleFsm">-</dd>
        <dt>Circle reason</dt><dd id="circleReason">-</dd>
      </dl>
    </section>
    <section class="panel">
      <h2>Safety</h2>
      <dl>
        <dt>Gate</dt><dd id="gate">-</dd>
        <dt>Reference ctl</dt><dd id="referenceControl">-</dd>
        <dt>Degraded</dt><dd id="degraded">-</dd>
      </dl>
    </section>
    <section class="panel">
      <h2>Reference</h2>
      <dl>
        <dt>Reference</dt><dd id="reference">-</dd>
        <dt>Visual ref</dt><dd id="visualReference">-</dd>
        <dt>Eligibility</dt><dd id="eligibility">-</dd>
        <dt>Lateral error</dt><dd id="lateralError">-</dd>
      </dl>
    </section>
    <section class="panel">
      <h2>Control</h2>
      <dl>
        <dt>Actuator</dt><dd id="actuator">-</dd>
        <dt>Threshold</dt><dd id="threshold">-</dd>
      </dl>
    </section>
    <section class="panel">
      <h2>Camera</h2>
      <dl>
        <dt>Frame source</dt><dd id="cameraSource">-</dd>
        <dt>V4L2 seq</dt><dd id="v4l2">-</dd>
        <dt>Timing</dt><dd id="cameraTiming">-</dd>
        <dt>Buffers</dt><dd id="buffers">-</dd>
        <dt>Pixel stats</dt><dd id="pixelStats">-</dd>
      </dl>
    </section>
  </aside>
</main>
<script>
const canvas = document.getElementById("frame");
const ctx = canvas.getContext("2d");
const statusDot = document.getElementById("statusDot");
const fields = {
  status: document.getElementById("status"),
  transportSummary: document.getElementById("transportSummary"),
  referenceSummary: document.getElementById("referenceSummary"),
  safetySummary: document.getElementById("safetySummary"),
  cameraSummary: document.getElementById("cameraSummary"),
  displayFps: document.getElementById("displayFps"),
  latency: document.getElementById("latency"),
  frameId: document.getElementById("frameId"),
  size: document.getElementById("size"),
  source: document.getElementById("source"),
  displayMode: document.getElementById("displayMode"),
  motionFsm: document.getElementById("motionFsm"),
  circleFsm: document.getElementById("circleFsm"),
  circleReason: document.getElementById("circleReason"),
  reference: document.getElementById("reference"),
  gate: document.getElementById("gate"),
  referenceControl: document.getElementById("referenceControl"),
  degraded: document.getElementById("degraded"),
  visualReference: document.getElementById("visualReference"),
  eligibility: document.getElementById("eligibility"),
  lateralError: document.getElementById("lateralError"),
  turn: document.getElementById("turn"),
  actuator: document.getElementById("actuator"),
  threshold: document.getElementById("threshold"),
  cameraSource: document.getElementById("cameraSource"),
  v4l2: document.getElementById("v4l2"),
  cameraTiming: document.getElementById("cameraTiming"),
  buffers: document.getElementById("buffers"),
  pixelStats: document.getElementById("pixelStats"),
};
function updateStatusTone() {
  const status = fields.status.textContent || "";
  if (status === "websocket" || status === "connected" || status === "polling" || status === "configured" || status === "config") {
    document.body.dataset.statusTone = "ok";
  } else if (status === "error") {
    document.body.dataset.statusTone = "error";
  } else {
    document.body.dataset.statusTone = "warn";
  }
  statusDot.title = status;
}
new MutationObserver(updateStatusTone).observe(fields.status, { childList: true, characterData: true, subtree: true });
updateStatusTone();
function nested(obj, path, fallback = "-") {
  let value = obj;
  for (const key of path) {
    if (!value || typeof value !== "object" || !(key in value)) return fallback;
    value = value[key];
  }
  return value ?? fallback;
}
function formatBool(value) {
  if (value === true) return "true";
  if (value === false) return "false";
  return "-";
}
function formatNumber(value, digits = 3) {
  return typeof value === "number" && Number.isFinite(value) ? value.toFixed(digits) : "-";
}
function formatInt(value) {
  return typeof value === "number" && Number.isFinite(value) ? String(Math.round(value)) : "-";
}
function formatUs(value) {
  return typeof value === "number" && Number.isFinite(value) ? `${Math.round(value)}us` : "-";
}
function setGateTone(vetoActive) {
  document.body.dataset.gateTone = vetoActive === true ? "error" : vetoActive === false ? "ok" : "warn";
}
function setReferenceTone(ready) {
  document.body.dataset.referenceTone = ready === true ? "ok" : ready === false ? "warn" : "warn";
}
function numberOrNull(value) {
  return typeof value === "number" && Number.isFinite(value) ? value : null;
}
function solveLinearSystem8(matrix) {
  for (let col = 0; col < 8; col++) {
    let pivot = col;
    let pivotAbs = Math.abs(matrix[pivot][col]);
    for (let row = col + 1; row < 8; row++) {
      const candidateAbs = Math.abs(matrix[row][col]);
      if (candidateAbs > pivotAbs) {
        pivot = row;
        pivotAbs = candidateAbs;
      }
    }
    if (pivotAbs < 1.0e-9) return null;
    if (pivot !== col) {
      const tmp = matrix[pivot];
      matrix[pivot] = matrix[col];
      matrix[col] = tmp;
    }
    const pivotValue = matrix[col][col];
    for (let current = col; current < 9; current++) matrix[col][current] /= pivotValue;
    for (let row = 0; row < 8; row++) {
      if (row === col) continue;
      const factor = matrix[row][col];
      if (Math.abs(factor) < 1.0e-9) continue;
      for (let current = col; current < 9; current++) {
        matrix[row][current] -= factor * matrix[col][current];
      }
    }
  }
  return matrix.map((row) => row[8]).concat([1.0]);
}
function buildHomography(src, dst) {
  if (!Array.isArray(src) || !Array.isArray(dst) || src.length !== 4 || dst.length !== 4) return null;
  const matrix = Array.from({ length: 8 }, () => Array(9).fill(0));
  for (let i = 0; i < 4; i++) {
    const x = src[i].x;
    const y = src[i].y;
    const u = dst[i].x;
    const v = dst[i].y;
    if (![x, y, u, v].every(Number.isFinite)) return null;
    const row = i * 2;
    matrix[row][0] = x;
    matrix[row][1] = y;
    matrix[row][2] = 1.0;
    matrix[row][6] = -u * x;
    matrix[row][7] = -u * y;
    matrix[row][8] = u;
    matrix[row + 1][3] = x;
    matrix[row + 1][4] = y;
    matrix[row + 1][5] = 1.0;
    matrix[row + 1][6] = -v * x;
    matrix[row + 1][7] = -v * y;
    matrix[row + 1][8] = v;
  }
  return solveLinearSystem8(matrix);
}
function applyHomography(homography, x, y) {
  if (!homography || homography.length !== 9) return null;
  const denom = homography[6] * x + homography[7] * y + homography[8];
  if (Math.abs(denom) < 1.0e-9) return null;
  const out = {
    x: (homography[0] * x + homography[1] * y + homography[2]) / denom,
    y: (homography[3] * x + homography[4] * y + homography[5]) / denom,
  };
  return Number.isFinite(out.x) && Number.isFinite(out.y) ? out : null;
}
function buildBevProjection(config) {
  const projector = config?.BEV_PROJECTOR;
  if (!projector || projector.VALID === false) return { valid: false, reason: "projector invalid" };
  const imagePoints = [];
  const bevPoints = [];
  for (let i = 0; i < 4; i++) {
    const row = numberOrNull(projector[`SOURCE_ROW_${i}`]);
    const col = numberOrNull(projector[`SOURCE_COL_${i}`]);
    const forward = numberOrNull(projector[`TARGET_FORWARD_${i}`]);
    const lateral = numberOrNull(projector[`TARGET_LATERAL_${i}`]);
    if ([row, col, forward, lateral].some((value) => value == null)) {
      return { valid: false, reason: "projector points missing" };
    }
    imagePoints.push({ x: col, y: row });
    bevPoints.push({ x: lateral, y: forward });
  }
  const bevToImage = buildHomography(bevPoints, imagePoints);
  const imageToBev = buildHomography(imagePoints, bevPoints);
  if (!bevToImage || !imageToBev) return { valid: false, reason: "homography failed" };
  return {
    valid: true,
    reason: projector.PROJECTOR_ID ?? "projector",
    imagePoints,
    imageToBev,
    bevToImage,
  };
}
function forwardSamplesFromConfig(config) {
  const geometry = config?.BEV_GEOMETRY;
  const samples = [];
  if (!geometry) return samples;
  for (let i = 0; i < 24; i++) {
    const value = numberOrNull(geometry[`FORWARD_SAMPLE_${i}`]);
    if (value != null) samples.push(value);
  }
  return samples;
}
function packedGrayBits(header) {
  const encoding = header.payload_encoding ?? "";
  const format = header.pixel_format ?? "";
  if (encoding === "gray1_packed" || format === "gray1") return 1;
  if (encoding === "gray2_packed" || format === "gray2") return 2;
  if (encoding === "gray4_packed" || format === "gray4") return 4;
  return null;
}
function putGrayImage(width, height, grayPixels) {
  if (canvas.width !== width || canvas.height !== height || !putGrayImage.imageData) {
    canvas.width = width;
    canvas.height = height;
    putGrayImage.imageData = ctx.createImageData(width, height);
  }
  const image = putGrayImage.imageData;
  if (image.width !== width || image.height !== height) {
    putGrayImage.imageData = ctx.createImageData(width, height);
    return putGrayImage(width, height, grayPixels);
  }
  for (let i = 0, j = 0; i < grayPixels.length; i++, j += 4) {
    const gray = grayPixels[i];
    image.data[j] = gray;
    image.data[j + 1] = gray;
    image.data[j + 2] = gray;
    image.data[j + 3] = 255;
  }
  ctx.putImageData(image, 0, 0);
}
function imageStats(grayPixels) {
  let pixelMin = 255;
  let pixelMax = 0;
  let pixelSum = 0;
  for (const gray of grayPixels) {
    if (gray < pixelMin) pixelMin = gray;
    if (gray > pixelMax) pixelMax = gray;
    pixelSum += gray;
  }
  return { min: pixelMin, max: pixelMax, mean: pixelSum / Math.max(1, grayPixels.length) };
}
function decodeGray(header, payload) {
  const width = header.width | 0;
  const height = header.height | 0;
  const pixelCount = width * height;
  const bits = packedGrayBits(header);
  if (width <= 0 || height <= 0) return null;
  if (bits != null) {
    if (payload.length !== Math.ceil((pixelCount * bits) / 8)) return null;
  } else if (payload.length !== pixelCount) {
    return null;
  }
  const grayPixels = new Uint8ClampedArray(pixelCount);
  const maxLevel = bits == null ? 0 : (1 << bits) - 1;
  for (let i = 0; i < pixelCount; i++) {
    let gray;
    if (bits != null) {
      const bitIndex = i * bits;
      const packed = payload[bitIndex >> 3];
      const shift = 8 - bits - (bitIndex & 7);
      const level = (packed >> shift) & maxLevel;
      gray = Math.round((level * 255) / Math.max(1, maxLevel));
    } else {
      gray = payload[i];
    }
    grayPixels[i] = gray;
  }
  return { width, height, pixels: grayPixels, stats: imageStats(grayPixels) };
}
function sampleDecodedBilinear(decoded, col, row) {
  if (!decoded || !Number.isFinite(col) || !Number.isFinite(row)) return 0;
  if (col < 0 || row < 0 || col > decoded.width - 1 || row > decoded.height - 1) return 0;
  const x0 = Math.floor(col);
  const y0 = Math.floor(row);
  const x1 = Math.min(decoded.width - 1, x0 + 1);
  const y1 = Math.min(decoded.height - 1, y0 + 1);
  const tx = col - x0;
  const ty = row - y0;
  const row0 = y0 * decoded.width;
  const row1 = y1 * decoded.width;
  const g00 = decoded.pixels[row0 + x0];
  const g10 = decoded.pixels[row0 + x1];
  const g01 = decoded.pixels[row1 + x0];
  const g11 = decoded.pixels[row1 + x1];
  const top = g00 * (1 - tx) + g10 * tx;
  const bottom = g01 * (1 - tx) + g11 * tx;
  return Math.round(top * (1 - ty) + bottom * ty);
}
function isImagePointInside(point, sourceWidth, sourceHeight) {
  return point &&
    point.x >= 0 &&
    point.y >= 0 &&
    point.x <= sourceWidth - 1 &&
    point.y <= sourceHeight - 1;
}
function visibleLateralRangeAtForward(projection, forwardM, sourceWidth, sourceHeight, lateralScanLimit) {
  let minLat = null;
  let maxLat = null;
  const steps = 1200;
  for (let index = 0; index <= steps; index++) {
    const lateralM = -lateralScanLimit + (2.0 * lateralScanLimit * index) / steps;
    const imagePoint = applyHomography(projection.bevToImage, lateralM, forwardM);
    if (!isImagePointInside(imagePoint, sourceWidth, sourceHeight)) continue;
    minLat = minLat == null ? lateralM : Math.min(minLat, lateralM);
    maxLat = maxLat == null ? lateralM : Math.max(maxLat, lateralM);
  }
  return minLat == null || maxLat == null ? null : { minLat, maxLat };
}
function visibleBevBounds(config, projection, sourceWidth, sourceHeight) {
  const projector = config?.BEV_PROJECTOR || {};
  const geometry = config?.BEV_GEOMETRY || {};
  const samples = forwardSamplesFromConfig(config);
  const searchLimit = Math.max(0.1, numberOrNull(geometry.SEARCH_LATERAL_LIMIT_M) ?? 1.6);
  const forwardMin = samples.length > 0 ? samples[0] : 0.061;
  const forwardMax = Math.max(forwardMin + 0.1, samples.length > 0 ? samples[samples.length - 1] : 1.5);
  const lateralScanLimit = Math.max(2.0, searchLimit * 4.0);
  let lateralMin = null;
  let lateralMax = null;
  const slices = 64;
  for (let index = 0; index <= slices; index++) {
    const forwardM = forwardMin + ((forwardMax - forwardMin) * index) / slices;
    const range = visibleLateralRangeAtForward(
      projection,
      forwardM,
      sourceWidth,
      sourceHeight,
      lateralScanLimit,
    );
    if (!range) continue;
    lateralMin = lateralMin == null ? range.minLat : Math.min(lateralMin, range.minLat);
    lateralMax = lateralMax == null ? range.maxLat : Math.max(lateralMax, range.maxLat);
  }
  if (lateralMin == null || lateralMax == null || lateralMax <= lateralMin) {
    lateralMin = -searchLimit;
    lateralMax = searchLimit;
  }
  const lateralPadding = Math.max(0.02, (lateralMax - lateralMin) * 0.025);
  return {
    lateralMin: lateralMin - lateralPadding,
    lateralMax: lateralMax + lateralPadding,
    forwardMin,
    forwardMax,
    projector,
  };
}
function bevDisplayGeometry(config, projection, sourceWidth, sourceHeight) {
  const bounds = visibleBevBounds(config, projection, sourceWidth, sourceHeight);
  const lateralSpan = Math.max(0.1, bounds.lateralMax - bounds.lateralMin);
  const forwardSpan = Math.max(0.1, bounds.forwardMax - bounds.forwardMin);
  const projector = bounds.projector || {};
  const baseWidth = Math.round(Math.max(2, numberOrNull(projector.DEBUG_GRID_WIDTH) ?? 160) * 2);
  const width = Math.max(320, Math.min(960, baseWidth * 2));
  const scalePxPerM = width / Math.max(1.0e-4, lateralSpan);
  const height = Math.max(96, Math.min(720, Math.round(forwardSpan * scalePxPerM)));
  return { width, height, ...bounds };
}
function renderRawFrame(decoded) {
  putGrayImage(decoded.width, decoded.height, decoded.pixels);
  return {
    ...decoded.stats,
    display: `raw ${decoded.width}x${decoded.height}`,
    overlaySpace: "raw",
    width: decoded.width,
    height: decoded.height,
  };
}
function renderBevFrame(header, decoded) {
  if (!runtimeConfig) return null;
  if (!bevProjection) bevProjection = buildBevProjection(runtimeConfig);
  if (!bevProjection.valid) return null;
  const sourceWidth = Math.max(1, numberOrNull(header.source_width) ?? decoded.width);
  const sourceHeight = Math.max(1, numberOrNull(header.source_height) ?? decoded.height);
  const geometry = bevDisplayGeometry(runtimeConfig, bevProjection, sourceWidth, sourceHeight);
  const output = new Uint8ClampedArray(geometry.width * geometry.height);
  const xScale = decoded.width / sourceWidth;
  const yScale = decoded.height / sourceHeight;
  for (let y = 0; y < geometry.height; y++) {
    const normalizedY = geometry.height > 1 ? y / (geometry.height - 1) : 1.0;
    const forwardM = geometry.forwardMax - normalizedY * (geometry.forwardMax - geometry.forwardMin);
    for (let x = 0; x < geometry.width; x++) {
      const normalizedX = geometry.width > 1 ? x / (geometry.width - 1) : 0.5;
      const lateralM = geometry.lateralMin + normalizedX * (geometry.lateralMax - geometry.lateralMin);
      const imagePoint = applyHomography(bevProjection.bevToImage, lateralM, forwardM);
      output[y * geometry.width + x] = imagePoint
        ? sampleDecodedBilinear(decoded, imagePoint.x * xScale, imagePoint.y * yScale)
        : 0;
    }
  }
  putGrayImage(geometry.width, geometry.height, output);
  const stats = imageStats(output);
  return {
    ...stats,
    display: `bev ${geometry.width}x${geometry.height} visible`,
    overlaySpace: "bev",
    geometry,
  };
}
function renderGray(header, payload) {
  const decoded = decodeGray(header, payload);
  if (!decoded) return null;
  if (initialDisplayMode === "bev") {
    const bev = renderBevFrame(header, decoded);
    if (bev) return bev;
    const raw = renderRawFrame(decoded);
    return { ...raw, display: `${raw.display} fallback` };
  }
  return renderRawFrame(decoded);
}
function pathCandidateItems(header) {
  const items = nested(header, ["steering_snapshot", "visual_reference", "path_candidates", "items"], []);
  return Array.isArray(items) ? items : [];
}
function candidateColor(kind, index) {
  const palette = {
    line: "#20c5ff",
    cross_exit: "#ffb000",
    circle_left: "#ff4fd8",
    circle_right: "#8b5cf6",
    roadblock_bypass: "#ff4f5e",
    ml_grounded: "#34d399",
  };
  const fallback = ["#20c5ff", "#ffb000", "#34d399", "#ff4f5e", "#8b5cf6"];
  return palette[kind] || fallback[index % fallback.length];
}
function finiteSamplePoint(sample) {
  const forwardM = numberOrNull(sample?.forward_m);
  const lateralM = numberOrNull(sample?.lateral_m);
  return forwardM == null || lateralM == null ? null : { forwardM, lateralM };
}
function mapBevSampleToCanvas(point, renderInfo, header) {
  if (renderInfo?.overlaySpace === "bev") {
    const geometry = renderInfo.geometry;
    if (!geometry) return null;
    const lateralSpan = geometry.lateralMax - geometry.lateralMin;
    const forwardSpan = geometry.forwardMax - geometry.forwardMin;
    if (lateralSpan <= 0 || forwardSpan <= 0) return null;
    return {
      x: ((point.lateralM - geometry.lateralMin) / lateralSpan) * (geometry.width - 1),
      y: ((geometry.forwardMax - point.forwardM) / forwardSpan) * (geometry.height - 1),
    };
  }
  if (renderInfo?.overlaySpace === "raw" && bevProjection?.valid) {
    const imagePoint = applyHomography(bevProjection.bevToImage, point.lateralM, point.forwardM);
    if (!imagePoint) return null;
    const sourceWidth = Math.max(1, numberOrNull(header.source_width) ?? renderInfo.width);
    const sourceHeight = Math.max(1, numberOrNull(header.source_height) ?? renderInfo.height);
    return {
      x: imagePoint.x * (renderInfo.width / sourceWidth),
      y: imagePoint.y * (renderInfo.height / sourceHeight),
    };
  }
  return null;
}
function visibleCanvasPoint(point) {
  const margin = 8;
  return point &&
    point.x >= -margin &&
    point.y >= -margin &&
    point.x <= canvas.width + margin &&
    point.y <= canvas.height + margin;
}
function drawPathCandidateOverlay(header, renderInfo) {
  const items = pathCandidateItems(header);
  if (!items.length || !renderInfo) return;
  ctx.save();
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  items.forEach((candidate, index) => {
    const points = (Array.isArray(candidate.samples) ? candidate.samples : [])
      .map(finiteSamplePoint)
      .filter(Boolean)
      .map((point) => mapBevSampleToCanvas(point, renderInfo, header))
      .filter(visibleCanvasPoint);
    if (!points.length) return;
    const color = candidateColor(candidate.kind, index);
    if (points.length >= 2) {
      ctx.beginPath();
      points.forEach((point, pointIndex) => {
        if (pointIndex === 0) {
          ctx.moveTo(point.x, point.y);
        } else {
          ctx.lineTo(point.x, point.y);
        }
      });
      ctx.strokeStyle = "rgba(0, 0, 0, 0.72)";
      ctx.lineWidth = 7;
      ctx.stroke();
      ctx.strokeStyle = color;
      ctx.lineWidth = index === 0 ? 3.2 : 2.4;
      ctx.stroke();
    }
    points.forEach((point) => {
      ctx.beginPath();
      ctx.arc(point.x, point.y, index === 0 ? 3.6 : 3.0, 0, Math.PI * 2);
      ctx.fillStyle = "rgba(0, 0, 0, 0.78)";
      ctx.fill();
      ctx.beginPath();
      ctx.arc(point.x, point.y, index === 0 ? 2.5 : 2.0, 0, Math.PI * 2);
      ctx.fillStyle = color;
      ctx.fill();
    });
  });
  ctx.restore();
}
let latestFrameId = null;
let latestRenderKey = null;
let latestSequence = 0;
let lastRenderMs = 0;
let smoothedFps = null;
let websocketOpen = false;
let lastWebSocketMessageMs = 0;
let lastTelemetryUpdateMs = 0;
let runtimeConfig = null;
let bevProjection = null;
let configFetchInFlight = false;
let lastConfigFetchMs = 0;
const pollDelayMs = 100;
const websocketStaleMs = 1000;
const telemetryUpdateMs = 100;
const configFetchRetryMs = 1000;
const initialDisplayMode = "__LS2K_DISPLAY_MODE__";
function handleEnvelope(buffer, transport) {
  const bytes = new Uint8Array(buffer);
  if (bytes.length < 8) return;
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const headerLen = view.getUint32(0);
  const payloadLen = view.getUint32(4);
  if (bytes.length !== 8 + headerLen + payloadLen) return;
  const headerText = new TextDecoder().decode(bytes.slice(8, 8 + headerLen));
  const header = JSON.parse(headerText);
  const payload = bytes.slice(8 + headerLen);
  latestSequence = Math.max(latestSequence, header.live_sequence ?? 0);
  if (header.type === "config_snapshot") {
    runtimeConfig = header.param_snapshot || null;
    bevProjection = runtimeConfig ? buildBevProjection(runtimeConfig) : null;
    fields.status.textContent = transport === "config" ? "configured" : transport;
    fields.transportSummary.textContent =
      `config / interval=${header.media_publish_interval_ms ?? "-"}ms`;
    fields.displayMode.textContent = initialDisplayMode === "bev"
      ? (bevProjection?.valid ? `BEV / ${bevProjection.reason}` : "BEV waiting config")
      : "Raw";
    return;
  }
  if (header.type === "image_frame") {
    const renderKey = `${header.frame_id ?? "-"}:${header.capture_time_ms ?? "-"}`;
    if (renderKey === latestRenderKey) return;
    latestRenderKey = renderKey;
    const pixelStats = renderGray(header, payload);
    if (!pixelStats) return;
    drawPathCandidateOverlay(header, pixelStats);
    latestFrameId = header.frame_id ?? latestFrameId;
    const steering = header.steering_snapshot || {};
    const camera = header.camera_frame || {};
    const circle = steering.circle_v2 || {};
    const nowMs = performance.now();
    if (lastRenderMs > 0) {
      const instantFps = 1000 / Math.max(1, nowMs - lastRenderMs);
      smoothedFps = smoothedFps == null ? instantFps : smoothedFps * 0.75 + instantFps * 0.25;
      fields.displayFps.textContent = formatNumber(smoothedFps, 1);
    }
    lastRenderMs = nowMs;
    if (nowMs - lastTelemetryUpdateMs < telemetryUpdateMs) return;
    lastTelemetryUpdateMs = nowMs;
    fields.status.textContent = transport;
    const gateVeto = nested(steering, ["safety_gate", "veto_active"], null);
    const referenceReady = nested(steering, ["reference_control", "ready"], null);
    setGateTone(gateVeto);
    setReferenceTone(referenceReady);
    fields.transportSummary.textContent =
      `${transport} / ${smoothedFps == null ? "-" : formatNumber(smoothedFps, 1)} fps`;
    fields.referenceSummary.textContent =
      `${nested(steering, ["reference", "source"])} / ` +
      `${formatNumber(nested(steering, ["lateral_error", "weighted_lateral_error_m"], null), 3)}m`;
    fields.safetySummary.textContent =
      `${gateVeto === true ? "veto" : gateVeto === false ? "clear" : "-"} / ` +
      `${nested(steering, ["safety_gate", "reason"])}`;
    fields.cameraSummary.textContent =
      `${camera.source ?? "-"} / seq=${camera.v4l2_sequence ?? "-"}`;
    fields.latency.textContent =
      `pubDelay=${formatInt((header.publish_time_ms ?? 0) - (header.capture_time_ms ?? 0))}ms / ` +
      `cap=${header.capture_time_ms ?? "-"} / host=${header.host_received_monotonic_ms ?? "-"}`;
    fields.frameId.textContent = String(header.frame_id ?? "-");
    fields.size.textContent =
      `${header.width}x${header.height} downsample=${header.downsample ?? 1} ` +
      `${header.payload_encoding ?? header.pixel_format ?? "raw"}`;
    fields.source.textContent =
      `${header.source_width ?? header.width}x${header.source_height ?? header.height} / ` +
      `${header.frame_source ?? "-"} / aligned=${formatBool(nested(header, ["snapshot_alignment", "aligned"], null))}`;
    fields.displayMode.textContent = pixelStats.display ?? (initialDisplayMode === "bev" ? "BEV" : "Raw");
    fields.motionFsm.textContent = header.motion_phase ?? "-";
    fields.circleFsm.textContent =
      `${circle.enabled === false ? "off" : circle.frame_phase ?? "-"} -> ${circle.next_phase ?? "-"}` +
      ` / ${circle.dir ?? "-"} / ${circle.reference_role ?? "-"}`;
    fields.circleReason.textContent = circle.reason ?? "-";
    fields.reference.textContent = `${nested(steering, ["reference", "mode"])} / ${nested(steering, ["reference", "source"])}`;
    fields.gate.textContent =
      `${formatBool(gateVeto)} / ` +
      `${nested(steering, ["safety_gate", "reason"])}`;
    fields.referenceControl.textContent =
      `${formatBool(referenceReady)} / ` +
      `${nested(steering, ["reference_control", "reason"])}`;
    fields.degraded.textContent =
      `${formatBool(nested(steering, ["degraded", "active"], null))} / ` +
      `${nested(steering, ["degraded", "reason"])}`;
    fields.visualReference.textContent =
      `${formatBool(nested(steering, ["visual_reference", "present"], null))} / ` +
      `${nested(steering, ["visual_reference", "source"])} / ` +
      `${nested(steering, ["visual_reference", "reason"])} / ` +
      `candidates=${nested(steering, ["visual_reference", "candidate_count"])}`;
    fields.eligibility.textContent =
      `${formatBool(nested(steering, ["eligibility", "usable"], null))} / ` +
      `${nested(steering, ["eligibility", "reason"])} / ` +
      `lead=${nested(steering, ["eligibility", "leading_usable_samples"])}`;
    fields.lateralError.textContent =
      `${formatNumber(nested(steering, ["lateral_error", "weighted_lateral_error_m"], null), 4)}m / ` +
      `n=${nested(steering, ["lateral_error", "weighted_sample_count"])} / ` +
      `${nested(steering, ["lateral_error", "reason"])}`;
    fields.turn.textContent = `${nested(steering, ["yaw_control", "turn_output_target"])} / ${nested(steering, ["actuator", "applied_turn_output"])}`;
    fields.actuator.textContent =
      `raw=${nested(steering, ["actuator", "raw_turn_output"])} / ` +
      `applied=${nested(steering, ["actuator", "applied_turn_output"])} / ` +
      `${nested(steering, ["actuator", "apply_outcome"])}`;
    fields.threshold.textContent = String(steering.threshold ?? "-");
    fields.cameraSource.textContent =
      `${camera.source ?? "-"} / ${camera.width ?? "-"}x${camera.height ?? "-"} / stride=${camera.stride ?? "-"}`;
    fields.v4l2.textContent =
      `seq=${camera.v4l2_sequence ?? "-"} / ts=${formatBool(camera.v4l2_timestamp_valid)}`;
    fields.cameraTiming.textContent =
      `poll=${formatUs(camera.poll_wait_us)} / dq=${formatUs(camera.dequeue_us)} / ` +
      `gray=${formatUs(camera.yuyv_to_gray_us)} / store=${formatUs(camera.store_submit_us)}`;
    fields.buffers.textContent =
      `drain=${camera.drained_buffer_count ?? "-"} / submitted=${camera.submitted_frame_count ?? "-"} / ` +
      `overwritten=${camera.overwritten_frame_count ?? "-"} / dropped=${camera.dropped_frame_count ?? "-"}`;
    fields.pixelStats.textContent =
      `min=${pixelStats.min} / max=${pixelStats.max} / mean=${formatNumber(pixelStats.mean, 1)}`;
  }
}
async function fetchConfig() {
  const nowMs = performance.now();
  if (configFetchInFlight || nowMs - lastConfigFetchMs < configFetchRetryMs) return;
  configFetchInFlight = true;
  lastConfigFetchMs = nowMs;
  try {
    const response = await fetch("/config.bin", { cache: "no-store" });
    if (response.ok) {
      handleEnvelope(await response.arrayBuffer(), "config");
    }
  } catch (error) {
    if (!runtimeConfig && initialDisplayMode === "bev") fields.displayMode.textContent = "BEV waiting config";
  } finally {
    configFetchInFlight = false;
  }
}
async function pollLatest() {
  try {
    if (!runtimeConfig) await fetchConfig();
    const nowMs = performance.now();
    if (websocketOpen && nowMs - lastWebSocketMessageMs < websocketStaleMs) return;
    const response = await fetch(`/latest.bin?seq=${latestSequence}`, { cache: "no-store" });
    if (response.status === 204) {
      if (!websocketOpen) fields.status.textContent = "waiting";
      return;
    }
    if (!response.ok) throw new Error(`status ${response.status}`);
    handleEnvelope(await response.arrayBuffer(), "polling");
  } catch (error) {
    if (fields.status.textContent !== "websocket") fields.status.textContent = "waiting";
  } finally {
    setTimeout(pollLatest, pollDelayMs);
  }
}
function connect() {
  const ws = new WebSocket(`${location.protocol === "https:" ? "wss" : "ws"}://${location.host}/ws`);
  ws.binaryType = "arraybuffer";
  ws.onopen = () => {
    websocketOpen = true;
    lastWebSocketMessageMs = performance.now();
    fields.status.textContent = "connected";
  };
  ws.onclose = () => {
    websocketOpen = false;
    fields.status.textContent = "reconnecting";
    setTimeout(connect, 750);
  };
  ws.onerror = () => { fields.status.textContent = "error"; };
  ws.onmessage = (event) => {
    websocketOpen = true;
    lastWebSocketMessageMs = performance.now();
    handleEnvelope(event.data, "websocket");
  };
}
fetchConfig();
connect();
pollLatest();
</script>
</body>
</html>
"""
    return html.replace(b"__LS2K_DISPLAY_MODE__", normalized_display_mode.encode("ascii"))


class LiveFrameHub:
    def __init__(self) -> None:
        self._condition = threading.Condition()
        self._sequence = 0
        self._latest: Optional[bytes] = None
        self._latest_config: Optional[bytes] = None
        self._summary: Dict[str, Any] = {
            "enabled": True,
            "messages_published": 0,
            "config_messages": 0,
            "image_messages": 0,
            "config_cached": False,
            "clients_connected": 0,
            "client_disconnects": 0,
            "client_errors": 0,
            "last_type": None,
            "last_frame_id": None,
            "last_sequence": 0,
            "last_message_bytes": 0,
        }

    def publish(self, header: Dict[str, Any], payload: bytes, receive_monotonic_ms: int) -> None:
        with self._condition:
            self._sequence += 1
            live_header = dict(header)
            live_header["host_received_monotonic_ms"] = receive_monotonic_ms
            live_header["live_sequence"] = self._sequence
            message = _encode_media_envelope(live_header, payload)
            self._latest = message
            self._summary["messages_published"] = int(self._summary["messages_published"]) + 1
            self._summary["last_type"] = live_header.get("type")
            self._summary["last_sequence"] = self._sequence
            self._summary["last_message_bytes"] = len(message)
            if live_header.get("type") == "config_snapshot":
                self._latest_config = message
                self._summary["config_messages"] = int(self._summary["config_messages"]) + 1
                self._summary["config_cached"] = True
            if live_header.get("type") == "image_frame":
                self._summary["image_messages"] = int(self._summary["image_messages"]) + 1
                self._summary["last_frame_id"] = live_header.get("frame_id")
            self._condition.notify_all()

    def wait_next(self, last_sequence: int, timeout_s: float) -> Tuple[int, Optional[bytes]]:
        with self._condition:
            if self._sequence <= last_sequence:
                self._condition.wait(timeout_s)
            if self._sequence <= last_sequence or self._latest is None:
                return last_sequence, None
            return self._sequence, self._latest

    def latest(self) -> Tuple[int, Optional[bytes]]:
        with self._condition:
            return self._sequence, self._latest

    def latest_config(self) -> Optional[bytes]:
        with self._condition:
            return self._latest_config

    def latest_since(self, sequence: int) -> Tuple[int, Optional[bytes]]:
        with self._condition:
            if self._sequence <= sequence:
                return self._sequence, None
            return self._sequence, self._latest

    def note_client_connected(self) -> None:
        with self._condition:
            self._summary["clients_connected"] = int(self._summary["clients_connected"]) + 1

    def note_client_disconnect(self) -> None:
        with self._condition:
            self._summary["client_disconnects"] = int(self._summary["client_disconnects"]) + 1

    def note_client_error(self) -> None:
        with self._condition:
            self._summary["client_errors"] = int(self._summary["client_errors"]) + 1

    def summary(self) -> Dict[str, Any]:
        with self._condition:
            return dict(self._summary)


class _ThreadingHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class SteeringMediaLiveServer:
    def __init__(self, host: str, port: int, display_mode: str = "bev") -> None:
        self._host = host
        self._port = port
        self._display_mode = "raw" if display_mode == "raw" else "bev"
        self._hub = LiveFrameHub()
        self._server: Optional[_ThreadingHTTPServer] = None
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._bound_url: Optional[str] = None

    @property
    def url(self) -> str:
        if self._bound_url is not None:
            return self._bound_url
        if self._server is None:
            return f"http://{self._host}:{self._port}/"
        host, port = self._server.server_address[:2]
        display_host = "127.0.0.1" if host in ("0.0.0.0", "::") else str(host)
        return f"http://{display_host}:{port}/"

    def start(self) -> None:
        outer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, format: str, *args: Any) -> None:
                return

            def do_GET(self) -> None:
                if self.path in ("/", "/index.html"):
                    body = _viewer_html(outer._display_mode)
                    self.send_response(200)
                    self.send_header("Content-Type", "text/html; charset=utf-8")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                    return
                if self.path == "/status.json":
                    body = _json_bytes(outer.summary())
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                    return
                if self.path == "/config.bin":
                    message = outer._hub.latest_config()
                    if message is None:
                        self.send_response(204)
                        self.send_header("Cache-Control", "no-store")
                        self.end_headers()
                        return
                    self.send_response(200)
                    self.send_header("Content-Type", "application/octet-stream")
                    self.send_header("Cache-Control", "no-store")
                    self.send_header("Content-Length", str(len(message)))
                    self.end_headers()
                    self.wfile.write(message)
                    return
                if self.path.startswith("/latest.bin"):
                    match = re.search(r"(?:[?&])seq=(\d+)", self.path)
                    client_sequence = int(match.group(1)) if match else 0
                    _, message = outer._hub.latest_since(client_sequence)
                    if message is None:
                        self.send_response(204)
                        self.send_header("Cache-Control", "no-store")
                        self.end_headers()
                        return
                    self.send_response(200)
                    self.send_header("Content-Type", "application/octet-stream")
                    self.send_header("Cache-Control", "no-store")
                    self.send_header("Content-Length", str(len(message)))
                    self.end_headers()
                    self.wfile.write(message)
                    return
                if self.path == "/ws":
                    self._handle_websocket()
                    return
                self.send_error(404)

            def _handle_websocket(self) -> None:
                key = self.headers.get("Sec-WebSocket-Key", "")
                if not key:
                    self.send_error(400, "missing Sec-WebSocket-Key")
                    return
                accept = base64.b64encode(
                    hashlib.sha1((key + WEBSOCKET_GUID).encode("ascii")).digest()
                ).decode("ascii")
                self.send_response(101)
                self.send_header("Upgrade", "websocket")
                self.send_header("Connection", "Upgrade")
                self.send_header("Sec-WebSocket-Accept", accept)
                self.end_headers()
                outer._hub.note_client_connected()
                last_sequence = 0
                try:
                    self.connection.settimeout(0.2)
                    config_message = outer._hub.latest_config()
                    if config_message is not None:
                        self.connection.sendall(_encode_ws_frame(config_message))
                    while not outer._stop.is_set():
                        self._discard_browser_input()
                        last_sequence, message = outer._hub.wait_next(last_sequence, 0.25)
                        if message is None:
                            continue
                        self.connection.sendall(_encode_ws_frame(message))
                except (BrokenPipeError, ConnectionResetError, OSError):
                    outer._hub.note_client_error()
                finally:
                    outer._hub.note_client_disconnect()

            def _discard_browser_input(self) -> None:
                while True:
                    readable, _, _ = select.select([self.connection], [], [], 0)
                    if not readable:
                        return
                    try:
                        chunk = self.connection.recv(4096)
                    except socket.timeout:
                        return
                    except BlockingIOError:
                        return
                    if not chunk:
                        raise ConnectionResetError("websocket client disconnected")
                    if len(chunk) < 2:
                        return
                    opcode = chunk[0] & 0x0F
                    if opcode == 0x8:
                        raise ConnectionResetError("websocket client closed")

        self._server = _ThreadingHTTPServer((self._host, self._port), Handler)
        self._bound_url = self.url
        self._thread = threading.Thread(
            target=self._server.serve_forever,
            name="steering-media-live-web",
            daemon=True,
        )
        self._thread.start()

    def close(self) -> Dict[str, Any]:
        self._stop.set()
        if self._server is not None:
            self._server.shutdown()
            self._server.server_close()
            self._server = None
        if self._thread is not None:
            self._thread.join(timeout=2.0)
            self._thread = None
        return self.summary()

    def publish(self, header: Dict[str, Any], payload: bytes, receive_monotonic_ms: int) -> None:
        self._hub.publish(header, payload, receive_monotonic_ms)

    def summary(self) -> Dict[str, Any]:
        summary = self._hub.summary()
        summary["url"] = self.url
        summary["display_mode"] = self._display_mode
        return summary
