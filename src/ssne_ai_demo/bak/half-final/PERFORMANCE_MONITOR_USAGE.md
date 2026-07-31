# Performance Monitor 使用说明

## 本次修改内容

- 已备份当前板端代码到：
  `device_backup_20260720_before_perf_monitor`
- 已确认：连续 64 帧取样逻辑没有修改。
- 已在 `device/main.cpp` 加入 `PerformanceMonitor`。
- 默认不开启，只有加 `--perf_monitor` 才会进入 perf 专用循环；不开启时仍走原来的普通循环。
- 三种模式都支持：
  `palm`、`palm_hand`、`fullcascade`
- 已新增日志分析脚本：
  `device/scripts/analyze_perf_log.py`
- 已同步到指定的 D 盘工程：
  `D:\A1_Project\data\A1_SDK_SC132GS\smartsens_sdk\smart_software\src\app_demo\face_detection\ssne_ai_demo\scripts\analyze_perf_log.py`
- `main.cpp` 和 `README.md` 也已同步到 D 盘对应工程目录。
- C++ 语法检查通过，Python 脚本编译检查通过，D 盘工程源码只读语法检查也通过。

## 板端测试命令

Palm only:

```sh
./ssne_ai_demo --mode palm --kInferInterval=1 --perf_monitor --perf_interval=30 --perf_sensor_fps=30
```

Palm + Hand:

```sh
./ssne_ai_demo --mode palm_hand --kInferInterval=1 --perf_monitor --perf_interval=30 --perf_sensor_fps=30
```

Palm + Hand + Gloss Translator:

```sh
./ssne_ai_demo --mode fullcascade --kInferInterval=1 --perf_monitor --perf_interval=30 --perf_sensor_fps=30 --fullcascade_debug_interval=999999
```

终端会打印简洁的 `[PERF]` 采样行和退出时的 `[PERF_SUMMARY]` 总结行。

## 离线分析日志

将开发板终端输出复制到 `board_output.log` 后运行：

```sh
python scripts/analyze_perf_log.py board_output.log --out-json perf.json --out-csv perf.csv
```

如果 fullcascade 想只统计 64 帧窗口 ready 后的稳定阶段，用：

```sh
python scripts/analyze_perf_log.py board_output.log --ready-only --samples-only
```

这时建议板端运行时把 `--perf_interval=1`，这样 ready-only 的统计最精确。
