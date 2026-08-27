# Performance Monitor 使用与评测规范

## 1. 原则

Performance Monitor 默认关闭，三种运行模式共用同一套计时链路。开启后板端只做以下工作：

- 用单调时钟测量帧周期、端到端和各阶段时间。
- 为减少 Aurora 终端篇幅，把时间放入固定 0.25 ms 直方图 bin。
- 记录样本数、成功应用帧数、取图失败次数和测试持续时间。
- 输入 `q` 后打印原始时间直方图。

板端不计算 FPS、均值、P90/P95、实时性比例、丢帧率、波动比例、延迟周期或得分；也不打印模型分类、检测数量或 OSD 门控统计。`scripts/analyze_perf_log.py` 在 PC 上完成全部指标计算。直方图只做固定 bin 计数，不排序样本。

## 2. 三种模式的简洁命令

```sh
# 只运行 Palm
./scripts/run.sh --mode palm

# 运行 Palm + Hand
./scripts/run.sh --mode palm_hand

# 运行完整 Palm + Hand + Gloss Translator
./scripts/run.sh --mode fullcascade
```

旧写法 `./scripts/run.sh palm_hand` 仍然兼容。不指定模式时默认 `fullcascade`。

开启性能计时可使用脚本简写；正式测试中 `--perf_interval=0` 和 fullcascade 静默诊断间隔会自动设置：

```sh
./scripts/run.sh --mode palm --perf --sensor-fps 90
./scripts/run.sh --mode palm_hand --perf --sensor-fps 90
./scripts/run.sh --mode fullcascade --perf --sensor-fps 90
```

等价的环境变量写法仍然可用：

```sh
PERF_MONITOR=1 PERF_SENSOR_FPS=90 PERF_INTERVAL=0 \
  PERF_WARMUP_FRAMES=60 ./scripts/run.sh --mode fullcascade
```

底层程序参数：

```text
--perf_monitor             开启计时，默认关闭
--no_perf_monitor          显式关闭
--perf_sensor_fps FLOAT    传感器实际配置 FPS；板端只原样记录
--perf_warmup_frames N     排除启动预热帧，默认 30
--perf_interval N          每 N 个样本打印原始时间；默认 0，运行中不刷屏
```

`--sensor-fps` 应填写传感器当前模式的真实配置值。Aurora 预览 FPS 和应用 FPS 不能替代 Sensor FPS；`ssne_api.h::GetImageData()` 没有曝光时间戳或硬件 frame id，程序无法自行发现真正的 Sensor FPS 或 ISP 覆盖帧。

## 3. 三种模式的计时字段

每种模式均输出相同字段；未运行的阶段保持为 0，方便统一使用一个 Python 脚本：

| 时间字段 | palm | palm_hand | fullcascade |
|---|---:|---:|---:|
| `frame_period_ms` / `e2e_ms` / `cap_ms` | 有 | 有 | 有 |
| `palm_ms` 及 Palm 子阶段 | 有 | 有 | 有 |
| `hand_ms` | 0 | 有 | 有 |
| `gloss_ms` | 0 | 0 | 有 |
| `osd_ms` 及 OSD 子阶段 | 有 | 有 | 有 |

字段含义：

```text
frame_period_ms         相邻应用循环起点间隔，用于离线 FPS 波动计算
e2e_ms                  GetImageData 前到 OSD 提交完成
cap_ms                  GetImageData
palm_ms                 Palm 全流程
palm_pre_ms             Palm 旋转、缩放和输入装载
palm_infer_ms           Palm ssne_inference
palm_post_ms            Palm getoutput、decode/NMS
hand_ms                 所有 Palm ROI 的 Hand 总耗时
gloss_ms                时序更新/Gloss 分类耗时
osd_ms                  OSD 总耗时
osd_palm_ms             Palm 图元构建
osd_hand_ms             Hand 显示门控、平滑与骨架构建
osd_flush_ms            graphic layer flush
osd_clear_ms            清理变为空的 layer
osd_texture_ms          分类位图更新
```

## 4. 终端复制

输入 `q` 后会看到：

```text
[PERF_RAW_META] mode=... format=raw_time_hist_v2 ...
[PERF_RAW_HIST] metric=frame_period_ms ...
[PERF_RAW_HIST] metric=e2e_ms ...
...
[PERF_RAW_END] ... status=complete
```

直方图只打印非零 bin，长内容自动分片。Aurora 无法保存终端时，从 `[PERF_RAW_META]` 开始复制到 `[PERF_RAW_END]` 为止。缺少 END 说明复制或退出不完整；必须使用 `q` 正常退出，不能直接杀进程。

0.25 ms bin 的代表值用于离线计算均值和分位数，量化误差约 ±0.125 ms。超过约 511.75 ms 的时间进入末 bin；比赛核心 P95 通常不受单个极端值影响。

## 5. 离线计算与比赛评分

```sh
python scripts/analyze_perf_log.py board_output.log \
  --out-json perf.json \
  --out-csv perf.csv \
  --plots-dir perf_plots
```

Python 计算：

```text
Fps_app = 成功应用帧数 / 测试持续时间
R = Fps_app / Fps_sensor
实时性基础分 = floor(10 * min(R, 1.0))

FPS 波动 = abs(FPS_P95 - FPS_mean) / FPS_mean
丢帧估算 = max(1 - min(R, 1), 显式取图失败率)

sensor_period_ms = 1000 / Fps_sensor
latency_T = e2e_P95 / sensor_period_ms
延迟基础分 = clamp(floor(11 - latency_T), 0, 10)
score_base_20 = 实时性基础分 + 延迟基础分，封顶 20
```

脚本只报告规则中能公式化的基础分和是否命中 `>5%` 丢帧、`>20%` FPS 波动条件。规则没有给出两项具体扣分数，因此脚本不会擅自减分。“任务二/三难度权重”由现场评委结合完成度人工判断，也不由脚本虚构加分。

由于 SDK 缺少硬件 frame id，`drop_rate_est` 是吞吐差和显式取图失败率的估算，不是传感器真实丢帧计数。

旧版 `raw_hist_v1`、`[PERF_SUMMARY]` 和旧 `[PERF]` 日志仍可解析。新版 timing-only 日志不包含分类 ready 状态，因此不再支持用它做 `--ready-only` 分类切片。

软件 `e2e_ms` 不包含曝光开始到 `GetImageData()` 返回的不可见区间。严格的光子到屏幕延迟应使用 120/240 FPS 高速摄影，至少统计 100 次事件。

## 6. 推荐实验

每组预热后连续运行至少 60 秒，重复 3 次，报告中位数和最差一次 P95。固定场景、人数、手数量、光照和 OSD 配置，避免不同 Hand ROI 数量破坏可比性。

```sh
# 三个任务的主测试
PERF_WARMUP_FRAMES=60 ./scripts/run.sh --mode palm --perf --sensor-fps 90
PERF_WARMUP_FRAMES=60 ./scripts/run.sh --mode palm_hand --perf --sensor-fps 90
PERF_WARMUP_FRAMES=60 ./scripts/run.sh --mode fullcascade --perf --sensor-fps 90

# Fullcascade 去掉分类位图，定位约 59 ms texture 尖峰
PERF_WARMUP_FRAMES=60 ./scripts/run.sh --mode fullcascade --perf --sensor-fps 90 \
  --osd_no_gloss

# 去掉全部 OSD，得到纯推理基线
PERF_WARMUP_FRAMES=60 ./scripts/run.sh --mode fullcascade --perf --sensor-fps 90 \
  --osd_mode=none
```
