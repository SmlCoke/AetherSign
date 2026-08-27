# 板端三模型数据链路说明

本文档按当前 `device` 代码梳理板端实时推理的数据链路：从摄像头拿到一帧 `720 x 1280` 灰度图开始，依次经过 Palm Detector、Hand Landmarker、SSTCN 手势分类模型，最后输出可视化和终端调试信息。

对应主流程入口是 `main.cpp`，核心实现分布在：

- `src/pipeline_image.cpp`: 摄像头在线取图
- `src/palm_detector.cpp`: palm 检测、旋转缩放、anchor 解码
- `src/hand_landmarker.cpp`: palm ROI 裁剪、手部 21 点反投影
- `src/fullcascade_gesture_recognizer.cpp`: Palm+Hand 时序打包、SSTCN 推理、稳定类别输出
- `src/utils.cpp`: OSD 绘制 palm 框和 hand 骨架

## 0. 总览

默认三模型串联运行命令：

```sh
./scripts/run.sh --mode fullcascade --kInferInterval=1
```

`fullcascade` 模式会串行打开 Palm、Hand 和 SSTCN。主循环中每隔 `kInferInterval` 帧执行一次模型链路：

```text
camera gray frame
  -> Palm Detector
  -> Hand Landmarker
  -> Gesture Recognizer / SSTCN
  -> OSD + terminal logs
```

当前固定配置：

```text
camera frame: 720 x 1280, SSNE_Y_8
palm input:   384 x 224, SSNE_Y_8
hand input:   256 x 256, SSNE_Y_8
gesture input contract: 1 x 4 x 54 x 64
gesture classes: 0 rain/雨, 1 long/长, 2 short/短, 3 go/去, 4 thick/粗, 5 no_gesture/无手势
```

注意：当前代码里的 `image_shape = {720, 1280}` 实际按 `width, height` 使用。也就是说原始摄像头图像是宽 720、高 1280 的竖屏灰度图。

## 1. 摄像头取图

初始化位置：`IMAGEPROCESSOR::Initialize`

```text
OnlineSetCrop(kPipeline0, 0, 720, 0, 1280)
OnlineSetOutputImage(kPipeline0, SSNE_Y_8, 720, 1280)
OpenOnlinePipeline(kPipeline0)
```

每帧取图位置：`IMAGEPROCESSOR::GetImage`

```text
GetImageData(img_sensor, kPipeline0, kSensor0, 0)
```

这一阶段输出：

```text
ssne_tensor_t image_tensor
width  = 720
height = 1280
format = SSNE_Y_8
data   = uint8 gray pixels, about 720 * 1280 bytes
```

后续 Palm 和 Hand 都直接使用这张原始灰度图。Palm 会对它做旋转缩放；Hand 会用它按 palm ROI 做仿射裁剪。

## 2. Palm Detector

模型路径：

```text
/app_demo/app_assets/models/palm.m1model
```

### 2.1 Palm 输入前处理

入口：`PALMDETECTOR::Predict`

前处理函数：`PreprocessRotateResize`

当前 `main.cpp` 中：

```text
use_ai_preprocess = false
rotate_clockwise  = true
output_layout     = HWC
```

因此 Palm 不走 SDK 的 AI preprocess，而是手动做灰度图处理：

```text
原始摄像头图: 720 x 1280, gray8
  -> clockwise rotate 成语义上的 1280 x 720
  -> bilinear resize 到 384 x 224
  -> 直接 copy 到 palm 模型输入 tensor
```

旋转关系可以理解为：

```text
rotated_x = original_height - 1 - original_y
rotated_y = original_x
```

也就是把竖屏板端画面转为接近训练/模型期望的横屏方向。

Palm 输入 tensor：

```text
  width  = 384
  height = 224
  format = SSNE_Y_8
  data   = uint8 gray pixels
  bytes  = 384 * 224 = 86016
```

### 2.2 Palm 模型输出

Palm 有 4 个输出，代码按元素数量自动映射为：

```text
reg14: 14 x 24 x 16 = 5376 values
cls14: 14 x 24 x 2  = 672 values
reg7:  7 x 12 x 16  = 1344 values
cls7:  7 x 12 x 2   = 168 values
```

其中：

```text
16 = 2 anchors * (4 box values + 2 keypoints * 2 xy values)
2  = 2 anchors' classification scores
```

当前按 HWC 方式读输出：

```text
index = (cell_y * feature_size + cell_x) * channel_count + channel
```

### 2.3 Palm anchor 解码

对 `14 x 14` 和 `7 x 7` 两个 head 的每个 cell、每个 anchor 解码。

anchor 中心：

```text
anchor.cx = (cell_x + 0.5) / feature_size
anchor.cy = (cell_y + 0.5) / feature_size
```

anchor 尺寸：

```text
14x24 head:
  anchor0: 0.1188828125 x 0.2170138889
  anchor1: 0.1298750000 x 0.3417500000

7x12 head:
  anchor0: 0.1716406250 x 0.2737222222
  anchor1: 0.1932968750 x 0.4076527778
```

分类分数小于 `0.25` 的候选会被过滤。

box 解码：

```text
cx = anchor.cx + dx * anchor.w
cy = anchor.cy + dy * anchor.h
w  = anchor.w * exp(dw)
h  = anchor.h * exp(dh)
box = [cx - w/2, cy - h/2, cx + w/2, cy + h/2]
```

palm 关键点解码：

```text
kx = anchor.cx + raw_kx * anchor.w
ky = anchor.cy + raw_ky * anchor.h
```

Palm 模型坐标是旋转后 `384 x 224` 模型空间的归一化坐标。反投影回原始 `720 x 1280` 画面时：

```text
rotated_x  = model_x * (1280 - 1)
rotated_y  = model_y * (720 - 1)
original_x = rotated_y
original_y = 1280 - 1 - rotated_x
```

然后 clamp 到原始画面范围内。

### 2.4 Palm 最终输出

Palm 输出结构是 `PalmResult`：

```text
PalmResult.valid
PalmResult.detections[]
```

每个 `PalmDetection` 包含：

```text
score
head_feature_size: 14 or 7
model_box: [x1, y1, x2, y2], 归一化模型坐标
pixel_box: [x1, y1, x2, y2], 原始 720 x 1280 画面像素坐标
keypoints[0..1]:
  model_x/model_y: 模型归一化坐标
  pixel_x/pixel_y: 原始画面像素坐标
  x/y: 原始画面归一化坐标
```

筛选策略：

```text
1. 合并 14x24 和 7x12 两个 head 的候选
2. 全局按 score 降序执行 NMS，IoU threshold = 0.10
3. 最多保留 2 个 palm
4. 最终仍按 score 从高到低排序
```

## 3. Hand Landmarker

模型路径：

```text
/app_demo/app_assets/models/hand.m1model
```

Hand 在 `--mode palm_hand` 或 `--mode fullcascade` 时运行。输入来自 Palm 的 `PalmResult` 和原始摄像头灰度图。

### 3.1 Hand ROI 生成

入口：`HANDLANDMARKER::Predict`

对每个 palm detection 执行一次 hand 推理。

ROI 由 palm 框和 palm 两个关键点生成：

```text
x1,y1,x2,y2 = palm pixel_box
raw_width   = x2 - x1
raw_height  = y2 - y1
center      = palm box center
```

旋转角来自 palm 两个关键点：

```text
wrist  = palm keypoints[0]
middle = palm keypoints[1]
dx = middle.pixel_x - wrist.pixel_x
dy = middle.pixel_y - wrist.pixel_y
rotation = normalize(pi/2 - atan2(-dy, dx))
```

ROI 扩张和偏移：

```text
roi_shift = (0.0, -0.1)
roi_scale = (1.8, 1.8)
long_side = max(raw_width, raw_height)
roi_width  = long_side * 1.8
roi_height = long_side * 1.8
```

最终得到一个带旋转的四点矩形：

```text
top_left
top_right
bottom_right
bottom_left
```

### 3.2 Hand 输入前处理

Hand 从原始 `720 x 1280` 灰度图里按 ROI 做仿射采样：

```text
原始摄像头图 + palm ROI
  -> affine crop
  -> bilinear sample
  -> 256 x 256 gray8
  -> hand model tensor
```

采样公式：

```text
tx = x / (256 - 1)
ty = y / (256 - 1)

src = top_left
    + tx * (top_right - top_left)
    + ty * (bottom_left - top_left)
```

超出原图范围的点按 0 填充。

Hand 输入 tensor：

```text
width  = 256
height = 256
format = SSNE_Y_8
data   = uint8 gray pixels
bytes  = 256 * 256 = 65536
```

### 3.3 Hand 模型输出

Hand 模型有 3 个输出，代码按 dtype 和元素数量映射：

```text
landmarks: 42 float values = 21 landmarks * 2 xy
hand_flag: 1 float value
handedness: 1 float value
```

所有输出要求是 `FLOAT32`。

`hand_flag` 和 `handedness` 会被 clamp 到 `[0, 1]`：

```text
hand_flag_score = clamp(raw_hand_flag, 0, 1)
handedness_score = clamp(raw_handedness, 0, 1)
```

当前约定：

```text
handedness_score <  0.5 => Left
handedness_score >= 0.5 => Right
```

### 3.4 Hand 关键点反投影

Hand landmark 输出的 `x/y` 被当作 ROI 内归一化坐标，反投影回原始摄像头画面：

```text
px = top_left.x + x * (top_right.x - top_left.x) + y * (bottom_left.x - top_left.x)
py = top_left.y + x * (top_right.y - top_left.y) + y * (bottom_left.y - top_left.y)
```

写入 `HandDetection.landmarks[i]`：

```text
x/y:       float pixel coordinates in original camera frame
pixel_x/y: rounded int pixel coordinates in original camera frame
```

### 3.5 Hand 最终输出

Hand 输出结构是 `HandResult`：

```text
HandResult.valid
HandResult.detections[]
```

每个 `HandDetection` 包含：

```text
landmarks[0..20]
hand_flag_score
handedness_score
has_hand_flag
has_handedness
valid
```

注意：`HANDLANDMARKER::DecodeOutputs` 解码成功后会把 `detection.valid = true`。`hand_flag_score` 只在 OSD 阶段按 `--osd_hand_confidence_threshold`（默认 0.99）过滤骨架；SSTCN 不再按该分数删除 21 点。因此低置信度时 Palm 框仍保留、Hand 骨架不画，但原始 Hand 坐标仍进入后续分类。

## 4. SSTCN Gesture Recognizer

模型路径：

```text
/app_demo/app_assets/models/slr5_fullcascade.m1model
```

ONNX 训练/导出时的语义输入输出：

```text
INPUT0:  语义形状 1 x 4 x 54 x 64（板端运行 dtype 由模型读取）
OUTPUT0: 1 x 6 x 1 x 1
```

A1 转换后，运行时输入 dtype 可能变成 INT8。当前代码会读取 `model_input_dtype`，并自动按运行时 dtype 打包输入。

### 4.1 从 HandResult 到 RawFrame

入口：`FULLCASCADEGESTURERECOGNIZER::UpdateAndPredict`

每次 hand 推理之后，SSTCN 先把当前帧转换为一个 `RawFrame`：

```text
RawFrame.values[2][27][3]
```

其中：

```text
side 0 = left
side 1 = right
point 0..20  = hand landmarks
point 21..24 = palm box corners
point 25..26 = palm p0/p9
channel 0 = x01
channel 1 = y01
channel 2 = valid
```

候选条件：

```text
palm.score >= fullcascade_min_palm_score, default 0.25
hand landmarks: hand_detection.valid == true（不检查 hand_flag_score）
```

槽位分配：

```text
1 candidate: palm center x < 0.5 => slot 0，否则 slot 1
2 candidates: 按 palm center x 从小到大放入 slot 0/1
```

Palm 与 Hand 通过相同 detection index 配对。`handedness_score` 保留供诊断，但当前不参与时序槽位排序。

### 4.2 关键点坐标转成训练坐标系

Hand 输出是原始竖屏图上的像素点。SSTCN 需要和训练端一致的图像归一化坐标。

默认 `--fullcascade_rotate_features` 开启：

```text
raw image:     720 x 1280
feature image: 1280 x 720
```

坐标旋转：

```text
rotated_x = 1280 - 1 - pixel_y
rotated_y = pixel_x

x01 = rotated_x / (1280 - 1)
y01 = rotated_y / (720 - 1)
```

如果使用 `--fullcascade_no_rotate_features`，则不旋转：

```text
x01 = pixel_x / (720 - 1)
y01 = pixel_y / (1280 - 1)
```

当前默认旋转的目的，是让板端竖屏摄像头的关键点坐标与服务器训练时 SLR500 横屏视频的坐标方向尽量一致。

### 4.3 64 帧环形缓冲

SSTCN 维护一个 64 帧 ring buffer：

```text
kFullCascadeWindowFrames = 64
```

当前是触发采集状态机：等待态保存最近 6 个 pre-roll 样本，连续 2 个有输入样本后开始采集；采集态把有手帧和无手全零帧都按原时间位置写入 64 帧窗口。`--fullcascade_no_input_reset_frames` 默认为 0，即不因短暂无手重置；若显式设置为正数，达到该连续空帧数才重置。

```text
--fullcascade_no_input_reset_frames, default 0
```

窗口完成后结果冻结显示；连续 12 个无输入样本后释放为 `no_gesture` 并回到等待态。

推理启动条件：

```text
buffered_frames >= fullcascade_warmup_frames, default 64
valid_frames_in_window >= max(fullcascade_min_valid_frames, 24)
```

没有 Palm/Hand 时 `RawFrame` 全零。等待态会直接输出类别 5 `no_gesture`，等价于训练端“整个窗口没有手”的负样本，同时避免为确定的空场景额外占用 NPU；已经触发的活动窗口不会跳过这些空帧，它们仍作为全零时间帧送入 SSTCN。

### 4.4 生成 SSTCN 输入张量

SSTCN 最终输入 buffer 形状语义：

```text
4 x 54 x 64
```

线性存储下标：

```text
index = ((channel * 54 + joint) * 64) + t
```

joint 排列：

```text
joint 0..26  = slot 0: hand 0..20 + palm box 21..24 + palm p0/p9 25..26
joint 27..53 = slot 1: hand 0..20 + palm box 21..24 + palm p0/p9 25..26
```

channel 排列：

```text
C0 = normalized x
C1 = normalized y
C2 = valid mask
C3 = side code
```

每一帧会做一次帧内中心尺度归一化：

```text
collect all valid landmarks in this frame
center_x = mean(valid x01)
center_y = mean(valid y01)
span_x = max_x - min_x
span_y = max_y - min_y
scale = max(span_x, span_y, 0.15)

norm_x = clamp((x01 - center_x) / scale, -2, 2)
norm_y = clamp((y01 - center_y) / scale, -2, 2)
```

写入规则：

```text
C0[joint,t] = norm_x
C1[joint,t] = norm_y
C2[joint,t] = 1.0
C3[joint,t] = -1.0 for left, +1.0 for right
```

缺失手/缺失点保持 0。

这一步和服务器训练端的 `slr5/features.py::normalize_frame()` 对齐：训练时 `.npy` 保存的是 `x01,y01,valid`，真正喂模型前也会按同样的 `center/scale/clip` 生成 `4 x 54 x 64`。

### 4.5 运行时 dtype 打包

当前代码先生成 float 特征：

```text
float input_buffer[4 * 54 * 64]
elements = 13824
float bytes = 55296
```

然后根据 A1 模型实际输入 dtype 打包：

```text
FLOAT32:
  直接传 float buffer, 55296 bytes

INT8:
  q = round(value * fullcascade_input_quant_scale)
  q = clamp(q, -128, 127)
  default fullcascade_input_quant_scale = 127.5
  bytes = 13824

UINT8:
  q = round(value * fullcascade_input_quant_scale + 128)
  q = clamp(q, 0, 255)
  bytes = 13824
```

输入 tensor 默认创建方式：

```text
--fullcascade_tensor_mode flat_bytes
width  = 13824
height = 1
format = SSNE_BYTES
dtype  = model_input_dtype
```

备用方式：

```text
--fullcascade_tensor_mode nchw
width  = 64
height = 216
format = SSNE_BYTES
dtype  = model_input_dtype
```

`216 = 4 * 54`。

### 4.6 SSTCN 输出与稳定结果

SSTCN 输出一个 tensor，代码读取前 6 个元素作为 logits：

```text
logits[0..5]
```

随后手动 softmax：

```text
prob[i] = exp(logit[i] - max_logit) / sum(...)
top_index = argmax(logits)
```

类别映射：

```text
0 = rain  = 雨
1 = long  = 长
2 = short = 短
3 = go    = 去
4 = thick = 粗
5 = no_gesture = 无手势
```

当类别 0..4 的 top probability 小于 `0.55`，或其与次高类的概率差小于 `0.08`，结果会拒识为 `no_gesture`。当前是“单个事件窗口完成一次推理后冻结”，因此不使用跨窗口多次投票；`stable_hits` 仅保留为结果状态字段的兼容配置。

```text
推理或拒识完成后：stable_index = top_index
输出冻结，直到连续 12 个无输入样本后释放为 no_gesture
```

终端默认紧凑日志：

```text
[FULLCASCADE] f=3735 pred=4(thick) p=0.662 stable=4(thick) sc=3 valid=36 palm=1 hand=1
```

开启 `--fullcascade_verbose` 后会输出 logits、probs、input min/max/nonzero、每只手的 handedness、wrist、middle_tip 等诊断信息。

## 5. OSD 可视化输出

OSD 初始化：

```text
VISUALIZER::Initialize
width  = 720
height = 1280
LUT    = /app_demo/app_assets/colorLUT.sscl
```

绘制入口：

```text
visualizer.DrawDetections(palm_result, hand_result)
visualizer.DrawDetections(palm_result, hand_result, &gesture_result)  // fullcascade
```

可视化内容：

```text
palm:
  score >= --osd_palm_confidence_threshold（默认 0.25）时绘制
  pixel_box 矩形框与 keypoints[0..1] 两个点

hand:
  按 detection index 与 Palm 一一配对
  配对 Palm 通过 OSD Palm 门控，且 hand_flag_score >= 0.99 时才画骨架
  骨架坐标使用显示专用运动自适应 EMA，基础 alpha=0.45；快速运动时趋近 1；不会回写 HandResult

gloss:
  fullcascade 模式下，使用 OSD 第 7 层 RLE/texture 图像层加载
  /app_demo/app_assets/osd_labels/*.ssbmp，将当前分类结果显示在画面左上角。
  优先显示 stable_index；窗口未 ready 时显示 WAITING。默认 stable-only，不跟随瞬时 top_index。
  默认标签为大字号中文/英文双行位图；若不同板端图像层方向不同，可通过
  --osd_gloss_dir 切换 osd_labels_fix_ccw、osd_labels_fix_transpose 或 osd_labels_plain_large。
```

默认 `--osd_mode auto`：

```text
palm mode: draw palm
palm_hand/fullcascade mode: draw palm + hand
```

可用模式：

```text
--osd_mode=all
--osd_mode=hand
--osd_mode=palm
--osd_mode=none
--osd_no_gloss
--osd_gloss_x N
--osd_gloss_y N
--osd_palm_confidence_threshold FLOAT
--osd_hand_confidence_threshold FLOAT
--osd_hand_smoothing_alpha FLOAT
```

Graphic layer 使用 SDK 双 DMA buffer：活动层把本帧图元一次原子 flush，不再每帧先 `osd_clean_layer()` 显示空白；只有从“上一帧有内容”变为“本帧无内容”的层才 clean。Palm OSD 门控只读取 `PalmDetection.score` 决定是否构建图元，不删除检测；Palm 坐标仍不做平滑。固定 alpha=0.45 会产生明显 Hand 拖尾，因此 Hand 显示滤波使用运动自适应 EMA。

Fullcascade 中的数据顺序为：先用原始 `palm_result + hand_result` 调用 `UpdateAndPredict()`，之后才调用 `DrawDetections()`。因此提高 Palm OSD 阈值只会同时隐藏该 Palm 的框/点和配对 Hand 骨架，不会改变 54 点特征、窗口触发、`no_gesture` 语义或分类结果。

## 6. 每帧数据对象流

一帧数据在主循环中的对象传递关系：

```text
image_tensor
  type: ssne_tensor_t
  from: IMAGEPROCESSOR::GetImage

palm_result
  type: PalmResult
  from: PALMDETECTOR::Predict(image_tensor)
  contains: palm boxes + 2 palm keypoints in original pixel coordinates

hand_result
  type: HandResult
  from: HANDLANDMARKER::Predict(image_tensor, palm_result)
  contains: 21 landmarks + hand_flag + handedness for each detected hand
  note: source_frame_index 标识结果来自哪次 Palm 推理；OSD 缓存帧不会重复更新 EMA

gesture_result
  type: GestureResult
  from: FULLCASCADEGESTURERECOGNIZER::UpdateAndPredict(palm_result, hand_result)
  contains: logits, probabilities, top_index, stable_index, valid window stats
```

## 7. 关键调试观测点

启动时应看到：

```text
[IMAGEPROCESSOR] Opened full-frame gray pipeline: 720x1280, format=...
[PALM] Manual preprocessing: camera=720x1280 -> clockwise rotated=1280x720 -> bilinear resized=384x224
[HAND] ROI preprocessing: camera=720x1280 -> affine ROI gray8=256x256
[FULLCASCADE] Input contract: INPUT0 source shape=1x4x54x64, float_bytes=55296
[FULLCASCADE] Runtime input tensor: width=13824, height=1, ...
[FULLCASCADE] Feature config: raw_image=720x1280, feature_image=1280x720, rotate_features_clockwise=1
```

如果手势不推理：

```text
waiting_for_window
```

表示 64 帧窗口还没填满。

```text
skip_empty_window
```

表示窗口中有效输入帧少于 `max(--fullcascade_min_valid_frames, 24)`。

如果出现：

```text
ret=403
Wrong input tensor
```

优先检查：

```text
model_input_dtype
runtime_input_dtype
input_width/input_height
input_mem_size
expected_runtime_bytes
```

如果手可视化正常但分类偏：

```text
1. 试 `--fullcascade_no_rotate_features`，对比坐标方向
2. 开 `--fullcascade_verbose`，观察左右槽位、logits/probs/input_nonzero/handedness
3. 根据实际转换模型标定参数校核 `--fullcascade_input_quant_scale`和 `--fullcascade_output_quant_scale`
4. 使用 `--fullcascade_golden_selftest` 检查内置服务器张量在 A1 上的一致性
```

## 8. 和服务器训练端的对应关系

训练端 MediaPipe 特征文件：

```text
raw/fullcascade frame shape = T x 2 x 27 x 3
last dim = x01, y01, valid
```

训练时转换成：

```text
model input = 4 x 54 x 64
```

板端现在复刻同一份最终模型输入语义：

```text
HandDetection pixel landmarks
  -> rotated feature x01/y01
  -> per-frame center/scale normalization
  + Palm box corners and Palm p0/p9
  -> 4 x 54 x 64
  -> dtype packing
  -> SSTCN
```

两端最需要保持一致的是：

```text
1. 坐标方向: 是否旋转成 1280 x 720
2. 左右手槽位: left=0/right=1, side_code=-1/+1
3. 归一化: center=当前帧所有有效点均值, scale=max(span_x,span_y,0.15)
4. 时序长度: 64 frames
5. 通道顺序: x, y, valid, side
6. joint 顺序: left 0..20, right 0..20
```

只要这六项一致，SSTCN 看到的输入格式就和服务器训练时保持同一契约。
