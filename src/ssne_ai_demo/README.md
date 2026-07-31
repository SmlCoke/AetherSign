# 文件目录结构

### bak

开发板端程序完整功能版本的 checkpoint. 目前包含：

- `preminilary/`: 初赛阶段的端侧程序，以及**重要文档、提示词模板**。详见：[README.md](./bak/preminilary/README.md)
- `vertical/`: 竖屏版本的端侧程序，以及**重要文档、提示词模板**。详见：[README.md](./bak/vertical/README.md)
- `half-final/`: 分赛区决赛阶段的端侧程序，以及**重要文档、提示词模板**。详见：[README.md](./bak/half-final/README.md)。包含完整 Palm Detector + Hand Landmarker + Gloss Translator 的串行级联链路，以及三种工作模式。Hand Landmarker 模型经过重训后性能得到提升。当前 Hand Landmarker 模型：AetherSign-HL-1.0。

### main

**现阶段(0717)**的端侧程序源码，同样包含**重要文档、提示词模板**。

该程序功能：

- 1280x720(横屏)格式的 Plam Detector 输入，Palm Detector 模型代号：AetherSign-Dragon-1.0
- 包含 Hand Landmarker 的串行级联接口，并且相比 `preminilary/` 版本，Hand Landmarker 重训后性能得到提升。当前 Hand Landmarker 模型：AetherSign-Peak-1.0。
- 详细说明见：[README.md](./main/README.md)

Palm 模型效果：PC 端不错，板端效果也不错
Hand Landmaker 模型效果：PC 和板端当前精度都比较低，只有部分手势姿态能够准备识别到 21 个关键点。