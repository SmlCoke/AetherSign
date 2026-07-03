# SLR 性能测试系统讲解

本文档说明 `ssne_ai_demo` 的性能测试系统，包括 `[PERF]` 日志口径、window 定义、均值/P95 指标含义、跳帧推理读数方式，以及离线分析脚本 `scripts/analyze_perf_log.py` 的使用方法。

## 1. 系统组成

性能测试链路由两部分组成：

1. 板端在线统计：`PerformanceMonitor`
   - 代码位置：`include/performance_monitor.hpp`、`src/performance_monitor.cpp`、`main.cpp`。
   - 功能：采集每帧各阶段耗时，并每隔固定帧数打印 `[PERF]` 日志。
   - 特点：不写板端文件，只走控制台输出。

2. PC 端离线分析：`scripts/analyze_perf_log.py`
   - 输入：板端保存下来的 `result.log`。
   - 输出：中文摘要、英文详细报告和 SVG 图表。
   - 用途：统计采集帧数、推理帧数、FPS、丢帧率、各阶段均值/P95 延迟，并绘制吞吐、阶段耗时、Palm 细分耗时、P95 时间线和 P95 饼图。

## 2. 关键配置

性能相关配置位于 `main.cpp`：

```cpp
uint32_t kInferInterval = 1;
bool enable_hand = false;
const uint32_t infer_interval = kInferInterval == 0 ? 1 : kInferInterval;
const uint32_t verbose_multiple = (50 + infer_interval / 2) / infer_interval;
const uint32_t verbose_interval =
    (verbose_multiple == 0 ? 1 : verbose_multiple) * infer_interval;
const bool perf_log_enabled = true;
const double sensor_fps_cfg = 90.0;
const uint32_t perf_report_interval_frames = 120;
```

- `kInferInterval`: 跳帧推理间隔。`1` 表示每帧推理，`3` 表示每 3 帧推理一次。
- `enable_hand`: 是否启用 hand landmarker。默认不启用；传入 `--enable_hand` 后启用 palm + hand 级联。
- `sensor_fps_cfg`: 摄像头配置帧率，目前为 `90.0` FPS。
- `perf_report_interval_frames`: 性能统计窗口长度，目前为 `120` 帧。

启动示例：

```text
./ssne_ai_demo
./ssne_ai_demo --kInferInterval 3
./ssne_ai_demo --kInferInterval=3 --enable_hand
./scripts/run.sh --kInferInterval=3 --enable_hand
```

## 3. Window 定义

`window` 是固定长度、互不重叠的性能统计区间。

```cpp
const uint32_t perf_report_interval_frames = 120;
```

因此：

```text
1 个 window = 120 帧
```

`PerformanceMonitor::AddFrame()` 每处理完一帧就把该帧耗时加入当前 window。当 `total_frames % perf_report_interval_frames == 0` 时，程序打印一组 `[PERF]` 日志，然后清空当前 window 缓存。

## 4. 每帧记录字段

每帧记录一个 `FrameTiming`：

```cpp
get_image_ms
palm_total_ms
palm_preprocess_ms
palm_preprocess_resize_ms
palm_preprocess_manual_load_ms
palm_input_load_ms
palm_inference_ms
palm_getoutput_ms
palm_output_meta_ms
palm_decode_ms
palm_verbose_log_ms
palm_accounted_ms
hand_total_ms
draw_ms
loop_ms
process_ms
```

跳帧推理时需要注意：

- `GetImage()` 和 OSD 绘图每帧都执行。
- palm detector 只在 `frame_index % kInferInterval == 0` 时执行。
- hand landmarker 只在 `--enable_hand` 启用、当前帧是推理帧、且 palm 有有效检测时执行。
- 非推理帧的 palm 细分耗时记录为 `0`。
- 未执行 hand 的帧中，`hand_total_*` 通常接近 `0`。

当 `kInferInterval > 1` 时，`palm_total_avg` 和 `hand_total_avg` 是按全部帧摊薄后的每帧平均耗时。离线脚本会额外给出 `avg * kInferInterval` 的单次推理平均耗时估计。

## 5. Avg 和 P95

`*_avg` 是当前 window 内该字段所有样本的算术平均值。

`*_p95` 是第 95 百分位数，表示当前 window 内约 95% 的样本不超过该值。P95 比最大值稳健，也比均值更能反映高尾延迟。

P95 不按 `kInferInterval` 换算；它直接来自当前 window 的样本分布。离线脚本只对 avg 给出单次推理估计。

## 6. PERF 日志类型

每个 window 输出 5 类日志：

```text
[PERF][summary] ...
[PERF][stage_ms] ...
[PERF][palm_detail_ms] ...
[PERF][latency] ...
[PERF][jitter] ...
```

### `[PERF][summary]`

常用字段：

- `frame`: 当前 window 最后一帧的 frame index。
- `total_frames`: 程序启动后累计处理帧数。
- `window_frames`: 当前 window 的样本帧数。
- `elapsed_s`: 程序启动到当前日志打印时的应用侧运行时间。
- `sensor_fps_cfg`: 配置的摄像头帧率。
- `app_fps_total`: 从程序启动到当前的整体平均 FPS。
- `app_fps_window`: 当前 window 内的应用处理 FPS。
- `R`: 实时性比例，计算为 `app_fps_window / sensor_fps_cfg`。
- `realtime_score_est`: 按 `floor(10 * clamp(R, 0, 1))` 估算的实时性评分。
- `drop_rate_est_pct`: 应用侧估算丢帧率。

### `[PERF][stage_ms]`

顶层阶段耗时：

- `get_image_*`: `GetImage()` 阶段耗时。
- `palm_total_*`: 整个 `PALMDETECTOR::Predict()` 耗时。
- `hand_total_*`: 整个 `HANDLANDMARKER::Predict()` 耗时。
- `draw_*`: OSD 清屏和绘制耗时。
- `process_*`: 从 `GetImage()` 返回后到绘图完成的处理耗时。
- `loop_*`: 从开始取图到绘图完成的整帧循环耗时。
- `loop_max`: 当前 window 中 `loop_ms` 的最大值。

### `[PERF][palm_detail_ms]`

Palm detector 内部细分耗时：

- `palm_preprocess_*`: `PreprocessResize()` 总耗时。
- `palm_preprocess_resize_*`: `720x1280 -> 224x224` 双线性缩放耗时。
- `palm_preprocess_manual_load_*`: 将 `manual_input_buffer` 装载到 `manual_input` tensor 的耗时。
- `palm_input_load_*`: 将预处理输出装载到模型输入 tensor 的耗时。
- `palm_inference_*`: palm 模型的 `ssne_inference()` 耗时。
- `palm_getoutput_*`: palm 模型的 `ssne_getoutput()` 耗时。
- `palm_output_meta_*`: 获取输出 tensor metadata 并完成输出映射的耗时。
- `palm_decode_*`: palm 输出 decode、NMS、候选选择和坐标映射耗时。
- `palm_verbose_log_*`: verbose 诊断打印耗时。
- `palm_accounted_*`: 上述细分阶段加和。

### `[PERF][latency]`

端到端延迟指标：

- `sensor_period_ms`: 摄像头帧周期。
- `e2e_loop_p95_ms`: 当前 window 的 `loop_ms` P95。
- `e2e_loop_p95_T`: `e2e_loop_p95_ms / sensor_period_ms`。
- `e2e_process_p95_ms`: 当前 window 的 `process_ms` P95。
- `e2e_process_p95_T`: `e2e_process_p95_ms / sensor_period_ms`。
- `latency_score_est_by_loop`: 以 `loop` P95 估算的延迟评分。

### `[PERF][jitter]`

抖动指标：

- `loop_ms_avg`
- `loop_ms_p95`
- `loop_jitter_p95_vs_avg_pct`
- `instant_fps_avg`
- `instant_fps_p95`
- `fps_jitter_p95_vs_avg_pct`

## 7. 离线分析脚本

脚本位置：

```text
scripts/analyze_perf_log.py
```

命令格式：

```text
python scripts/analyze_perf_log.py --mode palm --kInferInterval 1 --log result.log
python scripts/analyze_perf_log.py --mode palm_hand --kInferInterval 3 --log result.log --out_dir perf_out
```

参数：

- `--mode palm`: 统计 palm-only 模式。
- `--mode palm_hand`: 统计 palm + hand 级联模式。
- `--kInferInterval N`: 板端运行时使用的推理间隔。
- `--log`: 日志文件路径。
- `--out_dir`: 输出目录，默认与 log 文件相同。

输出文件：

- `*_stats_summary.txt`: 中文摘要。
- `*_stats_detail.txt`: 英文详细报告。
- `*_stats_throughput.svg`: FPS、实时性比例和丢帧率趋势图。
- `*_stats_stage_latency.svg`: 最新 window 顶层阶段均值/P95 柱状图。
- `*_stats_palm_detail.svg`: 最新 window palm 内部细分耗时图。
- `*_stats_iteration_pie.svg`: P95 推理迭代耗时构成饼图。
- `*_stats_iteration_pie_info.txt`: 饼图明细。
- `*_stats_p95_timeline.svg`: P95 延迟时间线。

## 8. 现场解读建议

优先看：

- `app_fps_window`: 当前窗口实际处理 FPS。
- `R`: 是否接近或超过 1。
- `drop_rate_est_pct`: 应用侧估算丢帧率。
- `loop_p95`: 一帧完整循环的高尾耗时。
- `process_p95`: 取图返回后的处理链路高尾耗时。
- `palm_detail_ms palm_preprocess_resize_*`: palm 输入缩放耗时。
- `palm_detail_ms palm_inference_*`: palm 纯模型推理耗时。
- `hand_total_*`: hand landmarker 总耗时。

如果 `loop_p95` 远大于 `process_p95`，主要观察取图等待；如果 `process_p95` 很高，主要观察模型、输入缩放、后处理或绘图链路。

