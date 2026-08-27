# SLR 手语识别系统工作流程说明

本文档说明当前 `ssne_ai_demo` 中 SLR（Sign Language Recognition，手语识别）三级级联系统的运行流程。入口文件是 `main.cpp`；`./scripts/run.sh --mode palm|palm_hand|fullcascade` 分别运行一、二、三级模型，不传模式时默认启动 `fullcascade`。旧式位置参数仍兼容。

## 1. 当前功能概览

当前程序完成的任务：

- 从板端摄像头获取 `720x1280` 的 `SSNE_Y_8` 灰度图。
- 将原始图像顺时针旋转 90 度，再双线性 resize 到 `384x224`。
- 将 `384x224` 灰度图送入 `/app_demo/app_assets/models/palm.m1model`。
- 解码模型 4 个输出，得到最多 2 个手掌检测结果。
- 每个检测结果包含 1 个手掌框和 2 个关键点：手腕、中指根部。
- 将模型坐标反变换回原始 `720x1280` 摄像头画面坐标。
- 默认使用 OSD 绘制 palm 检测框和 palm 两个关键点。
- `--osd_palm_confidence_threshold`（默认 0.25）只在显示层隐藏低分 Palm 图元以及与其配对的 Hand 骨架，不修改 PalmResult/HandResult。
- 在 `palm_hand/fullcascade` 模式下，若 palm 检测有效，则根据手掌框和两个关键点生成 hand ROI，裁剪 `256x256` 8-bit 灰度 ROI，并作为 `SSNE_Y_8` 输入串行调用 `/app_demo/app_assets/models/hand.m1model`。
- Hand 输出的 21 个二维关键点会投影回原始画面。`hand_flag_score < --osd_hand_confidence_threshold`（默认 0.99）时只禁止 OSD 骨架，不删除关键点，也不改变 SSTCN 输入。
- `fullcascade` 将每侧 `21 hand + 4 palm box corner + 2 palm keypoint` 打包为 `1x4x54x64`，输出 6 类（含 `no_gesture`）。

## 2. 主循环和刷新策略

主循环位于 `main.cpp`：

```cpp
while (!check_exit_flag()) {
    if (!processor.GetImage(&image_tensor)) {
        continue;
    }
    if (frame_index % infer_interval == 0) {
        palm_model.Predict(...);
        if (mode != palm) hand_model.Predict(...);
        if (mode == fullcascade && source_index % sample_stride == 0) {
            fullcascade_model.UpdateAndPredict(...);
        }
    }
    visualizer.DrawDetections(...);  // 显示过滤/平滑不回写模型结果
    frame_index += 1;
}
```

当前使用命令行参数控制推理策略：每 `kInferInterval` 帧执行一次 palm 推理；启用 hand 时，同一个推理帧会继续执行 palm + hand 级联推理。其余帧跳过推理但仍使用上一次的检测结果执行绘图。默认 `kInferInterval = 1`，即每帧刷新 palm 检测结果。

每个推理帧完整执行：

1. 摄像头取图（每帧都执行）。
2. 输入预处理。
3. palm 模型推理和后处理。
4. `palm_hand/fullcascade` 且 palm 有有效检测时，基于 palm bbox + wrist/middle 两点生成 ROI，串行执行 hand landmarker。
5. `fullcascade` 按采样步长更新 64 帧窗口；空帧保持全零，等待态无手直接输出 `no_gesture`。
6. OSD 构建本帧图元并原子 flush；非推理帧使用缓存结果，活动 graphic layer 不再先清空成白屏。

性能监控开启后，`palm_ms` 包住整个 `PALMDETECTOR::Predict()`；`palm_pre_ms`、`palm_infer_ms`、`palm_post_ms` 分别给出 CPU 预处理/输入装载、`ssne_inference()` 和 `getoutput + decode/NMS` 耗时。板端只记录时间 bin、帧/样本/失败计数和持续时间，不计算 FPS、均值、P95、实时性指标或评分；全部指标由 Python 离线计算。Gloss 的窗口统计、`motion_p95` 排序、RAW logits 和预测终端行也只在命中 `fullcascade_debug_interval` 或开启 verbose 时执行；脚本的 `--perf` 模式自动使用静默间隔。

关键问题的当前答案：

- 每隔多少帧推理一次：每 `kInferInterval` 帧推理一次。当前默认 `kInferInterval = 1`。
- 每隔多少帧绘图一次：每 1 帧绘图一次。非推理帧使用缓存的上一次 palm/hand 检测结果。
- 图像刷新频率是多少：代码没有固定 FPS，由取图、绘图和（仅在推理帧）palm 预处理、palm 推理、palm 后处理、hand ROI 预处理、hand 推理和 hand decode 的总耗时决定。
- 每隔多少帧打印详细日志：Palm 仅在 `--palm_verbose` 时约每 50 个应用帧打印；SSTCN 由 `--fullcascade_verbose` 和 `--fullcascade_debug_interval` 控制。
- 当前是否执行 hand landmarker：由 `--mode` 决定；`palm_hand/fullcascade` 执行。
- 当前默认是否打印详细日志：否，`palm_verbose = false`、`fullcascade_verbose = false`。
- 当前是否向板端文件系统写调试文件：否。C++ 程序不再写 `/tmp/palm_debug` 或任何 raw dump 文件。
- 当前是否打印性能时间：默认否；只有 `--perf_monitor` 开启。`--perf_interval=0` 仅在退出时打印原始时间直方图，非零时周期性打印原始耗时，不在板端生成统计指标。

如果后续需要调试，优先使用 `--palm_verbose` 或 `--fullcascade_verbose`，让诊断信息走控制台日志，而不是写板端文件。

## 3. 性能测试系统

当前 `main.cpp` 内置 `PerformanceMonitor`，三种模式共用同一计时接口。该模块不写文件、不计算评测指标，只向终端打印原始时间；实时性、端到端 P95、波动、丢帧估算和评分由 `scripts/analyze_perf_log.py` 完成。

详细性能测试系统介绍见：[PERFORMANCE_MONITOR_USAGE.md](./PERFORMANCE_MONITOR_USAGE.md)。

## 4. 摄像头取图

摄像头取图逻辑在 `src/pipeline_image.cpp`。

初始化时：

```cpp
format_online = SSNE_Y_8;
OnlineSetCrop(kPipeline0, 0, width, 0, height);
OnlineSetOutputImage(kPipeline0, format_online, width, height);
OpenOnlinePipeline(kPipeline0);
```

当前图像尺寸配置在 `main.cpp`：

```cpp
const std::array<int, 2> image_shape = {720, 1280};
```

含义是：

- `image_shape[0] = width = 720`
- `image_shape[1] = height = 1280`

每帧通过：

```cpp
GetImageData(img_sensor, kPipeline0, kSensor0, 0);
```

得到一帧 `ssne_tensor_t`。

## 5. 输入预处理

预处理入口是 `PALMDETECTOR::PreprocessRotateResize()`，位于 `src/palm_detector.cpp`。

当前关键配置：

```cpp
const std::array<int, 2> palm_input_shape = {224, 224};
const bool use_ai_preprocess = false;
const bool rotate_clockwise = true;
```

由于训练数据方向和板端摄像头方向不同，不能直接把 `720x1280` resize 到 `384x224`。正确流程是：

1. 读取原始 `720x1280` 灰度图。
2. 顺时针旋转 90 度，得到 `1280x720` 图像。
3. 使用接近 OpenCV 默认 `cv2.resize` 的双线性插值 resize 到 `384x224`。
4. 将 `384x224` 的 `uint8` 灰度 buffer 直接拷贝到模型输入 tensor。

当前实现是融合路径：对每个 `384x224` 输出像素，直接反算到旋转后的坐标，再映射回原始摄像头图像取 4 邻域做双线性插值。它不会生成完整的 `1280x720` 旋转中间 buffer。

相关函数：

- `ResizeClockwiseRotatedBilinear()`
- `ResizeBilinear()`

当前没有使用 SDK 的 AI preprocess pipeline。模型 normalize 参数仍会在初始化时读取并打印，但实际输入走手写预处理路径：

```cpp
load_tensor_buffer_ptr(inputs[0], manual_input_buffer.data(), manual_input_buffer.size());
```

## 6. 模型输入输出

palm 模型路径：

```text
/app_demo/app_assets/models/palm.m1model
```

palm 模型输入：

- `384x224`
- `SSNE_Y_8`
- `uint8` 灰度

palm 模型输出数量：

```cpp
static const int kPalmOutputCount = 4;
```

当前根据输出元素数量自动映射 4 个输出：

- `reg14`: `14 * 14 * 16 = 3136`
- `cls14`: `14 * 14 * 2 = 392`
- `reg7`: `7 * 7 * 16 = 784`
- `cls7`: `7 * 7 * 2 = 98`

每个网格有 2 个 anchor。每个 anchor 的回归通道数为：

```cpp
4 + 2 * 2 = 8
```

含义是：

- 4 个 box 回归量：`dx, dy, dw, dh`
- 2 个关键点，每个关键点有 `x, y`

hand landmarker 模型路径：

```text
/app_demo/app_assets/models/hand.m1model
```

hand landmarker 输入：

- `256x256`
- `SSNE_Y_8`
- `uint8` 灰度 ROI
- ROI 来自 palm 检测结果，不独立运行。

hand ROI 生成逻辑位于 `src/hand_landmarker.cpp`：

1. 使用 palm 检测框作为基础 rect。
2. 使用 palm 的两个关键点（手腕、中指根部）计算旋转角。
3. 按 `scale_x=1.8`、`scale_y=1.8`、`shift_y=-0.1` 扩展并平移 ROI。
4. 将原始摄像头灰度图按旋转 ROI 做 affine bilinear crop，得到 `256x256` 的 8-bit ROI。
5. 将 8-bit ROI 装载到 `SSNE_Y_8` tensor。

hand landmarker 输出数量：

```cpp
static const int kHandOutputCount = 3;
```

当前根据输出元素数量自动查找 42 个 landmark 值，并把其余两个标量按输出顺序作为 `hand_flag` 和 `handedness`：

- `landmarks`: `21 * 2 = 42`
- `hand_flag`: 手是否存在的置信度。只用于 OSD 骨架门控，默认阈值 0.99；不作为 SSTCN 特征门控。
- `handedness`: 左右手概率，目前只记录，不参与 OSD 绘制。

landmark 输出按当前模型契约直接解释为 ROI 内归一化坐标，并通过 ROI 三点仿射关系投影回原始 `720x1280` 画面坐标；首帧诊断会打印原始数值范围，便于发现模型契约不一致。

## 7. 输出内存布局

当前板端 SSNE 输出按 `HWC` 方式解码：

```cpp
const PalmOutputLayout output_layout = kPalmOutputLayoutHwc;
```

代码仍保留 `PalmOutputLayout` 和 `GetOutputIndex()`，这样以后如果某个新模型或新转换工具输出布局不同，可以切换到 `kPalmOutputLayoutNchw` 验证。但常规 verbose 日志中不再每 50 帧同时跑 NCHW/HWC 双路对照，避免稳定版本做额外计算和刷冗余日志。

## 8. Anchor 和后处理

核心常量定义在 `include/common.hpp`：

```cpp
kPalmFeatureHigh = 14 x 24
kPalmFeatureLow = 7 x 12
kPalmNumAnchorsPerCell = 2
kPalmNumKeypoints = 2
kPalmScoreThreshold = 0.25
kPalmNmsIouThreshold = 0.10
kPalmCrossHeadSuppressIou = 0.10
kPalmMaxDetections = 2
```

Anchor 尺寸：

- `14x24` head:
  - anchor 0: `0.1188828125 x 0.2170138889`
  - anchor 1: `0.129875 x 0.34175`
- `7x12` head:
  - anchor 0: `0.171640625 x 0.2737222222`
  - anchor 1: `0.193296875 x 0.4076527778`

box 解码：

```cpp
cx = anchor.cx + dx * anchor.w
cy = anchor.cy + dy * anchor.h
w = anchor.w * exp(dw)
h = anchor.h * exp(dh)
```

关键点解码：

```cpp
kx = anchor.cx + raw_kx * anchor.w
ky = anchor.cy + raw_ky * anchor.h
```

候选框分数低于 `0.25` 会被过滤。之后执行 NMS，并最多保留 2 个检测结果。

## 9. 坐标反变换

模型输出坐标是相对于旋转后再 resize 的 `384x224` 图像的归一化坐标。绘图需要映射回原始 `720x1280` 摄像头画面。

关键函数：

- `MapPoint()`
- `MapBox()`

在顺时针旋转 90 度时，归一化点先映射到旋转后图像：

```cpp
rotated_x = model_x * (rotated_width - 1)
rotated_y = model_y * (rotated_height - 1)
```

再反变换回原始图像：

```cpp
original_x = rotated_y
original_y = original_height - 1 - rotated_x
```

检测框通过 4 个角点分别反变换，然后重新取 `min/max` 得到原图上的轴对齐框。

## 10. OSD 绘制

绘制逻辑位于 `src/utils.cpp`。

当前每帧调用：

```cpp
visualizer.DrawDetections(palm_result, hand_result);
```

绘图步骤：

1. 对 Palm 结果执行显示专用门控：`score >= --osd_palm_confidence_threshold`（默认 0.25）时才绘制框和两个 Palm 点。
2. HandResult 与 PalmResult 按检测索引一一配对；只有配对 Palm 通过上述门控，且 `hand_flag_score >= --osd_hand_confidence_threshold`（默认 0.99），才绘制 Hand 骨架。
3. 两种显示门控都不创建过滤后的推理结果，也不回写原始对象；Gloss Translator 在 OSD 之前已经接收完整 Palm/Hand 数据。
4. 对通过门控的 21 点做显示专用运动自适应 EMA（`--osd_hand_smoothing_alpha`，默认基础 alpha 为 0.45），按 wrist 最近距离匹配最多两条显示轨迹；快速移动时 alpha 自动升至接近 1。
5. 将 Palm 与 Hand 图元加入本帧 graphic layer，活跃 layer 只做一次 SDK 原子 flush；仅清理从有内容变为空的 layer。
6. fullcascade 分类位图只有路径变化时才更新，第 7 层继续保留上一张稳定图。

当前手掌框不用 `TYPE_HOLLOW` 矩形绘制，而是用 4 条 `DrawLine()` 绘制：

- 上边
- 右边
- 下边
- 左边

这样走实心 OSD 图元路径，实测比空心矩形稳定。

当前可视化参数：

```cpp
point_size_ = 5
point_color_ = 2
box_border_ = 6
box_color_ = 2
hand_line_thickness_ = 3
hand_line_color_ = 3
```

hand landmarker 当前不绘制 21 个点本身，只绘制连线。连线关系与 `ref/osd/visualizer.cpp` 一致：

```text
0-1-2-3-4
0-5-6-7-8
5-9-10-11-12
9-13-14-15-16
13-17-18-19-20
0-17
```

## 11. 调试输出

当前默认：

```cpp
mode = palm_only
infer_interval = 1
palm_verbose = false
fullcascade_debug_interval = 30
perf_monitor = false
```

当 `palm_verbose=false`、`fullcascade_verbose=false` 且 performance monitor 关闭时，主循环不打印每帧详细诊断日志，只保留初始化、分类事件和错误日志。

启用 `palm_hand/fullcascade` 模式后，hand landmarker 会额外打印一次轻量诊断信息，包括首个有效 ROI 的四边形、灰度统计、归一化 landmark 原始数值范围以及投影后的 landmark bbox。该日志用于判断 hand 骨架偏移或偏小发生在 ROI、模型输出还是坐标投影阶段。

开启 `--palm_verbose` 时，Palm 每隔一定推理帧输出诊断信息；开启 `--fullcascade_verbose` 时，SSTCN 输出详细窗口、logits 和特征统计。性能评测建议两者均关闭，仅使用 `--perf_monitor`。

- 摄像头 tensor 信息。
- 手写预处理后的输入 tensor 信息。
- 模型输入 tensor 信息。
- 4 个输出 tensor 的尺寸、dtype、元素数量和值域统计。
- 输出映射结果。
- 当前使用的输出布局。
- 最终检测框和关键点坐标。

当前版本不再提供 C++ 文件 dump，不会向 `/tmp` 或其他板端目录写 raw 输入/输出。这样更适合烧录到开发板后的长期运行。

如果后续新增模型时确实需要逐元素对齐 Python/ONNX，建议临时增加受宏或显式开关保护的 dump 逻辑，用完后再移除；不要让文件写入路径长期默认存在于板端程序中。

## 12. 快速入手读代码路线

建议按下面顺序读：

1. `main.cpp`
   - 看主流程、模型路径、图像尺寸、是否旋转、`--kInferInterval`、`--mode`、verbose 开关和性能统计配置。
   - 确认当前推理频率（默认 `kInferInterval=1`，即每帧执行一次 palm 推理）和 hand 是否启用（默认不启用）。

2. `src/pipeline_image.cpp`
   - 看摄像头 pipeline 如何打开。
   - 确认输出格式是 `SSNE_Y_8`，尺寸是 `720x1280`。

3. `include/common.hpp`
   - 看 palm 相关结构体和常量。
   - 重点看 palm anchor、阈值、输出布局、`PalmDetection`、`PalmResult`、`PalmPredictTiming`，以及 hand 的 `HandDetection`、`HandResult`、`HANDLANDMARKER`。

4. `src/palm_detector.cpp`
   - 先看 `Initialize()` 和 `Predict()`。
   - `Predict()` 内部已经拆分计时：`palm_preprocess`、`palm_input_load`、`palm_inference`、`palm_getoutput`、`palm_output_meta`、`palm_decode`、`palm_verbose_log`。
   - `palm_preprocess` 内部还会继续拆为 `palm_preprocess_transform` 和 `palm_preprocess_manual_load`。当前旋转和 resize 是融合实现，`palm_preprocess_transform` 不是单独旋转耗时，而是融合旋转缩放耗时。
   - 再看 `PreprocessRotateResize()`。
   - 然后看 `MapOutputs()`、`DecodeHead()`、`SelectDetections()`。
   - 最后看 `MapPoint()` 和 `MapBox()`。

5. `src/hand_landmarker.cpp`
   - 先看 `Predict()`：它只在 palm 有检测结果时工作。
   - 再看 `BuildRoiRect()`、`PreprocessRoi()`、`DecodeOutputs()`。
   - 如果 hand 骨架不准，优先检查一次性 `[HAND][debug]` 日志中的 ROI 参数、归一化 hand 输出范围和投影 bbox。

6. `src/utils.cpp`
   - 看 OSD 如何清屏、画框、画关键点。
   - 如果日志坐标正常但画面显示异常，优先检查这里。

7. `src/osd-device.cpp`
   - 看底层 OSD 图元如何转成 SDK 调用。
   - 重点关注 layer、颜色、alpha、`TYPE_HOLLOW`/`TYPE_SOLID`。

8. `ref/palm/infer_model_gray.py`
   - 作为 Python 侧参考实现。
   - 新模型接入时，优先对齐这里的预处理、输出 reshape、后处理和 NMS。

9. `ref/hand/infer_frames_with_roi.py` 和 `ref/hand/roi_utils.py`
   - 作为 hand ROI、crop、21 点解码和反投影的 Python 侧参考实现。

## 13. 修改参数时的注意事项

- 如果想调整推理频率，启动时传入 `--kInferInterval N`（默认为 1）。值越大推理越少、平均处理压力越低，但 palm/hand 检测结果刷新越慢。
- 如果想启用 hand landmarker，选择 `--mode palm_hand` 或 `--mode fullcascade`。
- 板端推荐通过 `./scripts/run.sh --mode ...` 切换；加 `--perf --sensor-fps N` 可为当前模式开启相同的纯计时链路。
- `--osd_palm_confidence_threshold` 和 `--osd_hand_confidence_threshold` 均只影响 OSD；不要把它们误当成 Gloss 特征门控。
- 如果只想减少日志，不要改推理逻辑：保持 `--palm_verbose`/`--fullcascade_verbose` 关闭，并增大 `--fullcascade_debug_interval`。
- 性能评测时必须把真实传感器配置帧率传给 `--perf_sensor_fps`，否则实时性比例和延迟周期数会偏。
- 如果后续串行接入更多模型导致处理压力上升，可以增大 `kInferInterval` 来启用更激进的跳帧策略。
- 如果更换模型，必须重新确认：
  - 输入尺寸。
  - 输入 dtype。
  - 是否需要旋转。
  - resize 插值方式。
  - 输出数量和顺序。
  - 输出内存布局。
  - anchor 配置。
  - 坐标反变换。
  - OSD 绘制路径。
