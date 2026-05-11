# V5 视觉子类模型训练指南

本文面向一个没有上下文的 AI。目标是在已经完成人工标注后，继续训练板端可部署的 tiny32 TFLite Micro 模型。核心原则是：旧搜索结果只能作为强先验，不能机械复用；新一轮训练也不能从无约束随机搜索开始。

## 1. 本地事实

已阅读并核对的本地文件和目录：

- `model_training/README.md`：仍是旧 MobileNetV3 / 三分类说明，已经不符合当前 tiny32 / TFLM / 视觉子类路线。
- `model_training/train_tiny32_sixclass_scan.py`：当前主训练基线仍是 6 个视觉子类，且 `load_dataset()` 通过文件名推断子类。
- `model_training/train_tiny32_v4_mild_calib_scan.py`：包含 V4 增强、stress 评估、mild calibration、final export 逻辑。
- `model_training/generate_v4_stress_directed_candidates.py`：包含上一轮有效的 stress-directed 搜索空间。
- `model_training/retest_tiny32_v4_candidates.py`：包含上一轮重训复测路径。
- `model_training/experiments/v4_*`：包含旧 6 子类模型的大范围搜索、细扫、重训、量化和 stress 结果。
- `model_training/experiments/v4_stress_directed_20260508_215328/confusing_images/all_scan_best_clean_confusing_images.csv`：包含多模型共同混淆图片。
- `model_training/dataset/`：当前人工标注后的数据目录。

当前人工标注后的目录计数如下：

| parent | count |
| --- | ---: |
| supplies | 104 |
| vehicle | 96 |
| weapon | 104 |

| visual subclass | count |
| --- | ---: |
| first_aid_kit | 51 |
| telescope | 53 |
| ambulance | 55 |
| armoured_car | 41 |
| firearms_short | 23 |
| firearms_long | 31 |
| explosive_grenade | 44 |
| explosive_C4 | 6 |

关键结论：

- 新标注已经把旧 `firearms` 拆为 `firearms_short` 和 `firearms_long`，把旧 `explosive` 拆为 `explosive_grenade` 和 `explosive_C4`。
- `explosive_C4` 只有 6 张，不能用普通 5 折结果直接判断 C4 子类能力。它可以帮助模型学到“武器父类”的局部视觉模式，但 C4 子类 recall 只能作为参考，不应成为唯一淘汰条件。
- 当前训练脚本如果不修改，会继续把 `weapon/explosive_C4/...` 里的图片按文件名识别成旧 `explosive`，等于忽略人工标注结果。第一步必须先修 loader 和 label map。

## 2. 部署目标

当前部署链路为：

1. 板端检测红框或定位标志。
2. 根据定位标志做 ROI 裁剪。
3. ROI 输入模型。
4. 模型输出视觉子类或父类。
5. 板端最终只需要三大类结果：`supplies`、`vehicle`、`weapon`。

现有约束：

- 输入优先保持 `32x32` 灰度图。实际需求可能允许 `64x64`，但只有当 `32x32` 明显达不到目标时才扩大输入。
- 摄像头分辨率为 `320x240`，但模型只接收裁剪后的目标 ROI。
- 摄像头已经采用 BEV 变换，实际视角应大体一致。
- 当前寻线链路已把彩色转灰度。模型路线默认使用灰度，彩色只作为 accuracy 方向的离线 ablation，不作为第一部署方案。
- 板端为 TFLite Micro，模型必须使用板端算子可用且足够快的结构。
- 总有目标，不需要背景类。

## 3. 新标签体系

建议训练时使用 8 个视觉子类，部署时映射回 3 个父类。

建议内部 class map：

```python
VISUAL_CLASS_NAMES = [
    "first_aid_kit",
    "telescope",
    "ambulance",
    "armoured_car",
    "firearms_short",
    "firearms_long",
    "explosive_grenade",
    "explosive_c4",
]

PARENT_NAMES = ["supplies", "vehicle", "weapon"]

VISUAL_TO_PARENT = [
    0,  # first_aid_kit -> supplies
    0,  # telescope -> supplies
    1,  # ambulance -> vehicle
    1,  # armoured_car -> vehicle
    2,  # firearms_short -> weapon
    2,  # firearms_long -> weapon
    2,  # explosive_grenade -> weapon
    2,  # explosive_c4 -> weapon
]
```

注意事项：

- 磁盘目录名当前是 `explosive_C4`，内部 label 可以标准化为 `explosive_c4`，但 `class_map.json` 必须记录目录名到内部 label 的映射。
- loader 应优先使用子目录名推断 weapon 的细分类，不要依赖旧文件名。例如 `weapon/explosive_C4/explosive_070.jpg` 应识别为 `explosive_c4`，不能识别为旧 `explosive`。
- supplies 和 vehicle 当前仍可从文件名前缀得到视觉子类，但更稳妥的长期方案是所有 parent 都显式子目录化。
- 训练输出可以是 8 维视觉子类，再由板端或导出前逻辑映射到 3 父类。
- 如果部署要求更简单，可以训练双头模型：训练期保留 8 类 visual head 辅助学习，部署导出只保留 3 类 parent head。

## 4. 旧搜索结果如何使用

旧结果来自 6 个视觉子类，不可直接和新 8 子类结果横向比较。它们的价值是告诉我们哪些结构、增强、学习率区间、正则强度和量化策略更可能有效。

上一轮强先验：

| 锚点 | 旧配置 | 旧结论 | 新训练用法 |
| --- | --- | --- | --- |
| `sd097` | `spacetodepth_conv`, filters `[8,16,32]`, lr `0.00318`, l2 `1e-4`, dropout `0.003`, batch `16`, max pool, ReLU, `sdiag_lowres` | full-data 复评中 clean 和 stress 综合最好，旧部署推荐主模型 | balance 方向第一锚点，必须复测新 8 子类 |
| `sd175` | `[8,16,32]`, lr `0.00286`, l2 `1e-4`, dropout `0.005`, `sdiag_soft` | stress mean 方向强 | balance/accuracy 辅助锚点 |
| `sd218` | `[8,16,32]`, lr `0.00286`, l2 `1e-4`, dropout `0.003`, `sdiag_speed` | 高速和 blur/noise 场景有价值 | balance stress 锚点 |
| `stab007` | `[8,16,32]`, lr `0.00318`, l2 `1e-4`, dropout `0`, base 增强 | 稳定、简洁 | balance 基础对照 |
| `stab024` / `sd104` | `[6,12,24]`, `spacetodepth_conv`, 无 dense, 无 extra conv | 估算约 `5296 us`，体积约 `10 KB` 级别，fast 方向有价值 | fast 第一锚点 |
| extra conv 类 | 旧结果中体积和耗时明显增加，综合收益不稳定 | 默认不作为主线 | accuracy 方向少量验证 |

旧 full-data stress 复评参考：

- `sd097_seed20261502`：clean `0.9671 / 0.9519`，stress min `0.8846`，stress mean `0.9176`。
- `sd175_seed20261501`：clean `0.9704 / 0.9519`，stress min `0.8558`，stress mean `0.9231`。
- `sd218_seed20261501`：clean `0.9342 / 0.9062`，stress min `0.8846`，stress mean `0.9059`。
- `prev_stab007_full`：clean `0.9704 / 0.9615`，stress min `0.7812`，stress mean `0.8836`。

这些数字只能作为旧任务的基准线。新任务中如果父类准确率略有波动，但共同混淆图明显减少，也可能是更好的模型。

## 5. 必须先做的代码修正

开始任何训练前，先完成以下检查：

1. 新建或派生 `train_tiny32_v5_visual_subclass_scan.py`，不要直接破坏旧 V4 脚本。
2. 把 `SUBCLASS_NAMES` 改为 8 个视觉子类。
3. 把 `SUBCLASS_TO_PARENT` 改为 `[0,0,1,1,2,2,2,2]`。
4. 修改 `load_dataset()`，让它能读取人工标注子目录。
5. 输出并保存 `class_map.json`、`parent_map.json`、`dataset_manifest.csv`。
6. smoke run 必须验证：
   - 8 类标签正确加载。
   - `explosive_C4` 被映射到 `explosive_c4`。
   - 模型输出维度为 8 或训练双头时 parent 输出维度为 3。
   - TFLite 导出后输出维度与板端映射一致。
   - rotation/mirror transform 确实加入训练。
   - Keras float、float TFLite、int8 TFLite 可以对同一批样本输出并评估。

推荐 loader 规则：

- 如果图片路径形如 `dataset/<parent>/<visual_subclass>/<file>`，用 `<visual_subclass>` 作为视觉子类。
- 如果图片路径形如 `dataset/<parent>/<file>`，用文件名前缀作为视觉子类。
- 对所有样本强校验 parent 映射，不允许 silently fallback。

## 6. 评估体系

不能只看 clean accuracy。模型选择至少包含以下结果：

| 评估项 | 用途 |
| --- | --- |
| clean parent accuracy / macro recall / worst recall | 真实部署主指标 |
| clean visual subclass accuracy / macro recall / worst recall | 检查新视觉子类是否学到，但不是唯一指标 |
| hard clean set | 检查历史共同混淆图是否修复 |
| stress parent metrics | 检查高速、低分辨率、噪声、模糊场景 |
| rotation/mirror stress | 确认训练加入旋转镜像后泛化正常 |
| quantization audit | 防止 Keras 好、int8 掉线 |
| model bytes / estimated board us / actual board us | 防止 accuracy 方向牺牲过大 |
| seed mean / seed min | 防止单 seed 偶然性 |

固定 stress 建议沿用 V4：

```text
rot90
rot180
rot270
mirror_lr
mirror_lr_rot90
mirror_lr_rot180
mirror_lr_rot270
noise_0p06
hblur5_noise_0p06
diagblur5_noise_0p08
noise_0p10
vblur5
diagblur5
```

hard clean set 初始应包含历史多模型共同混淆图片，至少包含：

```text
dataset/supplies/first_aid_kit_050.jpg
dataset/weapon/explosive_030.jpg
dataset/weapon/explosive_070.jpg
dataset/weapon/explosive_105.jpg
dataset/weapon/explosive_124.jpg
dataset/supplies/first_aid_kit_058.jpg
dataset/weapon/explosive_092.jpg
dataset/supplies/telescope_141.jpg
dataset/weapon/firearms_002.jpg
dataset/vehicle/ambulance_020.jpg
dataset/supplies/telescope_149.jpg
dataset/weapon/firearms_175.jpg
```

如果人工标注移动了文件路径，应通过文件名或 manifest 追踪，不要因为路径改变丢失 hard set。

## 7. 数据切分策略

普通随机切分不够。推荐：

1. parent 层面保持 supplies / vehicle / weapon 分布平衡。
2. visual subclass 层面尽量保持分布，但 `explosive_c4` 只有 6 张，不要用单次 split 判断。
3. 筛选阶段使用 3 到 5 个 seed 的 repeated split。
4. C4 稀缺类使用 scarce-subclass aware split：
   - 每个评估 seed 尽量保证 val/test 至少有 1 张 C4。
   - 记录每个 seed 的 C4 样本位置。
   - 单独报告 C4 结果，不用单 seed C4 recall 淘汰模型。
5. 最终部署模型可以在选定参数后使用全数据重训，但必须保留之前 repeated split 和 hard set 结果作为证据。

## 8. 训练共同约束

所有方向都建议遵守：

- 输入：`32x32x1`，灰度，float 训练，int8 导出。
- 训练增强：每张图必须加入旋转和镜像，至少覆盖 4 个旋转和左右镜像组合。
- 主结构优先：`spacetodepth_conv`。
- 激活优先：ReLU，ReLU6 作为量化和 clipping ablation。
- head 优先：GAP 后直接分类，无 dense；dense 只在 accuracy 方向验证。
- 默认不开 extra conv；只在 accuracy 方向少量验证。
- 默认 batch：`16` 或 `24`。大 batch 可以提速但可能降低小数据泛化。
- 默认 early stopping + ReduceLROnPlateau。
- 评估必须使用 int8 TFLite 结果参与排序。
- 每个入围模型至少做多 seed retest。

## 9. 方向一：fast

目标：尽可能快，同时不明显破坏父类识别和 hard set。这个方向适合板端实时预算紧张时使用。

目标区间：

- 估算耗时优先低于 `6000 us`。
- 模型体积优先约 `10 KB` 级别。
- parent clean 不应明显低于 balance。
- hard clean 和 stress 不能崩。

优先搜索空间：

| 参数 | 候选 |
| --- | --- |
| architecture | `spacetodepth_conv` 优先；`depthwise_pool` 作为对照 |
| filters | `[6,12,24]`, `[5,10,20]`, `[7,14,28]`, `[8,12,24]`, `[8,16,24]` |
| dense_units | `0` |
| extra_conv | `False` |
| first_kernel | `3` |
| pool | `max` 优先，`avg` 少量验证 |
| activation | `relu` 优先，`relu6` 少量验证 |
| lr | `0.00278`, `0.00286`, `0.00294`, `0.00302`, `0.00318`, `0.00326` |
| l2 | `7e-5`, `1e-4`, `1.25e-4`, `1.5e-4`, `2e-4` |
| dropout | `0`, `0.003`, `0.005` |
| batch | `16`, `24`, `32` |
| augment | `sdiag_base`, `sdiag_soft`, `sdiag_lowres`, `sdiag_speed`, `v4_speed_mild` |

fast 方向还应测试：

- 8 视觉子类输出后在板端映射父类。
- 3 parent head 直接输出，作为更快 head 的对照。
- 去 Softmax，导出 logits，板端 `argmax` 或 parent 聚合。
- `AveragePool2D` / `Mean` 替代 GAP 路径，前提是板端算子耗时证明确实更快。
- QAT fine-tune 后是否能弥补小模型量化损失。
- 使用 accuracy teacher 蒸馏到 fast student。

fast 方向淘汰条件：

- 任一父类 worst recall 在多 seed 中显著低于 balance。
- hard clean set 中旧核心混淆图片仍大量错。
- int8 TFLite 比 Keras float 明显掉线且 QAT 无法修复。
- 实测板端耗时没有比 `[8,16,32]` balance 明显更快。

## 10. 方向二：balance

目标：主部署候选。用旧最佳附近的强先验重新训练 8 视觉子类，兼顾速度、stress、hard set 和稳定性。

第一锚点：

```text
architecture = spacetodepth_conv
filters = [8,16,32]
dense_units = 0
extra_conv = False
first_kernel = 3
pool = max
activation = relu
batch_size = 16
learning_rate = 0.00318
l2 = 1e-4
dropout = 0.003
augment = sdiag_lowres
train_transforms = rot_mirror
```

必须覆盖的邻域：

| 参数 | 候选 |
| --- | --- |
| filters | `[8,16,32]`, `[7,14,28]`, `[8,16,24]`, `[8,18,36]`, `[10,18,36]` |
| lr | `0.00278`, `0.00286`, `0.00294`, `0.00302`, `0.00310`, `0.00318`, `0.00326`, `0.00334` |
| l2 | `7e-5`, `1e-4`, `1.25e-4`, `1.5e-4`, `2e-4`, `3e-4` |
| dropout | `0`, `0.003`, `0.005`, `0.008`, `0.01` |
| batch | `16`, `24` |
| pool | `max` 主扫，`avg` 少量 |
| activation | `relu` 主扫，`relu6` 少量 |
| augment | `sdiag_soft`, `sdiag_mid`, `sdiag_lowres`, `sdiag_speed`, `sdiag_roi`, `v4_lowres_mix` |

必须包含的旧锚点变体：

- `sd097` 变体：`[8,16,32]`, lr `0.00318`, l2 `1e-4`, dropout `0.003`, `sdiag_lowres`。
- `sd175` 变体：`[8,16,32]`, lr `0.00286`, l2 `1e-4`, dropout `0.005`, `sdiag_soft`。
- `sd218` 变体：`[8,16,32]`, lr `0.00286`, l2 `1e-4`, dropout `0.003`, `sdiag_speed`。
- `stab007` 变体：`[8,16,32]`, lr `0.00318`, l2 `1e-4`, dropout `0`, base 低分辨率增强。

balance 方向评分建议：

| 指标 | 权重 |
| --- | ---: |
| int8 clean parent accuracy | 0.18 |
| int8 clean parent worst recall | 0.18 |
| hard clean parent accuracy | 0.18 |
| hard clean parent worst recall | 0.12 |
| stress parent macro recall | 0.12 |
| stress parent worst recall | 0.10 |
| visual subclass macro recall | 0.04 |
| Keras/int8 agreement | 0.04 |
| speed/size score | 0.04 |

balance 方向验收建议：

- 实测或估算耗时优先不超过旧 `[8,16,32]` `spacetodepth_conv` 的 `7163 us` 级别。
- hard clean set 相比旧 6 子类模型应明显改善，特别是 explosive/firearms 的历史混淆图。
- parent worst recall 和 stress worst recall 不能只在单 seed 好，要看 mean 和 min。

## 11. 方向三：accuracy

目标：找出上限模型，作为部署候选、teacher 或分析工具。这个方向允许更慢，但仍需受板端算子和体积约束。

搜索空间：

| 参数 | 候选 |
| --- | --- |
| architecture | `spacetodepth_conv`, `stride_conv`, `conv_pool`, `depthwise_pool` |
| filters | `[8,18,36]`, `[10,18,36]`, `[10,20,40]`, `[10,20,48]`, `[12,24,48]`, `[16,24,48]` |
| dense_units | `0`, `8`, `16`, `24`, `32` |
| extra_conv | `False`, `True` |
| first_kernel | `3`, `5` 少量 |
| pool | `max`, `avg` |
| activation | `relu`, `relu6`, `hard_swish` 仅当板端耗时可接受 |
| lr | `0.0012`, `0.0016`, `0.0020`, `0.0023`, `0.0026`, `0.00286`, `0.00318` |
| l2 | `1e-6`, `5e-6`, `1e-5`, `3e-5`, `7e-5`, `1e-4`, `3e-4`, `6e-4` |
| dropout | `0`, `0.003`, `0.008`, `0.02`, `0.05`, `0.08`, `0.12`, `0.20` |
| augment | `sdiag_soft`, `sdiag_mid`, `sdiag_hard`, `sdiag_lowres`, `sdiag_speed`, `v4_highspeed` |

accuracy 方向可做的激进策略：

- 双头训练：visual 8 类 head + parent 3 类 head，部署 parent head。
- visual logits 聚合到 parent，和 parent head 直接输出做对照。
- 训练 64x64 teacher，再蒸馏给 32x32 student。64x64 不作为第一部署模型，只用于判断信息上限。
- 训练彩色 teacher，再蒸馏给灰度 student。彩色直接部署成本高，默认不优先。
- label smoothing，建议 `0.02`、`0.05`、`0.08` 小范围测试。
- class-balanced loss 或 focal loss，重点观察 `explosive_c4`、`firearms_short`、`firearms_long`，但不能牺牲 parent 识别。
- SAM、SWA、EMA 作为高成本 ablation。
- 更长 epoch + 更低 lr fine-tune，用于少数入围模型。
- QAT from scratch 和 QAT fine-tune，用于修复 int8 损失。

accuracy 方向淘汰条件：

- 只提升 visual subclass accuracy，但 parent hard set 没改善。
- 体积和耗时增加明显，却没有超过 balance 的 hard/stress 综合分。
- 量化后性能回落到 balance 以下。
- 依赖板端未验证或慢算子。

## 12. 数据增强策略

必须使用：

- `rot_mirror`：每张图加入旋转和镜像。
- ROI jitter：模拟红框裁剪偏差，建议 pad `1` 到 `3`。
- brightness/contrast：模拟曝光变化。
- Gaussian noise：模拟传感器噪声。
- salt-pepper：模拟干扰点。
- motion blur：水平、垂直、对角方向。
- lowres/downscale：模拟高速和远距离下的有效像素减少。

建议作为 ablation：

- gamma/exposure 抖动。
- JPEG 压缩或轻微块状噪声。
- 轻微仿射平移和缩放，不要改变目标语义。
- class-specific augmentation：对 `explosive_c4`、`firearms_short`、`firearms_long` 增强稍强。
- teacher-generated soft labels。
- mixup：小数据可能有用，但对视觉目标边界清晰的 tiny32 分类也可能伤害，放在 accuracy 方向少量测。
- cutout/random erasing：只在 accuracy 方向少量测，可能遮挡关键目标。

不建议默认使用：

- 无约束 hard example oversampling。历史混淆图应优先作为评估集和定向增强依据，而不是直接复制进训练导致分布扭曲。
- 过强 blur/noise 作为唯一增强。旧结果说明增强过强可能同时压低 clean 和 subclass 学习。
- 对 `explosive_c4` 简单重复到和其他类一样多。可以用 class-balanced batch 或 loss，但必须和无权重版本对照。

## 13. 量化策略

所有入围模型都必须做 quantization audit：

1. Keras float。
2. float TFLite。
3. int8 TFLite。
4. Keras vs int8 agreement。
5. clean / hard / stress 三套结果。

已知可测策略：

- PTQ calibration sweep：
  - clean calibration。
  - mild stress calibration。
  - hard stress calibration。
  - balanced per-class calibration。
  - hard image calibration。
- QAT fine-tune：
  - 从 best Keras model 开始。
  - 小 lr，例如 `1e-5`、`3e-5`、`1e-4`。
  - epoch `10`、`20`、`40`。
- QAT from scratch：
  - 成本高，只给 top config 测。
- 去 Softmax：
  - 训练时 Dense 输出 logits，loss 用 `from_logits=True`。
  - 导出时不含 Softmax，板端 argmax 或 parent head argmax。
  - 如果需要把 8 子类聚合为 3 父类，优先测试 parent head；直接 sum logits 未必校准良好。
- ReLU6 / clipping：
  - 可能改善 int8 动态范围。
  - 需要和 ReLU 对照，不要默认替换。
- teacher distillation：
  - accuracy teacher 教 balance/fast student。
  - loss 可组合 hard label CE + KL soft label。
- Cross-layer equalization + bias correction：
  - 只在 PTQ 损失仍明显时测试。
- AdaRound 简化版：
  - 实现成本高，作为最后一层量化修补策略。
- 16x8 quant：
  - 只有板端算子和内存验证通过后才作为候选。

## 14. 模型结构思路清单

高优先级：

- `spacetodepth_conv + Conv2D + Pool + GAP + Dense head`。
- `[8,16,32]` balance anchor。
- `[6,12,24]` fast anchor。
- 8 visual subclass head，部署 parent mapping。
- 双头训练，部署 parent head。
- 去 Softmax logits 输出，板端 argmax。

中优先级：

- `depthwise separable` 结构，如果板端实测更快。
- `stride_conv` 替代 pool。
- `AveragePool2D` / `Mean` 替代部分 pooling。
- ReLU6 clipping。
- 小 dense head，例如 `8`、`16`，只在提升 hard/stress 时保留。
- `[10,18,36]`、`[10,20,40]` accuracy model。
- accuracy teacher -> fast student 蒸馏。

低优先级：

- extra conv。
- first kernel `5`。
- hard_swish。
- 64x64 直接部署。
- 彩色模型直接部署。
- 复杂 reduce/broadcast/slice/gather/LSTM/batch matmul。
- ensemble。可以用于离线分析或 teacher，不适合板端单模型部署。

## 15. 搜索计划

### Phase 0：smoke

目标是证明新标签和导出链路正确，不追求好分数。

必须完成：

- 8 类加载。
- class counts 输出。
- 1 个 `[6,12,24]` fast smoke。
- 1 个 `[8,16,32]` balance smoke。
- Keras/float TFLite/int8 TFLite 输出维度正确。
- hard clean set 能跑。
- stress set 能跑。

### Phase 1：targeted coarse

不要随机乱扫。用旧最佳附近构造有方向的候选。

建议并行跑三条 lane：

| lane | 候选量 | seed | 目的 |
| --- | ---: | ---: | --- |
| fast | 80 到 140 | 2 | 找到最快可用下界 |
| balance | 120 到 220 | 2 | 找主部署候选 |
| accuracy | 80 到 160 | 2 | 找 teacher 和上限模型 |

筛选规则：

- 每条 lane 保留 top 12。
- 全局再按综合分保留 top 24。
- 强制保留 fast anchor、balance anchor、accuracy 最宽模型各至少 2 个，即使初筛分数一般。

### Phase 2：multi-seed retest

对 Phase 1 入围模型做 5 seed retest。

排序时看：

- mean score。
- min score。
- hard clean mean。
- hard clean min。
- stress min。
- int8 agreement。
- 耗时和体积。

只靠单 seed 第一名不能进入最终部署。

### Phase 3：local fine scan

围绕 top 6 做细扫。

每个 top config 只改少量邻近参数：

- lr：乘以 `0.92`, `0.96`, `1.00`, `1.04`, `1.08`。
- l2：`0.7x`, `1.0x`, `1.25x`, `1.5x`, `2.0x`。
- dropout：当前位置附近 `0`, `0.003`, `0.005`, `0.008`, `0.01`。
- augment：soft / lowres / speed / mid 四个邻近版本。
- batch：`16` 和 `24`。
- activation：只对 top balance 测 ReLU6。
- pool：只对 top balance 测 avg。

每个邻域至少 3 seed，最终只保留在 hard/stress 和 seed min 上同时成立的模型。

### Phase 4：final retrain

对最终 top 3 到 5 做：

- best split model 保存。
- full-data retrain。
- int8 export。
- quantization audit。
- hard clean report。
- stress report。
- 如果可用，板端实际 benchmark。

最终部署模型不一定是 clean 第一名，应优先选择综合稳定的模型。

## 16. 推荐初始候选

第一轮必须包含：

| name | lane | architecture | filters | lr | l2 | dropout | augment |
| --- | --- | --- | --- | ---: | ---: | ---: | --- |
| `fast_stab024_v5` | fast | `spacetodepth_conv` | `[6,12,24]` | `0.00286` | `1e-4` | `0` | `sdiag_base` |
| `fast_sd104_v5` | fast | `spacetodepth_conv` | `[6,12,24]` | `0.00318` | `1e-4` | `0` | `sdiag_base` |
| `fast_lowres_v5` | fast | `spacetodepth_conv` | `[6,12,24]` | `0.00286` | `1e-4` | `0.003` | `sdiag_lowres` |
| `balance_sd097_v5` | balance | `spacetodepth_conv` | `[8,16,32]` | `0.00318` | `1e-4` | `0.003` | `sdiag_lowres` |
| `balance_sd175_v5` | balance | `spacetodepth_conv` | `[8,16,32]` | `0.00286` | `1e-4` | `0.005` | `sdiag_soft` |
| `balance_sd218_v5` | balance | `spacetodepth_conv` | `[8,16,32]` | `0.00286` | `1e-4` | `0.003` | `sdiag_speed` |
| `balance_stab007_v5` | balance | `spacetodepth_conv` | `[8,16,32]` | `0.00318` | `1e-4` | `0` | `sdiag_base` |
| `accuracy_wide_10_18_36_v5` | accuracy | `spacetodepth_conv` | `[10,18,36]` | `0.00286` | `1e-4` | `0.003` | `sdiag_soft` |
| `accuracy_wide_10_20_40_v5` | accuracy | `spacetodepth_conv` | `[10,20,40]` | `0.0023` | `1e-4` | `0.02` | `sdiag_mid` |
| `accuracy_extra_v5` | accuracy | `spacetodepth_conv` | `[10,18,36]` | `0.0023` | `1e-4` | `0.02` | `sdiag_lowres` |

## 17. 评分公式建议

先把所有指标归一化到 `[0,1]`。建议每条 lane 使用不同权重。

fast：

```text
score =
  0.16 * clean_parent_accuracy
+ 0.16 * clean_parent_worst
+ 0.16 * hard_parent_accuracy
+ 0.12 * hard_parent_worst
+ 0.10 * stress_macro
+ 0.10 * stress_worst
+ 0.14 * speed_score
+ 0.06 * size_score
+ 0.10 * seed_min_score
```

balance：

```text
score =
  0.18 * clean_parent_accuracy
+ 0.18 * clean_parent_worst
+ 0.18 * hard_parent_accuracy
+ 0.12 * hard_parent_worst
+ 0.12 * stress_macro
+ 0.10 * stress_worst
+ 0.04 * visual_macro
+ 0.04 * quant_agreement
+ 0.04 * speed_size_score
```

accuracy：

```text
score =
  0.16 * clean_parent_accuracy
+ 0.16 * clean_parent_worst
+ 0.20 * hard_parent_accuracy
+ 0.14 * hard_parent_worst
+ 0.14 * stress_macro
+ 0.10 * stress_worst
+ 0.06 * visual_macro
+ 0.04 * quant_agreement
```

所有 lane 都要额外记录：

- `score_mean`
- `score_min`
- `hard_mean`
- `hard_min`
- `stress_min_mean`
- `stress_min_min`
- `board_us`
- `tflite_bytes`

排序时不能只看 `score_mean`，至少要用 `score_min` 过滤不稳定模型。

## 18. 验收标准

建议按三档验收。

fast 可接受：

- 明显快于 balance，目标 `<= 6000 us`。
- parent clean 和 hard set 没有灾难性下降。
- stress min 不明显低于 balance。
- int8 与 Keras 不严重背离。

balance 可接受：

- 板端或估算耗时接近 `7163 us` 级别。
- hard clean set 明显优于旧 6 子类模型。
- repeated seed 的 min 不崩。
- int8 TFLite 是最终排序对象，不是 Keras float。
- 没有依赖未验证算子。

accuracy 可接受：

- hard/stress 或 parent worst recall 明显超过 balance。
- 可以作为 teacher，哪怕不直接部署。
- 如果要部署，必须给出板端耗时和体积理由。

最终部署候选建议：

- 主模型：balance 第一名。
- 备用高速模型：fast 第一名。
- teacher 或诊断模型：accuracy 第一名。

## 19. 需要重点观察的错误

上一轮共同混淆说明旧 6 子类标签存在结构性问题：

- `explosive` 内部混合了 grenade 和 C4。
- `firearms` 内部混合了短枪和长枪。
- 一些 first aid kit 和 explosive 互相混淆。
- 一些 telescope 和 firearms 互相混淆。
- 一些 ambulance / armoured car / explosive 在模糊或低分辨率下互相混淆。

新 8 类训练后要检查：

- C4 是否仍被大量判成 supplies 或 vehicle。
- grenade 是否仍被判成 first_aid_kit 或 ambulance。
- firearms_short 和 firearms_long 是否都能映射回 weapon，即使子类互相错也不应影响父类。
- first_aid_kit_050、explosive_070、explosive_030 等核心 hard 图是否改善。
- 如果子类互错但父类正确，部署层面可以接受；如果父类错，必须优先处理。

## 20. 数据增补建议

如果训练仍卡住，最有效的数据补强方向不是继续扩大随机搜索，而是补数据：

- 增加 `explosive_C4`，当前只有 6 张，优先采集到至少 20 到 30 张。
- 给 firearms_short / firearms_long 各补高速、低分辨率、噪声样本。
- 对 hard clean set 中的同类实物补拍不同角度、亮度、距离。
- 采集真实板端 ROI，而不是只用离线裁剪图。
- 保留每张图的 ROI 裁剪参数，便于复现板端输入分布。
- 对新补数据不要只补某一类，避免父类分布严重偏移。

## 21. 运行和产物组织

建议输出目录：

```text
model_training/experiments/v5_visual_subclass_<timestamp>/
  run_config.json
  class_map.json
  parent_map.json
  dataset_manifest.csv
  stage1_fast/
  stage1_balance/
  stage1_accuracy/
  stage2_retest/
  stage3_fine/
  final/
  hard_clean_reports/
  quant_audit/
  board_benchmark/
  summary.json
  top_models.csv
```

每个模型保存：

- Keras best。
- Keras full。
- float TFLite。
- int8 TFLite。
- calibration manifest。
- clean eval。
- hard eval。
- stress eval。
- quant audit。
- config JSON。
- seed。
- git diff 或脚本版本 hash。

不要覆盖旧 V3/V4 结果，不要把 generated/result 文件写入生产模型目录。

## 22. 并行策略

如果使用 GPU 和 tmux：

- 三条 lane 可以并行：fast、balance、accuracy。
- 单进程内 TensorFlow 线程不要开太大，避免多个训练进程互相抢 CPU。
- 每个 worker 写独立输出目录。
- 每个 worker 定期写 `progress.json`。
- 复跑时跳过已完成 trial。
- 先跑 smoke，再大并行。
- 遇到卡死或 2 小时无新 trial，应先检查 GPU/CPU 内存、进程栈、日志和是否在 TFLite conversion。

建议并行优先级：

1. balance 主扫。
2. fast 主扫。
3. accuracy 上限扫。
4. top-k retest。
5. top-k fine scan。
6. final full retrain。

## 23. 禁止事项

- 不要直接用旧 6 类脚本训练并声称用了新标注。
- 不要只看 clean accuracy。
- 不要用单 seed 选择最终模型。
- 不要因为 `explosive_c4` recall 波动就淘汰所有模型。
- 不要为了追求 subclass accuracy 牺牲 parent accuracy。
- 不要默认把模型做大。旧结果中 extra conv 和宽模型没有稳定证明值得增加板端成本。
- 不要让 hard clean set 混入训练后还用它当独立验证。
- 不要在没有板端算子验证的情况下使用复杂算子。
- 不要把测试脚本、临时模型、结果日志写进生产 runtime 路径。

## 24. 推荐下一步

下一步不要立刻开大搜索。先按这个顺序执行：

1. 派生 V5 训练脚本，修正 8 视觉子类 loader。
2. 生成 `dataset_manifest.csv` 并人工快速检查 8 类计数。
3. 跑两个 smoke：`[6,12,24]` fast 和 `[8,16,32]` balance。
4. 确认 Keras/float TFLite/int8 TFLite 输出维度和 parent 映射正确。
5. 跑 Phase 1 三 lane targeted coarse。
6. 对 top-k 做 Phase 2 多 seed retest。
7. 对 top-k 做 Phase 3 细扫。
8. 对 top 3 到 5 做 final full retrain、quant audit 和板端 benchmark。

如果只能先选一条主线，优先从 balance 的 `sd097` 邻域开始，因为它是上一轮综合最强、结构最简单、板端成本可控的锚点。fast 和 accuracy 同时保留，但不要让它们阻塞 balance 主模型产出。

## 25. 板端可用算子与实测性能补充

本节追加自板端 TFLite Micro 算子测试结果，来源为：

- 全集覆盖结果：`new/verification/tflm_op_benchmark/results/full_ops_20260506_185857/summary.json`
- 全集微基准：`new/verification/tflm_op_benchmark/results/full_ops_20260506_185857/board_ops_benchmark.csv`
- 重点结构复测：`new/verification/tflm_op_benchmark/results/retest_valuable_20260506_190205/board_model_benchmark.csv`
- 训练脚本中的板端耗时引用：`model_training/train_tiny32_sixclass_scan.py` 的 `BOARD_LATENCY_US`

解释规则：

- `availability` 只表示 resolver 能注册该算子，不等于该算子在目标模型中性能好。
- `microbench` 是小型合成模型实测，适合判断单算子可运行和大致代价，但不能直接相加成完整模型耗时。
- `modelbench` 更接近 tiny32 网络结构，应优先用于选择 V5 模型结构。
- `UNIDIRECTIONAL_SEQUENCE_LSTM` 在全集 summary 中同时出现在 yellow 和 red，是因为 resolver 可注册但 benchmark `AllocateTensors()` 失败。实际训练中按 RED 处理。
- `PRELU` 注册失败且 benchmark 失败，按不可用处理。

全集测试统计：

| result dir | availability | microbench | modelbench | green | yellow | red |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `full_ops_20260506_185857` | 64 | 65 | 11 | 62 | 1 | 2 |
| `retest_valuable_20260506_190205` | 64 | 0 | 8 | 19 | 44 | 1 |

### 25.1 可用算子全集

以下算子在 `full_ops_20260506_185857` 中为 GREEN，即 resolver 可用且有成功 benchmark 证据，或属于核心候选算子：

```text
ADD
ADD_N
ARG_MAX
ARG_MIN
AVERAGE_POOL_2D
BATCH_MATMUL
BATCH_TO_SPACE_ND
BROADCAST_ARGS
BROADCAST_TO
CAST
CONCATENATION
CONV_2D
DEPTHWISE_CONV_2D
DEPTH_TO_SPACE
DEQUANTIZE
DIV
ELU
EXPAND_DIMS
FILL
FULLY_CONNECTED
GATHER
GATHER_ND
HARD_SWISH
L2_NORMALIZATION
L2_POOL_2D
LEAKY_RELU
LOGISTIC
LOG_SOFTMAX
MAXIMUM
MAX_POOL_2D
MEAN
MINIMUM
MIRROR_PAD
MUL
PACK
PAD
PADV2
QUANTIZE
REDUCE_ALL
REDUCE_MAX
REDUCE_MIN
RELU
RELU6
RESHAPE
SELECT_V2
SHAPE
SLICE
SOFTMAX
SPACE_TO_BATCH_ND
SPACE_TO_DEPTH
SPLIT
SPLIT_V
SQUEEZE
STRIDED_SLICE
SUB
SUM
SVDF
TANH
TRANSPOSE
TRANSPOSE_CONV
UNPACK
ZEROS_LIKE
```

不可用于 V5 主模型：

| op | 板端结果 | 处理 |
| --- | --- | --- |
| `PRELU` | `register_failed`，microbench `allocate_tensors_failed` | 禁用 |
| `UNIDIRECTIONAL_SEQUENCE_LSTM` | resolver 可注册，但 microbench `allocate_tensors_failed` | 禁用 |

### 25.2 V5 推荐算子白名单

V5 模型主线优先只使用下面这些算子：

```text
CONV_2D
DEPTHWISE_CONV_2D
MAX_POOL_2D
AVERAGE_POOL_2D
MEAN
FULLY_CONNECTED
RESHAPE
SOFTMAX
RELU
RELU6
SPACE_TO_DEPTH
ADD
MUL
CONCATENATION
QUANTIZE
DEQUANTIZE
```

推荐程度：

| 等级 | 算子 | 训练建议 |
| --- | --- | --- |
| 第一优先 | `CONV_2D`, `SPACE_TO_DEPTH`, `MAX_POOL_2D`, `MEAN`, `FULLY_CONNECTED`, `RESHAPE`, `RELU` | V5 fast/balance 主线 |
| 第一优先但可裁剪 | `SOFTMAX` | 训练保留；部署可测试 logits + 板端 argmax，若结果一致则移除 |
| 第一优先且隐式存在 | `QUANTIZE`, `DEQUANTIZE` | 量化链路需要，不作为结构搜索变量 |
| 第二优先 | `DEPTHWISE_CONV_2D`, `AVERAGE_POOL_2D`, `RELU6` | 作为 fast/量化 ablation |
| 第二优先 | `ADD`, `MUL`, `CONCATENATION` | 只在 residual、缩放或多分支结构明确收益时使用 |
| 第三优先 | `HARD_SWISH`, `MAXIMUM`, `MINIMUM`, `LEAKY_RELU`, `LOGISTIC`, `TANH` | 可用但默认不用；只有 accuracy 明显提升才保留 |
| 不推荐 | `BATCH_MATMUL`, `TRANSPOSE_CONV`, `SVDF`, `GATHER*`, `BROADCAST*`, 复杂 slice/reduce | 可跑不代表适合本任务；默认排除出搜索空间 |
| 禁用 | `PRELU`, `UNIDIRECTIONAL_SEQUENCE_LSTM` | 不进入训练和导出 |

### 25.3 单算子微基准

来自 `full_ops_20260506_185857/board_ops_benchmark.csv`。单位为 microseconds。

| case | ops | arena bytes | min | avg | p95 | 结论 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `micro_conv2d_c8_k3` | `CONV_2D` | 10144 | 4549 | 6794 | 12746 | Conv 是主要耗时来源 |
| `micro_conv2d_c16_k5` | `CONV_2D` | 18400 | 20145 | 20333 | 20364 | 5x5 或更宽通道成本很高 |
| `micro_depthwise_c1_k3` | `DEPTHWISE_CONV_2D` | 2944 | 473 | 478 | 486 | depthwise 单算子很快 |
| `micro_depthwise_pointwise_c16` | `DEPTHWISE_CONV_2D|CONV_2D` | 18688 | 3032 | 3049 | 3079 | depthwise + pointwise 有潜力 |
| `micro_maxpool` | `MAX_POOL_2D` | 2064 | 36 | 38 | 37 | 成本很低 |
| `micro_avgpool` | `AVERAGE_POOL_2D` | 2064 | 40 | 42 | 41 | 成本很低，略慢于 maxpool |
| `micro_mean` | `MEAN` | 1888 | 92 | 93 | 93 | GAP 可接受 |
| `micro_fully_connected` | `FULLY_CONNECTED` | 4064 | 362 | 366 | 375 | 小 head 成本可接受 |
| `micro_softmax` | `FULLY_CONNECTED|SOFTMAX` 等 | 3872 | 98 | 100 | 103 | Softmax 本身不是主要瓶颈，但可裁剪 |
| `micro_relu` | `RELU` | 2784 | 31 | 31 | 32 | 成本低 |
| `micro_relu6` | `RELU6` | 2768 | 7 | 7 | 8 | 成本很低，适合量化 ablation |
| `micro_hard_swish` | `HARD_SWISH` | 2784 | 72 | 72 | 73 | 可用，但不是免费 |
| `micro_add` | `ADD` | 2832 | 61 | 61 | 62 | 可用，残差结构需谨慎 |
| `micro_mul` | `MUL` | 2800 | 40 | 41 | 41 | 可用 |
| `micro_concat` | `CONCATENATION` | 3824 | 48 | 50 | 49 | 可用，多分支会增加整体内存和图复杂度 |
| `micro_space_to_depth` | `SPACE_TO_DEPTH` | 2784 | 62 | 67 | 63 | 成本低，适合 tiny32 主线 |
| `micro_reshape` | `RESHAPE` 等 | 3344 | 2 | 2 | 3 | 成本可以忽略 |

补充观察：

- `ARG_MAX` / `ARG_MIN`、`REDUCE_MAX` / `REDUCE_MIN` / `SUM`、`TRANSPOSE`、`SLICE`、`STRIDED_SLICE` 等小算子在合成 microbench 中可以运行且耗时很低，但这类算子不应主动引入主模型。
- `BATCH_MATMUL`、`TRANSPOSE_CONV` 等也有成功 microbench，但与当前 32x32 ROI 分类需求不匹配，默认排除。
- 微小算子的 `avg`、`p95` 有时受计时粒度和系统抖动影响，不应过度解读 1 到 3 us 的差异。

### 25.4 模型结构板端实测

优先采用 `retest_valuable_20260506_190205/board_model_benchmark.csv`。这些数值也对应当前训练脚本中的 `BOARD_LATENCY_US` 参考。单位为 microseconds。

| modelbench case | 主要结构 | arena bytes | min | avg | p95 | 训练建议 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `candidate_spacetodepth_conv_8_16_32` | `SPACE_TO_DEPTH + CONV_2D + MAX_POOL_2D + MEAN + FC + SOFTMAX` | 6112 | 6571 | 7163 | 10611 | V5 balance 主线，速度和内存综合最好 |
| `candidate_depthwise_8_16_32_gap` | `DEPTHWISE_CONV_2D + pointwise CONV_2D + MAX_POOL_2D + MEAN + FC + SOFTMAX` | 14160 | 6484 | 7386 | 14517 | fast/accuracy ablation，p95 和 arena 不如 SpaceToDepth 稳 |
| `candidate_hardswish_depthwise_8_16_32` | depthwise 结构 + `HARD_SWISH` | 20736 | 7315 | 7601 | 7509 | 仅当准确率明显提升时保留 |
| `candidate_stride_conv_8_16_32_gap` | stride conv 降采样，无 pool | 5600 | 11110 | 12019 | 15787 | arena 小但速度慢于 SpaceToDepth，非主线 |
| `candidate_conv_8_16_32_max_gap` | 普通 Conv + MaxPool | 13136 | 21744 | 22829 | 24196 | 板端太慢，除非 accuracy 大幅领先 |
| `candidate_conv_8_16_32_avg_gap` | 普通 Conv + AvgPool | 13136 | 21723 | 23358 | 37740 | 不优先；AvgPool 没有带来结构级收益 |
| `sixclass_full_int8` | 旧导出模型 | 24624 | 127228 | 136969 | 190852 | 不适合作为 V5 板端结构参考 |
| `sixclass_best_int8` | 旧导出模型 | 24624 | 127309 | 137012 | 190647 | 不适合作为 V5 板端结构参考 |

结构选择结论：

- V5 主线继续用 `spacetodepth_conv`。它平均 `7163 us`，arena `6112 bytes`，比普通 `conv_pool` 快约 3 倍，且内存更低。
- `depthwise_pool` 单算子很快，但完整结构 arena 更高，p95 波动更大。它仍值得在 fast lane 做对照，但不能假设一定优于 `spacetodepth_conv`。
- `stride_conv` arena 最低，但平均 `12019 us`，不适合作为最快路线，除非新 8 子类训练中准确率明显更稳。
- `hard_swish_depthwise` 可运行，平均 `7601 us`，arena `20736 bytes`。只有 accuracy 或 stress 明显提升时才应进入候选。
- 普通 `conv_pool` 平均 `22829 us`，默认排除出板端主线；accuracy lane 可以少量保留用于 teacher 或上限分析。
- 旧 `sixclass_*_int8` 在这套 benchmark 中约 `137 ms`，说明旧导出模型或结构不是 V5 可部署方向。

### 25.5 对 V5 搜索空间的直接影响

更新后的实际搜索约束：

- fast lane：
  - 主扫 `[6,12,24]` 和 `[8,16,32]` 的 `spacetodepth_conv`。
  - 少量测试 `depthwise_pool`，但必须记录 p95 和 arena。
  - 不用 `conv_pool`，除非作为精度 teacher。
- balance lane：
  - 主锚点仍是 `spacetodepth_conv [8,16,32]`。
  - `SOFTMAX` 可以保留做训练稳定基线，同时做 logits export ablation。
  - `RELU6` 可作为量化友好变体测试。
- accuracy lane：
  - 可以测试 `depthwise_pool`、`hardswish_depthwise`、少量普通 `conv_pool`。
  - 如果普通 `conv_pool` 精度没有显著超过 `spacetodepth_conv`，应立即淘汰。
  - 不使用 `PRELU`、`UNIDIRECTIONAL_SEQUENCE_LSTM`、`TRANSPOSE_CONV`、`BATCH_MATMUL` 等与任务不匹配或风险高的算子。

最终推荐的 V5 默认导出结构：

```text
Input 32x32x1
SPACE_TO_DEPTH block_size=2
CONV_2D + RELU
MAX_POOL_2D
CONV_2D + RELU
MAX_POOL_2D
CONV_2D + RELU
MEAN / GlobalAveragePooling
FULLY_CONNECTED
SOFTMAX 或 logits 输出
```
