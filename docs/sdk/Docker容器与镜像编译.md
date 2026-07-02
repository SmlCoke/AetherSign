# Docker容器与镜像编译

## I. 编译过程
关于之前编译的镜像，通过
```bash
docker run -itd --name A1_Builder -v "./data:/home/smartsens_flying_chip_a1_sdk" -p 8080:8080 a1-sdk-builder
```
将容器挂载到了本地的 `D:\Docker\project\smartsen\data` 目录。
然后利用：
```bash
docker exec -it A1_Builder bash
root@978515f4c2cf:/home/smartsens_flying_chip_a1_sdk# cd smartsens_sdk
root@978515f4c2cf:/home/smartsens_flying_chip_a1_sdk# ./scripts/a1_sc132gs_build.sh
```



## II. 编译产物和源码在哪里？

因为在启动容器时使用了 `-v "./data:/home/smartsens_flying_chip_a1_sdk"`，即我们将 `data` 文件夹与容器内的 `/home/smartsens_flying_chip_a1_sdk` 目录进行了绑定，**所以本地 `data` 文件夹存放了调试开发板所需要的所有核心文件和编译产物。该文件夹非常重要！**

此外，还有两个重要子目录：

1. **最终烧录镜像（固件）**：
    就在 `data\smartsens_sdk\output\images\` 目录下。之前烧录用到的 `zImage.smartsens-m1-evb` 就在这里，可以直接用 Windows 里的烧录工具去读取它，不需要从 Docker 里面往外拷贝。
2. **官方示例源码**：
    根据之前的教程，它在 `data\smart_software\src\app_demo\` 目录下。
    这也就是说：
    **完全可以在 Windows 的 VS Code 里读代码、写代码、修改模型路径，保存后，容器里会瞬间同步更新！**


## III. 如果 Docker 容器损毁了怎么办？

如果不小心执行了 `docker rm -f A1_Builder` 把容器彻底删除了，或者电脑重装系统了，只要 `data` 文件夹还在那几个小时编译出的所有 `.o` 中间文件、工具链、源码就全都在。
*   **重新编译**：
    只需要在 `D:\Docker\project\smartsen\` 目录下，重新运行一次启动命令：
    `docker run -itd --name A1_Builder_New -v "./data:/home/smartsens_flying_chip_a1_sdk" -p 8080:8080 a1-sdk-builder`
*   **为什么不需要再等 1.5 小时？**
    再次进入新容器，执行 `./scripts/a1_sc132gs_build.sh` 时，底层的 `make` 编译系统会去检查所有的文件时间戳。它会发现 GCC 编译器已经有了，Linux 内核的 `.o` 文件也都在啊。
    于是它会**直接跳过这 99% 的庞大工程**，只花几秒钟检查一下有没有新修改的 C++ 代码（比如刚改的 `app_demo`），然后花很少时间把它们重新打包进 `zImage`。

**总结：只要 `data` 目录不删，以后哪怕换十个电脑、建一百个容器，编译时间永远只有 1~2 分钟（即增量编译）！**

