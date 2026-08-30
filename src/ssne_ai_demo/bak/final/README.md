# AetherSign: Sign Language Recognition System

> [!IMPORTANT]
> **许可证边界说明**
>
> 本目录在公共 Git 中仅保留可公开发布的 PeakDragonSoar 团队原创组件。出于厂商 SDK 许可边界考虑，部分构建配置、程序入口、OSD、Pipeline 集成文件，以及其修改版本未纳入公共历史。因此，下文现有的编译命令仅适用于团队本地保留的完整目录；**公开部分代码无法直接编译运行**。
>
> 完整可编译环境由两部分组成：本仓库**公开的 AetherSign 原创组件**，以及用户**通过合法授权渠道取得的厂商 SDK/集成文件**。请先取得相应 SDK 使用许可，再通过仓库 Issue 联系维护者获取复现指导和可提供的团队原创组件。请勿在公开 Issue 上传 SDK 文件、许可证凭据或其他厂商材料。团队不承诺、也不会公开分发厂商来源文件或其修改版本。

## I. 代码简介

本段程序是基于思特威 A1 NPU 的中文手语识别系统的板端程序源代码。
除了本文件夹下的代码，还需要配合：

```
D:\Docker\project\smartsen\data\A1_SDK_SC132GS\smartsens_sdk\output\opt\m1_sdk\usr\include\smartsoc
```

下的头文件，尤其是：

- ssne_api.h
- osd_lib_api.h

主要文档：

- （古老）最开始 Palm Detector 模型上板踩坑总结出来的经验：[PALM_DEBUGGING_NOTES.md](./PALM_DEBUGGING_NOTES.md)
- SLR 手语识别系统工作流程说明：[SLR_WORKFLOW.md](./SLR_WORKFLOW.md)
- SLR 性能测试说明：[PERFORMANCE_MONITOR_USAGE.md](./PERFORMANCE_MONITOR_USAGE.md)


## II. 文件目录结构

### 2.1 `./main.cpp`

- 程序入口文件，负责解析命令行参数、初始化 SDK/模型/摄像头/OSD、运行主循环、调度 palm + hand 级联推理、绘图和性能统计。

### 2.2 `./include/`

- `common.hpp`: 定义 palm detector、hand landmarker、图像处理器等核心类、结果结构体和模型相关常量。
- `log.hpp`: 简单日志宏定义。
- `osd-device.hpp`: OSD 底层绘制封装的头文件，声明线框、点、线段等显示接口。
- `performance_monitor.hpp`: 性能统计系统的头文件，定义 `PerfSample` 和 `PerformanceMonitor`。
- `utils.hpp`: 上层可视化绘制工具声明，负责把检测结果转成 OSD 绘制调用。

### 2.3 `./scripts/`

- `analyze_perf_log.py`: PC 端性能日志分析与绘图脚本，输出终端摘要，并可生成 JSON、CSV 和 SVG 图表。（该脚本仅在 PC 端使用）
- `run.sh`: 板端启动脚本，将命令行参数转发给 `ssne_ai_demo`。

### 2.4 `./src/`

- `src/palm_detector.cpp`：Palm Detector 预处理、推理、解码和 NMS。
- `src/hand_landmarker.cpp`：手部 ROI 裁剪、推理、解码和坐标投影。
- `src/fullcascade_gesture_recognizer.cpp`：将 Palm + Hand 输出打包为 `[1,4,54,64]` 并运行 SSTCN。
- `src/pipeline_image.cpp`：摄像头流水线图像采集。
- `src/utils.cpp`、`src/osd-device.cpp`：OSD 绘制。

### 2.5 `./CMakeLists.txt`

- CMake 构建入口，定义 `ssne_ai_demo` 可执行文件、源码列表、头文件路径、SDK 库链接和安装规则。

### 2.6 `./app_assets/`

将以下文件放置于 `app_assets/` 目录下：

- `colorLUT.sscl`: OSD 绘制使用的颜色查找表。
- `osd_labels/*.ssbmp`: fullcascade 模式下用于在 OSD 图像层显示分类结果的位图标签。

`osd_labels` 当前为大字号中文/英文双行标签，并按开发板实测的 texture layer 方向做了默认补偿。
如果上板后仍出现旋转或镜像，可不重新编译，直接通过 `--osd_gloss_dir` 切换备用标签目录：

```text
/app_demo/app_assets/osd_labels             默认方向补偿版
/app_demo/app_assets/osd_labels_fix_ccw     反向 90 度补偿版
/app_demo/app_assets/osd_labels_fix_transpose 转置补偿版
/app_demo/app_assets/osd_labels_plain_large 未旋转的大字号原图版
```

将以下文件放置于 `app_assets/models/` 目录下：

- `models/palm.m1model`: palm detector 板端模型文件。
- `models/hand.m1model`: hand landmarker 板端模型文件。
- `models/slr5_fullcascade.m1model`: fullcascade 分类器模型文件。

`slr5_fullcascade.m1model` 必须符合导出的分类器契约：

```text
INPUT0:  [1, 4, 54, 64]
OUTPUT0: [1, 6, 1, 1]
classes: 0=rain, 1=long, 2=short, 3=go, 4=thick, 5=no_gesture
```

### 2.7 `./cmake_config/`

- `Paths.cmake`: 配置 M1 SDK 头文件、库文件和第三方依赖路径，供 `CMakeLists.txt` 引用。

### 2.8 `./scripts`

- `scripts/analyze_perf_log.py`：解析复制的终端日志，输出 FPS、延迟和各阶段耗时。
- `run.sh` 终端快速启动脚本，转发参数给 `ssne_ai_demo`。

## III. 编译运行方式

### 3.1 编译

将该文件整个文件夹放置在赛题二仓库的 `/smartsens_sdk/smart_software/src/app_demo/slr_system/ssne_ai_demo/` 目录下（注意 `slr_system` 是我们项目的文件夹名字，与原来的 face_detection 例程处于同一层级）。
然后：

```bash
cd A1_SDK_SC132GS/smartsens_sdk/
./scripts/a1_sc132gs_build.sh
```

### 3.2 运行

烧录到 A1 开发板后，重启开发板即可启动我们的 slr_system 程序。也可通过：
    
```sh
./ssne_ai_demo --mode palm --kInferInterval=1
./ssne_ai_demo --mode palm_hand --kInferInterval=1
./ssne_ai_demo --mode fullcascade --kInferInterval=1
```

每种模式下，每帧处理流程不同：

- `palm`：仅运行 Palm Detector。
- `palm_hand`：运行 Palm Detector，然后运行 Hand Landmarker。
- `fullcascade`：运行 Palm Detector、Hand Landmarker，然后运行 54 点 SSTCN 分类器。

推荐在板端通过统一脚本切换：

```sh
./scripts/run.sh --mode palm
./scripts/run.sh --mode palm_hand
./scripts/run.sh --mode fullcascade
```

其中 `kInferInterval` 是推理间隔帧数，默认为 1，即**每帧都推理**。可设置为 2、3 等整数，表示**每隔多少帧才进行一次推理**。

常用参数组合：

```sh
# 每 2 帧执行一次 Palm + Hand 推理
./ssne_ai_demo --mode palm_hand --kInferInterval=2

# 关闭 OSD，仅运行 Fullcascade 推理
./ssne_ai_demo --mode fullcascade --osd_mode=none

# Fullcascade 默认会在左上角显示分类位图；如需关闭该标签
./ssne_ai_demo --mode fullcascade --osd_no_gloss

# 仅显示手部关键点
./ssne_ai_demo --mode palm_hand --osd_mode=hand

# 指定 Palm 和 Hand 模型
./ssne_ai_demo --mode palm_hand --palm_model=/app_demo/app_assets/models/palm.m1model --hand_model=/app_demo/app_assets/models/hand.m1model

# 输出 Palm 调试信息
./ssne_ai_demo --mode palm --palm_verbose

# Palm 分数低于 0.60 时，只在 OSD 隐藏对应 Palm 图元和 Hand 骨架
./ssne_ai_demo --mode fullcascade --osd_palm_confidence_threshold=0.60

# Hand 置信度只控制 OSD；默认小于 0.99 不画骨架
./ssne_ai_demo --mode fullcascade --osd_hand_confidence_threshold=0.99

# 调节仅用于显示的运动自适应 EMA；1.0 完全关闭坐标平滑
./ssne_ai_demo --mode fullcascade --osd_hand_smoothing_alpha=0.45
```

`--osd_mode` 可选值为 `auto`、`all`、`hand`、`palm` 和 `none`。fullcascade 模式默认从
`/app_demo/app_assets/osd_labels` 加载 `.ssbmp` 位图，把当前稳定分类结果显示在 OSD
图像层；可用 `--osd_no_gloss` 关闭，或通过 `--osd_gloss_x`、`--osd_gloss_y` 调整位置。
默认 `--osd_gloss_stable_only` 只在 stable 类别变化时刷新分类位图，避免未稳定 top-1
抖动导致 RLE/texture 图像层频繁更新；如需恢复旧行为，可加 `--osd_gloss_top_fallback`。
若分类标签显示方向不正确，可用 `--osd_gloss_dir=/app_demo/app_assets/osd_labels_fix_ccw`
或其他备用目录快速切换。

`--osd_palm_confidence_threshold` 默认 0.25；低于它的 Palm 框、Palm 两点和与该 Palm 同索引的 Hand 骨架均不绘制。`hand_flag_score` 的默认 0.99 阈值继续控制通过 Palm 显示门控后的 Hand 骨架。两种阈值都只作用于 OSD：PalmResult、HandResult 和送入 SSTCN 的坐标完全不修改。EMA 在静止或微动时使用配置的基础 alpha，快速移动时自动提高到接近 1。没有 Palm/Hand 的帧在时序输入中保持全零语义；未触发手势时可直接输出训练标签 `no_gesture`。

**性能监控（Performance Monitor）默认关闭**。仅在性能分析时启用：

```sh
./ssne_ai_demo --mode fullcascade --kInferInterval=1 --perf_monitor --perf_interval=0
```

当未设置 `--perf_monitor` 时不会采集或输出性能样本。`--perf_warmup_frames` 可排除启动预热；正式测试使用 `--perf_interval=0`，运行中不刷屏，输入 `q` 后只输出紧凑 `[PERF_RAW_*]` 时间直方图。板端不计算 FPS、均值、P95、实时性比例或评分，统一交给 Python 离线完成。

三种模式的性能监控命令：

```sh
./ssne_ai_demo --mode palm --kInferInterval=1 --perf_monitor --perf_interval=0 --perf_sensor_fps=80

./ssne_ai_demo --mode palm_hand --kInferInterval=1 --perf_monitor --perf_interval=0 --perf_sensor_fps=80

./ssne_ai_demo --mode fullcascade --kInferInterval=1 --perf_monitor --perf_interval=0 --perf_sensor_fps=80 --fullcascade_debug_interval=999999
```

`--perf_interval=0` 表示运行中不打印逐样本日志，退出时只输出可复制的紧凑原始块；`--perf_sensor_fps=80` 仅设置离线统计的参考帧率，不修改摄像头帧率。完整评分口径、丢帧估算限制和实验矩阵见 `PERFORMANCE_MONITOR_USAGE.md`。

### 3.3 快速运行（利用`./scripts/run.sh`）

实际板端运行时可以通过 `./scripts/run.sh` 脚本快速启动：

- 运行 palm 模式（仅手掌检测）：`./scripts/run.sh --mode palm`
- 运行 palm_hand 模式（手掌检测 + 手部关键点检测）：`./scripts/run.sh --mode palm_hand`
- 运行 fullcascade 模式（手掌检测 + 手部关键点检测 + 手势识别）：`./scripts/run.sh --mode fullcascade`

脚本默认不开启性能监控。可用 `./scripts/run.sh --mode fullcascade --perf --sensor-fps 90` 开启纯计时监控；传感器 FPS 应填写现场的真实配置值。旧式位置参数和环境变量写法仍兼容。

不指定模式时默认运行 `fullcascade`。模式后的参数会追加到默认命令中，可用于调整或覆盖默认值：

```sh
# 默认运行 fullcascade
./scripts/run.sh

# Palm + Hand 每 2 帧推理一次，并仅显示手部关键点
./scripts/run.sh --mode palm_hand --kInferInterval=2 --osd_mode=hand

# Fullcascade：Palm OSD 阈值 0.60；不影响 Gloss 输入
./scripts/run.sh --mode fullcascade --osd_palm_confidence_threshold=0.60

# Fullcascade 开启详细调试日志
./scripts/run.sh --mode fullcascade --fullcascade_verbose --fullcascade_debug_interval=30
```
