# 文件目录结构

### bak

开发板端程序完整功能版本的 checkpoint. 目前包含：

- `preminilary/`: 初赛阶段的端侧程序，以及**重要文档、提示词模板**。详见：[README.md](./bak/preminilary/README.md)

### main

**现阶段(0627)**的端侧程序源码，同样包含**重要文档、提示词模板**。

该程序功能：

- 720x1280(竖屏)格式的 Plam Detector 输入
- 包含 Hand Landmarker 的串行级联接口
- 详细说明见：[README.md](./main/README.md)

Palm 模型效果：PC 端不错，板子上很差