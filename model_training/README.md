# MobileNetV3-Small & TFLite 部署与训练环境

此文件夹包含基于 TensorFlow 和 Keras 训练 MobileNetV3-Small 三分类模型，并将其转换为 TFLite 格式以在边缘计算侧推理的代码和环境配置。生成的 TFLite 量化模型可以直接部署到例如 Loongson 2K0300 等低功耗设备中。

## 目录结构
- `venv/`：Python 虚拟环境，内含所需的 TensorFlow、OpenCV 等依赖库。
- `dataset/`：图片数据集目录。其下应含有分类名称相关的子文件夹（当前支持三分类：`supplies`、`vehicle`、`weapon`）。在训练时代码会自动根据此目录加载并划分训练集与验证集。
- `saved_models/`：程序运行时自动生成，存放保存的各阶段模型文件（`.keras`，`.tflite`）。

### Python 脚本功能说明
本环境内集成了训练到测试的完整开发工具链：

1. **`train.py` (模型训练核心脚本)**
   构建基于 MobileNetV3-Small 的迁移层（冻结原始特征，只训练上层分类器），一键读取 `dataset/` 数据集进行拟合训练。为了兼容最新版的 TensorFlow，默认直接保存至标准 Keras 3 的 `.keras` 后缀格式。
2. **`export_tflite.py` (模型 TFLite 导出脚本)**
   自动读取训练好的 `.keras` 模型并转为对应的 `.tflite` 格式。代码中默认会额外生成一份启用了默认优化属性的静态量化版模型（`_quantized.tflite`），极大地减小了体积并获得最快的硬件推理速度。
3. **`test_model.py` (静态图片推理验证脚本)**
   功能验证工具。每次执行会从您的 `dataset/` 随意抽查一张真实图片片段，并同时送入原生的 Keras 模型、基础的 TFLite，以及量化的 TFLite 内层测算，验证是否输出类别一致并展示预测置信度，这可以检测量化有没有严重削弱分类精度。
4. **`webcam_test.py` (摄像头实时预演推流脚本)**
   依赖计算机本地环境的 OpenCV 来读取设备摄像头画面流，并应用量化版 TFLite 作实时的视频逐帧分类与推理测算界面，直观地评估模型的实际检测率和 FPS 执行性能。

## 使用流程

### 一、激活虚拟环境
项目已经配置好了一个包含所有必需库的虚拟环境。在 `model_training` 目录下执行：
```bash
source venv/bin/activate
```
此时您会看到命令提示符加上了 `(venv)` 的前缀。

### 二、开始模型训练
确保 `dataset/` 中准备好了分类图片目录，随后执行：
```bash
python train.py
```
这将在 `saved_models/` 目录下生成一个名为 `mobilenet_v3_small_model.keras` 的预训练结果集。

### 三、导出为跨平台 TFLite
运行导出脚本把它压入至移动/嵌入端兼容模式：
```bash
python export_tflite.py
```
转换成功后会包含体积只有 1.2M 左右的极速版 `mobilenet_v3_small_quantized.tflite` 模型。

### 四、测试与验证
您可以通过下述指令对新出炉的模型执行测试：
```bash
# 单帧随机图片交叉验证
python test_model.py

# 桌面开启摄像头进行动态验证
python webcam_test.py
```
