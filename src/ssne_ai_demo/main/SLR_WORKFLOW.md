# SLR 手语识别系统工作流程说明

本文档说明 `ssne_ai_demo` 当前的板端运行流程。程序由 palm detector 和可选 hand landmarker 两级模型组成：默认只运行 palm detector；启动时传入 `--enable_hand` 后，在 palm 检测成功的推理帧上继续串行调用 hand landmarker。

## 1. 当前功能概览

- 从板端摄像头获取 `720x1280` 的 `SSNE_Y_8` 灰度图。
- 将原始灰度图按双线性插值缩放到 `224x224`。
- 将 `224x224` 灰度图送入 `/app_demo/app_assets/models/palm.m1model`。
- 解码 palm 模型 4 个输出，得到最多 2 个手掌检测结果。
- 每个 palm 检测结果包含 1 个手掌框和 2 个关键点：手腕、中指根部。
- 将模型归一化坐标直接映射回原始 `720x1280` 摄像头画面坐标。
- 默认使用 OSD 绘制 palm 检测框和两个 palm 关键点。
- 传入 `--enable_hand` 后，基于 palm 检测结果生成 hand ROI，裁剪 `256x256` 灰度 ROI，并串行调用 `/app_demo/app_assets/models/hand.m1model`。
- 启用 hand 时，解码 hand landmarker 的 21 个二维关键点，投影回原始画面坐标，并绘制 hand 骨架连线。

## 2. 主循环和刷新策略

主循环位于 `main.cpp`：

```cpp
while (!check_exit_flag()) {
    processor.GetImage(&image_tensor);

    PalmPredictTiming palm_timing;
    if (frame_index % infer_interval == 0) {
        const bool verbose_log = verbose && (frame_index % verbose_interval == 0);
        palm_model.Predict(&image_tensor, &palm_result, frame_index, verbose_log, &palm_timing);
        if (enable_hand) {
            hand_model.Predict(&image_tensor, palm_result, &hand_result);
        } else {
            hand_result.Clear();
        }
    }

    visualizer.DrawDetections(palm_result, hand_result);
    perf_monitor.AddFrame(frame_index, timing);
    frame_index += 1;
}
```

命令行参数：

- `--kInferInterval N`: 每 `N` 帧执行一次 palm 推理，默认 `1`。
- `--enable_hand`: 启用 palm + hand 级联模式。

每一帧都会取图和绘图。只有推理帧会执行 palm detector；启用 hand 时，推理帧还会在 palm 有有效检测后执行 hand landmarker。非推理帧沿用上一次检测结果绘制。

## 3. 摄像头取图

摄像头取图逻辑在 `src/pipeline_image.cpp`。

```cpp
format_online = SSNE_Y_8;
OnlineSetCrop(kPipeline0, 0, width, 0, height);
OnlineSetOutputImage(kPipeline0, format_online, width, height);
OpenOnlinePipeline(kPipeline0);
```

图像尺寸配置在 `main.cpp`：

```cpp
const std::array<int, 2> image_shape = {720, 1280};
```

含义：

- `image_shape[0] = width = 720`
- `image_shape[1] = height = 1280`

## 4. Palm 输入预处理

预处理入口是 `PALMDETECTOR::PreprocessResize()`，位于 `src/palm_detector.cpp`。

关键配置：

```cpp
const std::array<int, 2> palm_input_shape = {224, 224};
const bool use_ai_preprocess = false;
```

当前流程：

1. 读取原始 `720x1280` 灰度图。
2. 使用接近 Python 参考脚本中 `Image.Resampling.BILINEAR` 的双线性插值缩放到 `224x224`。
3. 将 `224x224` 的 `uint8` 灰度 buffer 装载到 `manual_input` tensor。
4. 将同一 buffer 复制到 palm 模型输入 tensor。

核心调用：

```cpp
ResizeBilinear(camera_data, src_width, src_height,
               manual_input_buffer.data(), input_shape[0], input_shape[1]);
load_tensor_buffer_ptr(inputs[0], manual_input_buffer.data(), manual_input_buffer.size());
```

## 5. Palm 模型输入输出

palm 模型路径：

```text
/app_demo/app_assets/models/palm.m1model
```

palm 模型输入：

- `224x224`
- `SSNE_Y_8`
- `uint8` 灰度

palm 模型输出数量：

```cpp
static const int kPalmOutputCount = 4;
```

输出按元素数量自动映射为：

- `reg14`: `14 * 14 * 16 = 3136`
- `cls14`: `14 * 14 * 2 = 392`
- `reg7`: `7 * 7 * 16 = 784`
- `cls7`: `7 * 7 * 2 = 98`

每个网格有 2 个 anchor。每个 anchor 的回归通道数是 `8`：

- 4 个 box 回归量：`dx, dy, dw, dh`
- 2 个关键点，每个关键点有 `x, y`

输出内存布局按 `HWC` 解码：

```cpp
const PalmOutputLayout output_layout = kPalmOutputLayoutHwc;
```

## 6. Anchor 和后处理

核心常量定义在 `include/common.hpp`：

```cpp
kPalmFeature14 = 14
kPalmFeature7 = 7
kPalmNumAnchorsPerCell = 2
kPalmNumKeypoints = 2
kPalmScoreThreshold = 0.45
kPalmNmsIouThreshold = 0.25
kPalmMaxDetections = 2
```

Anchor 尺寸与 `ref/palm0701/anchor_utils.py` 对齐：

- `14x14` head:
  - anchor 0: `0.084651 x 0.032030`
  - anchor 1: `0.104590 x 0.079621`
- `7x7` head:
  - anchor 0: `0.137044 x 0.049292`
  - anchor 1: `0.157819 x 0.088213`

box 解码：

```cpp
cx = anchor.cx + dx * anchor.w;
cy = anchor.cy + dy * anchor.h;
w = anchor.w * exp(dw);
h = anchor.h * exp(dh);
```

关键点解码：

```cpp
kx = anchor.cx + raw_kx * anchor.w;
ky = anchor.cy + raw_ky * anchor.h;
```

候选框分数低于 `0.45` 会被过滤。之后对两个 head 的候选统一执行 NMS，并最多保留 2 个检测结果。

## 7. 坐标映射和 OSD

模型输出坐标是 `224x224` 输入图上的归一化坐标。绘图时直接映射回原始画面：

```cpp
pixel_x = model_x * (image_width - 1);
pixel_y = model_y * (image_height - 1);
```

绘制逻辑位于 `src/utils.cpp`：

1. 清理 OSD 图层 `0..6`。
2. 绘制 palm 检测框。
3. 绘制 palm 的两个关键点。
4. 如果 hand 结果有效，绘制 hand 21 点骨架连线。
5. flush 图层 `0..6`。

palm 框使用 4 条 `DrawLine()` 绘制，避免空心矩形路径不稳定。

## 8. Hand Landmarker

hand landmarker 默认不启用。传入 `--enable_hand` 后，只有 palm 有有效检测时才执行。

hand 模型路径：

```text
/app_demo/app_assets/models/hand.m1model
```

hand 输入：

- `256x256`
- `SSNE_Y_8`
- `uint8` 灰度 ROI

hand ROI 生成逻辑在 `src/hand_landmarker.cpp`：

1. 使用 palm 检测框作为基础 rect。
2. 使用 wrist 和 middle 两个 palm 关键点估计手部方向。
3. 按 `scale_x=1.8`、`scale_y=1.8`、`shift_y=-0.1` 扩展和平移 ROI。
4. 对原始灰度图做 affine bilinear crop，得到 `256x256` ROI。
5. 将 ROI 装载到 hand 模型输入 tensor。

hand 输出数量：

```cpp
static const int kHandOutputCount = 3;
```

当前根据输出元素数量自动查找 42 个 landmark 值，并把另外两个标量作为 `hand_flag` 和 `handedness`。

## 9. 性能监控

`main.cpp` 内置 `PerformanceMonitor`，每 `120` 帧打印一次 `[PERF]` 日志。性能系统不写文件，只走终端输出；离线统计和绘图由 `scripts/analyze_perf_log.py` 完成。

Palm 内部细分字段：

- `palm_preprocess`: `PreprocessResize()` 总耗时。
- `palm_preprocess_resize`: 双线性缩放耗时。
- `palm_preprocess_manual_load`: 将 `manual_input_buffer` 装载到 `manual_input` tensor 的耗时。
- `palm_input_load`: 将预处理输出装载到模型输入 tensor 的耗时。
- `palm_inference`: `ssne_inference()` 耗时。
- `palm_getoutput`: `ssne_getoutput()` 耗时。
- `palm_output_meta`: 获取输出 tensor metadata 和输出映射耗时。
- `palm_decode`: decode、NMS、候选选择和坐标映射耗时。
- `palm_verbose_log`: verbose 诊断打印耗时。
- `palm_accounted`: 已细分阶段加和。

详细性能测试系统见 [SLR_PERFORMANCE_TESTING.md](./SLR_PERFORMANCE_TESTING.md)。

## 10. 快速读代码路线

1. `main.cpp`: 主流程、命令行参数、推理间隔、hand 开关和性能统计。
2. `src/pipeline_image.cpp`: 摄像头 pipeline 输出格式和尺寸。
3. `include/common.hpp`: palm/hand 结构体、常量、模型类声明。
4. `src/palm_detector.cpp`: palm 输入缩放、输出映射、decode、NMS 和坐标映射。
5. `src/hand_landmarker.cpp`: hand ROI、crop、模型调用和关键点投影。
6. `src/utils.cpp`: OSD 绘制。
7. `src/performance_monitor.cpp`: `[PERF]` 日志统计。
8. `scripts/analyze_perf_log.py`: PC 端性能日志分析和图表输出。
