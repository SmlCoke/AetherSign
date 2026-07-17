# 文件目录结构

### preminilary/

初赛最终 SSNE_AI_DEMO 代码，包含两种功能:

1. Palm Detector
2. Palm Detector + Hand Landmarker (串行级联)

两种模式可以通过命令行参数切换，并且附带有 Performance Monitor。

详细说明参考：[README.md](./preminilary/README.md)

其余重要文档：

- 项目背景: [project-7.md](./preminilary/project-7.md)
- 提示词模板：[template.md](./preminilary/template.md)

### vertical

竖屏版本：720x1280 格式的图像作为原始输入，同样包含两种功能：

1. Palm Detector
2. Palm Detector + Hand Landmarker (串行级联)

两种模式可以通过命令行参数切换，并且附带有 Performance Monitor。

但是 Palm Detector 没有训练好，检测效果很差。

最终放弃该方案。