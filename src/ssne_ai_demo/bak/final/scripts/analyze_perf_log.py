#!/usr/bin/env python3
"""Analyze ssne_ai_demo Performance Monitor logs.

Usage:
  python analyze_perf_log.py board_output.log --ready-only
"""

from __future__ import annotations

import argparse
import csv
import html
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


def parse_histogram_bins(text: str) -> dict[int, int]:
    bins: dict[int, int] = defaultdict(int)
    for token in text.split(","):
        if not token or ":" not in token:
            continue
        index_text, count_text = token.split(":", 1)
        try:
            bins[int(index_text)] += int(count_text)
        except ValueError:
            continue
    return dict(bins)


def weighted_percentile(pairs: list[tuple[float, int]], q: float) -> float:
    ordered = sorted((value, count) for value, count in pairs if count > 0)
    total = sum(count for _, count in ordered)
    if total <= 0:
        return 0.0
    target = max(1, math.ceil(q * total))
    cumulative = 0
    for value, count in ordered:
        cumulative += count
        if cumulative >= target:
            return value
    return ordered[-1][0]


def get_float(row: dict[str, Any], key: str, default: float = 0.0) -> float:
    value = row.get(key, default)
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def summarize_raw_histogram(
    session: dict[str, Any], sensor_fps_override: float = 0.0
) -> dict[str, Any]:
    meta = session["meta"]
    events = session.get("events", {})
    histograms = session.get("histograms", {})
    mode = str(meta.get("mode", "unknown"))
    sample_count = int(meta.get("samples", 0))
    rendered_frames = int(meta.get("frames", 0))
    elapsed_ms = get_float(meta, "elapsed_ms")
    elapsed_s = max(elapsed_ms / 1000.0, 1e-9)
    fps_sensor = (
        sensor_fps_override
        if sensor_fps_override > 0.0
        else get_float(meta, "fps_sensor", 30.0)
    )
    sensor_period_ms = 1000.0 / fps_sensor if fps_sensor > 0.0 else 0.0
    fps_app = rendered_frames / elapsed_s
    fps_infer = sample_count / elapsed_s

    def pairs(field: str) -> list[tuple[float, int]]:
        hist = histograms.get(field, {})
        bin_width_ms = get_float(hist, "bin_us", 250.0) / 1000.0
        return [
            (int(index) * bin_width_ms, int(count))
            for index, count in hist.get("bins", {}).items()
        ]

    def metric(field: str) -> dict[str, float]:
        hist = histograms.get(field, {})
        count = int(hist.get("count", 0))
        values = pairs(field)
        histogram_sum = sum(value * item_count for value, item_count in values)
        avg_ms = (
            get_float(hist, "sum_ms") / count
            if count > 0 and "sum_ms" in hist
            else histogram_sum / count if count > 0 else 0.0
        )
        max_ms = (
            get_float(hist, "max_ms")
            if "max_ms" in hist
            else max((value for value, item_count in values if item_count > 0), default=0.0)
        )
        return {
            "avg_ms": avg_ms,
            "p50_ms": weighted_percentile(values, 0.50),
            "p90_ms": weighted_percentile(values, 0.90),
            "p95_ms": weighted_percentile(values, 0.95),
            "max_ms": max_ms,
        }

    e2e = metric("e2e_ms")
    period_pairs = [
        (1000.0 / period_ms, count)
        for period_ms, count in pairs("frame_period_ms")
        if period_ms > 0.0
    ]
    fps_p5 = weighted_percentile(period_pairs, 0.05)
    fps_p95 = weighted_percentile(period_pairs, 0.95)
    realtime_ratio = fps_app / fps_sensor if fps_sensor > 0.0 else 0.0
    throughput_drop_rate = max(0.0, 1.0 - min(realtime_ratio, 1.0))
    capture_failures = int(meta.get("capture_failures", 0))
    capture_failure_ratio = capture_failures / max(
        1, rendered_frames + capture_failures
    )
    drop_rate_est = max(throughput_drop_rate, capture_failure_ratio)
    fps_p95_delta_ratio = (
        abs(fps_p95 - fps_app) / fps_app if fps_app > 0.0 else 0.0
    )
    e2e_pairs = pairs("e2e_ms")
    e2e_count = sum(count for _, count in e2e_pairs)
    frame_budget_miss_ratio = (
        sum(count for value, count in e2e_pairs if value > sensor_period_ms)
        / e2e_count
        if e2e_count > 0
        else 0.0
    )
    realtime_score = math.floor(10.0 * min(realtime_ratio, 1.0))
    latency_t = e2e["p95_ms"] / sensor_period_ms if sensor_period_ms > 0.0 else 0.0
    latency_score = max(0, min(10, math.floor(11.0 - latency_t)))
    divisor = max(1, sample_count)

    return {
        "mode": mode,
        "samples_total": sample_count,
        "samples_used": sample_count,
        "ready_only": False,
        "raw_histogram": True,
        "raw_format": str(meta.get("format", "unknown")),
        "raw_complete": bool(session.get("complete", False)),
        "histogram_bin_ms": get_float(meta, "hist_bin_us", 250.0) / 1000.0,
        "fps_app": fps_app,
        "fps_infer": fps_infer,
        "fps_sensor": fps_sensor,
        "sensor_period_ms": sensor_period_ms,
        "realtime_ratio": realtime_ratio,
        "fps_p5": fps_p5,
        "fps_p95": fps_p95,
        "fps_p95_delta_ratio": fps_p95_delta_ratio,
        "drop_rate_est": drop_rate_est,
        "frame_budget_miss_ratio": frame_budget_miss_ratio,
        "drop_penalty": drop_rate_est > 0.05,
        "fps_jitter_penalty": fps_p95_delta_ratio > 0.20,
        "realtime_score_0_10": realtime_score,
        "latency_T": latency_t,
        "latency_score_0_10": latency_score,
        "score_base_0_20": realtime_score + latency_score,
        "e2e": e2e,
        "capture": metric("cap_ms"),
        "palm_total": metric("palm_ms"),
        "palm_pre": metric("palm_pre_ms"),
        "palm_infer": metric("palm_infer_ms"),
        "palm_post": metric("palm_post_ms"),
        "hand_total": metric("hand_ms"),
        "gloss_total": metric("gloss_ms"),
        "osd": metric("osd_ms"),
        "osd_clear": metric("osd_clear_ms"),
        "osd_palm": metric("osd_palm_ms"),
        "osd_hand": metric("osd_hand_ms"),
        "osd_flush": metric("osd_flush_ms"),
        "osd_texture": metric("osd_texture_ms"),
        "event_counters_available": bool(events),
        "osd_texture_update_ratio": get_float(events, "osd_texture_updates")
        / divisor,
        "palm_det_avg": get_float(events, "palm_det_sum") / divisor,
        "hand_det_avg": get_float(events, "hand_det_sum") / divisor,
        "hand_drawn_avg": get_float(events, "hand_drawn_sum") / divisor,
        "hand_suppressed_avg": get_float(events, "hand_suppressed_sum")
        / divisor,
    }


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
            "p90_ms": percentile(values, 0.90),
            "p95_ms": percentile(values, 0.95),
            "max_ms": percentile(values, 1.00),
        }

    e2e = metric("e2e_ms")
    realtime_ratio = fps_app / sensor_fps if sensor_fps > 0.0 else 0.0
    realtime_score = math.floor(10.0 * min(realtime_ratio, 1.0))
    frame_periods = [
        get_float(row, "frame_period_ms")
        for row in used
        if get_float(row, "frame_period_ms") > 0.0
    ]
    instant_fps = [1000.0 / period for period in frame_periods]
    fps_p5 = percentile(instant_fps, 0.05)
    fps_p95 = percentile(instant_fps, 0.95)
    fps_p95_delta_ratio = (
        abs(fps_p95 - fps_app) / fps_app if fps_app > 0.0 else 0.0
    )
    drop_rate_est = max(0.0, 1.0 - min(realtime_ratio, 1.0))
    frame_budget_miss_ratio = mean(
        [1.0 if get_float(row, "e2e_ms") > sensor_period_ms else 0.0 for row in used]
    )
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
        "fps_p5": fps_p5,
        "fps_p95": fps_p95,
        "fps_p95_delta_ratio": fps_p95_delta_ratio,
        "drop_rate_est": drop_rate_est,
        "frame_budget_miss_ratio": frame_budget_miss_ratio,
        "drop_penalty": drop_rate_est > 0.05,
        "fps_jitter_penalty": fps_p95_delta_ratio > 0.20,
        "realtime_score_0_10": realtime_score,
        "latency_T": latency_t,
        "latency_score_0_10": latency_score,
        "score_base_0_20": realtime_score + latency_score,
        "e2e": e2e,
        "capture": metric("cap_ms"),
        "palm_total": metric("palm_ms"),
        "palm_pre": metric("palm_pre_ms"),
        "palm_infer": metric("palm_infer_ms"),
        "palm_post": metric("palm_post_ms"),
        "hand_total": metric("hand_ms"),
        "gloss_total": metric("gloss_ms"),
        "osd": metric("osd_ms"),
        "osd_clear": metric("osd_clear_ms"),
        "osd_palm": metric("osd_palm_ms"),
        "osd_hand": metric("osd_hand_ms"),
        "osd_flush": metric("osd_flush_ms"),
        "osd_texture": metric("osd_texture_ms"),
        "osd_texture_update_ratio": mean(
            [get_float(row, "osd_texture_updated") for row in used if "osd_texture_updated" in row]
        ),
        "palm_det_avg": mean([get_float(row, "palm_det") for row in used]),
        "hand_det_avg": mean([get_float(row, "hand_det") for row in used]),
        "hand_drawn_avg": mean([get_float(row, "hand_drawn") for row in used]),
        "hand_suppressed_avg": mean(
            [get_float(row, "hand_suppressed") for row in used]
        ),
    }


def print_report(summary: dict[str, Any]) -> None:
    print(f"\nmode={summary['mode']}")
    if summary.get("raw_histogram"):
        print(
            "source=compact_raw_histogram bin={:.3f}ms complete={}".format(
                summary.get("histogram_bin_ms", 0.0),
                summary.get("raw_complete", False),
            )
        )
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
        "fps_p5={fps_p5:.3f} fps_p95={fps_p95:.3f} p95_delta={fps_p95_delta_ratio:.3f} "
        "drop_est={drop_rate_est:.3f} budget_miss={frame_budget_miss_ratio:.3f} "
        "penalties(drop={drop_penalty}, jitter={fps_jitter_penalty})".format(**summary)
    )
    print(
        "e2e_avg={avg:.3f}ms e2e_p90={p90:.3f}ms e2e_p95={p95:.3f}ms e2e_max={mx:.3f}ms "
        "latency_T={latency_T:.3f} latency_score={latency_score_0_10}/10".format(
            avg=summary["e2e"]["avg_ms"],
            p90=summary["e2e"]["p90_ms"],
            p95=summary["e2e"]["p95_ms"],
            mx=summary["e2e"]["max_ms"],
            **summary,
        )
    )
    print(
        "base_score={score_base_0_20}/20 (before organizer-defined penalties)".format(
            **summary
        )
    )
    print("stage avg / p90 / p95 ms:")
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
        print(
            f"  {key:12s} avg={item['avg_ms']:.3f} "
            f"p90={item['p90_ms']:.3f} p95={item['p95_ms']:.3f}"
        )
    print("osd breakdown avg / p90 / p95 ms:")
    for key in ["osd_clear", "osd_palm", "osd_hand", "osd_flush", "osd_texture"]:
        item = summary[key]
        print(
            f"  {key:12s} avg={item['avg_ms']:.3f} "
            f"p90={item['p90_ms']:.3f} p95={item['p95_ms']:.3f}"
        )
    if summary.get("event_counters_available", True):
        print("osd texture update ratio={:.3f}".format(summary["osd_texture_update_ratio"]))
        print(
            "detections: palm_avg={:.2f} hand_avg={:.2f} drawn_avg={:.2f} suppressed_avg={:.2f}".format(
                summary["palm_det_avg"],
                summary["hand_det_avg"],
                summary["hand_drawn_avg"],
                summary["hand_suppressed_avg"],
            )
        )
    else:
        print("event counters: not recorded (timing-only monitor)")


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
        "p90_ms": get_float(row, f"{prefix}_p90_ms"),
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
    realtime_score = math.floor(10.0 * min(realtime_ratio, 1.0))
    latency_score = max(0, min(10, math.floor(11.0 - latency_t)))

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
        "fps_p5": get_float(row, "fps_p5"),
        "fps_p95": get_float(row, "fps_p95"),
        "fps_p95_delta_ratio": get_float(row, "fps_p95_delta_ratio"),
        "drop_rate_est": get_float(row, "drop_rate_est"),
        "frame_budget_miss_ratio": get_float(row, "frame_budget_miss_ratio"),
        "drop_penalty": bool(int(row.get("drop_penalty", 0))),
        "fps_jitter_penalty": bool(int(row.get("fps_jitter_penalty", 0))),
        "realtime_score_0_10": realtime_score,
        "latency_T": latency_t,
        "latency_score_0_10": latency_score,
        "score_base_0_20": realtime_score + latency_score,
        "e2e": {
            "avg_ms": get_float(row, "e2e_avg_ms"),
            "p50_ms": 0.0,
            "p90_ms": get_float(row, "e2e_p90_ms"),
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
        "osd_clear": metric_from_summary(row, "osd_clear"),
        "osd_palm": metric_from_summary(row, "osd_palm"),
        "osd_hand": metric_from_summary(row, "osd_hand"),
        "osd_flush": metric_from_summary(row, "osd_flush"),
        "osd_texture": metric_from_summary(row, "osd_texture"),
        "osd_texture_update_ratio": get_float(row, "osd_texture_update_ratio"),
        "palm_det_avg": get_float(row, "palm_det_avg"),
        "hand_det_avg": get_float(row, "hand_det_avg"),
        "hand_drawn_avg": get_float(row, "hand_drawn_avg"),
        "hand_suppressed_avg": get_float(row, "hand_suppressed_avg"),
    }


def svg_text(text: str) -> str:
    return html.escape(text, quote=True)


def write_bar_chart(
    path: Path,
    title: str,
    rows: list[tuple[str, dict[str, float]]],
    metrics: list[tuple[str, str]],
) -> None:
    width = max(900, 190 + 90 * len(rows) * max(1, len(metrics)))
    height = 540
    left, right, top, bottom = 90, 35, 80, 105
    plot_w = width - left - right
    plot_h = height - top - bottom
    values = [stats.get(key, 0.0) for _, stats in rows for key, _ in metrics]
    y_max = max(values) if values else 1.0
    y_max = max(1.0, y_max * 1.18)
    colors = ["#2f80ed", "#f2994a", "#27ae60", "#9b51e0"]
    group_w = plot_w / max(1, len(rows))
    bar_w = min(28, group_w / (len(metrics) + 1.5))

    parts: list[str] = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        f'<text x="{width / 2:.1f}" y="38" text-anchor="middle" '
        f'font-family="Arial, Microsoft YaHei, sans-serif" font-size="24" font-weight="700">{svg_text(title)}</text>',
        f'<line x1="{left}" y1="{top + plot_h}" x2="{left + plot_w}" y2="{top + plot_h}" stroke="#333" stroke-width="1.5"/>',
        f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}" stroke="#333" stroke-width="1.5"/>',
    ]
    for i in range(6):
        y_value = y_max * i / 5
        y = top + plot_h - plot_h * i / 5
        parts.append(f'<line x1="{left}" y1="{y:.1f}" x2="{left + plot_w}" y2="{y:.1f}" stroke="#e8eef5" stroke-width="1"/>')
        parts.append(
            f'<text x="{left - 10}" y="{y + 4:.1f}" text-anchor="end" '
            f'font-family="Arial" font-size="13" fill="#333">{y_value:.1f}</text>'
        )
    parts.append(
        f'<text x="24" y="{top + plot_h / 2:.1f}" transform="rotate(-90 24 {top + plot_h / 2:.1f})" '
        f'text-anchor="middle" font-family="Arial" font-size="15" fill="#333">Latency (ms)</text>'
    )
    for row_idx, (label, stats) in enumerate(rows):
        group_x = left + row_idx * group_w + group_w / 2
        start_x = group_x - (len(metrics) * bar_w + (len(metrics) - 1) * 8) / 2
        for metric_idx, (key, metric_label) in enumerate(metrics):
            value = stats.get(key, 0.0)
            bar_h = plot_h * value / y_max
            x = start_x + metric_idx * (bar_w + 8)
            y = top + plot_h - bar_h
            parts.append(
                f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_w:.1f}" height="{bar_h:.1f}" '
                f'rx="3" fill="{colors[metric_idx % len(colors)]}"/>'
            )
            parts.append(
                f'<text x="{x + bar_w / 2:.1f}" y="{y - 5:.1f}" text-anchor="middle" '
                f'font-family="Arial" font-size="12" fill="#111">{value:.2f}</text>'
            )
        parts.append(
            f'<text x="{group_x:.1f}" y="{top + plot_h + 28}" text-anchor="middle" '
            f'font-family="Arial, Microsoft YaHei, sans-serif" font-size="14" fill="#222">{svg_text(label)}</text>'
        )
    legend_x = left
    legend_y = height - 42
    for metric_idx, (_, metric_label) in enumerate(metrics):
        x = legend_x + metric_idx * 130
        parts.append(f'<rect x="{x}" y="{legend_y - 12}" width="16" height="16" fill="{colors[metric_idx % len(colors)]}"/>')
        parts.append(
            f'<text x="{x + 23}" y="{legend_y + 1}" font-family="Arial" font-size="14" fill="#222">{svg_text(metric_label)}</text>'
        )
    parts.append("</svg>")
    path.write_text("\n".join(parts), encoding="utf-8")


def write_line_chart(
    path: Path,
    title: str,
    samples: list[dict[str, Any]],
    fields: list[tuple[str, str, str]],
) -> None:
    width, height = 980, 500
    left, right, top, bottom = 85, 35, 75, 80
    plot_w = width - left - right
    plot_h = height - top - bottom
    xs = list(range(len(samples)))
    values = [get_float(row, field) for row in samples for field, _, _ in fields]
    y_max = max(values) if values else 1.0
    y_max = max(1.0, y_max * 1.18)
    parts: list[str] = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        f'<text x="{width / 2}" y="36" text-anchor="middle" '
        f'font-family="Arial, Microsoft YaHei, sans-serif" font-size="24" font-weight="700">{svg_text(title)}</text>',
        f'<line x1="{left}" y1="{top + plot_h}" x2="{left + plot_w}" y2="{top + plot_h}" stroke="#333" stroke-width="1.5"/>',
        f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}" stroke="#333" stroke-width="1.5"/>',
    ]
    for i in range(6):
        y_value = y_max * i / 5
        y = top + plot_h - plot_h * i / 5
        parts.append(f'<line x1="{left}" y1="{y:.1f}" x2="{left + plot_w}" y2="{y:.1f}" stroke="#e8eef5" stroke-width="1"/>')
        parts.append(
            f'<text x="{left - 10}" y="{y + 4:.1f}" text-anchor="end" font-family="Arial" font-size="13">{y_value:.1f}</text>'
        )
    denom = max(1, len(xs) - 1)
    for field, label, color in fields:
        points = []
        for idx, row in enumerate(samples):
            x = left + plot_w * idx / denom
            value = get_float(row, field)
            y = top + plot_h - plot_h * value / y_max
            points.append(f"{x:.1f},{y:.1f}")
        if points:
            parts.append(
                f'<polyline points="{" ".join(points)}" fill="none" stroke="{color}" stroke-width="2.2"/>'
            )
    parts.append(
        f'<text x="{left + plot_w / 2}" y="{height - 28}" text-anchor="middle" font-family="Arial" font-size="15">Printed PERF sample index</text>'
    )
    parts.append(
        f'<text x="24" y="{top + plot_h / 2}" transform="rotate(-90 24 {top + plot_h / 2})" '
        f'text-anchor="middle" font-family="Arial" font-size="15">Latency (ms)</text>'
    )
    legend_x = left
    legend_y = height - 58
    for idx, (_, label, color) in enumerate(fields):
        x = legend_x + idx * 170
        parts.append(f'<line x1="{x}" y1="{legend_y}" x2="{x + 28}" y2="{legend_y}" stroke="{color}" stroke-width="3"/>')
        parts.append(
            f'<text x="{x + 36}" y="{legend_y + 5}" font-family="Arial" font-size="14">{svg_text(label)}</text>'
        )
    parts.append("</svg>")
    path.write_text("\n".join(parts), encoding="utf-8")


def write_plots(out_dir: Path, summaries: list[dict[str, Any]], samples_by_mode: dict[str, list[dict[str, Any]]]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    for summary in summaries:
        mode = str(summary["mode"])
        prefix = mode.replace("/", "_")
        stage_rows = [
            ("Capture", summary["capture"]),
            ("Palm", summary["palm_total"]),
            ("Hand", summary["hand_total"]),
            ("Gloss", summary["gloss_total"]),
            ("OSD", summary["osd"]),
        ]
        write_bar_chart(
            out_dir / f"{prefix}_stage_latency.svg",
            f"{mode} stage latency",
            stage_rows,
            [("avg_ms", "avg"), ("p90_ms", "p90"), ("p95_ms", "p95")],
        )
        osd_rows = [
            ("Clear", summary["osd_clear"]),
            ("Palm draw", summary["osd_palm"]),
            ("Hand draw", summary["osd_hand"]),
            ("Graphic flush", summary["osd_flush"]),
            ("Texture label", summary["osd_texture"]),
        ]
        write_bar_chart(
            out_dir / f"{prefix}_osd_breakdown.svg",
            f"{mode} OSD breakdown",
            osd_rows,
            [("avg_ms", "avg"), ("p90_ms", "p90"), ("p95_ms", "p95")],
        )
        samples = samples_by_mode.get(mode, [])
        if samples:
            write_line_chart(
                out_dir / f"{prefix}_osd_timeseries.svg",
                f"{mode} OSD latency over time",
                samples,
                [
                    ("osd_ms", "OSD total", "#2f80ed"),
                    ("osd_texture_ms", "Texture label", "#f2994a"),
                    ("osd_flush_ms", "Graphic flush", "#27ae60"),
                ],
            )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path, help="Copied Aurora/serial terminal log")
    parser.add_argument("--sensor-fps", type=float, default=0.0, help="Override sensor FPS")
    parser.add_argument(
        "--samples-only",
        action="store_true",
        help="Ignore compact RAW/[PERF_SUMMARY] and use printed [PERF] sample lines",
    )
    parser.add_argument(
        "--ready-only",
        action="store_true",
        help="For fullcascade, prefer samples after the 64-frame window is ready",
    )
    parser.add_argument("--out-json", type=Path)
    parser.add_argument("--out-csv", type=Path)
    parser.add_argument("--plots-dir", type=Path, help="Write SVG charts to this directory")
    args = parser.parse_args()

    samples_by_mode: dict[str, list[dict[str, Any]]] = defaultdict(list)
    summaries: list[dict[str, Any]] = []
    raw_sessions: list[dict[str, Any]] = []
    current_raw: dict[str, Any] | None = None
    for line in args.log.read_text(encoding="utf-8", errors="ignore").splitlines():
        if line.startswith("[PERF] "):
            row = parse_kv_line(line)
            mode = str(row.get("mode", "unknown"))
            samples_by_mode[mode].append(row)
        elif line.startswith("[PERF_SUMMARY] "):
            summaries.append(parse_kv_line(line))
        elif line.startswith("[PERF_RAW_META] "):
            if current_raw is not None:
                raw_sessions.append(current_raw)
            current_raw = {
                "meta": parse_kv_line(line),
                "events": {},
                "histograms": {},
                "complete": False,
            }
        elif line.startswith("[PERF_RAW_EVENTS] ") and current_raw is not None:
            current_raw["events"] = parse_kv_line(line)
        elif line.startswith("[PERF_RAW_HIST] ") and current_raw is not None:
            row = parse_kv_line(line)
            metric_name = str(row.get("metric", ""))
            if not metric_name:
                continue
            initial_hist: dict[str, Any] = {
                "bin_us": get_float(row, "bin_us", 250.0),
                "hist_bins": int(row.get("hist_bins", 0)),
                "count": int(row.get("count", 0)),
                "bins": defaultdict(int),
            }
            if "sum_ms" in row:
                initial_hist["sum_ms"] = get_float(row, "sum_ms")
            if "max_ms" in row:
                initial_hist["max_ms"] = get_float(row, "max_ms")
            hist = current_raw["histograms"].setdefault(metric_name, initial_hist)
            for index, count in parse_histogram_bins(str(row.get("bins", ""))).items():
                hist["bins"][index] += count
        elif line.startswith("[PERF_RAW_END] ") and current_raw is not None:
            current_raw["complete"] = True
            raw_sessions.append(current_raw)
            current_raw = None
    if current_raw is not None:
        raw_sessions.append(current_raw)

    computed: list[dict[str, Any]] = []
    if raw_sessions and not args.samples_only and not args.ready_only:
        computed = [summarize_raw_histogram(item, args.sensor_fps) for item in raw_sessions]
        for item in computed:
            if not item.get("raw_complete", False):
                print(
                    "WARNING: copied raw report is incomplete; include the "
                    "[PERF_RAW_END] line and retry."
                )
    elif summaries and not args.samples_only and not args.ready_only:
        computed = [summarize_final_row(row, args.sensor_fps) for row in summaries]
    else:
        if args.ready_only and raw_sessions and not samples_by_mode:
            print(
                "WARNING: --ready-only needs periodic [PERF] lines. "
                "The compact histogram covers all post-warmup samples."
            )
            computed = [summarize_raw_histogram(item, args.sensor_fps) for item in raw_sessions]
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

    if args.plots_dir:
        write_plots(args.plots_dir, computed, samples_by_mode)
        print(f"saved_plots={args.plots_dir}")


if __name__ == "__main__":
    main()
