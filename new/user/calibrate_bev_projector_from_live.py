#!/usr/bin/env python3
"""Calibrate BEV_PROJECTOR source points from a live straight-road frame.

The tool consumes the read-only live viewer endpoints. It does not reproduce
runtime reference/control logic; it only estimates the image-side projector
points from observed road boundaries and optionally writes default_params.json.
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
import struct
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_PARAMS_PATH = SCRIPT_DIR.parent / "config" / "default_params.json"
DEFAULT_OUTPUT_ROOT = SCRIPT_DIR.parent / "verification"


@dataclass(frozen=True)
class Envelope:
    header: Dict[str, Any]
    payload: bytes


@dataclass(frozen=True)
class RowObservation:
    row: int
    left_col: int
    right_col: int

    @property
    def width_px(self) -> int:
        return self.right_col - self.left_col + 1

    @property
    def center_col(self) -> float:
        return (self.left_col + self.right_col) * 0.5


@dataclass(frozen=True)
class LineFit:
    slope: float
    intercept: float
    kept_points: Tuple[Tuple[float, float], ...]
    rms_px: float
    max_abs_px: float

    def at(self, row: float) -> float:
        return self.slope * row + self.intercept


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Estimate BEV_PROJECTOR.SOURCE_* from the current live viewer frame "
            "using multi-row straight-road boundary fitting."
        )
    )
    parser.add_argument("--live-url", default="http://127.0.0.1:8765")
    parser.add_argument("--params-path", type=Path, default=DEFAULT_PARAMS_PATH)
    parser.add_argument("--output-dir", type=Path, default=None)
    parser.add_argument(
        "--row-mode",
        choices=("observed-quantile", "config", "explicit"),
        default="observed-quantile",
        help=(
            "observed-quantile estimates near/far source rows from reliable fitted rows; "
            "config keeps current SOURCE_ROW_*/SOURCE_COL_* row anchors; explicit uses "
            "--near-row and --far-row."
        ),
    )
    parser.add_argument("--near-row", type=float, default=None)
    parser.add_argument("--far-row", type=float, default=None)
    parser.add_argument("--near-row-quantile", type=float, default=0.90)
    parser.add_argument("--far-row-quantile", type=float, default=0.10)
    parser.add_argument(
        "--white-threshold",
        default="auto",
        help="gray threshold separator; pixels above it are white. Use 'auto' for Otsu.",
    )
    parser.add_argument("--min-run-px", type=int, default=20)
    parser.add_argument("--min-row-width-ratio", type=float, default=0.22)
    parser.add_argument("--max-row-width-ratio", type=float, default=0.95)
    parser.add_argument("--min-observations", type=int, default=60)
    parser.add_argument("--max-fit-rms-px", type=float, default=4.0)
    parser.add_argument("--write-params", action="store_true")
    parser.add_argument(
        "--force",
        action="store_true",
        help="write params even when quality checks warn.",
    )
    return parser.parse_args()


def fetch_url(url: str, timeout_s: float = 3.0) -> bytes:
    request = urllib.request.Request(url, headers={"Cache-Control": "no-store"})
    with urllib.request.urlopen(request, timeout=timeout_s) as response:
        if response.status == 204:
            return b""
        if response.status != 200:
            raise RuntimeError(f"{url} returned HTTP {response.status}")
        return response.read()


def decode_envelope(data: bytes, source: str) -> Envelope:
    if len(data) < 8:
        raise RuntimeError(f"{source} is shorter than the media envelope prefix")
    header_len, payload_len = struct.unpack(">II", data[:8])
    expected = 8 + header_len + payload_len
    if len(data) != expected:
        raise RuntimeError(
            f"{source} length mismatch: expected {expected} bytes, got {len(data)}"
        )
    header_bytes = data[8 : 8 + header_len]
    payload = data[8 + header_len :]
    return Envelope(json.loads(header_bytes.decode("utf-8")), payload)


def fetch_live_envelopes(live_url: str) -> Tuple[Envelope, Envelope]:
    base = live_url.rstrip("/")
    config_data = fetch_url(f"{base}/config.bin")
    if not config_data:
        raise RuntimeError("live viewer has no config snapshot yet")
    config = decode_envelope(config_data, "config.bin")

    latest: Optional[Envelope] = None
    last_error: Optional[BaseException] = None
    for attempt in range(5):
        try:
            latest_data = fetch_url(f"{base}/latest.bin?seq=0")
            if latest_data:
                candidate = decode_envelope(latest_data, "latest.bin")
                if candidate.header.get("type") == "image_frame":
                    latest = candidate
                    break
        except (RuntimeError, urllib.error.URLError) as error:
            last_error = error
        time.sleep(0.15 * (attempt + 1))
    if latest is None:
        if last_error is not None:
            raise RuntimeError(f"failed to fetch live image frame: {last_error}") from last_error
        raise RuntimeError("live viewer latest message is not an image frame")
    return config, latest


def packed_gray_bits(header: Dict[str, Any]) -> Optional[int]:
    encoding = header.get("payload_encoding")
    pixel_format = header.get("pixel_format")
    if encoding == "gray1_packed" or pixel_format == "gray1":
        return 1
    if encoding == "gray2_packed" or pixel_format == "gray2":
        return 2
    if encoding == "gray4_packed" or pixel_format == "gray4":
        return 4
    return None


def decode_gray(header: Dict[str, Any], payload: bytes) -> Tuple[int, int, List[int]]:
    width = int(header.get("width", 0))
    height = int(header.get("height", 0))
    if width <= 0 or height <= 0:
        raise RuntimeError(f"invalid frame dimensions: {width}x{height}")
    pixel_count = width * height
    bits = packed_gray_bits(header)
    if bits is None:
        if len(payload) != pixel_count:
            raise RuntimeError(
                f"raw gray payload size mismatch: expected {pixel_count}, got {len(payload)}"
            )
        return width, height, list(payload)
    expected = (pixel_count * bits + 7) // 8
    if len(payload) != expected:
        raise RuntimeError(
            f"packed gray payload size mismatch: expected {expected}, got {len(payload)}"
        )
    max_level = (1 << bits) - 1
    pixels: List[int] = []
    for index in range(pixel_count):
        bit_index = index * bits
        packed = payload[bit_index >> 3]
        shift = 8 - bits - (bit_index & 7)
        level = (packed >> shift) & max_level
        pixels.append(round((level * 255) / max(1, max_level)))
    return width, height, pixels


def otsu_threshold(pixels: Sequence[int]) -> int:
    hist = [0] * 256
    for gray in pixels:
        hist[max(0, min(255, int(gray)))] += 1
    total = len(pixels)
    sum_total = sum(level * count for level, count in enumerate(hist))
    sum_background = 0
    weight_background = 0
    best_threshold = 0
    best_between = -1.0
    for threshold, count in enumerate(hist):
        weight_background += count
        sum_background += threshold * count
        weight_foreground = total - weight_background
        if weight_background == 0 or weight_foreground == 0:
            continue
        mean_background = sum_background / weight_background
        mean_foreground = (sum_total - sum_background) / weight_foreground
        between = (
            weight_background
            * weight_foreground
            * (mean_background - mean_foreground)
            * (mean_background - mean_foreground)
        )
        if between > best_between:
            best_between = between
            best_threshold = threshold
    return best_threshold


def resolve_white_threshold(raw: str, pixels: Sequence[int]) -> int:
    if raw == "auto":
        return otsu_threshold(pixels)
    try:
        threshold = int(raw)
    except ValueError as error:
        raise RuntimeError(f"invalid --white-threshold: {raw!r}") from error
    if threshold < 0 or threshold > 254:
        raise RuntimeError("--white-threshold must be in [0, 254]")
    return threshold


def row_intervals(
    pixels: Sequence[int],
    width: int,
    row: int,
    threshold: int,
    min_run_px: int,
) -> List[Tuple[int, int]]:
    intervals: List[Tuple[int, int]] = []
    in_run = False
    start = 0
    offset = row * width
    for col in range(width):
        white = pixels[offset + col] > threshold
        if white and not in_run:
            start = col
            in_run = True
        if in_run and (not white or col == width - 1):
            end = col - 1 if not white else col
            if end - start + 1 >= min_run_px:
                intervals.append((start, end))
            in_run = False
    return intervals


def choose_central_interval(
    intervals: Sequence[Tuple[int, int]],
    width: int,
    previous: Optional[Tuple[int, int]],
) -> Optional[Tuple[int, int]]:
    if not intervals:
        return None
    image_center = (width - 1) * 0.5

    def interval_score(interval: Tuple[int, int]) -> float:
        left, right = interval
        center = (left + right) * 0.5
        interval_width = right - left + 1
        center_penalty = abs(center - image_center) / max(1.0, width * 0.5)
        contains_penalty = 0.0 if left <= image_center <= right else 0.4
        full_width_penalty = max(0.0, (interval_width - width * 0.95) / max(1.0, width * 0.05))
        continuity_penalty = 0.0
        if previous is not None:
            continuity_penalty = (
                abs(left - previous[0]) + abs(right - previous[1])
            ) / max(1.0, width)
        return center_penalty + contains_penalty + full_width_penalty + continuity_penalty

    return min(intervals, key=interval_score)


def detect_road_observations(
    pixels: Sequence[int],
    width: int,
    height: int,
    threshold: int,
    min_run_px: int,
    min_width_ratio: float,
    max_width_ratio: float,
) -> List[RowObservation]:
    observations: List[RowObservation] = []
    previous: Optional[Tuple[int, int]] = None
    min_width = max(1.0, width * min_width_ratio)
    max_width = max(min_width + 1.0, width * max_width_ratio)
    for row in range(0, height):
        intervals = row_intervals(pixels, width, row, threshold, min_run_px)
        chosen = choose_central_interval(intervals, width, previous)
        if chosen is None:
            continue
        previous = chosen
        row_width = chosen[1] - chosen[0] + 1
        if min_width <= row_width <= max_width:
            observations.append(RowObservation(row, chosen[0], chosen[1]))
    return observations


def fit_line(points: Sequence[Tuple[float, float]]) -> LineFit:
    if len(points) < 2:
        raise RuntimeError("not enough points for line fit")
    kept = list(points)
    for _ in range(5):
        slope, intercept = least_squares_line(kept)
        residuals = [abs(col - (slope * row + intercept)) for row, col in kept]
        median = statistics.median(residuals)
        mad = statistics.median(abs(value - median) for value in residuals) or 1.0
        gate = max(3.0, median + 3.0 * 1.4826 * mad)
        next_kept = [point for point, residual in zip(kept, residuals) if residual <= gate]
        if len(next_kept) == len(kept) or len(next_kept) < max(2, len(points) // 3):
            break
        kept = next_kept
    slope, intercept = least_squares_line(kept)
    residuals = [abs(col - (slope * row + intercept)) for row, col in kept]
    rms = math.sqrt(sum(value * value for value in residuals) / max(1, len(residuals)))
    max_abs = max(residuals) if residuals else 0.0
    return LineFit(slope, intercept, tuple(kept), rms, max_abs)


def least_squares_line(points: Sequence[Tuple[float, float]]) -> Tuple[float, float]:
    mean_row = sum(row for row, _ in points) / len(points)
    mean_col = sum(col for _, col in points) / len(points)
    denominator = sum((row - mean_row) * (row - mean_row) for row, _ in points)
    if denominator <= 1.0e-9:
        raise RuntimeError("line fit rows are degenerate")
    slope = sum((row - mean_row) * (col - mean_col) for row, col in points) / denominator
    intercept = mean_col - slope * mean_row
    return slope, intercept


def quantile(values: Sequence[float], q: float) -> float:
    if not values:
        raise RuntimeError("cannot compute quantile of empty values")
    clipped = max(0.0, min(1.0, q))
    sorted_values = sorted(values)
    position = clipped * (len(sorted_values) - 1)
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    if lower == upper:
        return sorted_values[lower]
    mix = position - lower
    return sorted_values[lower] * (1.0 - mix) + sorted_values[upper] * mix


def projector_from_config(config_header: Dict[str, Any]) -> Dict[str, Any]:
    snapshot = config_header.get("param_snapshot")
    if not isinstance(snapshot, dict):
        raise RuntimeError("config snapshot does not contain param_snapshot")
    projector = snapshot.get("BEV_PROJECTOR")
    if not isinstance(projector, dict):
        raise RuntimeError("config snapshot does not contain BEV_PROJECTOR")
    return projector


def choose_source_rows(
    args: argparse.Namespace,
    projector: Dict[str, Any],
    left_fit: LineFit,
    right_fit: LineFit,
) -> Tuple[float, float]:
    if args.row_mode == "explicit":
        if args.near_row is None or args.far_row is None:
            raise RuntimeError("--row-mode explicit requires --near-row and --far-row")
        return float(args.near_row), float(args.far_row)
    if args.row_mode == "config":
        near_row = (float(projector["SOURCE_ROW_0"]) + float(projector["SOURCE_ROW_1"])) * 0.5
        far_row = (float(projector["SOURCE_ROW_2"]) + float(projector["SOURCE_ROW_3"])) * 0.5
        return near_row, far_row

    left_rows = {int(round(row)) for row, _ in left_fit.kept_points}
    right_rows = {int(round(row)) for row, _ in right_fit.kept_points}
    common_rows = sorted(left_rows & right_rows)
    if len(common_rows) < 2:
        raise RuntimeError("left/right fitted row overlap is empty")
    far_row = quantile(common_rows, args.far_row_quantile)
    near_row = quantile(common_rows, args.near_row_quantile)
    return near_row, far_row


def make_suggested_projector(
    projector: Dict[str, Any],
    near_row: float,
    far_row: float,
    left_fit: LineFit,
    right_fit: LineFit,
    x_scale_to_source: float,
    y_scale_to_source: float,
    frame_id: Any,
) -> Dict[str, Any]:
    result = dict(projector)
    now = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    result["PROJECTOR_ID"] = f"bev_projector_straight_fit_{now}"
    result["PROJECTOR_HASH"] = f"bev-projector-multipoint-fit-frame-{frame_id}-{now}"
    source_points = {
        0: (near_row, left_fit.at(near_row)),
        1: (near_row, right_fit.at(near_row)),
        2: (far_row, left_fit.at(far_row)),
        3: (far_row, right_fit.at(far_row)),
    }
    for index, (row, col) in source_points.items():
        result[f"SOURCE_ROW_{index}"] = quantize_pixel(row * y_scale_to_source)
        result[f"SOURCE_COL_{index}"] = quantize_pixel(col * x_scale_to_source)
    return result


def quantize_pixel(value: float) -> float:
    return round(value * 2.0) / 2.0


def quality_warnings(
    args: argparse.Namespace,
    observations: Sequence[RowObservation],
    width: int,
    height: int,
    near_row: float,
    far_row: float,
    left_fit: LineFit,
    right_fit: LineFit,
) -> List[str]:
    warnings: List[str] = []
    if len(observations) < args.min_observations:
        warnings.append(
            f"only {len(observations)} row observations; expected >= {args.min_observations}"
        )
    if near_row <= far_row + height * 0.15:
        warnings.append(f"near/far rows are too close: near={near_row:.1f}, far={far_row:.1f}")
    if min(near_row, far_row) < 0 or max(near_row, far_row) > height - 1:
        warnings.append(f"source rows outside image: near={near_row:.1f}, far={far_row:.1f}")
    if max(left_fit.rms_px, right_fit.rms_px) > args.max_fit_rms_px:
        warnings.append(
            f"fit RMS too high: left={left_fit.rms_px:.2f}px right={right_fit.rms_px:.2f}px"
        )
    near_left = left_fit.at(near_row)
    near_right = right_fit.at(near_row)
    far_left = left_fit.at(far_row)
    far_right = right_fit.at(far_row)
    if near_left >= near_right or far_left >= far_right:
        warnings.append("fitted left/right boundaries cross")
    if (near_right - near_left) <= (far_right - far_left):
        warnings.append(
            "near fitted road width is not larger than far width; straight-road perspective is suspect"
        )
    for label, col in (
        ("near_left", near_left),
        ("near_right", near_right),
        ("far_left", far_left),
        ("far_right", far_right),
    ):
        if col < 0 or col > width - 1:
            warnings.append(f"{label} source column is outside image: {col:.1f}")
    return warnings


def write_outputs(
    output_dir: Path,
    header: Dict[str, Any],
    config_header: Dict[str, Any],
    width: int,
    height: int,
    pixels: Sequence[int],
    threshold: int,
    observations: Sequence[RowObservation],
    near_row: float,
    far_row: float,
    left_fit: LineFit,
    right_fit: LineFit,
    current_projector: Dict[str, Any],
    suggested_projector: Dict[str, Any],
    warnings: Sequence[str],
) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    result = {
        "frame": {
            "frame_id": header.get("frame_id"),
            "width": width,
            "height": height,
            "source_width": header.get("source_width", width),
            "source_height": header.get("source_height", height),
            "payload_encoding": header.get("payload_encoding", header.get("pixel_format")),
            "motion_phase": header.get("motion_phase"),
            "live_sequence": header.get("live_sequence"),
        },
        "threshold": threshold,
        "observations": {
            "count": len(observations),
            "row_min": min((item.row for item in observations), default=None),
            "row_max": max((item.row for item in observations), default=None),
        },
        "fit": {
            "near_row": near_row,
            "far_row": far_row,
            "left": fit_to_json(left_fit),
            "right": fit_to_json(right_fit),
        },
        "current_projector": current_projector,
        "suggested_projector": suggested_projector,
        "warnings": list(warnings),
        "config_projector_id": current_projector.get("PROJECTOR_ID"),
        "config_publish_time_ms": config_header.get("publish_time_ms"),
    }
    result_path = output_dir / "calibration_result.json"
    with result_path.open("w", encoding="utf-8") as file:
        json.dump(result, file, indent=2, ensure_ascii=False)
        file.write("\n")
    maybe_write_overlay(
        output_dir / "calibration_overlay.png",
        width,
        height,
        pixels,
        threshold,
        observations,
        near_row,
        far_row,
        left_fit,
        right_fit,
    )
    return result_path


def fit_to_json(fit: LineFit) -> Dict[str, Any]:
    rows = [row for row, _ in fit.kept_points]
    return {
        "slope_col_per_row": fit.slope,
        "intercept_col": fit.intercept,
        "kept_points": len(fit.kept_points),
        "row_min": min(rows) if rows else None,
        "row_max": max(rows) if rows else None,
        "rms_px": fit.rms_px,
        "max_abs_px": fit.max_abs_px,
    }


def maybe_write_overlay(
    path: Path,
    width: int,
    height: int,
    pixels: Sequence[int],
    threshold: int,
    observations: Sequence[RowObservation],
    near_row: float,
    far_row: float,
    left_fit: LineFit,
    right_fit: LineFit,
) -> None:
    try:
        from PIL import Image, ImageDraw
    except Exception:
        return
    image = Image.new("RGB", (width, height))
    image.putdata([(gray, gray, gray) for gray in pixels])
    draw = ImageDraw.Draw(image)
    for obs in observations[:: max(1, len(observations) // 80)]:
        draw.point((obs.left_col, obs.row), fill=(0, 255, 255))
        draw.point((obs.right_col, obs.row), fill=(0, 255, 255))
    for row in range(height):
        left = left_fit.at(row)
        right = right_fit.at(row)
        if 0 <= left < width:
            draw.point((left, row), fill=(255, 59, 48))
        if 0 <= right < width:
            draw.point((right, row), fill=(52, 199, 89))
    for row, color in ((far_row, (255, 204, 0)), (near_row, (0, 122, 255))):
        y = int(round(row))
        draw.line((0, y, width - 1, y), fill=color, width=1)
    for row in (near_row, far_row):
        left = left_fit.at(row)
        right = right_fit.at(row)
        radius = 4
        for col, color in ((left, (255, 59, 48)), (right, (52, 199, 89))):
            draw.ellipse(
                (
                    col - radius,
                    row - radius,
                    col + radius,
                    row + radius,
                ),
                outline=color,
                width=2,
            )
    draw.text((6, 6), f"white>{threshold}", fill=(255, 204, 0))
    image.save(path)


def update_params_file(params_path: Path, suggested_projector: Dict[str, Any]) -> None:
    data = json.loads(params_path.read_text(encoding="utf-8"))
    if not isinstance(data.get("BEV_PROJECTOR"), dict):
        raise RuntimeError(f"{params_path} has no BEV_PROJECTOR object")
    projector = data["BEV_PROJECTOR"]
    for key in ("PROJECTOR_ID", "PROJECTOR_HASH"):
        projector[key] = suggested_projector[key]
    for index in range(4):
        projector[f"SOURCE_ROW_{index}"] = suggested_projector[f"SOURCE_ROW_{index}"]
        projector[f"SOURCE_COL_{index}"] = suggested_projector[f"SOURCE_COL_{index}"]
    with params_path.open("w", encoding="utf-8") as file:
        json.dump(data, file, indent=2, ensure_ascii=False)
        file.write("\n")


def source_scale(header: Dict[str, Any], width: int, height: int) -> Tuple[float, float]:
    source_width = int(header.get("source_width", width) or width)
    source_height = int(header.get("source_height", height) or height)
    return source_width / max(1.0, float(width)), source_height / max(1.0, float(height))


def main() -> int:
    args = parse_args()
    config, latest = fetch_live_envelopes(args.live_url)
    projector = projector_from_config(config.header)
    width, height, pixels = decode_gray(latest.header, latest.payload)
    threshold = resolve_white_threshold(str(args.white_threshold), pixels)
    observations = detect_road_observations(
        pixels,
        width,
        height,
        threshold,
        args.min_run_px,
        args.min_row_width_ratio,
        args.max_row_width_ratio,
    )
    if len(observations) < 2:
        raise RuntimeError("no usable straight-road observations detected")
    left_fit = fit_line([(obs.row, obs.left_col) for obs in observations])
    right_fit = fit_line([(obs.row, obs.right_col) for obs in observations])
    near_row, far_row = choose_source_rows(args, projector, left_fit, right_fit)
    x_scale, y_scale = source_scale(latest.header, width, height)
    suggested = make_suggested_projector(
        projector,
        near_row,
        far_row,
        left_fit,
        right_fit,
        x_scale,
        y_scale,
        latest.header.get("frame_id", "unknown"),
    )
    warnings = quality_warnings(
        args,
        observations,
        width,
        height,
        near_row,
        far_row,
        left_fit,
        right_fit,
    )
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output_dir = args.output_dir or DEFAULT_OUTPUT_ROOT / f"bev-projector-calibration-{timestamp}"
    result_path = write_outputs(
        output_dir,
        latest.header,
        config.header,
        width,
        height,
        pixels,
        threshold,
        observations,
        near_row,
        far_row,
        left_fit,
        right_fit,
        projector,
        suggested,
        warnings,
    )

    print(f"[calib] frame_id={latest.header.get('frame_id')} {width}x{height}")
    print(f"[calib] threshold=gray>{threshold} observations={len(observations)}")
    print(
        "[calib] fit "
        f"left_rms={left_fit.rms_px:.2f}px right_rms={right_fit.rms_px:.2f}px "
        f"near_row={near_row:.1f} far_row={far_row:.1f}"
    )
    for index in range(4):
        old_row = projector.get(f"SOURCE_ROW_{index}")
        old_col = projector.get(f"SOURCE_COL_{index}")
        new_row = suggested.get(f"SOURCE_ROW_{index}")
        new_col = suggested.get(f"SOURCE_COL_{index}")
        print(
            f"[calib] SOURCE_{index}: "
            f"row {old_row} -> {new_row}, col {old_col} -> {new_col}"
        )
    if warnings:
        for warning in warnings:
            print(f"[WARN] {warning}")
    print(f"[calib] wrote evidence {result_path}")
    overlay_path = result_path.with_name("calibration_overlay.png")
    if overlay_path.exists():
        print(f"[calib] wrote overlay {overlay_path}")

    if args.write_params:
        if warnings and not args.force:
            print("[ERROR] quality warnings present; rerun with --force to write anyway", file=sys.stderr)
            return 2
        update_params_file(args.params_path, suggested)
        print(f"[calib] updated {args.params_path}")
    else:
        print("[calib] params unchanged; add --write-params to apply this suggestion")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"[ERROR] {error}", file=sys.stderr)
        sys.exit(1)
