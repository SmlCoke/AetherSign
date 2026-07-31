#!/usr/bin/env python3
"""Analyze ssne_ai_demo Performance Monitor logs.

Usage:
  python analyze_perf_log.py board_output.log --ready-only
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
from collections import defaultdict
from pathlib import Path
from typing import Any


TOKEN_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([^ \t\r\n]+)")


def parse_value(text: str) -> Any:
    if text in {"true", "false"}:
        return text == "true"
    try:
        if any(ch in text for ch in ".eE"):
            return float(text)
        return int(text)
    except ValueError:
        return text


def parse_kv_line(line: str) -> dict[str, Any]:
    return {key: parse_value(value) for key, value in TOKEN_RE.findall(line)}


def percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = max(0, math.ceil(q * len(ordered)) - 1)
    return ordered[min(index, len(ordered) - 1)]


def mean(values: list[float]) -> float:
    return sum(values) / len(values) if values else 0.0


def get_float(row: dict[str, Any], key: str, default: float = 0.0) -> float:
    value = row.get(key, default)
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def summarize_samples(
    mode: str,
    samples: list[dict[str, Any]],
    sensor_fps: float,
    ready_only: bool,
) -> dict[str, Any]:
    used = samples
    if ready_only and mode == "fullcascade":
        ready = [row for row in samples if int(row.get("gloss_ready", 0)) == 1]
        if ready:
            used = ready

    last = samples[-1] if samples else {}
    fps_app = get_float(last, "fps_app")
    fps_infer = get_float(last, "fps_infer")
    if sensor_fps <= 0.0:
        sensor_fps = get_float(last, "fps_sensor", 30.0)
    sensor_period_ms = 1000.0 / sensor_fps if sensor_fps > 0.0 else 0.0

    def metric(field: str) -> dict[str, float]:
        values = [get_float(row, field) for row in used if field in row]
        return {
            "avg_ms": mean(values),
            "p50_ms": percentile(values, 0.50),
            "p95_ms": percentile(values, 0.95),
            "max_ms": percentile(values, 1.00),
        }

    e2e = metric("e2e_ms")
    realtime_ratio = fps_app / sensor_fps if sensor_fps > 0.0 else 0.0
    realtime_score = math.floor(10.0 * min(realtime_ratio, 1.0))
    latency_t = e2e["p95_ms"] / sensor_period_ms if sensor_period_ms > 0.0 else 0.0
    latency_score = max(0, min(10, math.floor(11.0 - latency_t)))

    return {
        "mode": mode,
        "samples_total": len(samples),
        "samples_used": len(used),
        "ready_only": ready_only and mode == "fullcascade",
        "fps_app": fps_app,
        "fps_infer": fps_infer,
        "fps_sensor": sensor_fps,
        "sensor_period_ms": sensor_period_ms,
        "realtime_ratio": realtime_ratio,
        "realtime_score_0_10": realtime_score,
        "latency_T": latency_t,
        "latency_score_0_10": latency_score,
        "e2e": e2e,
        "capture": metric("cap_ms"),
        "palm_total": metric("palm_ms"),
        "palm_pre": metric("palm_pre_ms"),
        "palm_infer": metric("palm_infer_ms"),
        "palm_post": metric("palm_post_ms"),
        "hand_total": metric("hand_ms"),
        "gloss_total": metric("gloss_ms"),
        "osd": metric("osd_ms"),
        "palm_det_avg": mean([get_float(row, "palm_det") for row in used]),
        "hand_det_avg": mean([get_float(row, "hand_det") for row in used]),
    }


def print_report(summary: dict[str, Any]) -> None:
    print(f"\nmode={summary['mode']}")
    print(
        "samples: used={samples_used} total={samples_total} ready_only={ready_only}".format(
            **summary
        )
    )
    print(
        "fps_app={fps_app:.3f} fps_infer={fps_infer:.3f} "
        "fps_sensor={fps_sensor:.3f} R={realtime_ratio:.3f} realtime_score={realtime_score_0_10}/10".format(
            **summary
        )
    )
    print(
        "e2e_avg={avg:.3f}ms e2e_p95={p95:.3f}ms e2e_max={mx:.3f}ms "
        "latency_T={latency_T:.3f} latency_score={latency_score_0_10}/10".format(
            avg=summary["e2e"]["avg_ms"],
            p95=summary["e2e"]["p95_ms"],
            mx=summary["e2e"]["max_ms"],
            **summary,
        )
    )
    print("stage avg / p95 ms:")
    for key in [
        "capture",
        "palm_total",
        "palm_pre",
        "palm_infer",
        "palm_post",
        "hand_total",
        "gloss_total",
        "osd",
    ]:
        item = summary[key]
        print(f"  {key:12s} avg={item['avg_ms']:.3f} p95={item['p95_ms']:.3f}")
    print(
        "detections: palm_avg={:.2f} hand_avg={:.2f}".format(
            summary["palm_det_avg"], summary["hand_det_avg"]
        )
    )


def flatten_summary(summary: dict[str, Any]) -> dict[str, Any]:
    flat: dict[str, Any] = {}
    for key, value in summary.items():
        if isinstance(value, dict):
            for sub_key, sub_value in value.items():
                flat[f"{key}_{sub_key}"] = sub_value
        else:
            flat[key] = value
    return flat


def metric_from_summary(row: dict[str, Any], prefix: str) -> dict[str, float]:
    return {
        "avg_ms": get_float(row, f"{prefix}_avg_ms"),
        "p50_ms": 0.0,
        "p95_ms": get_float(row, f"{prefix}_p95_ms"),
        "max_ms": get_float(row, f"{prefix}_max_ms"),
    }


def summarize_final_row(
    row: dict[str, Any], sensor_fps_override: float = 0.0
) -> dict[str, Any]:
    fps_app = get_float(row, "fps_app")
    fps_sensor = (
        sensor_fps_override
        if sensor_fps_override > 0.0
        else get_float(row, "fps_sensor", 30.0)
    )
    sensor_period_ms = 1000.0 / fps_sensor if fps_sensor > 0.0 else 0.0
    e2e_p95_ms = get_float(row, "e2e_p95_ms")
    realtime_ratio = fps_app / fps_sensor if fps_sensor > 0.0 else 0.0
    latency_t = e2e_p95_ms / sensor_period_ms if sensor_period_ms > 0.0 else 0.0

    return {
        "mode": str(row.get("mode", "unknown")),
        "samples_total": int(row.get("samples", 0)),
        "samples_used": int(row.get("samples", 0)),
        "ready_only": False,
        "fps_app": fps_app,
        "fps_infer": get_float(row, "fps_infer"),
        "fps_sensor": fps_sensor,
        "sensor_period_ms": sensor_period_ms,
        "realtime_ratio": realtime_ratio,
        "realtime_score_0_10": math.floor(10.0 * min(realtime_ratio, 1.0)),
        "latency_T": latency_t,
        "latency_score_0_10": max(0, min(10, math.floor(11.0 - latency_t))),
        "e2e": {
            "avg_ms": get_float(row, "e2e_avg_ms"),
            "p50_ms": 0.0,
            "p95_ms": e2e_p95_ms,
            "max_ms": get_float(row, "e2e_max_ms"),
        },
        "capture": metric_from_summary(row, "cap"),
        "palm_total": metric_from_summary(row, "palm"),
        "palm_pre": metric_from_summary(row, "palm_pre"),
        "palm_infer": metric_from_summary(row, "palm_infer"),
        "palm_post": metric_from_summary(row, "palm_post"),
        "hand_total": metric_from_summary(row, "hand"),
        "gloss_total": metric_from_summary(row, "gloss"),
        "osd": metric_from_summary(row, "osd"),
        "palm_det_avg": 0.0,
        "hand_det_avg": 0.0,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path, help="Copied Aurora/serial terminal log")
    parser.add_argument("--sensor-fps", type=float, default=0.0, help="Override sensor FPS")
    parser.add_argument(
        "--samples-only",
        action="store_true",
        help="Ignore [PERF_SUMMARY] and calculate from printed [PERF] sample lines",
    )
    parser.add_argument(
        "--ready-only",
        action="store_true",
        help="For fullcascade, prefer samples after the 64-frame window is ready",
    )
    parser.add_argument("--out-json", type=Path)
    parser.add_argument("--out-csv", type=Path)
    args = parser.parse_args()

    samples_by_mode: dict[str, list[dict[str, Any]]] = defaultdict(list)
    summaries: list[dict[str, Any]] = []
    for line in args.log.read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith("[PERF] "):
            row = parse_kv_line(line)
            mode = str(row.get("mode", "unknown"))
            samples_by_mode[mode].append(row)
        elif line.startswith("[PERF_SUMMARY] "):
            summaries.append(parse_kv_line(line))

    computed: list[dict[str, Any]] = []
    if summaries and not args.samples_only and not args.ready_only:
        computed = [summarize_final_row(row, args.sensor_fps) for row in summaries]
    else:
        for mode, samples in samples_by_mode.items():
            sensor_fps = args.sensor_fps
            if sensor_fps <= 0.0 and summaries:
                for item in summaries:
                    if str(item.get("mode", "")) == mode:
                        sensor_fps = get_float(item, "fps_sensor", 30.0)
                        break
            if sensor_fps <= 0.0:
                sensor_fps = 30.0
            computed.append(summarize_samples(mode, samples, sensor_fps, args.ready_only))

    if not computed and summaries:
        print("No [PERF] sample lines found. Showing [PERF_SUMMARY] values from the log.")
        for row in summaries:
            print(json.dumps(row, ensure_ascii=False, indent=2))
        return

    for summary in computed:
        print_report(summary)

    if args.out_json:
        args.out_json.write_text(json.dumps(computed, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"\nsaved_json={args.out_json}")

    if args.out_csv:
        rows = [flatten_summary(item) for item in computed]
        fieldnames = sorted({key for row in rows for key in row.keys()})
        with args.out_csv.open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows)
        print(f"saved_csv={args.out_csv}")


if __name__ == "__main__":
    main()
