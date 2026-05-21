# V6 父类主导、闭集 C4 记忆与稀有证据训练路线

> 2026-05-17 更新：V6 仍作为父类主导、C4 闭集记忆和 V5 平面子类路线修正的基础文档。后续针对“旧最佳模型与 CTD rescue 模型互补，但板端不能双 CNN 推理”的高速融合主线，转向 `V7_SINGLE_BACKBONE_MOE_FUSION_ROADMAP.md`。V7 在 V6 的 parent-first / C4 evidence 基础上，引入 shared-trunk MoE、multi-teacher delta distillation、negative distillation、Group DRO、gate focal、prototype/SupCon/ArcFace、augmentation consistency、QAT、SAM/SWA 等组合策略，目标是在 `<=8ms` 优先、`<=12ms` 保底的推理约束内冲击 int8 clean parent `304/304`。

本文面向后续继续训练或实现脚本的 AI/工程师。目标是把当前 V5 平面 8 视觉子类训练，升级为更符合部署目标的 V6 路线：父类识别为主，子类信息为辅助，`explosive_c4` 不再作为与其他类别平等竞争的最终类别，而是作为闭集目标进行逐实例记忆、扰动鲁棒和误报约束。

## 1. 背景与核心判断

当前板端最终需要的结果仍是 3 个父类：

- `supplies`
- `vehicle`
- `weapon`

V5 采用 8 个视觉子类：

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

VISUAL_TO_PARENT = [0, 0, 1, 1, 2, 2, 2, 2]
```

V5 的问题不是没有做旋转/镜像增强。当前 trainer 已经有 `expand_rotation_mirror_with_parent()`，训练时 `train_transforms == "rot_mirror"` 会把每张图扩展为 4 个旋转版本和对应镜像版本，共 8 个版本。

V5 的根本问题是最终父类来自 8-way subclass argmax 再折叠到父类。`explosive_c4` 只有 6 张，且视觉形态和旧 `explosive_grenade` 差异很大。让 C4 在 8 个子类里和其他类别平等竞争，会把一个少样本稀有形态变成全局决策边界的一部分；当 C4 子类没有赢时，父类可能被错误折叠。

V6 的核心判断：

- 最终部署判定必须以父类 head 为主。
- 子类可以错，父类不能因此被拖垮。
- `firearms_short` 和 `firearms_long` 是合理子类，应保留为 weapon 内部辅助结构。
- `explosive_c4` 应作为稀有形态证据或属性任务训练，而不是作为最终平等类别。
- V4 的父类总体稳定性应被保留，C4 是需要定向修正的盲点，不应因为修 C4 丢掉 V4 的整体边界。

关键修正：

- 用户已确认当前数据集就是全集，后续不会出现新的 C4 外观泛化需求。
- 因此 C4 不应再按开放少样本泛化来优化，而应按闭集目标优化：6 张原始图都必须在原图和部署扰动下被识别为 weapon/C4 evidence。
- C4 leave-one-out 仍有诊断价值，但不再是最终淘汰门槛。最终门槛应是闭集 6/6 原图召回、压力增强召回、非 C4 误报和父类总体质量。
- `explosive_030.jpg` 是裸电路形态孤例，和另外 5 张箱体/包块形态明显不同。它不应被迫和 5 张箱体图共享单一 C4 形态边界，应显式拆成 `c4_box_like` 与 `c4_circuit_like`，必要时再加逐实例或 prototype evidence。
- 真实摄像头困境不是开放外观泛化，而是高速通过时的运动模糊、轻微噪点、亮度/对比度漂移导致 32x32 细节消失。V6 的 C4 闭集记忆必须在这些摄像头扰动下成立。

摄像头扰动必须进入第一轮搜索，而不是等 final 再补：

```text
camera_stress =
  mild_motion_blur(length=2,3,5; angle=0,45,90,135)
  mild_gaussian_noise(sigma=2/255,4/255,6/255)
  mild_brightness_contrast_jitter
  optional_defocus_blur(kernel=3)
```

约束：

- blur/noise 强度应贴近真实车载摄像头，不做破坏语义的极端增强。
- 对 C4，尤其 `explosive_030.jpg`，必须单独报告 camera stress 下的逐实例召回。
- 对非 C4，必须报告 camera stress 下的 C4 evidence 误报；不能为了救 C4 把模糊 grenade/firearm 大量打成 C4。

## 2. 总体目标

V6 的目标不是单纯提高 8-class subclass accuracy，而是产出若干个父类部署候选：

- `balanced`: clean / hard / stress 综合最稳。
- `fast`: 耗时和体积更低，但父类指标不能崩。
- `c4_rescue`: C4 parent recall 明显改善，同时非 C4 误报可控。
- `stress_robust`: stress worst recall 最好。
- `small`: int8 bytes 最小，父类指标仍可接受。

最终扫描结果不应只给一个模型，而应在 top 模型中保留不同侧重点。

## 3. 非目标

以下不是 V6 的第一目标：

- 不追求 C4 子类本身的高置信 8-way 准确率。
- 不把 6 张 C4 的指标作为唯一淘汰条件。
- 不把 C4 当作开放世界 few-shot 泛化问题；当前目标是闭集全集记忆。
- 不把 C4 leave-one-out 当作最终 gate；它只用于判断模型是否真正学到形态规律。
- 不做无约束 hard example oversampling 后再把同一 hard set 当独立评估。
- 不只看 Keras float 结果，最终必须看 int8 TFLite 结果。
- 不默认扩大输入到 64x64，除非 32x32 路线被证明确实不足。
- 不把子类 head 的输出直接当板端最终控制结论。

## 4. 推荐模型语义

推荐主结构：

```text
32x32 grayscale input
  -> tiny32 backbone
  -> parent_head: 3 logits
  -> weapon_sub_head: 4 logits, only meaningful for weapon samples
  -> c4_attr_head: 1 binary logit
  -> c4_box_head: 1 binary logit, optional but recommended
  -> c4_circuit_head: 1 binary logit, optional but recommended
  -> c4_instance_head: 6 logits, optional closed-set memory
  -> c4_embedding: 8 or 16 dims, optional prototype evidence
  -> optional other_sub_head: supplies/vehicle 内部辅助子类
```

输出语义：

- `parent_head` 是主部署输出。
- `weapon_sub_head` 用于训练表示和调试分析，不直接决定最终父类。
- `c4_attr_head` 是 C4-like 总证据，可用于 evidence 输出或保守 rescue 规则，但第一阶段不直接覆盖 `parent_head`。
- `c4_box_head` 学 5 张箱体/包块 C4，避免裸电路孤例稀释主 C4 表示。
- `c4_circuit_head` 专门学 `explosive_030.jpg` 的裸电路孤例。
- `c4_instance_head` 或 `c4_embedding` 只服务闭集全集，不要求开放泛化。
- `other_sub_head` 可选，仅当它提升父类 hard/stress 时保留。

部署优先级：

```text
primary parent = argmax(parent_head)
aux evidence = weapon_sub_head + c4_attr_head + c4_box_head + c4_circuit_head + optional prototype_score
```

C4 evidence 推荐聚合：

```text
c4_evidence = max(c4_attr_score, c4_box_score, c4_circuit_score, prototype_score)
```

其中 `prototype_score` 如果启用，应只用 6 个 C4 原型和低维 embedding 计算，作为 evidence 输出或训练正则，不让它单独推翻父类主头。

不要再使用：

```text
primary parent = VISUAL_TO_PARENT[argmax(8-way subclass)]
```

## 5. 基础损失设计

第一版推荐损失：

```text
L =
  1.00 * CE(parent)
+ 0.10 * CE(weapon_sub | parent = weapon)
+ 0.30 * BCE(c4_like)
+ 0.20 * BCE(c4_box_like)
+ 0.20 * BCE(c4_circuit_like)
+ 0.05 * CE(c4_instance | C4 only, optional)
+ 0.05 * CE(other_sub | parent in supplies/vehicle)
+ 0.10 * consistency(rot/mirror parent logits)
```

可扫范围：

| 参数 | 候选值 |
| --- | --- |
| `weapon_sub_weight` | `0.05`, `0.10`, `0.20` |
| `c4_attr_weight` | `0.15`, `0.30`, `0.60` |
| `c4_box_weight` | `0.10`, `0.20`, `0.40` |
| `c4_circuit_weight` | `0.20`, `0.40`, `0.80` |
| `c4_instance_weight` | `0.00`, `0.03`, `0.08` |
| `c4_proto_weight` | `0.00`, `0.05`, `0.10` |
| `other_sub_weight` | `0.00`, `0.03`, `0.05` |
| `consistency_weight` | `0.00`, `0.05`, `0.10` |
| `parent_label_smoothing` | `0.00`, `0.02`, `0.05` |

约束：

- `parent_head` 权重始终最大。
- 长尾 reweight/focal/logit adjustment 只先作用于 `weapon_sub_head` 或 `c4_attr_head`。
- `c4_circuit_like` 可以比 `c4_box_like` 权重更高，因为它只有 1 张原图，是闭集孤例目标。
- 不建议直接对 `parent_head` 做激进 class weighting。

## 6. 路线 A: 父类主头 + 子类辅助

### A1. Parent + 8-way auxiliary

结构：

```text
parent_head: 3-way CE
subclass_head: 8-way CE, low weight
deploy: parent_head only
```

用途：

- 保留全部视觉子类信息。
- 让子类学习只作为 shared backbone 的正则。

风险：

- 8-way aux 仍可能让 C4 过度影响表示。
- 如果 aux 权重大，会回到 V5 的平面分类问题。

建议权重：

```text
subclass_aux_weight = 0.03, 0.05, 0.10
```

### A2. Parent + weapon-only auxiliary

结构：

```text
parent_head: 3-way CE, all samples
weapon_sub_head: firearm_short / firearm_long / explosive_grenade / c4_like
deploy: parent_head only
```

这是首推路线。C4 只在 weapon 内部参与局部结构学习，不和 supplies/vehicle 子类平等竞争。

建议权重：

```text
weapon_sub_weight = 0.05, 0.10, 0.20
```

保留条件：

- clean parent 不下降或小幅下降。
- hard parent accuracy 提升。
- 闭集 C4 原图 6/6 parent recall。
- C4 stress recall 提升，尤其 `explosive_030.jpg` 的裸电路形态。
- 非 C4 weapon 不因 C4-like 辅助头产生大量误报。

### A3. Parent + split weapon branch

结构：

```text
weapon_group_head: firearm / explosive
firearm_head: short / long
explosive_head: grenade / c4_like
```

用途：

- 把 weapon 内部层级显式化。
- 减少 C4 直接和 firearm short/long 竞争的压力。

风险：

- head 数量变多，训练和导出复杂度上升。
- 只有在 A2 不够稳时再做。

## 7. 路线 B: C4-like 属性头

C4 不作为平等类别，而作为二分类属性：

```text
c4_like = 1: explosive_c4
c4_like = 0: all non-C4
```

闭集全集下，应进一步拆分 C4 内部形态：

```text
c4_box_like = 1:
  explosive_019.jpg
  explosive_124.jpg
  explosive_141.jpg
  explosive_142.jpg
  explosive_154.jpg

c4_circuit_like = 1:
  explosive_030.jpg
```

这不是为了开放泛化出新的 C4 子类，而是为了避免 1 张裸电路孤例被 5 张箱体形态平均掉。最终 evidence 可以合并：

```text
c4_evidence = max(c4_like, c4_box_like, c4_circuit_like)
```

重点 hard negatives：

- `explosive_grenade`
- `firearms_short`
- `firearms_long`
- 容易和 C4 外形相似的 vehicle/supplies hard 图

### B1. 训练用属性头

`c4_attr_head` 只参与训练，不进入部署输出。

优点：

- 风险最低。
- 可以让 backbone 关注 C4 稀有形态。

### B2. Evidence 输出

部署时保留：

```text
parent_prob
c4_like_score
weapon_sub_prob
```

但最终主分类仍来自 `parent_head`。这符合“模型输出证据，不直接控制动作”的工程原则。

### B3. 保守 rescue 规则

只有当父类判定接近边界时才允许 `c4_attr_head` 参与：

```text
if parent_head.weapon is near threshold and c4_like_score is very high:
    emit c4_rescue_evidence
```

注意：

- 第一轮不建议直接改变最终 parent。
- 先评估 false positive，再决定是否启用。
- rescue 规则要在报告中单独统计，不混进纯模型指标。

### B4. 闭集 C4 instance/prototype evidence

在闭集全集约束下，可以激进使用主机端高成本训练换取板端可承受的轻量 evidence：

```text
embedding_dim = 8 or 16
c4_instance_head = 6-way CE, only for C4 samples
prototype_score = max similarity(embedding, six_c4_prototypes)
```

训练目标：

- 每张 C4 原图在 rot/mirror/stress 下都靠近自己的 prototype。
- 非 C4，尤其 grenade/firearm/hard negatives，远离所有 C4 prototypes。
- `explosive_030.jpg` 可以拥有独立 prototype，不要求它靠近箱体 C4。

部署成本：

- 6 个 prototype x 8 或 16 维，板端额外成本很小。
- 如果不想改板端输出，prototype 也可以只作为训练正则，导出时仍只保留 `parent_head` 与少量 evidence head。

## 8. 路线 C: V4 teacher + C4 rescue

V4 的价值是父类整体稳定，问题是 C4 盲区。V6 应该保留 V4 的稳定父类边界，同时纠正 C4。

训练信号：

```text
samples where V4 parent prediction == human parent:
  CE(parent_label)
  KL(V4_parent_soft_label, student_parent)

samples where V4 parent prediction != human parent:
  CE(parent_label)
  disable teacher KL

C4 samples:
  CE(parent_label = weapon)
  BCE(c4_like = 1)
  BCE(c4_box_like or c4_circuit_like = 1)

hard negatives:
  BCE(c4_like = 0)
  BCE(c4_box_like = 0)
  BCE(c4_circuit_like = 0)
```

推荐损失：

```text
L =
  1.00 * CE(parent_label)
+ alpha * KL(V4_parent_soft / T, student_parent / T)
+ beta  * BCE(c4_like)
+ gamma * CE(weapon_sub)
```

可扫范围：

| 参数 | 候选值 |
| --- | --- |
| `alpha` | `0.10`, `0.30`, `0.60` |
| `T` | `2`, `4` |
| `beta` | `0.15`, `0.30`, `0.60` |
| `gamma` | `0.05`, `0.10`, `0.20` |
| `teacher_error_policy` | `mask_wrong_parent` |
| `teacher_keep_scale` | `1.00` |

关键点：

- 不只是 C4 样本需要屏蔽 teacher 错误；所有 V4 父类错误样本都必须排除 teacher KL。
- 对 V4 父类正确样本，V4 soft label 是稳定边界的强先验。
- 当前实现应记录 `teacher_keep_rate`、`teacher_wrong_count`、`c4_wrong_count`，避免 teacher 分支暗中学习错误标签。
- top V4 模型可用多个 teacher ensemble，host 端成本可接受。

## 9. 路线 D: 对比学习与 C4 原型

C4 样本极少，普通 CE 不一定适合学出稳定边界。现在已确认 C4 是闭集全集，因此原型路线应从“少样本泛化辅助”升级为“闭集记忆主分支”：6 张 C4 都可以拥有自己的低维 prototype，尤其 `explosive_030.jpg` 不需要被拉向箱体 C4 的共同中心。

结构：

```text
backbone -> embedding_dim 8 or 16
embedding -> parent_head
embedding -> auxiliary heads
```

损失：

```text
L =
  CE(parent)
+ lambda_parent_supcon * SupCon(parent)
+ lambda_c4_proto * prototype_distance(c4)
+ lambda_c4_neg * hard_negative_margin(non_c4, c4_proto)
+ lambda_c4_instance * CE(c4_instance | C4 only)
+ lambda_camera_consistency * consistency(clear, blurred/noisy)
```

推荐候选：

| 参数 | 候选值 |
| --- | --- |
| `embedding_dim` | `8`, `16` |
| `lambda_parent_supcon` | `0.02`, `0.05`, `0.10` |
| `lambda_c4_proto` | `0.05`, `0.10`, `0.20` |
| `lambda_c4_neg` | `0.05`, `0.10` |

部署方式：

- 第一阶段只把 embedding/prototype 作为训练正则，部署仍用 `parent_head`。
- 如果效果明显，可保留一个 8/16 维 C4 prototype 距离作为 evidence。
- prototype evidence 的阈值必须在 camera stress false positive 上校准。

风险：

- batch 构造更复杂。
- batch 太小时 SupCon 不稳定。
- 不应作为第一批唯一主线，但应进入第一轮粗扫，因为闭集 C4 和低维 prototype 的假设非常匹配。

## 10. 路线 E: 解耦训练

长尾识别里，表示学习和分类器平衡通常可以分开做。V6 推荐增加解耦训练分支。

### E1. Parent representation pretrain

训练：

```text
backbone + parent_head
loss = CE(parent)
augmentation = rot_mirror + selected stress aug
```

目标：

- 先学稳父类边界。
- 不让 6 张 C4 过早扭曲 backbone。

### E2. Auxiliary head balancing

训练：

```text
freeze backbone or low LR backbone
train weapon_sub_head + c4_attr_head
```

可以使用：

- C4 balanced sampling。
- focal BCE。
- class-balanced loss。
- logit adjustment。
- hard negative mining。

### E3. Joint low-LR fine-tune

训练：

```text
unfreeze all or partial backbone
low LR
parent CE still dominant
aux heads lower weight
```

保留条件：

- parent clean/stress 不被辅助头破坏。
- int8 后仍稳定。

## 11. 路线 F: 长尾方法局部化

长尾方法适合 C4，但不应直接污染父类主头。

### F1. Focal BCE for C4 attr

用途：

- 降低大量 easy negative 的影响。
- 让模型关注难区分的 C4/hard-negative 样本。

候选：

```text
gamma = 1.0, 2.0
positive_weight = 2, 4, 8
```

### F2. Class-Balanced Loss for weapon_sub

用途：

- 处理 weapon 内部 `firearms_short` / `firearms_long` / `grenade` / `c4_like` 不均衡。

候选：

```text
beta = 0.9, 0.99, 0.999
```

### F3. Logit Adjustment or Balanced Softmax for weapon_sub

用途：

- 修正长尾类别先验导致的子类偏置。

约束：

- 只作用于 `weapon_sub_head`。
- 不作用于 `parent_head`，除非后续实验证明父类也存在严重长尾偏置。

## 12. 路线 G: QAT 与 int8 稳定化

最终部署看 int8 TFLite Micro，而不是 float Keras。因此 QAT 应进入 fine/final stage。

推荐阶段：

```text
coarse stage:
  post-training quantization, 快速筛掉明显失败配置

fine stage:
  top family 做 QAT fine-tune
  每个 family 至少 3 seed

final stage:
  int8 clean/hard/stress 全报告
  float-int8 agreement 进入排序
```

QAT 保留条件：

- int8 parent clean 不下降或提升。
- int8 stress worst 提升。
- float-int8 agreement 提升。
- 体积和耗时仍在板端可接受范围内。

## 13. 数据与 split 规则

### 基本规则

- 当前数据视为冻结，除非明确重新开放采集。
- 所有增强必须只在 train split 内生成。
- 原图的 rot/mirror/stress 版本不能跨 train/val/test 泄漏。
- hard clean set 可以作为诊断和定向评估，但不要无约束混入训练后继续当独立 hard 评估。
- 摄像头 stress 增强必须包含高速轻微运动模糊和轻微噪点，因为这是实际部署的主要退化来源。

### C4 闭集实例表

当前 C4 全集只有 6 张，应在训练和评估代码中显式编码实例 id 与形态组：

| file | group | 训练含义 |
| --- | --- | --- |
| `explosive_019.jpg` | `c4_box_like` | C4 箱体/包块形态 |
| `explosive_124.jpg` | `c4_box_like` | C4 箱体/包块形态 |
| `explosive_141.jpg` | `c4_box_like` | C4 箱体/包块形态 |
| `explosive_142.jpg` | `c4_box_like` | C4 箱体/包块形态 |
| `explosive_154.jpg` | `c4_box_like` | C4 箱体/包块形态 |
| `explosive_030.jpg` | `c4_circuit_like` | 裸电路孤例，必须独立照顾 |

这张表不是临时注释，而应进入 candidate generator 或 dataset metadata，保证每次无人值守扫描都按同一语义生成标签。

### 摄像头 stress 规则

训练增强建议：

```text
rot_mirror: 必选
camera_mild_motion_blur: 低概率混入
camera_mild_noise: 低概率混入
brightness_contrast_jitter: 低概率混入
```

评估增强建议固定成可复现网格：

```text
for each original image:
  rot/mirror variants
  motion blur length 2/3/5, angle 0/45/90/135
  gaussian noise sigma 2/255,4/255,6/255
  brightness/contrast mild endpoints
```

注意：

- 训练时不要把每种 stress 都全量展开到同一权重，否则会让少数 C4 在训练集中权重过大。
- 评估时必须全量展开并按原图聚合，尤其报告 C4 6 张的 worst-case。
- `explosive_030.jpg` 的细线/电路结构容易被 blur 抹掉，所以要单独统计 `c4_circuit_camera_recall`。

### C4 leave-one-out

C4 LOO 仍建议保留，但其地位从最终 gate 降级为诊断：

```text
for each original C4 image:
  hold out this original C4 and all derived augmentations
  train on remaining data
  evaluate held-out C4 parent prediction
```

报告：

- `c4_parent_recall_loo`
- `c4_attr_recall_loo`
- `c4_parent_confusion_loo`
- 每张 C4 的预测父类、parent probability、c4_like score

解释：

- 如果 LOO 好，说明模型学到了可迁移 C4 形态。
- 如果 LOO 差但闭集 camera stress 好，在当前全集部署目标下仍可能可用。
- 如果 LOO 好但闭集 camera stress 差，不能接受，因为真实问题是高速模糊和噪点。

### C4 false positive

必须报告：

- 非 C4 weapon 中 `c4_attr_head` 的误报率。
- 非 weapon 中 `c4_attr_head` 的误报率。
- hard clean 中 C4-like 高分但 parent 非 weapon 的样本列表。

## 14. 评估指标

主排序指标：

- `clean_parent_acc_min`
- `clean_parent_worst_min`
- `hard_parent_acc_min`
- `hard_parent_worst_min`
- `stress_parent_worst_min`
- `camera_stress_parent_worst_min`
- `closed_set_c4_parent_recall`
- `closed_set_c4_evidence_recall`
- `c4_camera_stress_recall`
- `c4_circuit_camera_recall`
- `c4_parent_recall_loo`, diagnostic only
- `c4_false_positive_rate`
- `c4_camera_false_positive_rate`
- `float_int8_agreement`
- `board_us` 或估计耗时
- `int8_bytes`

辅助指标：

- `weapon_sub_acc`
- `firearm_short_recall`
- `firearm_long_recall`
- `grenade_recall`
- `c4_attr_auc` 或 thresholded recall/precision
- `c4_box_recall`
- `c4_circuit_recall`
- `c4_instance_worst_recall`
- `c4_fp0_threshold`
- per-seed variance

建议综合分：

```text
score =
  0.28 * clean_parent_acc_min
+ 0.18 * stress_parent_worst_min
+ 0.14 * camera_stress_parent_worst_min
+ 0.14 * hard_parent_acc_min
+ 0.08 * hard_parent_worst_min
+ 0.08 * closed_set_c4_parent_recall
+ 0.05 * c4_camera_stress_recall
+ 0.03 * (1 - c4_camera_false_positive_rate)
+ 0.02 * float_int8_agreement
- latency_size_penalty
```

说明：

- C4 指标进入分数，但不能压过父类总体质量。
- 闭集 C4 原图和 camera stress 是主指标；LOO 只作为诊断列展示。
- 如果 `c4_camera_false_positive_rate` 明显升高，模型不能作为 `c4_rescue` 候选，即使 C4 recall 很高。
- 对 `c4_rescue` 侧重点，先用 FP=0 或接近 0 的阈值筛候选，再在候选内比较 C4 recall。
- 排序时必须看 seed min，不能只看 mean。
- `hard_parent_worst_min = 0` 的模型原则上不能作为 balanced 候选，只能作为某些侧重点候选保留。

## 15. 搜索阶段

继续采用“激进大范围粗扫，再细扫”的策略，但搜索空间转向 V6 head/loss/training stage。

### Stage 0: 实现与探针

任务：

- 新增 `train_tiny32_v6_parent_primary_scan.py`，或在 V5 trainer 基础上扩展。
- 新增 head 类型：`parent_aux`, `parent_weapon_aux`, `parent_c4_attr`, `parent_c4_box_circuit`, `parent_teacher_c4`, `parent_metric_proto`, `parent_c4_instance`。
- 新增 C4 LOO evaluator。
- 新增 false positive evaluator。
- 新增闭集 C4 evaluator：6 张原图逐实例、box/circuit 分组、camera stress worst-case。
- 新增 camera stress evaluator：高速轻微 motion blur、轻微 noise、亮度/对比度漂移。
- 新增 V4 teacher soft label 生成/读取路径。
- 单配置 smoke test，确认 Keras、TFLite int8、metrics JSON 都正常。

退出条件：

- 1 个小配置能完成训练、int8 export、clean/hard/stress/C4 LOO 报告。
- 1 个小配置能完成 closed-set C4 和 camera stress 报告。
- 无数据泄漏警告。
- `trial_results.jsonl` 可被 summarizer 解析。

### Stage 1: 粗扫

目标：

- 覆盖路线 A/B/C/E/F 的主要组合。
- 每配置 1 seed。
- 优先找方向，不在单点上消耗太久。

建议候选族：

```text
v6_A2_parent_weapon_aux
v6_B_closed_c4_box_circuit
v6_B_closed_c4_instance
v6_C_masked_v4_teacher_closed_c4
v6_E_decoupled_parent_aux
v6_F_weapon_longtail_local
v6_D_proto8_or_proto16
```

粗扫保留标准：

- clean parent 不明显低于 V5 当前 top。
- hard/stress 至少一个方向有明显改善。
- closed-set C4 原图 6/6 或接近 6/6。
- C4 camera stress recall 有改善迹象，尤其 `explosive_030.jpg`。
- C4 camera stress false positive 可控。
- int8 agreement 不崩。

当前泛化式 V6 粗扫如果刚开始，应直接推翻并重扫。理由是它没有把 `c4_box_like`、`c4_circuit_like`、逐实例/prototype 和 camera stress 放进 Stage 1 主搜索，继续跑会消耗资源但无法回答真实问题。

### Stage 2: 家族筛选

目标：

- 每个方向选 top 10 到 20。
- 每个配置 3 seed。
- 加 closed-set C4、camera stress、C4 LOO 和 false positive 完整报告。

保留标准：

- `score_min` 进入 top。
- 或者单项极强：fast / C4 rescue / stress / small。
- seed 间波动可解释。

### Stage 3: 细扫

围绕 top family 扫：

- filters: V4/V5 已验证的 tiny32 邻域。
- architecture: `spacetodepth_conv`, `depthwise_pool`, `stride_conv`, 少量 `hardswish_depthwise`。
- dropout: `0`, `0.003`, `0.01`, `0.02`。
- LR: top 周围上下 2 到 3 档。
- loss weights: parent/weapon/c4/teacher/consistency。
- calibration: balanced clean, rotmirror, hard/stress, camera stress calibration。

### Stage 4: QAT fine-tune

目标：

- 对 top family 做 QAT。
- 每个 family 至少 3 seed。
- int8 重新排序。

保留：

- `balanced_qat`
- `fast_qat`
- `c4_rescue_qat`
- `stress_robust_qat`

### Stage 5: 最终重训与导出

目标：

- 固定超参后全数据重训。
- 保留 repeated split、hard、stress、closed-set C4、camera stress、C4 LOO 作为证据。
- 导出最终 int8 模型和报告。

注意：

- 全数据重训结果不能替代前面 split 证据。
- 最终报告必须解释为什么选择多个候选，而不是只给一个 winner。

## 16. 推荐第一批实验矩阵

第一批不应再跑旧的泛化式 V6 矩阵，而应改成闭集 C4 + 摄像头 stress 矩阵。扫描样本仍控制在 1000 个以内，优先粗扫 200 到 500 个配置；如果资源充足，可用 10 到 16 路并行。

| 分支 | head | 关键参数 | 目的 |
| --- | --- | --- | --- |
| `A2` | parent + weapon_sub | `weapon_sub_weight=0.05/0.10/0.20` | 验证父类主导 + weapon 局部子类 |
| `B1` | parent + c4_box + c4_circuit | `box_weight=0.1/0.2/0.4`, `circuit_weight=0.2/0.4/0.8` | 避免裸电路孤例被箱体 C4 平均掉 |
| `B2` | parent + c4_attr + instance | `instance_weight=0.03/0.08` | 直接优化闭集 6 张逐实例记忆 |
| `C1` | parent + masked teacher + c4 heads | `alpha=0.1/0.3`, `T=2/4`, `mask_wrong_parent` | 保留 V4 正确边界并排除全部 V4 错例 |
| `D1` | parent + prototype 8/16 | `embedding_dim=8/16`, `proto_weight=0.05/0.10` | 用低维原型覆盖 6 张 C4，尤其裸电路孤例 |
| `E1` | decoupled | parent pretrain -> aux/proto balance -> joint | 验证解耦训练是否更稳 |
| `F1` | parent + focal c4 | `gamma=1/2`, `pos_weight=2/4/8` | 验证局部长尾损失 |
| `S1` | camera stress train mix | `blur_prob=0.10/0.25`, `noise_prob=0.10/0.25` | 直接针对高速模糊和轻微噪点 |

第一批可以同时引入 `D_metric_proto`，因为当前任务已从 few-shot 泛化转为闭集记忆。prototype 的板端成本可控，host 端高成本已被允许。

## 17. 实现任务拆分

### Trainer

建议新增文件：

```text
model_training/train_tiny32_v6_parent_primary_scan.py
```

核心新增：

- `HeadConfig`: 定义 parent/aux/c4/teacher/prototype head。
- `LossConfig`: 定义各 loss 权重和 long-tail 方法。
- `TrainingStageConfig`: 支持 one-stage 和 decoupled stage。
- `TeacherConfig`: 支持读取 V4 soft label。
- `C4Metadata`: 固定 6 张 C4 的 instance id、box/circuit group、原始文件名。
- `C4EvalConfig`: 支持 closed-set、camera stress、leave-one-out 和 false positive 报告。
- `CameraStressConfig`: 固定 motion blur/noise/brightness grid，保证无人值守扫描可复现。

### Candidate generator

建议新增或扩展：

```text
model_training/generate_v6_parent_primary_candidates.py
```

需要支持：

- `coarse`
- `fine`
- `qat`
- `summarize`
- 按侧重点选 top：balanced / fast / c4_rescue / stress / small。

### Launcher

建议新增：

```text
model_training/run_v6_parent_primary_pipeline_tmux.sh
```

要求：

- 使用 tmux 后台运行。
- watcher 低频、安静，不持续高频监控 GPU/内存。
- `watcher_state.json` 只记录阶段、完成数、失败数、top 摘要。
- 并发可吃满资源，但如果出现 partial/score-0 行，优先降低并发或隔离分支。

### Summarizer

建议新增：

```text
model_training/summarize_v6_parent_primary_results.py
```

输出：

- top by balanced score。
- top by C4 rescue。
- top by C4 camera stress recall。
- top by `explosive_030.jpg` rescue。
- top by stress。
- top by speed。
- top by size。
- failed/partial rows。
- 每个 top 模型的 per-seed min/mean/std。

## 18. 结果报告模板

每个候选模型应至少报告：

```json
{
  "trial": "...",
  "family": "A2_parent_weapon_aux",
  "head": "...",
  "loss_weights": {},
  "architecture": "...",
  "filters": [8, 16, 32],
  "activation": "relu",
  "train_transforms": "rot_mirror",
  "clean_parent_acc_min": 0.0,
  "hard_parent_acc_min": 0.0,
  "hard_parent_worst_min": 0.0,
  "stress_parent_worst_min": 0.0,
  "camera_stress_parent_worst_min": 0.0,
  "closed_set_c4_parent_recall": 0.0,
  "closed_set_c4_evidence_recall": 0.0,
  "c4_camera_stress_recall": 0.0,
  "c4_circuit_camera_recall": 0.0,
  "c4_parent_recall_loo": 0.0,
  "c4_false_positive_rate": 0.0,
  "c4_camera_false_positive_rate": 0.0,
  "float_int8_agreement": 0.0,
  "board_us": 0,
  "int8_bytes": 0,
  "score_min": 0.0
}
```

最终 Markdown 汇总必须包含：

- 为什么 V5 平面 8-class 不再作为主线。
- 每条路线的最好结果。
- 多个 top 模型的侧重点。
- C4 6 张逐张结果，包括 `explosive_030.jpg` 单独列。
- camera stress 下 C4 逐张结果和非 C4 误报。
- false positive 代表样本。
- int8 与 float 差异。
- 是否建议进入板端实测。

## 19. 风险与防护

### C4 过拟合

风险：

- 6 张 C4 被增强后看似很多，实际仍只有 6 个原始样本。
- 但当前目标是闭集全集，适度记忆不是错误；错误的是只记住清晰静态图而不抗真实摄像头退化。

防护：

- closed-set original + camera stress 逐实例报告。
- C4 LOO 作为诊断，不作为唯一 gate。
- 原图级 split。
- false positive 报告。
- C4 指标只占综合分的一部分。

### 高速模糊与轻微噪点

风险：

- 32x32 灰度下，运动模糊会直接抹掉 C4 裸电路的细线结构。
- 轻微噪点会让 grenade/firearm 上的局部纹理更像 C4 evidence，造成误报。

防护：

- Stage 1 就加入 camera stress 训练和评估。
- 单独报告 `c4_circuit_camera_recall`。
- 在 FP=0 或接近 0 的阈值下比较 C4 evidence，而不是只看无阈值 recall。
- 如果 32x32 灰度在 `explosive_030.jpg` 的 blur stress 上始终失败，再开 32x32 RGB 或轻量双通道 side branch，而不是直接扩大到 64x64。

### 父类稳定性下降

风险：

- 为了修 C4 牺牲 V4 已有父类边界。

防护：

- V4 teacher distillation。
- parent CE 权重最大。
- hard/stress min 指标进入主排序。

### 辅助 head 误导部署

风险：

- `c4_attr_head` 高分被错误当成最终 weapon 判定。

防护：

- 第一阶段只作 evidence。
- rescue 规则单独报告。
- 不和纯模型 parent 指标混算。

### int8 退化

风险：

- float 结果好，int8 后崩。

防护：

- coarse 阶段即做 int8 export。
- fine 阶段加入 QAT。
- float-int8 agreement 进入分数。

### 监控导致不稳定

风险：

- 高频监控导致训练环境卡死。

防护：

- tmux 后台跑。
- watcher 低频，只读结果文件和状态，不持续轮询重资源指标。

## 20. 外部方法参考

这些方法不是要照搬，而是支持 V6 的设计取舍：

- Hierarchical classification / making better mistakes: https://arxiv.org/abs/1912.09393
- Class-Balanced Loss: https://openaccess.thecvf.com/content_CVPR_2019/html/Cui_Class-Balanced_Loss_Based_on_Effective_Number_of_Samples_CVPR_2019_paper.html
- Long-tail logit adjustment: https://research.google/pubs/long-tail-learning-via-logit-adjustment/
- Balanced Softmax: https://arxiv.org/abs/2007.10740
- Focal Loss: https://arxiv.org/abs/1708.02002
- Decoupling representation and classifier: https://openreview.net/pdf?id=r1gRTCVFvB
- Supervised Contrastive Learning: https://proceedings.neurips.cc/paper/2020/hash/d89a66c7c80a29b1bdbab0f2a1a94af8-Abstract.html
- Prototypical Networks: https://arxiv.org/abs/1703.05175
- TensorFlow quantization aware training: https://www.tensorflow.org/model_optimization/guide/quantization/training_comprehensive_guide

## 21. 推荐落地顺序

最推荐的实际执行顺序：

1. 停掉刚开始的旧 V6 泛化式粗扫，避免继续消耗资源。
2. 实现 C4 metadata：6 张实例 id、`c4_box_like`、`c4_circuit_like`。
3. 实现 camera stress evaluator：motion blur、mild noise、brightness/contrast。
4. 实现 `A2_parent_weapon_aux`、`B_closed_c4_box_circuit`、`B_closed_c4_instance`。
5. 加 `C_masked_v4_teacher_closed_c4`，teacher KL 只保留 V4 父类正确样本。
6. 同步加入 `D_metric_proto`，因为闭集记忆和低板端成本使它成为第一轮合理路线。
7. 加 `E_decoupled`，验证是否比 one-stage 更稳。
8. 对 top family 做 QAT。
9. 最终导出多个侧重点候选，而不是只导出单一 winner。

若只能先做一条，优先做：

```text
parent_head
+ weapon_sub_head
+ c4_attr_head
+ c4_box_head
+ c4_circuit_head
+ masked V4 teacher
+ camera stress eval
```

这是最贴合当前问题的结构：父类主导、firearm short/long 合理保留、C4 作为闭集 evidence 修正 V4 盲点，并直接面对真实摄像头的高速模糊和轻微噪点。
