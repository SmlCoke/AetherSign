# 板端三模型数据链路说明

本文档按当前 `device` 代码梳理板端实时推理的数据链路：从摄像头拿到一帧 `720 x 1280` 灰度图开始，依次经过 Palm Detector、Hand Landmarker、SSTCN 手势分类模型，最后输出可视化和终端调试信息。

对应主流程入口是 `main.cpp`，核心实现分布在：

- `src/pipeline_image.cpp`: 摄像头在线取图
- `src/palm_detector.cpp`: palm 检测、旋转缩放、anchor 解码
- `src/hand_landmarker.cpp`: palm ROI 裁剪、手部 21 点反投影
- `src/gesture_recognizer.cpp`: 关键点时序打包、SSTCN 推理、稳定类别输出
- `src/utils.cpp`: OSD 绘制 palm 框和 hand 骨架

## 0. 总览

默认三模型串联运行命令：

```sh
./ssne_ai_demo --kInferInterval=1 --enable_gesture
```

`--enable_gesture` 会自动打开 `--enable_hand`。主循环中每隔 `kInferInterval` 帧执行一次模型链路：

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
palm input:   224 x 224, SSNE_Y_8
hand input:   256 x 256, SSNE_Y_8
gesture input contract: 1 x 4 x 42 x 64
gesture classes: 0 rain/雨, 1 long/长, 2 short/短, 3 go/去, 4 thick/粗
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
  -> bilinear resize 到 224 x 224
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
width  = 224
height = 224
format = SSNE_Y_8
data   = uint8 gray pixels
bytes  = 224 * 224 = 50176
```

### 2.2 Palm 模型输出

Palm 有 4 个输出，代码按元素数量自动映射为：

```text
reg14: 14 x 14 x 16 = 3136 values
cls14: 14 x 14 x 2  = 392 values
reg7:  7 x 7 x 16   = 784 values
cls7:  7 x 7 x 2    = 98 values
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
14x14 head:
  anchor0: 0.10 x 0.10
  anchor1: 0.18 x 0.18

7x7 head:
  anchor0: 0.25 x 0.25
  anchor1: 0.40 x 0.40
```

分类分数小于 `0.50` 的候选会被过滤。

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

Palm 模型坐标是旋转后 `224 x 224` 模型空间的归一化坐标。反投影回原始 `720 x 1280` 画面时：

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
1. 14x14 head 先做 NMS, IoU threshold = 0.30
2. 7x7 head 按分数补充，若和已选框 IoU > 0.35 则抑制
3. 最多保留 2 个 palm
4. 按 score 从高到低排序
```

## 3. Hand Landmarker

模型路径：

```text
/app_demo/app_assets/models/hand.m1model
```

Hand 只在 `--enable_hand` 或 `--enable_gesture` 时运行。输入来自 Palm 的 `PalmResult` 和原始摄像头灰度图。

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

注意：`HANDLANDMARKER::DecodeOutputs` 解码成功后会把 `detection.valid = true`。后续 SSTCN 阶段才根据 `--gesture_min_hand_score` 过滤低置信度手。

## 4. SSTCN Gesture Recognizer

模型路径：

```text
/app_demo/app_assets/models/slr5_hand42.m1model
```

ONNX 训练/导出时的语义输入输出：

```text
INPUT0:  float32, 1 x 4 x 42 x 64
OUTPUT0: float32, 1 x 5 x 1 x 1
```

A1 转换后，运行时输入 dtype 可能变成 INT8。当前代码会读取 `model_input_dtype`，并自动按运行时 dtype 打包输入。

### 4.1 从 HandResult 到 RawFrame

入口：`GESTURERECOGNIZER::UpdateAndPredict`

每次 hand 推理之后，SSTCN 先把当前帧转换为一个 `RawFrame`：

```text
RawFrame.values[2][21][3]
```

其中：

```text
side 0 = left
side 1 = right
landmark 0..20
channel 0 = x01
channel 1 = y01
channel 2 = valid
```

过滤条件：

```text
detection.valid == true
hand_flag_score >= gesture_min_hand_score, default 0.50
```

左右手分配：

```text
has_handedness:
  handedness_score >= 0.5 => right slot
  handedness_score <  0.5 => left slot

no handedness:
  mean x >= 0.5 => right slot
  mean x <  0.5 => left slot
```

如果同一侧有多个检测，保留 `hand_flag_score` 更高的那个。`--gesture_swap_hands` 会在这里交换左右手槽位。

### 4.2 关键点坐标转成训练坐标系

Hand 输出是原始竖屏图上的像素点。SSTCN 需要和训练端一致的图像归一化坐标。

默认 `--gesture_rotate_features` 开启：

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

如果使用 `--gesture_no_rotate_features`，则不旋转：

```text
x01 = pixel_x / (720 - 1)
y01 = pixel_y / (1280 - 1)
```

当前默认旋转的目的，是让板端竖屏摄像头的关键点坐标与服务器训练时 SLR500 横屏视频的坐标方向尽量一致。

### 4.3 64 帧环形缓冲

SSTCN 维护一个 64 帧 ring buffer：

```text
kGestureWindowFrames = 64
```

每个有效/无效手帧都会进入缓冲。若连续无手超过：

```text
--gesture_no_hand_reset_frames, default 20
```

则清空序列，避免“手离开画面后还在用旧窗口分类”。

推理启动条件：

```text
buffered_frames >= gesture_warmup_frames, default 64
valid_frames_in_window >= gesture_min_valid_frames, default 8
```

### 4.4 生成 SSTCN 输入张量

SSTCN 最终输入 buffer 形状语义：

```text
4 x 42 x 64
```

线性存储下标：

```text
index = ((channel * 42 + joint) * 64) + t
```

joint 排列：

```text
joint 0..20  = left hand landmark 0..20
joint 21..41 = right hand landmark 0..20
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

这一步和服务器训练端的 `slr5/features.py::normalize_frame()` 对齐：训练时 `.npy` 保存的是 `x01,y01,valid`，真正喂模型前也会按同样的 `center/scale/clip` 生成 `4 x 42 x 64`。

### 4.5 运行时 dtype 打包

当前代码先生成 float 特征：

```text
float input_buffer[4 * 42 * 64]
elements = 10752
float bytes = 43008
```

然后根据 A1 模型实际输入 dtype 打包：

```text
FLOAT32:
  直接传 float buffer, 43008 bytes

INT8:
  q = round(value * gesture_input_quant_scale)
  q = clamp(q, -128, 127)
  default gesture_input_quant_scale = 64
  bytes = 10752

UINT8:
  q = round(value * gesture_input_quant_scale + 128)
  q = clamp(q, 0, 255)
  bytes = 10752
```

输入 tensor 默认创建方式：

```text
--gesture_tensor_mode flat_bytes
width  = 10752
height = 1
format = SSNE_BYTES
dtype  = model_input_dtype
```

备用方式：

```text
--gesture_tensor_mode nchw
width  = 64
height = 168
format = SSNE_BYTES
dtype  = model_input_dtype
```

`168 = 4 * 42`。

### 4.6 SSTCN 输出与稳定结果

SSTCN 输出一个 tensor，代码读取前 5 个元素作为 logits：

```text
logits[0..4]
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
```

稳定输出：

```text
如果连续 top_index 相同，stable_count += 1
否则 stable_count = 1
stable_count >= gesture_stable_hits(default 3) 后，stable_index 生效
```

终端默认紧凑日志：

```text
[GESTURE] f=3735 pred=4(thick) p=0.662 stable=4(thick) sc=26 valid=36 hand=0
```

开启 `--gesture_verbose` 后会输出 logits、probs、input min/max/nonzero、每只手的 handedness、wrist、middle_tip 等诊断信息。

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
```

可视化内容：

```text
palm:
  pixel_box 矩形框
  keypoints[0..1] 两个点

hand:
  21 点骨架连线
```

默认 `--osd_mode auto`：

```text
gesture disabled: draw palm + hand
gesture enabled:  hide palm, draw hand skeleton only
```

可用模式：

```text
--osd_mode=all
--osd_mode=hand
--osd_mode=palm
--osd_mode=none
```

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

gesture_result
  type: GestureResult
  from: GESTURERECOGNIZER::UpdateAndPredict(hand_result)
  contains: logits, probabilities, top_index, stable_index, valid window stats
```

## 7. 关键调试观测点

启动时应看到：

```text
[IMAGEPROCESSOR] Opened full-frame gray pipeline: 720x1280, format=...
[PALM] Manual preprocessing: camera=720x1280 -> clockwise rotated=1280x720 -> bilinear resized=224x224
[HAND] ROI preprocessing: camera=720x1280 -> affine ROI gray8=256x256
[GESTURE] Input contract: INPUT0 float32 shape=1x4x42x64, bytes=43008
[GESTURE] Runtime input tensor: width=10752, height=1, ...
[GESTURE] Feature config: raw_image=720x1280, feature_image=1280x720, rotate_features_clockwise=1
```

如果手势不推理：

```text
waiting_for_window
```

表示 64 帧窗口还没填满。

```text
skip_empty_window
```

表示窗口中有效手帧少于 `--gesture_min_valid_frames`。

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
1. 试 --gesture_no_rotate_features，对比坐标方向
2. 试 --gesture_swap_hands，对比左右手槽位
3. 试 --gesture_input_quant_scale=32 或 128，对比 INT8 输入量化尺度
4. 开 --gesture_verbose 观察 logits/probs/input_nonzero/handedness
```

## 8. 和服务器训练端的对应关系

训练端 MediaPipe 特征文件：

```text
raw .npy shape = T x 2 x 21 x 3
last dim = x01, y01, valid
```

训练时转换成：

```text
model input = 4 x 42 x 64
```

板端现在复刻同一份最终模型输入语义：

```text
HandDetection pixel landmarks
  -> rotated feature x01/y01
  -> per-frame center/scale normalization
  -> 4 x 42 x 64
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
