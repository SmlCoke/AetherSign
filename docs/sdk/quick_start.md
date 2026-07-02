# A1 SDK 比赛开发 Quick Start

这份文档只回答一件事：

**如果我要在这套 SDK 里写自己的代码，把它编起来，上板验证，我应该怎么做。**

不追求讲全，只追求你现在能顺着走通流程。

## 1. 先记住这几个最重要的目录

比赛阶段，你主要只看这几个地方：

| 路径 | 你用它干什么 |
|---|---|
| `scripts/` | 编译入口 |
| `smart_software/src/app_demo/face_detection/ssne_ai_demo/` | 官方 demo 源码，你最容易上手的代码区 |
| `smart_software/package/ssne_ai_demo/` | 告诉 Buildroot 怎样编译并安装这个 demo |
| `smart_software/board/m1pro/rootfs_overlay/` | 系统启动脚本，决定上板后自动加载什么、自动运行什么 |
| `output/target/app_demo/` | 检查你的程序有没有被装进镜像 |
| `output/images/` | 最终镜像产物，烧录时重点看这里 |

## 2. 你现在最推荐的开发方式

对于比赛，我建议你先不要自己新造一套工程，**直接在官方 demo 的框架上改**。

也就是先把你的代码放在这里：

`smart_software/src/app_demo/face_detection/ssne_ai_demo/`

原因很简单：

- 这个目录已经能编通
- 已经知道怎么取相机图像
- 已经知道怎么链接厂商 SDK 库
- 已经知道怎么被打包进镜像
- 已经知道怎么在板子启动后自动运行

换句话说，你现在最省时间的路线不是“从零新建项目”，而是：

1. 先在 `ssne_ai_demo` 上改出你自己的版本
2. 跑通编译
3. 跑通上板
4. 之后如果你觉得有必要，再把它独立成自己的 app

## 3. 这套流程到底是怎么串起来的

你可以把完整链路理解成下面这 6 步：

1. 你在 `smart_software/src/app_demo/face_detection/ssne_ai_demo/` 里写代码
2. `smart_software/package/ssne_ai_demo/ssne_ai_demo.mk` 把这个目录当成一个 Buildroot 包来编译
3. 编译后的程序、脚本、模型会被安装到 `output/target/app_demo/`
4. 根文件系统被打包进内核镜像
5. 最终产物出现在 `output/images/zImage.smartsens-m1-evb`
6. 板子启动后，启动脚本会自动加载模块并运行 `app_demo/scripts/run.sh`

所以你真正要关心的是三层：

- 代码层：`smart_software/src/app_demo/face_detection/ssne_ai_demo/`
- 打包层：`smart_software/package/ssne_ai_demo/`
- 启动层：`smart_software/board/m1pro/rootfs_overlay/`

## 4. 你应该把哪些文件放在哪里

### 最简单的做法：直接改官方 demo

在这个目录下工作：

`smart_software/src/app_demo/face_detection/ssne_ai_demo/`

目录含义可以先这样记：

| 位置 | 放什么 |
|---|---|
| `demo_face.cpp` | 主程序入口 |
| `src/` | 你的主要 `.cpp` 实现文件 |
| `include/` | 你的头文件 |
| `app_assets/models/` | 模型文件 |
| `app_assets/` | 其他资源文件 |
| `scripts/run.sh` | 板子上运行这个程序时执行的脚本 |
| `CMakeLists.txt` | 这个 app 的编译规则 |

### 你现在最实用的文件放置建议

- 新增业务逻辑：放到 `src/` 和 `include/`
- 改程序主流程：改 `demo_face.cpp`
- 换模型：放到 `app_assets/models/`
- 改运行参数或启动命令：改 `scripts/run.sh`
- 新增源码文件：通常要确认 `CMakeLists.txt` 能把它们编进去

当前 `CMakeLists.txt` 已经会自动把 `src/*.cpp` 加入编译，所以：

- 你往 `src/` 里加新的 `.cpp`，一般不需要额外手动登记
- 但如果你要换主程序入口，比如不想用 `demo_face.cpp`，那就要改 `CMakeLists.txt`

## 5. 第一次上手，建议你先这样改代码

先不要一开始就大改架构。

建议分三步：

1. 先只改 `demo_face.cpp` 里的打印和简单逻辑，确认你改的代码真的上板生效
2. 再把自己的算法代码拆到 `src/` 和 `include/`
3. 最后再考虑换模型、改输入输出流程、改自动启动方式

这样你能最快确认：

- 代码改动是否参与编译
- 编译结果是否进入镜像
- 板子运行的是否是你改过的程序

## 6. 摄像头这一层你现在应该怎么理解

这是你第二个困扰点的核心。

### 比赛阶段的正确思路

**先不要把自己当成“在写摄像头驱动的人”。**

你当前更像是在：

- 使用厂商已经准备好的底层驱动和内核模块
- 通过厂商 SDK 的用户态接口拿图像
- 在用户态完成预处理、推理、后处理和显示

也就是说，比赛阶段你通常应该：

- 复用官方 demo 的取图流程
- 复用官方 demo 的 SDK 接口
- 在这个基础上换你自己的算法逻辑

而不是一上来就改：

- 传感器驱动
- 内核驱动
- 设备树

### 这套 demo 里摄像头数据是怎么来的

官方 demo 的思路是：

1. 启动脚本先加载一批 `.ko` 模块
2. 用户态程序初始化 SSNE / 图像处理相关能力
3. 通过 SDK 接口从 `kSensor0` 取图
4. 再把图像送入预处理和推理流程

也就是说，你在代码里真正应该优先学习的是：

- `IMAGEPROCESSOR`
- `GetImageData(...)`
- `SCRFDGRAY`
- `VISUALIZER`

这些都已经在官方 demo 里串好了。

### 对你最重要的结论

如果你的目标只是完成比赛：

- 先把“相机取图”当作一个已经可用的输入源
- 你重点改“取到图之后怎么处理”
- 除非明确遇到底层问题，否则先不要碰驱动和设备树

## 7. 你第一次开发时，建议按这条最短路径走

### Step 1: 先编一次官方工程

先完整编译一次 SDK：

```bash
bash scripts/build_release_sdk.sh
```

它会做这些事：

- 下载依赖
- 检查工具链
- 检查内核源码
- 加载 `smartsens_m1pro_release_defconfig`
- 编整个 SDK

### Step 2: 确认关键产物存在

编完先看：

- `output/images/zImage.smartsens-m1-evb`
- `output/target/app_demo/ssne_ai_demo`

如果这两个都存在，说明：

- 镜像编出来了
- demo 也确实被装进根文件系统了

### Step 3: 开始改你的代码

先改这里：

- `smart_software/src/app_demo/face_detection/ssne_ai_demo/demo_face.cpp`
- `smart_software/src/app_demo/face_detection/ssne_ai_demo/src/`
- `smart_software/src/app_demo/face_detection/ssne_ai_demo/include/`

### Step 4: 只重编 app

改完代码后，先不要每次都全量编 SDK。

先试：

```bash
bash scripts/build_app.sh
```

这个脚本会：

- 删除 `output/build/ssne_ai_demo/`
- 重新编译 `ssne_ai_demo`

这是你比赛阶段最高频的命令之一。

### Step 5: 检查 app 是否重新装进目标系统

看这里：

- `output/target/app_demo/ssne_ai_demo`
- `output/target/app_demo/app_assets/`
- `output/target/app_demo/scripts/run.sh`

如果这些更新了，说明你的 app 已经重新被打进目标文件系统。

### Step 6: 需要最终镜像时，再做完整构建

如果你最终要重新生成可烧录镜像，再跑：

```bash
bash scripts/build_release_sdk.sh
```

然后重点拿：

- `output/images/zImage.smartsens-m1-evb`

## 8. 烧录 / 上板验证时你该怎么看

这套仓库里我确认到的是“编译出镜像”的部分，**不是完整的烧录工具链**。

所以你现在可以这样理解：

- 编译结果在 `output/images/zImage.smartsens-m1-evb`
- 这个文件就是你后续烧录时最应该关注的产物
- 真正的烧录动作，按官方烧录工具或官方文档流程做

也就是说，这套仓库负责：

- 生成镜像

官方工具负责：

- 把镜像写进开发板

### 上板后程序为什么会自动跑

因为启动链已经配好了：

- `rootfs_overlay/etc/init.d/rcS`
- `rootfs_overlay/usr/smartsoc/smartsoc_start.sh`
- `app_demo/scripts/run.sh`

所以如果镜像烧录成功，板子启动后通常会自动：

1. 加载相关内核模块
2. 进入 `app_demo`
3. 执行 `run.sh`
4. 启动 `ssne_ai_demo`

## 9. 如果你想让“我的程序”代替官方 demo，最省事的方法是什么

### 方法 A：直接在官方 demo 上改

这是最推荐的。

你不需要改包名，不需要改 Buildroot 配置，不需要改安装路径。

你只需要：

- 继续使用 `ssne_ai_demo`
- 把里面的代码逐步替换成你的逻辑

优点：

- 最稳
- 最少改动
- 最适合比赛赶进度

### 方法 B：复制一份官方 demo，做成你自己的 app

如果你后面觉得需要和官方 demo 分开，可以这样做：

1. 新建目录  
   `smart_software/src/app_demo/<你的应用名>/ssne_ai_demo/`
2. 把官方 demo 内容复制过去
3. 修改  
   `smart_software/configs/smartsens_m1pro_release_defconfig`
4. 把  
   `BR2_PACKAGE_SSNE_AI_DEMO_APP="face_detection"`  
   改成  
   `BR2_PACKAGE_SSNE_AI_DEMO_APP="<你的应用名>"`

因为 `ssne_ai_demo.mk` 现在是按这个规则找源码的：

`smart_software/src/app_demo/<app_name>/ssne_ai_demo`

所以你只要改 `BR2_PACKAGE_SSNE_AI_DEMO_APP`，它就会去新的目录找源码。

### 比赛阶段建议

比赛阶段优先用方法 A。

等你确认流程跑通，再考虑方法 B。

## 10. 什么时候你才需要碰内核 / 驱动 / 设备树

只有在遇到下面这类问题时，才建议你往下看：

- 摄像头根本起不来
- 分辨率、裁剪、方向明显不对
- 某些模块加载失败
- OSD / NPU / DMA 行为异常
- 你需要改板级硬件配置

这时才逐步去看：

- `smart_software/src/linux-5.15.24/`
- `smart_software/src/linux-5.15.24/arch/arm/boot/dts/`
- `smart_software/src/linux-5.15.24/drivers/`
- `smart_software/board/m1pro/rootfs_overlay/usr/smartsoc/smartsoc_start.sh`

如果你现在只是做比赛算法应用，先别把精力花在这里。

## 11. 你现在最值得照着做的一份最小流程

### 日常开发循环

1. 改 `smart_software/src/app_demo/face_detection/ssne_ai_demo/` 里的代码
2. 跑 `bash scripts/build_app.sh`
3. 检查 `output/target/app_demo/ssne_ai_demo`
4. 需要出镜像时，跑 `bash scripts/build_release_sdk.sh`
5. 拿 `output/images/zImage.smartsens-m1-evb` 去按官方流程烧录
6. 上板观察是否自动启动、相机是否正常、算法结果是否符合预期

### 你最先读的 5 个文件

我建议你先读这 5 个：

1. `scripts/build_release_sdk.sh`
2. `scripts/build_app.sh`
3. `smart_software/src/app_demo/face_detection/ssne_ai_demo/demo_face.cpp`
4. `smart_software/package/ssne_ai_demo/ssne_ai_demo.mk`
5. `smart_software/board/m1pro/rootfs_overlay/usr/smartsoc/smartsoc_start.sh`

把这 5 个看懂，你对“代码怎么进镜像、镜像怎么上板、板子怎么启动程序”的理解就会清楚很多。

## 12. 当前这个 SDK 有一个你要提前知道的小坑

`scripts/run.sh` 里现在写的是：

```bash
./ssne_ai_demo -f ./app_assets/app_config.json
```

但当前目录里我看到的核心资源主要是：

- `app_assets/models/face_640x480.m1model`
- `app_assets/colorLUT.sscl`

而 `demo_face.cpp` 里模型路径是直接写死的：

`/app_demo/app_assets/models/face_640x480.m1model`

所以你后面如果遇到：

- 启动脚本参数不匹配
- `app_config.json` 缺失
- 程序启动方式和源码不一致

先优先核对：

- `smart_software/src/app_demo/face_detection/ssne_ai_demo/scripts/run.sh`
- `smart_software/src/app_demo/face_detection/ssne_ai_demo/demo_face.cpp`

不要在这个坑里卡太久。

## 13. 最后一句话版结论

你现在最应该做的是：

- 把官方 `ssne_ai_demo` 当成你的工程模板
- 先在它里面改代码
- 用 `bash scripts/build_app.sh` 做高频迭代
- 用 `bash scripts/build_release_sdk.sh` 生成最终镜像
- 用 `output/images/zImage.smartsens-m1-evb` 按官方流程烧录上板

相机、驱动、设备树这些底层先默认“官方已经帮你打通”，你先把精力集中在：

- 取图之后的处理逻辑
- 模型替换
- 程序编译
- 上板验证

这才是比赛阶段最稳、最快的路径。
