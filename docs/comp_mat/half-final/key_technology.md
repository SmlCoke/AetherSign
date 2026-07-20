# AetherSign 项目关键技术总结

## I. 轻量化部署

1. Sign2Skeleton2Gloss 路线：将原始视频帧**先转化为骨骼关键点坐标序列**，然后对骨骼关键点坐标序列进行时空建模，而非直接对原始视频帧进行时空建模。该路线的优势在于：
    - **数据量小**：骨骼关键点坐标序列的数据量远小于原始视频帧（例如原始1280x720灰度图数据量大约在 1280x720B, 转化出的关键点最多约42个二维坐标），减少了模型输入的维度。
    - **计算效率高**：由于输入数据量小，Gloss Translator 模型推理速度更快，参数量更小，适合在资源受限的设备上部署。
    - **鲁棒性强**：骨骼关键点对光照、背景等因素不敏感，提高了模型在不同环境下的泛化能力。
2. 从 ROI 回归关键点 
3. Hand Landmarker 的重参数化技术
    - 空间特征提取模块中的 RepConv 模块：在训练时使用"卷积+BN"的结构，在推理时将其转化为单个卷积层，减少了推理时的计算量。
    
    $$\begin{align*} 
    & \mathbf{y_1}  = \mathbf{Wx} \\ 
    & \mathbf{y}  = \frac{(\mathbf{y_1} - \mu_{\text{running}})}{\sqrt{\sigma_{\text{running}}^2 + \epsilon}} \cdot \gamma + \beta \Rightarrow \\
    & \mathbf{y} = \mathbf{Wx} \cdot \frac{\gamma}{\sqrt{\sigma_{\text{running}}^2 + \epsilon}} - \mu_{\text{running}} \cdot \frac{\gamma}{\sqrt{\sigma_{\text{running}}^2 + \epsilon}} + \beta \\
    \end{align*}$$

    记：$\sigma^{\prime}=\sqrt{\sigma_{\text{running}}^2 + \epsilon}$，则：


    $$\mathbf{y} = \mathbf{\frac{W}{\sigma^{\prime}}x}+\beta-\mu_{\text{running}} \cdot \frac{\gamma}{\sigma^{\prime}}$$

## II. 多阶段训练

训练 Hand Landmarker 模型时，采用了多阶段训练策略。具体流程如下：

1. pretrain: 采用自行录制的大规模数据集，利用 Goolge MediaPipe 官方模型进行自动化标注，得到 pesudo label，共计约 12w 张 Hand ROI 正样本图像，用于预训练。此外，预训练阶段本身也分两个阶段
    - geometry : 仅训练 Landmarks 21个坐标的预测能力，仅使用正样本
    - multitask: 多任务训练，同时回归 landmarks, handedness, hand_flag，此阶段同时采用正样本和人工复核得出的真正的负样本。
2. finetune: 采用人工标注的高质量数据集+部分pretrain阶段伪标签数据集进行微调，期望提升 landmarks 的预测精度