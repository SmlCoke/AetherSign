# Palm Detector 接入要点

本文档记录当前 palm detector 板端链路中最需要保持一致的部分。目标是快速确认模型输入、输出解码、坐标映射和 OSD 绘制是否与 Python 参考链路一致。

## 1. 当前结论

当前 palm detector 的关键点：

1. 摄像头输出是 `720x1280` 的 `SSNE_Y_8` 灰度图。
2. Palm 输入预处理只做双线性缩放：`720x1280 -> 224x224`。
3. `224x224` 灰度 buffer 以 `uint8` 形式装载到 palm 模型输入 tensor。
4. SSNE 输出按 `HWC` flatten 顺序解码。
5. Anchor 尺寸与 `ref/palm0701/anchor_utils.py` 保持一致。
6. OSD palm 框使用 4 条 solid line 绘制。

## 2. 输入链路

Python 参考脚本入口：

```python
def preprocess_palm_image(image):
    gray = validate_gray_image(image)
    pil = Image.fromarray(gray, mode="L").resize(
        (PALM_INPUT_SIZE, PALM_INPUT_SIZE), Image.Resampling.BILINEAR
    )
    normalized = np.asarray(pil, dtype=np.float32) / 255.0
    return normalized[np.newaxis, np.newaxis, :, :]
```

板端对应实现位于 `src/palm_detector.cpp`：

```cpp
ResizeBilinear(camera_data,
               src_width,
               src_height,
               manual_input_buffer.data(),
               input_shape[0],
               input_shape[1]);
```

当前 `input_shape` 是：

```cpp
const std::array<int, 2> palm_input_shape = {224, 224};
```

如果 palm 检测异常，先确认：

- 摄像头 tensor 的 `width=720`、`height=1280`。
- 摄像头 tensor 的 format 是 `SSNE_Y_8`。
- `manual_input_buffer` 大小是 `224 * 224`。
- 双线性插值的坐标映射与参考脚本一致。

## 3. 输出解码

palm 模型输出数量固定为 4：

```cpp
static const int kPalmOutputCount = 4;
```

当前按元素数量映射输出：

- `reg14`: `3136`
- `cls14`: `392`
- `reg7`: `784`
- `cls7`: `98`

每个 anchor 的回归值：

```text
dx, dy, dw, dh, k0x, k0y, k1x, k1y
```

当前输出 layout：

```cpp
const PalmOutputLayout output_layout = kPalmOutputLayoutHwc;
```

索引含义：

```cpp
index = cell_index * channel_count + channel;
```

如果输出 shape 或检测结果异常，优先通过 verbose 日志确认：

- 每个 output 的 `inferred_elements` 是否匹配。
- 输出值域是否合理。
- 输出映射是否 valid。
- `active_layout` 是否为 `HWC`。

## 4. Anchor 配置

Anchor 尺寸与 `ref/palm0701/anchor_utils.py` 一致：

```text
14x14:
  [0.084651, 0.032030]
  [0.104590, 0.079621]

7x7:
  [0.137044, 0.049292]
  [0.157819, 0.088213]
```

板端实现位于：

```text
PALMDETECTOR::GetAnchor()
```

如果 Python 侧更新 anchor，板端必须同步更新这里。

## 5. 坐标映射

palm 输出坐标是相对于模型输入图的归一化坐标。板端直接映射到原始摄像头画面：

```cpp
pixel_x = model_x * (image_shape[0] - 1);
pixel_y = model_y * (image_shape[1] - 1);
```

检测框通过四个角点调用 `MapPoint()` 后再取 `min/max`，得到 OSD 使用的轴对齐框。

## 6. OSD 绘制

可视化逻辑位于 `src/utils.cpp`。

palm 框不使用空心矩形图元，而是使用 4 条 `DrawLine()`：

```cpp
DrawLine(x1, y1, x2, y1)
DrawLine(x2, y1, x2, y2)
DrawLine(x2, y2, x1, y2)
DrawLine(x1, y2, x1, y1)
```

这样与当前 OSD 图元路径更稳定。关键点使用 `DrawPoint()` 绘制。

## 7. 调试顺序

接入或替换 palm 模型时，建议按下面顺序检查：

1. 输入尺寸、dtype、format。
2. `224x224` 输入 buffer 的像素统计。
3. 输出 tensor 数量、元素数量和值域。
4. 输出顺序和 `HWC` 解码索引。
5. anchor 尺寸和阈值。
6. box/keypoint decode 公式。
7. 坐标映射结果。
8. OSD 绘制。

当前 C++ 程序默认不向板端文件系统写 dump。需要调试时优先打开 `verbose`，让诊断信息走控制台日志。

