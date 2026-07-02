# 构建路径与重命名评估

本文档只分析当前 SDK 构建脚本和目录名之间的耦合关系。没有执行编译，也没有把父目录 `face_detection` 改名。

## 1. 已完成的入口文件清理

当前主程序入口已经从：

```text
demo_face.cpp
```

改为：

```text
main.cpp
```

同时 `CMakeLists.txt` 已同步：

```cmake
add_executable(ssne_ai_demo ${PROJECT_SOURCE_DIR}/main.cpp)
```

因此仅入口文件改名不会影响 Buildroot 包名、板端可执行文件名或运行脚本。最终安装到板端的程序仍然是：

```text
/app_demo/ssne_ai_demo
```

运行脚本仍然是：

```text
/app_demo/scripts/run.sh
```

## 2. 当前构建链路

用户给出的构建命令最终会执行：

```bash
./scripts/a1_sc132gs_build.sh
```

该脚本主要流程：

```bash
if zImage exists:
    skip first build_release_sdk.sh
else:
    build_release_sdk.sh

build_app.sh
build_release_sdk.sh
```

`scripts/build_app.sh` 内容是：

```bash
rm -rf output/build/ssne_ai_demo/
make ssne_ai_demo-rebuild
```

也就是说，应用源码目录不是由 `a1_sc132gs_build.sh` 直接写死，而是由 Buildroot 的 `ssne_ai_demo` 包规则决定。

## 3. 父目录 `face_detection` 的耦合点

关键文件：

```text
smart_software/package/ssne_ai_demo/ssne_ai_demo.mk
smart_software/package/ssne_ai_demo/Config.in
smart_software/configs/smartsens_m1pro_release_defconfig
```

`ssne_ai_demo.mk` 中源码路径拼接方式是：

```make
SSNE_AI_DEMO_SITE = $(S1SRC)/app_demo/$(call qstrip,$(BR2_PACKAGE_SSNE_AI_DEMO_APP)/ssne_ai_demo)
```

当前配置中：

```text
BR2_PACKAGE_SSNE_AI_DEMO_APP="face_detection"
```

所以实际源码路径是：

```text
smart_software/src/app_demo/face_detection/ssne_ai_demo
```

`Config.in` 中还有默认值：

```text
default "face_detection"
default "src/app_demo/face_detection"
```

其中第二个默认值会导出为：

```make
EXPORT_LIB_M1_SDK_ROOT_PATH = $(call qstrip,$(BR2_M1_SDK_ROOT_PATH))
```

而本工程的 `cmake_config/Paths.cmake` 会用它拼 SDK include/lib 路径：

```cmake
set(SDK_ROOT "$ENV{BASE_DIR}/$ENV{EXPORT_LIB_M1_SDK_ROOT_PATH}" CACHE PATH "SDK root directory")
```

因此，父目录名 `face_detection` 不是纯展示名称，它参与了 Buildroot 找源码和 CMake 找 SDK 库的过程。

## 4. 是否可以直接把 `face_detection` 改成 `slr_system`

结论：不要只改文件夹名。

如果只把目录：

```text
src/app_demo/face_detection
```

改成：

```text
src/app_demo/slr_system
```

但不改 Buildroot 配置，`make ssne_ai_demo-rebuild` 仍会去找：

```text
src/app_demo/face_detection/ssne_ai_demo
```

结果大概率是源码路径不存在，应用包构建失败。

## 5. 推荐的完整改名方案

如果后续确实要把父目录改成 `slr_system`，建议同步修改：

1. 移动目录：

```text
smart_software/src/app_demo/face_detection/ssne_ai_demo
```

到：

```text
smart_software/src/app_demo/slr_system/ssne_ai_demo
```

2. 修改 `smart_software/configs/smartsens_m1pro_release_defconfig`：

```text
BR2_PACKAGE_SSNE_AI_DEMO_APP="slr_system"
```

3. 修改 `smart_software/package/ssne_ai_demo/Config.in` 默认值：

```text
default "slr_system"
default "src/app_demo/slr_system"
```

4. 清理 Buildroot 应用构建缓存：

```bash
rm -rf output/build/ssne_ai_demo/
```

这一步 `scripts/build_app.sh` 已经会做。

5. 重新执行完整镜像构建流程。

## 6. 板端运行是否受影响

只要 Buildroot 能成功把应用安装到目标系统，板端运行路径不需要跟着改。安装规则仍会把内容复制到：

```text
output/target/app_demo/
```

板端启动脚本仍然进入：

```text
app_demo
```

并执行：

```text
./scripts/run.sh
```

也就是说，父目录 `face_detection`/`slr_system` 主要影响编译期源码查找，不直接影响板端 `/app_demo` 运行路径。

## 7. 本次没有修改的内容

本次没有直接修改：

- `smart_software/src/app_demo/face_detection` 父目录名。
- `BR2_PACKAGE_SSNE_AI_DEMO_APP`。
- `BR2_M1_SDK_ROOT_PATH`。
- Buildroot 配置和 defconfig。

原因是用户当前只要求评估，不要求真正改父目录名。



## 附录：一个问题

对，这个 SDK 的 Buildroot 配置文件不在 `output/.config`，而是在 SDK 根目录：

```sh
/home/smartsens_flying_chip_a1_sdk/A1_SDK_SC132GS/smartsens_sdk/.config
```

我本地确认了一下，当前根目录 `.config` 里确实还是旧值：

```text
BR2_PACKAGE_SSNE_AI_DEMO_APP="face_detection"
```

所以你应该这样查：

```sh
cd /home/smartsens_flying_chip_a1_sdk/A1_SDK_SC132GS/smartsens_sdk

grep -n "BR2_PACKAGE_SSNE_AI_DEMO_APP" .config
```

**推荐修法：重新生成配置**

因为你的 defconfig 里已经改成了 `slr_system`，所以直接重新生成 `.config`：

```sh
make BR2_EXTERNAL=./smart_software smartsens_m1pro_release_defconfig
```

然后检查：

```sh
grep -n "BR2_PACKAGE_SSNE_AI_DEMO_APP" .config
```

期望看到：

```text
BR2_PACKAGE_SSNE_AI_DEMO_APP="slr_system"
```

然后重新编译：

```sh
./scripts/a1_sc132gs_build.sh
```

或者只重编 app：

```sh
rm -rf output/build/ssne_ai_demo/
make ssne_ai_demo-rebuild
```

**如果你想最小改动，也可以直接改当前 `.config`**

```sh
sed -i 's/BR2_PACKAGE_SSNE_AI_DEMO_APP="face_detection"/BR2_PACKAGE_SSNE_AI_DEMO_APP="slr_system"/' .config
```

再确认：

```sh
grep -n "BR2_PACKAGE_SSNE_AI_DEMO_APP" .config
```

然后：

```sh
rm -rf output/build/ssne_ai_demo/
make ssne_ai_demo-rebuild
```

我更推荐第一种，因为它会让 `.config` 和 `smart_software/configs/smartsens_m1pro_release_defconfig` 保持一致。问题本质就是：`a1_sc132gs_build.sh` 看到 SDK 已经编译过，就跳过了重新生成配置那一步，于是旧 `.config` 继续生效。