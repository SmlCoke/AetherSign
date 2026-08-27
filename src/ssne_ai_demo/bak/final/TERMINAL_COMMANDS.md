# 板端运行命令

## 三种模式：关闭 Performance Monitor

```sh
./scripts/run.sh --mode palm --no-perf
./scripts/run.sh --mode palm_hand --no-perf
./scripts/run.sh --mode fullcascade --no-perf
```

## 三种模式：开启 Performance Monitor

Aurora 观测参考帧率按 69 FPS 记录：

```sh
PERF_INTERVAL=0 PERF_WARMUP_FRAMES=60 ./scripts/run.sh --mode palm --perf --sensor-fps 69
PERF_INTERVAL=0 PERF_WARMUP_FRAMES=60 ./scripts/run.sh --mode palm_hand --perf --sensor-fps 69
PERF_INTERVAL=0 PERF_WARMUP_FRAMES=60 ./scripts/run.sh --mode fullcascade --perf --sensor-fps 69
```

按摄像头标称 90 FPS 记录：

```sh
PERF_INTERVAL=0 PERF_WARMUP_FRAMES=60 ./scripts/run.sh --mode palm --perf --sensor-fps 90
PERF_INTERVAL=0 PERF_WARMUP_FRAMES=60 ./scripts/run.sh --mode palm_hand --perf --sensor-fps 90
PERF_INTERVAL=0 PERF_WARMUP_FRAMES=60 ./scripts/run.sh --mode fullcascade --perf --sensor-fps 90
```

运行至少 60 秒后输入：

```text
q
```

## Palm OSD 置信度阈值

取值范围：`0.0`～`1.0`；默认值：`0.25`。

```sh
./scripts/run.sh --mode palm --no-perf --osd_palm_confidence_threshold=0.50
./scripts/run.sh --mode palm_hand --no-perf --osd_palm_confidence_threshold=0.50
./scripts/run.sh --mode fullcascade --no-perf --osd_palm_confidence_threshold=0.50
```

## Hand OSD 置信度阈值

取值范围：`0.0`～`1.0`；默认值：`0.99`。

```sh
./scripts/run.sh --mode palm_hand --no-perf --osd_hand_confidence_threshold=0.99
./scripts/run.sh --mode fullcascade --no-perf --osd_hand_confidence_threshold=0.99
```

## 同时设置两个 OSD 阈值

```sh
./scripts/run.sh --mode palm_hand --no-perf \
  --osd_palm_confidence_threshold=0.50 \
  --osd_hand_confidence_threshold=0.99

./scripts/run.sh --mode fullcascade --no-perf \
  --osd_palm_confidence_threshold=0.50 \
  --osd_hand_confidence_threshold=0.99
```

## Performance Monitor 与两个 OSD 阈值同时使用

```sh
PERF_INTERVAL=0 PERF_WARMUP_FRAMES=60 ./scripts/run.sh \
  --mode fullcascade --perf --sensor-fps 69 \
  --osd_palm_confidence_threshold=0.50 \
  --osd_hand_confidence_threshold=0.99
```

两个置信度阈值只过滤 OSD 绘制，不过滤送入 Gloss Translator 的 Palm、Hand 数据。

## PC 端分析复制的性能日志

```sh
python scripts/analyze_perf_log.py performance_log.txt
python scripts/analyze_perf_log.py performance_log.txt --sensor-fps 69
python scripts/analyze_perf_log.py performance_log.txt --sensor-fps 90
```
