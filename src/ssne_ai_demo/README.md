# 文件目录结构

## 公共源码与许可证边界

`bak/` 下的四个公开端侧调度程序版本已经过许可证边界清理：公共 Git **仅保留可公开发布的 AetherSign 团队原创源码与项目文档**，它们是用于说明各比赛阶段实现的源码归档，**不是可脱离厂商 SDK 独立编译的完整应用**。

为避免重新分发受厂商条款约束的材料，公共历史中不包含部分构建配置、程序入口、OSD、Pipeline 集成文件及其修改版本；模型、LUT、OSD 标签等运行资产也继续仅在团队本地保存，不由 Git 公开跟踪。四个版本 README 中原有的编译和运行命令只适用于团队本地保留的完整目录。

完整可编译环境由以下内容共同组成：

1. 本仓库公开的 **AetherSign 团队原创组件**；
2. 用户通过官方或其他合法授权渠道取得的**厂商 SDK 与集成文件**；
3. 用户有权使用的**模型和其他运行资产**。

如需复现，请**先取得相应厂商 SDK 的合法使用许可**，再通过本仓库 Issue 联系维护者，说明所用 SDK 版本、硬件版本和目标 checkpoint，以获取文件放置方式、兼容性核验等复现指导，以及可提供的团队原创组件。请勿在公开 Issue 上传 SDK 文件、许可证凭据或其他厂商材料。团队不承诺、也不会公开分发厂商来源文件或其修改版本。

## 版本目录

### bak

开发板端程序完整功能版本的 checkpoint. 目前包含：

- `preminilary/`: 初赛阶段的端侧程序，以及**重要文档、提示词模板**。详见：[README.md](./bak/preminilary/README.md)
- `vertical/`: 竖屏版本的端侧程序，以及**重要文档、提示词模板**。详见：[README.md](./bak/vertical/README.md)
- `half-final/`: 分赛区决赛阶段的端侧程序，以及**重要文档、提示词模板**。详见：[README.md](./bak/half-final/README.md)。包含完整 Palm Detector + Hand Landmarker + Gloss Translator 的串行级联链路，以及三种工作模式。Hand Landmarker 模型经过重训后性能得到提升。当前 Hand Landmarker 模型：AetherSign-HL-1.0。
- `final/`: 决赛阶段的端侧程序，以及**重要文档、提示词模板**。详见：[README.md](./bak/final/README.md)。包含完整 Palm Detector + Hand Landmarker + Gloss Translator 的串行级联链路，以及三种工作模式。Palm Detector 模型更新至：Eos-2.1；Hand Landmarker 模型更新至：Iris-2.0 系列；Gloss Translator 模型更新至：Muse-2.0。

### main

当前正式可运行版本的端侧调度程序，目前已经转移至 `final` 版本。
