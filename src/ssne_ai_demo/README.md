# 文件目录结构

### bak

开发板端程序完整功能版本的 checkpoint. 目前包含：

- `preminilary/`: 初赛阶段的端侧程序，以及**重要文档、提示词模板**。详见：[README.md](./bak/preminilary/README.md)
- `vertical/`: 竖屏版本的端侧程序，以及**重要文档、提示词模板**。详见：[README.md](./bak/vertical/README.md)
- `half-final/`: 分赛区决赛阶段的端侧程序，以及**重要文档、提示词模板**。详见：[README.md](./bak/half-final/README.md)。包含完整 Palm Detector + Hand Landmarker + Gloss Translator 的串行级联链路，以及三种工作模式。Hand Landmarker 模型经过重训后性能得到提升。当前 Hand Landmarker 模型：AetherSign-HL-1.0。

