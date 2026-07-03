<div align="center">

# 🤟 AetherSign (以太印记)
**面向机器人场景的高动态零延迟实时手语翻译与交互系统**

[**中文**](README.md) | [**English**](README_EN.md)

[![Hardware](https://img.shields.io/badge/Sensor-SmartSens_SC132GS-blue.svg)](https://www.smartsenstech.com/) [![NPU](https://img.shields.io/badge/NPU-FeelingVision_A1_(0.8TOPS)-orange.svg)]() [![Institution](https://img.shields.io/badge/Institution-SJTU-red.svg)](https://www.sjtu.edu.cn/) [![SmlCoke](https://img.shields.io/badge/SmlCoke-https://smlcoke.com-brightgreen.svg)](https://smlcoke.com) [![License](https://img.shields.io/badge/License-MIT-green.svg)](./LICENSE)

“在光影与高速的交错中，镌刻无障碍沟通的印记”

[系统架构](#-系统架构) • [核心技术亮点](#-核心技术亮点) • [快速开始](#-快速开始) • [目录结构](#-目录结构) • [团队信息](#-团队信息)

</div>

---

## I. 项目简介

**AetherSign (以太印记)** 是为参加2026年全国大学生集成电路创新创业大赛——思特威杯开发的工业级边缘视觉系统。

本项目聚焦**机器人场景** 下的高频人机交互痛点。基于 **思特威 SC132GS 全局快门图像传感器** 与 **飞凌微 A1 AI 开发套件**，我们在极其受限的边缘算力（0.8 TOPS @ INT8）下，打造了一套**高帧率 (90fps)、极低延迟、全天候 (支持 0-Lux 红外纯黑环境)** 的连续手语/手势翻译交互系统。

本系统旨在赋予服务机器人、特种机械狗在复杂光照和剧烈运动状态下，精准捕捉并理解人类快速手语指令的能力。


---

## II. 系统架构

*(建议后续在此处插入一张精美的系统架构架构图 System_Architecture.png)*

---

## III. 核心技术亮点 (Core Competitiveness)

---

## IV. 快速开始

本项目基于飞凌微 A1 SDK 开发，提供一键式编译和部署支持。详细开发指南请参考 `docs/sdk/quick_start.md`。

### 环境准备 (Docker)
参考 `docs/sdk/Docker容器与镜像编译.md`，推荐使用官方的 Docker 镜像以统一编译环境：
```bash
# 启动包含交叉编译工具链的容器
docker run -it -v $PWD:/workspace smartsens/a1_build_env:latest /bin/bash
```

### 编译与运行

```bash
cd A1_SDK_SC132GS/smartsens_sdk/
./scripts/a1_sc132gs_build.sh
```

部署后，飞凌微套件上电后可自动加载环境，也可通过板卡启动脚本 `app_demo/scripts/run.sh` 开启推流与交互应用。

---

## V. 目录结构

目前仓库包含官方 SDK 文档与赛题说明，后续源代码将逐步合并入主分支。

---

## 👥 团队信息

* **团队名称：** PeakDragonSoar (巅峰龙翔)
* **项目名称：** AetherSign (以太印记)
* **团队成员：** 由 3 名来自 **上海交通大学 (SJTU)** 的 **2023级微电子科学与工程系** 本科生组成。

我们在底层硬件架构与上层应用开发领域开展全面的跨界合作，致力于深入发掘算力与感知的极限，探索嵌入式硬件下的高动态人机交互新范式。
