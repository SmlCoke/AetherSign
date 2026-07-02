# SDK目录解析笔记

本文基于 `SDK目录解析prompt.md`，并结合我对当前 `smartsens_sdk` 目录的选择性阅读整理而成。目标不是把所有 Buildroot 细节一次讲完，而是先帮第一次接触内核编译、镜像构建、烧录和板级 demo 的同学建立一张“这套 SDK 到底怎么分层、我现在该看哪里”的地图。

## 先说结论

- 这个仓库本体是一个 **Buildroot 主工程**。
- 真正和 A1 开发板强相关的内容，主要不在 Buildroot 主树里，而是在 `smart_software/` 这个 **厂商扩展目录（BR2_EXTERNAL）** 里。
- 如果你现在的目标只是先完成比赛，大多数时候你只需要重点看：
  - `scripts/`
  - `smart_software/configs/`
  - `smart_software/board/m1pro/`
  - `smart_software/src/app_demo/face_detection/ssne_ai_demo/`
  - `smart_software/package/ssne_ai_demo/`
  - `output/images/`
- 如果以后要做真实项目、改板级功能、改内核、调设备树、加新应用包，那就要进一步看：
  - `smart_software/src/linux-5.15.24/`
  - `smart_software/package/`
  - `smart_software/board/m1pro/rootfs_overlay/`
  - `output/build/`
  - `output/opt/m1_sdk/`

## 这套 SDK 的整体层次

可以把这套工程理解成四层：

| 层次 | 位置 | 作用 |
|---|---|---|
| Buildroot 主框架层 | `smartsens_sdk/` 根目录下的大多数目录 | 提供通用的交叉编译、根文件系统打包、内核构建、包管理能力 |
| SmartSens 板级扩展层 | `smart_software/` | 放 A1 开发板相关的 defconfig、rootfs overlay、内核源码、demo 包、工具链 |
| 构建入口层 | `scripts/` | 提供下载、编译整个 SDK、单独重编 demo 的脚本 |
| 构建产物层 | `output/` | 放最终镜像、中间产物、目标根文件系统、主机工具、SDK 库等 |

从脚本和配置能看出，这套工程不是直接用 Buildroot 默认配置，而是通过：

```bash
make BR2_EXTERNAL=./smart_software smartsens_m1pro_release_defconfig
make
```

把 `smart_software/` 当作 Buildroot 的外部扩展树接进去。

也就是说：

- `smartsens_sdk/` 负责“搭建构建系统”
- `smart_software/` 负责“告诉构建系统 A1 板子应该怎么编、编什么、带什么 demo、带什么库”

## 一个非常关键的认识

当前配置生成的是 **带 initramfs 的内核镜像**。

从配置里可以看到：

- Buildroot 开了 `BR2_TARGET_ROOTFS_INITRAMFS=y`
- 内核配置里有 `CONFIG_INITRAMFS_SOURCE="${BR_BINARIES_DIR}/rootfs.cpio"`
- 最终产物里有 `output/images/zImage.smartsens-m1-evb`

这意味着：

- 根文件系统会被打进内核镜像
- 板子开机后，很多内容是从这个镜像解包到 RAM 里跑
- 你在板子终端里临时改的东西，**掉电后大概率会丢**
- 真想固化改动，应该改 `rootfs_overlay`、Buildroot package、内核源码或配置，然后重新编译镜像

## 根目录各个文件夹是什么意思

下面这张表重点解释当前 `smartsens_sdk/` 根目录下你实际能看到的目录。

| 目录 | 它是干什么的 | 现在比赛阶段要不要深看 | 真实项目会不会碰 |
|---|---|---|---|
| `arch/` | Buildroot 的架构支持层，决定 ARM/x86 等架构相关配置 | 暂时不用深看 | 偶尔 |
| `boot/` | Buildroot 里和启动相关包有关的定义，例如 bootloader 支持 | 暂时不用深看 | 偶尔 |
| `cache/` | 厂商脚本下载并缓存的大包，当前能看到内核 tar 包、工具链 tar 包、package tar 包 | 要知道它是缓存区 | 会碰 |
| `dl/` | Buildroot 下载第三方源码包后的缓存目录 | 知道用途即可 | 会碰 |
| `docs/` | Buildroot 文档 | 暂时不用深看 | 需要时查 |
| `fs/` | Buildroot 生成各种文件系统镜像的规则 | 暂时不用深看 | 偶尔 |
| `linux/` | Buildroot 的 Linux 构建规则层，不是你的实际内核业务代码 | 暂时不用深看 | 偶尔 |
| `output/` | 最重要的产物目录，包含最终镜像、中间编译目录、目标根文件系统等 | **必看** | 必看 |
| `package/` | Buildroot 官方包仓库，数量很多，绝大多数和你这次比赛无关 | 暂时不用深看 | 会碰 |
| `patch/` | 厂商补丁目录，当前看到 `0001-Add-support-of-A1.patch`，用于给上游内核打补丁 | 了解即可 | 会碰 |
| `scripts/` | 最实用的入口脚本，负责下载依赖、编完整 SDK、重编 demo | **必看** | 必看 |
| `smart_software/` | 这套 A1 SDK 的核心定制区，真正和板子、demo、工具链、内核源码相关 | **必看** | 必看 |
| `support/` | Buildroot 自己的辅助脚本、Kconfig 逻辑、测试基础设施 | 暂时不用深看 | 偶尔 |
| `system/` | Buildroot 通用根文件系统骨架和初始化基础规则 | 暂时不用深看 | 偶尔 |
| `toolchain/` | Buildroot 通用工具链规则，不是你当前项目专用工具链本体 | 暂时不用深看 | 偶尔 |
| `utils/` | Buildroot 辅助工具 | 暂时不用深看 | 偶尔 |

### 根目录里几个常见文件也顺便说明一下

| 文件 | 作用 |
|---|---|
| `.config` | 当前 Buildroot 生效配置，通常由 defconfig 生成后再展开 |
| `.defconfig` | 当前工程的基础默认配置模板 |
| `Config.in` | Buildroot 的顶层 Kconfig 入口 |
| `Makefile` | Buildroot 的总构建入口 |
| `README` | Buildroot 的通用说明，不是这套 A1 SDK 的专项说明 |

## 这里有个“容易误会”的点

在你给的 `SDK目录解析prompt.md` 里，提到了 Buildroot 常见的 `board/`、`configs/` 等目录。

但在这套 SDK 里，真正高频使用的板级 `board/`、`configs/` 并不在仓库根目录，而是被放到了：

- `smart_software/board/`
- `smart_software/configs/`

这是因为厂商把 A1 相关内容做成了 **BR2_EXTERNAL 外部树**，而不是直接塞进 Buildroot 主树里。

## `smart_software/` 才是你真正要认识的核心区域

`smart_software/` 目录下当前主要有这些内容：

| 路径 | 作用 | 重要程度 |
|---|---|---|
| `smart_software/board/m1pro/` | A1/M1 Pro 板级目录，放 rootfs overlay 和内核 defconfig | 很高 |
| `smart_software/configs/` | Buildroot 的 defconfig 和 busybox 配置 | 很高 |
| `smart_software/package/` | 厂商自定义包，目前看到 `m1_sdk_lib` 和 `ssne_ai_demo` | 很高 |
| `smart_software/src/app_demo/` | 用户态 demo 源码 | 很高 |
| `smart_software/src/linux-5.15.24/` | 实际参与构建的内核源码目录 | 中到很高 |
| `smart_software/toolchain/` | A1 使用的外部交叉编译工具链 | 中 |
| `smart_software/Config.in` | 把自定义包挂进 Buildroot 菜单 | 中 |
| `smart_software/external.mk` | 把自定义包规则挂进 Buildroot | 中 |
| `smart_software/local.mk` | 关键文件，告诉 Buildroot 用 `smart_software/src/linux-5.15.24` 作为内核源码 | 很高 |
| `smart_software/external.desc` | 这个外部树的描述信息 | 低 |

## `smart_software/` 下面最重要的几个子目录怎么理解

### 1. `smart_software/configs/`

这里是“这块板子默认怎么编”的总开关。

你当前最重要的文件是：

- `smart_software/configs/smartsens_m1pro_release_defconfig`
- `smart_software/configs/busybox.config`

从 `smartsens_m1pro_release_defconfig` 可以看出很多关键关系：

- 使用的是 ARM Cortex-A7
- 使用外部工具链
- rootfs overlay 指向 `smart_software/board/m1pro/rootfs_overlay`
- 内核配置文件指向 `smart_software/board/m1pro/smartsens_demo_linux_defconfig`
- 设备树目标名是 `smartsens-m1-evb`
- 默认会打包 `m1_sdk_lib`
- 默认会打包 `ssne_ai_demo`
- 默认 demo 类型是 `face_detection`

一句话理解：

`smart_software/configs/` 决定了“整个系统默认长什么样”。

### 2. `smart_software/board/m1pro/`

这里是板级定制区，主要有两类内容：

- `rootfs_overlay/`
- `smartsens_demo_linux_defconfig`

#### `rootfs_overlay/`

这是 **覆盖到根文件系统里的内容**。里面的文件会在构建 rootfs 时被拷进去。

你当前能看到的关键文件包括：

- `etc/inittab`
- `etc/fstab`
- `etc/init.d/rcS`
- `etc/init.d/rcK`
- `usr/smartsoc/smartsoc_start.sh`
- `usr/bin/image_dump`
- `usr/debug.user.ini`

其中最值得你理解的是启动链：

1. `inittab` 指定系统启动时跑 `/etc/init.d/rcS`
2. `rcS` 负责挂载、创建设备节点、启动 mdev
3. `rcS` 最后执行 `/usr/smartsoc/smartsoc_start.sh`
4. `smartsoc_start.sh` 会先 `insmod` 一批内核模块
5. 然后它 `cd app_demo` 并运行 `./scripts/run.sh`
6. `run.sh` 会启动 `ssne_ai_demo`

这说明：

- 这套镜像启动后，会自动加载 AI/图像相关模块
- 还会自动拉起 demo
- 如果以后你想改“开机启动什么”“先加载哪些模块”，这里就是第一现场

#### `smartsens_demo_linux_defconfig`

这是 **内核自己的配置文件**，不是 Buildroot 配置。

它决定：

- 编哪些驱动
- 是否支持模块
- 调试选项是否打开
- 文件系统、串口、SPI、DMA 等内核能力是否启用

一句话区分：

- `smart_software/configs/smartsens_m1pro_release_defconfig` 管整个系统怎么构建
- `smart_software/board/m1pro/smartsens_demo_linux_defconfig` 管 Linux 内核本身怎么配置

### 3. `smart_software/package/`

这里是厂商自定义 Buildroot 包。

当前你真正要认识的两个包是：

| 路径 | 作用 |
|---|---|
| `smart_software/package/m1_sdk_lib/` | 把 A1 SDK 的库、头文件、内核模块安装进目标系统 |
| `smart_software/package/ssne_ai_demo/` | 把 demo 源码编译成程序，再把程序和资源安装进目标系统 |

这部分很重要，因为它决定了：

- 你的 demo 是怎么被 Buildroot 编译的
- 程序最后被装到镜像里的哪个位置
- 模型文件和脚本是怎么被打包进去的

从 `ssne_ai_demo.mk` 可以看出：

- 源码路径来自 `smart_software/src/app_demo/face_detection/ssne_ai_demo`
- 构建方式是 CMake
- 安装时会把 `ssne_ai_demo`、`app_assets/`、`scripts/` 一起拷到目标系统的 `app_demo/` 下

这也和 `output/target/app_demo/` 里的实际结果对应上了。

### 4. `smart_software/src/app_demo/face_detection/ssne_ai_demo/`

这就是你现在比赛阶段最应该读的“业务代码区”。

它里面大概可以这样理解：

| 路径 | 作用 |
|---|---|
| `demo_face.cpp` | 主程序入口 |
| `src/` | 具体实现代码 |
| `include/` | 头文件 |
| `app_assets/models/` | 模型文件 |
| `scripts/run.sh` | 运行脚本 |
| `CMakeLists.txt` | 应用的编译规则 |
| `cmake_config/Paths.cmake` | SDK 头文件和库的路径设置 |
| `README.md` | 这个 demo 的结构和流程说明 |

这个 demo 不是一个孤立可执行文件，它依赖：

- `output/opt/m1_sdk/usr/include/smartsoc/` 下的头文件
- `output/opt/m1_sdk/usr/lib/` 下的 SDK 动态库
- 系统启动时加载的一批 `.ko` 内核模块

也就是说，它是一个“跑在厂商 SDK 生态上的用户态 AI demo”。

### 5. `smart_software/src/linux-5.15.24/`

这里才是实际参与构建的内核源码。

这一点非常关键，因为很多新人会以为根目录的 `linux/` 就是内核源码区，但这里不是。根目录的 `linux/` 更像是 Buildroot 的内核构建规则层；真正的内核源码在：

- `smart_software/src/linux-5.15.24/`

你以后如果要碰这些问题，就要来这里：

- 设备树
- 板级硬件资源
- 驱动
- 内核模块
- DMA / OSD / NPU / 串口 / SPI 等底层能力

目前我确认到的板级设备树关键文件是：

- `smart_software/src/linux-5.15.24/arch/arm/boot/dts/smartsens-m1-evb.dts`
- `smart_software/src/linux-5.15.24/arch/arm/boot/dts/smartsens-m1.dtsi`

一句话理解：

- 改用户态 demo，看 `app_demo`
- 改板级硬件描述，看 `dts`
- 改内核驱动，看 `drivers`

### 6. `smart_software/toolchain/`

这里放的是当前板子使用的外部交叉编译工具链。

你平时不用进去改它，但要知道：

- 应用是用它交叉编译的
- Buildroot 配置里已经把它接好了
- 如果以后出现 ABI、库版本、交叉编译器不一致等问题，它就会变得重要

## `scripts/` 目录怎么理解

这几个脚本非常值得你先看懂：

| 脚本 | 作用 |
|---|---|
| `scripts/build_dl.sh` | 下载内核 tar 包、工具链 tar 包、package tar 包到 `cache/` |
| `scripts/build_release_sdk.sh` | 解压依赖、打补丁、加载 defconfig、编完整 SDK |
| `scripts/build_app.sh` | 删除 `output/build/ssne_ai_demo/` 后单独重编 demo |
| `scripts/a1_sc132gs_build.sh` | 检查是否编过 SDK，必要时先编，再重编 app，再整体编一遍，并复制 zImage |

如果你只想先把工程跑起来，最先应该看的是：

- `scripts/build_release_sdk.sh`

因为它直接把“整个 SDK 是怎么拼起来的”写出来了。

## `output/` 目录怎么理解

`output/` 是 Buildroot 最重要的产物目录。第一次接触时最容易混乱的是：哪些是最终镜像，哪些只是中间目录。

### 最重要的几个子目录

| 路径 | 作用 |
|---|---|
| `output/images/` | 最终镜像产物目录 |
| `output/build/` | 每个包和组件的中间构建目录 |
| `output/host/` | 构建过程中在主机侧使用的工具 |
| `output/target/` | 目标根文件系统展开后的目录树，便于检查文件有没有被装进去 |
| `output/opt/m1_sdk/` | 厂商 SDK 的库、头文件、模块等打包中间结果 |

### 你当前已经能看到的最终产物

`output/images/` 里现在有：

- `rootfs.cpio`
- `rootfs.cpio.gz`
- `zImage.smartsens-m1-evb`

对你来说最重要的是：

- `zImage.smartsens-m1-evb`

它对应的是当前板子的内核镜像产物。

### `output/target/` 要怎么理解

`output/target/` 不是“板子正在运行的真实根文件系统”，更像是打包前的目标目录树。

这里面现在能看到：

- `app_demo/`
- `lib/`
- `usr/`
- `etc/`

并且 `app_demo/` 下已经能看到：

- `ssne_ai_demo`
- `app_assets/`
- `scripts/run.sh`

这恰好能帮助你验证：

- 你的 demo 有没有被成功编译并安装进根文件系统
- 模型文件有没有被打进去
- 启动脚本有没有被打进去

### `output/build/` 为什么也值得看

出编译错误时，经常不是去根目录找，而是去：

- `output/build/linux-custom/`
- `output/build/ssne_ai_demo/`
- 其他出错包对应的构建目录

因为错误日志、临时生成文件、CMake 构建目录基本都在那里。

### `output/opt/m1_sdk/` 为什么值得知道

这个目录可以把它理解为“给应用链接用的厂商 SDK 中间安装区”。

这里当前能看到：

- `usr/include/smartsoc/` 头文件
- `usr/lib/` 动态库
- `extra/` 里的一批 `.ko` 内核模块

所以如果你以后想知道：

- demo 究竟用了哪些 SDK 头文件
- 动态库从哪里来
- 启动脚本加载的模块来自哪里

就要看这个目录。

## `patch/` 目录为什么要知道

`scripts/build_release_sdk.sh` 里做了这样一件事：

1. 先从 `cache/linux-5.15.24.tar.gz` 解压上游内核
2. 再应用 `patch/0001-Add-support-of-A1.patch`

这说明：

- 当前内核源码不是纯上游源码
- 厂商对上游内核做过 A1 支持补丁
- 如果以后你在上游 Linux 文档里看到了某些东西和当前源码不一样，不一定是你看错了，可能是厂商 patch 改过

## 如果只是为了完成比赛，我建议重点关注哪些目录

这是我最推荐的最小关注集合：

| 优先级 | 目录 | 为什么看它 |
|---|---|---|
| 1 | `scripts/` | 先知道怎么编、怎么重编 |
| 2 | `smart_software/configs/` | 先知道当前系统默认选了什么 |
| 3 | `smart_software/board/m1pro/rootfs_overlay/` | 先知道系统怎么启动、怎么自动跑 demo |
| 4 | `smart_software/src/app_demo/face_detection/ssne_ai_demo/` | 这是你最可能直接改的业务代码 |
| 5 | `smart_software/package/ssne_ai_demo/` | 知道 demo 为什么能被 Buildroot 编进去 |
| 6 | `output/images/` | 看最终镜像有没有生成 |
| 7 | `output/target/app_demo/` | 验证程序、脚本、模型有没有装进镜像 |
| 8 | `output/opt/m1_sdk/` | 遇到头文件、库、模块问题时很有用 |

### 比赛阶段暂时可以“不深挖”的目录

下面这些目录你现在知道名字和大致作用就够了，不必一上来就花很多时间：

- `arch/`
- `boot/`
- `docs/`
- `fs/`
- 根目录的 `linux/`
- 根目录的 `package/`
- `support/`
- `system/`
- 根目录的 `toolchain/`
- `utils/`

原因很简单：

- 这些大多是 Buildroot 的通用基础设施
- 你当前的比赛目标更像是“把现有板级 SDK 跑起来并改 demo”
- 不是“从零搭一套发行版”或“深度定制整个内核构建系统”

## 如果是真实做项目任务、要持续写代码，需要关注哪些目录

真实项目一般会按任务类型分别落到不同目录。下面这张表比较实用。

| 你在做什么 | 重点目录 | 说明 |
|---|---|---|
| 改 AI/视觉业务逻辑 | `smart_software/src/app_demo/face_detection/ssne_ai_demo/` | 最直接的业务代码区 |
| 换模型、换资源文件 | `smart_software/src/app_demo/face_detection/ssne_ai_demo/app_assets/` | 模型和资源跟代码一起打包 |
| 让程序被系统自动启动 | `smart_software/board/m1pro/rootfs_overlay/etc/init.d/`、`usr/smartsoc/` | 启动脚本在这里 |
| 增加一个新的应用包 | `smart_software/package/`、`smart_software/Config.in` | 要把你的程序做成 Buildroot 包 |
| 改板子默认构建项 | `smart_software/configs/` | 改 defconfig、busybox 配置 |
| 改内核配置 | `smart_software/board/m1pro/smartsens_demo_linux_defconfig` | 内核开关在这里 |
| 改设备树 | `smart_software/src/linux-5.15.24/arch/arm/boot/dts/` | 板级硬件资源描述在这里 |
| 改驱动 | `smart_software/src/linux-5.15.24/drivers/` | 底层驱动源码在这里 |
| 查应用依赖哪些头文件和库 | `output/opt/m1_sdk/` | 厂商 SDK 的头文件、库、模块都在这里 |
| 看构建失败的中间状态 | `output/build/` | 出错时经常要来这里看日志和临时文件 |

## 关于 SC132GS / 摄像头这一层，我的判断

你在背景里提到了 `SC132GS`。

我在当前仓库里做了选择性检索后，有两个结论：

1. 在我这次选择性查阅到的源码范围里，**没有直接搜到明确的 `SC132GS` 字样**
2. 当前 demo 的取图方式，更像是通过厂商 SDK 提供的接口取 `kSensor0` 的图像，再走 AI 预处理和推理流程

这说明至少在你现在这套比赛工程里：

- 你大概率**不需要一上来就从传感器驱动源码啃起**
- 更现实的入口是先把：
  - demo 跑通
  - 模型替换或改逻辑
  - 输出结果可视化
  - 镜像重新编译与烧录
  这些事情做好

如果以后你真的要处理更底层的问题，比如：

- 摄像头初始化异常
- 分辨率/裁剪方式不对
- DMA/OSD/NPU 模块联动有问题
- 板级硬件资源映射需要改

那时再去重点看：

- `smart_software/src/linux-5.15.24/arch/arm/boot/dts/`
- `smart_software/src/linux-5.15.24/drivers/`
- `output/opt/m1_sdk/extra/` 里的内核模块
- `smart_software/board/m1pro/rootfs_overlay/usr/smartsoc/smartsoc_start.sh`

## 我建议你的学习顺序

如果你是第一次做这个方向，我建议按这个顺序熟悉：

1. 先看 `scripts/build_release_sdk.sh`
2. 再看 `smart_software/configs/smartsens_m1pro_release_defconfig`
3. 再看 `smart_software/board/m1pro/rootfs_overlay/`
4. 然后重点看 `smart_software/src/app_demo/face_detection/ssne_ai_demo/`
5. 编一次后去看 `output/images/` 和 `output/target/app_demo/`
6. 最后再进入 `smart_software/src/linux-5.15.24/`

这样做的好处是：

- 先把“工程怎么跑起来”搞明白
- 再看“默认系统长什么样”
- 然后才看“我真正要改的程序”
- 最后才进入“更难的内核和设备树层”

这个顺序对比赛最省时间，也最不容易陷进底层细节里出不来。

## 最后用一句话总结

如果只为了完成比赛，请把主要精力放在：

- `scripts/`
- `smart_software/configs/`
- `smart_software/board/m1pro/`
- `smart_software/src/app_demo/face_detection/ssne_ai_demo/`
- `smart_software/package/ssne_ai_demo/`
- `output/images/`

如果以后要做真实项目，再逐步扩展到：

- `smart_software/src/linux-5.15.24/`
- `smart_software/package/`
- `output/build/`
- `output/opt/m1_sdk/`

先把这套 SDK 当成“厂商已经帮你搭好的 Buildroot + 板级平台 + AI demo 框架”，你当前最重要的任务不是重造地基，而是先学会在这个框架里改、编、打包、运行、验证。
