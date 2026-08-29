<h1 align="center">AetherSign（以太印记）</h1>

<div align="center">

<img src="./docs/assets/aethersign-hero.svg" alt="AetherSign — 从动作到语义的端侧手语识别系统" width="100%" />

<br />

[![Sensor](https://img.shields.io/badge/Sensor-SC132GS_90_FPS-0891B2?style=flat-square)](#-系统架构) [![Compute](https://img.shields.io/badge/NPU-0.8_TOPS_INT8-8B5CF6?style=flat-square)](#-核心技术亮点) [![Pipeline](https://img.shields.io/badge/Pipeline-Sign2Skeleton2Gloss-059669?style=flat-square)](#-系统架构)

**面向机器人场景的高动态实时手语翻译与交互系统**

*“让涌动的情绪，找到表达的声音。”*

[项目简介](#-I-项目简介) · [系统架构](#-II-系统架构) · [模型家族](#-III-模型家族) · [快速开始](#-IV-快速开始) · [目录结构](#-V-目录结构) · [复现指南](#-VI-复现指南)

</div>

---

## ✦ I. 项目简介

**AetherSign（以太印记）** 是一套面向机器人高动态交互场景的端侧中文手语识别系统。系统以 **思特威 SC132GS 全局快门传感器** 为视觉入口，在仅有 **0.8 TOPS @ INT8** 算力的飞凌微 A1 Vision 平台上，完成从手掌定位、手部关键点提取到手语词（Gloss）分类的完整链路。

与依赖 RGB 视频和云端推理的常规方案不同，AetherSign 将连续图像尽早压缩为低维、可解释的骨骼特征，再通过轻量时空网络理解动作。配合灰度成像与红外补光，系统面向强光、暗光、纯黑和快速运动等复杂条件，为服务机器人、特种机器人与无障碍交互终端提供低延迟的“动作到语义”接口。

> [!NOTE]
> 最终版本已将 **Eos(Palm Detector) + Iris(Hand Landmarker) + Muse(Gloss Translator)** 三个模型全部部署至 A1 NPU，打通板端孤立手语词识别链路。目前 **Fullcascade 模式**下应用平均帧率约为 19 fps。

| 维度 | 当前系统 |
| :-- | :-- |
| 视觉输入 | SC132GS，1280 × 720，最高 90 FPS，全局快门灰度成像 |
| 边缘算力 | 飞凌微 A1 Vision，0.8 TOPS @ INT8 |
| 推理链路 | 手掌检测 → 21 点手部关键点 → 54 点时空特征 → Gloss 分类 |
| 板端模型 | Palm Detector → Hand Landmarker → Gloss Translator |
| 性能实测 | P95 延迟：`palm` ≈ 36 ms · `palm_hand` ≈ 78 ms · `fullcascade` ≈ 78 ms |
| 目标场景 | 机器人指令理解、无障碍交互、强光 / 暗光 / 红外环境 |

最终版本的项目介绍文档：[project-12.md](./docs/project/project-12.md)。

---

## ⬡ II. 系统架构

<p align="center">
  <img src="./docs/assets/aethersign-workflow.svg" alt="AetherSign 系统架构：SC132GS、CPU/NPU 异构串行推理与 OSD 输出" width="100%" />
</p>

AetherSign 采用 **Sign → Skeleton → Gloss** 的三级视觉压缩路径：SC132GS 捕获高速灰度帧，**Eos 模型**定位手部，**Iris 模型**提取每只手 21 个关键点，控制器维护 64 帧特征窗口，最后由**Muse 模型**完成孤立词分类并通过 OSD 输出结果。

视觉模型在 A1 NPU 上级联运行；CPU 负责预后处理、坐标变换与特征缓存。`mode` 控制切换工作模式，`kInferInterval` 可调整推理间隔，Performance Monitor 记录三种运行模式的 P95 延迟与分阶段耗时。

系统上板后的**端侧数据流**：

![AetherSign 端侧数据流](./docs/assets/aethersign-heterogeneous-serial-datastream.svg)

---

## ◈ III. 模型家族

为了让三个模型拥有统一、易记且与项目气质一致的身份，我们将它们命名为 **Eos · Iris · Muse**。三者取自古希腊神话，与 AetherSign 所承载的“以太”意象共同构成“看见、连接、表达”的完整叙事：

| 品牌名 | 技术名 | 职责 | 命名含义 |
| :-- | :-- | :-- | :-- |
| **AetherSign Eos** | Palm Detector | 从全帧中发现双手并生成候选框 | *Eos* 是希腊神话中的**黎明女神**；如第一缕微光划破黑暗，模型首先从灰度画面中发现并定位手掌，为后续链路指明方向 |
| **AetherSign Iris** | Hand Landmarker | 将手部 ROI 映射为 21 点骨骼拓扑 | *Iris* 是连接天地的**彩虹女神**与诸神信使；模型连接离散关键点，将像素编织成完整、可解释的手部几何结构 |
| **AetherSign Muse** | Gloss Translator | 从时空骨骼序列中识别 Gloss | *Muse* 是掌管诗歌、语言与灵感的**文艺女神**；模型为物理动作赋予语言与语义，将骨骼序列转化为人类可读的 Gloss |

代码和命令行继续使用 `palm`、`palm_hand`、`fullcascade` 等技术标识；新名称用于 README、演示界面和比赛展示，不改变现有接口。

各模型的最终 benchmark 以及应用实际上板效果见下。

#### Eos 模型 benchmark

<details><summary>Click to expand</summary>

自建 1500 张 1280x720 图像数据集下，**Eos-2.1(Final version) 模型**与其余两种开源手部检测模型的精度对比：

| 模型 | Params | 平均 IOU | Precision | Recall | AP@0.5 | 
| --- | --- | --- | --- | --- | --- |
| **Eos-2.1(Final version)** | **1.368M** | 0.861 | 96.03% | **99.57%** | **98.92%** | 
| HaGRIDv2 YOLOv10n Hand Detector | 2.720M | **0.912** | **99.34%** | 96.26% | 97.91% |
| 100DOH Faster R-CNN X101-FPN | 104.8 M | 0.865 | 98.59% | 93.75% | 97.52% | 

</details>

#### Iris 模型 benchmark

<details><summary>Click to expand</summary>

自建 402 张 Hand ROI 图像数据集下，**Iris-2.0(Final version) 模型**与其余几种开源手部关键点检测模型的精度对比：

| 模型 | Mean pixel error | P95 pixel error | Handedness Acc | Params | delay@A1 |
|---|---|---|---|---|---|
| **AetherSign: Iris-2.0-lite** | 10.43(↓52.5%) | 24.98(↓54.6%) | 89.55%(↓2.4%) | **0.85 M(↓55.5%)** | **≈20 ms** |
| **AetherSign: Iris-2.0-pro** | 10.14(↓53.8%) | 23.77(↓56.8%) | 81.59%(↓11.1%) | 1.91 M | ≈22 ms |
| **AetherSign: Iris-2.0-max** | 9.71(↓55.8%) | 23.26(↓57.7%) | 98.26%(↑7.0%) | 1.91 M | ≈22 ms |
| MediaPipe Hand TFLite | 7.46 | 20.95 | **99.00%** | 2.71 M | N/A |
| RTMPose-m Hand5 | **6.37** | **18.76** | N/A | 13.76 M | N/A |
| HaMeR-CVPR24 | 7.89 | 20.06 | N/A | 672 M | N/A |
| Hamba-NeurIPS24 | 7.91 | 20.28 | N/A | 733 M | N/A |
| **Iris-1.0（分赛区版本 Baseline）** | 21.97 | 55.01 | 91.79% | 1.91M | ≈ 25ms |

</details>

#### 应用实际上板表现

<details><summary>Click to expand</summary>

AetherSign 实时手语识别系统上板后的工作表现如下。

| 模式 | 有效测试时长 | 应用FPS | R=FPSapp/90 | E2E平均 | E2E P95 | FPS P5-P95 | Hand触发率 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Palm | 98.4s | 27.38 | 0.304 | 36.48ms | 36.75ms | 27.21-27.59 | 0% |
| Palm+Hand | 65.8s | 14.77 | 0.164 | 67.69ms | 78.00ms | 12.82-27.59 | 93.1% |
| Fullcascade | 80.9s | 19.47 | 0.216 | 51.33ms | 78.00ms | 12.82-27.78 | 37.7% |

</details>

---

## 🚀 IV. 快速开始

### 4.1 前置条件

本仓库是项目代码、模型和文档的版本档案，**不能脱离厂商 SDK 独立编译**。开始前请准备：

- 飞凌微 A1 Vision 开发套件与思特威 SC132GS Sensor；
- `A1_SDK_SC132GS/smartsens_sdk` 编译环境；
- 厂商 SDK 头文件，尤其是 `ssne_api.h` 与 `osd_lib_api.h`；
- 板端模型与 OSD 资源文件。

当前最新板端版本位于 [`src/ssne_ai_demo/bak/final/`](./src/ssne_ai_demo/bak/final/)，完整参数说明见其 [README](./src/ssne_ai_demo/bak/final/README.md)。

### 4.2 集成至 A1 SDK

将 `final` 版本的代码放入 SDK 的应用目录：

```text
A1_SDK_SC132GS/
└── smartsens_sdk/
    └── smart_software/src/app_demo/slr_system/ssne_ai_demo/
```

确保运行资源按以下结构放置：

```text
app_assets/
├── colorLUT.sscl
├── osd_labels/
└── models/
    ├── palm.m1model
    ├── hand.m1model
    └── slr5_fullcascade.m1model
```

### 4.3 编译与烧录

在 SDK 根目录执行：

```bash
cd A1_SDK_SC132GS/smartsens_sdk/
./scripts/a1_sc132gs_build.sh
```

构建完成后，按照厂商工具链流程将镜像烧录至 A1 开发板。SDK 的日常增量编译、镜像位置与启动链路可参考 [`docs/sdk/quick_start.md`](./docs/sdk/quick_start.md)；容器环境见 [`docs/sdk/Docker容器与镜像编译.md`](./docs/sdk/Docker容器与镜像编译.md)。

### 4.4 板端运行

推荐使用启动脚本：

```sh
# 完整链路：Eos + Iris + Muse
./scripts/run.sh --mode fullcascade

# 仅运行手掌检测
./scripts/run.sh --mode palm

# 手掌检测 + 关键点定位
./scripts/run.sh --mode palm_hand
```

| 模式 | 运行链路 | 适合场景 |
| :-- | :-- | :-- |
| `palm` | Eos | 手掌检测调试与性能基线 |
| `palm_hand` | Eos → Iris | 关键点精度与 OSD 骨骼显示 |
| `fullcascade` | Eos → Iris → Muse | 完整孤立手语词识别 |

详细命令可以参考 [TERMINAL_COMMANDS.md](./src/ssne_ai_demo/bak/final/TERMINAL_COMMANDS.md)。

---

## 🗂 V. 目录结构

```text
AetherSign/
├── README.md                         # 项目主页
├── docs/
│   ├── assets/                       # README 与答辩视觉素材
│   ├── project/                      # 最新项目背景与进展     
│   ├── problem/                      # 赛题说明
│   ├── sdk/                          # A1 SDK、构建与模型转换文档
│   └── comp_mat/                     # 各阶段比赛提交材料
├── models/
│   ├── half_final/                   # 分赛区决赛模型归档
│   └── perminlary/                   # 初赛模型归档（历史目录名）
└── src/
    └── ssne_ai_demo/
        ├── README.md                 # 端侧程序版本索引
        └── bak/
            ├── final/                # 当前最新完整板端链路
            ├── half-final/           # 分赛区决赛版本（历史目录名）
            ├── preminilary/          # 初赛版本（历史目录名）
            └── vertical/             # 竖屏 / 方向实验版本
```

仓库以**比赛阶段归档**为主，因此历史目录名与版本快照会被保留；开发和复现时请优先从 `final` 版本开始。

当前仓库的 `src` 部分仅存放端侧调度程序，完整的模型训练程序参见下方的 [复现指南](#-VI-复现指南)。

---

## 🔬 VI. 复现指南

AetherSign 项目按照：Eos → Iris → Muse 的顺序完成训练和部署。每个模型的训练、评估和推理代码均在独立仓库中，且均提供了 README 文档说明。

<table>
  <tr>
    <th>组件</th>
    <th>仓库</th>
    <th>描述</th>
    <th>版本</th>
  </tr>
  <tr>
    <td><strong>Eos(Palm Detector)</strong></td>
    <td><a href="https://github.com/sui-yu-x/Eos">https://github.com/sui-yu-x/Eos</a></td>
    <td>Eos模型的<strong>训练-导出</strong>系统</td>
    <td>None</td>
  </tr>
  <tr>
    <td rowspan="3"><strong>Iris(Hand Landmarker)</strong></td>
    <td><a href="https://github.com/SmlCoke/HandLandmarksFab">https://github.com/SmlCoke/HandLandmarksFab</a></td>
    <td>Iris 模型训练的<strong>数据集半自动化标注系统</strong>：HLMF</td>
    <td>HLMF-3.0-final</td>
  </tr>
  <tr>
    <td><a href="https://github.com/SmlCoke/HandClassifierFab">https://github.com/SmlCoke/HandClassifierFab</a></td>
    <td>Iris 模型数据集制作系统的辅助模型：<strong>Hand Classifier</strong> 模型的训练-导出系统</td>
    <td>HCF-1.0-final</td>
  </tr>
  <tr>
    <td><a href="https://github.com/SmlCoke/HandLandmarkerLab">https://github.com/SmlCoke/HandLandmarkerLab</a></td>
    <td>Iris 模型<strong>训练系统</strong>：<strong>HLML</strong></td>
    <td>HLML-4.0-final</td>
  </tr>
  <tr>
    <td><strong>Muse(Gloss Translator)</strong></td>
    <td><a href="https://github.com/zhangchengxiang316/Muse">https://github.com/zhangchengxiang316/Muse</a></td>
    <td>Muse 模型的<strong>训练-导出系统</strong></td>
    <td>None</td>
  </tr>
</table>

> [!ATTENTION]
> **Hand Classifier(HCF)** 是为了配合 RTMPose/HaMeR 做 handedness / hand presence 标注而训练的**辅助教师模型**，**非实际正式部署**的模型。

---

## 🏁 VII. 参赛历程

- [x] 2026-05-07: 初赛结束
- [x] 2026-06-09: 成功晋级分赛区决赛线下参赛
- [x] 2026-07-24: 华东分赛区决赛结束，获**华东分赛区一等奖**🏅
- [x] 2026-08-25: 全国总决赛结束，获**全国总决赛一等奖**🏆️

---


## 🙏 VIII. 致谢

AetherSign 的诞生，离不开众多开源项目、科研团队、数据集以及个人贡献者的支持与努力。

### 8.1 开源项目与教师模型

在开发 AetherSign 的过程中，我们深受以下项目和模型的启发与支持，特此致谢：

- [Google MediaPipe](https://github.com/google-ai-edge/mediapipe) — 本项目实现手部检测与关键点定位功能的重要参考，同时也充当了 **“本项目第一个教师模型”** 的角色，在**初赛以及分赛区决赛推进**过程中发挥了重要作用。
- [RTMPose / MMPose](https://github.com/open-mmlab/mmpose) — 在构建手部关键点标注（Hand Landmarker）的训练数据集时，我们将其作为教师模型使用。得益于 RTMPose-m Hand5 模型在 **SC132GS 域普通样本上的高精度表现**，我们就此正式确立了**多教师模型互补+几何约束筛选的半自动化标注策略**。
- [HaMeR / CVPR24](https://github.com/geopavlakos/hamer) — 同样作为手指关键点的教师模型，HaMeR 在 SC132GS 域的**困难/遮挡样本上表现优异，泛化性较强**，是**全国总决赛阶段最终采纳的主教师模型**。
- [Hamba / NeurIPS24](https://github.com/humansensinglab/Hamba) — 同样是手指关键点检测的教师模型，为我们的 Iris 模型 Benchmark 提供了重要的参考数据。
- [SSTCN / CVPR21Chal-SLR](https://github.com/jackyjsy/CVPR21Chal-SLR) — 在**时间序列建模和孤立手语分类流程**的设计中，该项目提供了重要的参考思路。


关于上述项目的开源许可证及引用规范，请参阅其各自的代码仓库和相关学术出版物。

### 8.2 数据集

我们要特别感谢中国科学技术大学（USTC）视觉手语研究组（VSLRG），让我们得以使用以下中国手语数据集：

- [CSL / SLR500](https://ustc-slr.github.io/datasets/2015_csl/)
- [CSL-Daily](https://ustc-slr.github.io/datasets/2021_csl_daily/)

请注意，这些数据集的使用需遵循其各自的发布协议。
AetherSign **不会**重新分发原始数据集。
如果您希望获取这些数据，请访问官方数据集页面进行申请，并严格遵守相关的使用条款。

### 8.3 贡献者

AetherSign 由 **PeakDragonSoar团队** 主导开发。

- **团队名称：** PeakDragonSoar（巅峰龙翔）
- **项目名称：** AetherSign（以太印记）
- **团队成员：** 3 名来自上海交通大学 2023 级微电子科学与工程专业的本科生:
    - [@SmlCoke](https://github.com/SmlCoke) — 负责**手指关键点检测模型 Iris 的构建**：HLMF 系统、HLML 系统以及 HCF 训练。
    - [@sui-yu-x](https://github.com/sui-yu-x) — 负责**手部检测模型 Eos 的构建**。
    - [@zhangchengxiang316](https://github.com/zhangchengxiang316) — 负责**孤立手语词分类模型 Muse 的构建以及端侧调度程序的集成**

感谢所有团队成员，正是他们在 AetherSign 的开发、部署、性能优化以及竞赛参赛等各个阶段付出的辛勤努力，才让这个项目得以完善。


![](./docs/assets/posters/aethersign-poster-edge.png)

*我们希望在有限算力与真实物理环境之间，找到一种更轻、更快、更可靠的人机沟通方式——让每一个动作，都能抵达它所表达的意义。*

<div align="center">

<img src="./docs/assets/aethersign-logo-minimal.svg" alt="AetherSign" width="52" />

<sub>AetherSign · Eos → Iris → Muse · 最终归档</sub>

</div>
