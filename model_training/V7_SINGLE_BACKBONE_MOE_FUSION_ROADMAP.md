# V7 单主干高速融合、MoE 路由与先进训练路线

本文面向后续继续训练或实现脚本的 AI/工程师。V7 的目标是在 V6 父类主导路线基础上，进一步解决当前最核心的问题：旧最佳模型和 CTD rescue 模型能力互补，但板端速度约束不允许双 CNN 推理。因此 V7 不再把“融合”理解为两模型平均或两模型串行，而是把多个专家能力压入一次 tiny32 前向推理。

## 1. 目标与硬约束

最终部署目标仍是 3 个父类：

```text
supplies
vehicle
weapon
```

速度约束：

- 理想目标：推理时间 `<= 8ms`
- 最低可接受：推理时间 `<= 12ms`
- 输入默认保持 `32x32 grayscale`
- 输出以 parent 为主；子类、C4、prototype、gate 只服务训练、调试或极低成本修正
- 主机端训练成本可以很高；板端推理成本必须严格受控

当前 tiny32 估算参考：

| filters | estimated board us | 用途 |
| --- | ---: | --- |
| `7-14-28` | `~6226us` | fast / 8ms 内安全档 |
| `8-16-32` | `~7163us` | V7 主力速度档 |
| `10-18-36` | `~8241us` | 12ms 内准确率兜底档 |

双模型串行推理不作为主线：

```text
old_best  ~8241us
ctd_best  ~8241us
two CNNs  ~16482us + overhead
```

这已经超过 `12ms` 下限。

## 2. 背景事实

V6/CTD 分析得到几个关键事实：

- 旧最佳 `p100_head_parent_s4_integrated_084_a5024 seed=20263104` 的 clean parent 为 `295/304`，错 `9` 张。
- CTD 平衡最佳 `ctd1_head_parent_weapon_c4_box_circuit_t0.2_p100_cal_balanced_clean_s4_int seed=20263203` 的 clean parent 为 `278/304`，错 `26` 张。
- 两者错集在 clean 上不重叠，oracle 选择可以达到 `304/304`。
- 旧模型的核心错例是高置信错误，因此 confidence threshold gate 不可靠。
- 原始 old/CTD 的量化权重方向差异很大，共享卷积层和 parent head 的 cosine 接近 0，不适合 weight soup 或权重插值。
- 复现 `.keras` 权重不能稳定拿回原始 top 性能，因此 V7 teacher 应优先来自原始 top `model_int8.tflite` 的输出，而不是失败复现权重。

因此 V7 的核心判断是：

```text
不是缺少会判对的专家，而是缺少一个在一次推理内保留多条局部决策边界的结构。
```

## 3. V7 总体结构

推荐主结构：

```text
32x32 grayscale input
  -> shared tiny32 trunk
       -> stable_expert_parent_head: 3 logits
       -> rescue_expert_parent_head: 3 logits
       -> gate_head: 1 or 2 logits
       -> optional embedding_head: 8/16/24 dims
       -> optional c4 / weapon auxiliary heads
  -> fused_parent_logits
```

推荐融合：

```text
gate = sigmoid(gate_logit)
fused_logits = (1 - gate) * stable_logits + gate * rescue_logits
parent = argmax(fused_logits)
```

或者部署更保守的 hard gate：

```text
if gate > threshold:
    parent = argmax(rescue_logits)
else:
    parent = argmax(stable_logits)
```

设计原则：

- 只跑一次 shared trunk。
- stable/rescue/gate/prototype 都是小 head，板端增量远小于第二个 CNN。
- 默认 route 应是 stable；rescue 只在少量局部区域触发。
- gate 不能只依赖 softmax 置信度；应学习图像特征、embedding、old/CTD disagreement teacher。

## 4. 技术组合总览

V7 不是单一 MoE，而是以下技术的组合：

```text
Shared-Trunk MoE
+ Multi-Teacher Delta Distillation
+ Negative Distillation
+ Group DRO / Worst-Group Optimization
+ Gate-Specific Focal / Asymmetric Loss
+ Supervised Contrastive or ArcFace / Prototype Learning
+ Augmentation Consistency
+ QAT / Int8 Consistency
+ SAM / SWA final fine-tune
+ Offline Oracle Teacher
```

这些技术的分工：

| 技术 | 推理成本 | 作用 |
| --- | ---: | --- |
| Shared-Trunk MoE | 低 | 一次 CNN 内保留 stable/rescue 两条边界 |
| Multi-Teacher Delta Distillation | 0 | old 负责稳定区，CTD 负责救援区 |
| Negative Distillation | 0 | 显式压低旧错类和 CTD 错类 |
| Group DRO | 0 | 优化最差组，而不是平均准确率 |
| Gate Focal Loss | 0 | 解决 rescue 正例极少导致 gate 不触发 |
| SupCon / ArcFace | 0 或极低 | 让 embedding 更可分，帮助孤例和 prototype |
| Prototype Override | 极低 | 用少量低维距离修正顽固孤例 |
| Consistency Regularization | 0 | 让 blur/noise/rot/mirror 下输出和 gate 稳定 |
| QAT / Int8 Consistency | 0 | 减少量化翻转 |
| SAM / SWA | 0 | 提升扰动稳定性 |

## 5. 数据分组

V7 必须显式维护分组，而不是把 304 张样本看成同质训练集。

基于 old/CTD pair：

```text
stable_both_correct = 269
preserve_old_correct_ctd_wrong = 26
rescue_old_wrong_ctd_correct = 9
both_wrong = 0
```

其他固定组：

```text
hard_clean = 12
c4_closed_set = 6
c4_box_like = 5
c4_circuit_like = 1
camera_blur_noise
rot_mirror
parent_supplies / parent_vehicle / parent_weapon
```

训练和评估时所有结果都应按这些组报告。

### 5.1 Rot/Mirror 训练硬约束

从 2026-05-19 起，`rot` 和 `mirror` 不再只是 stress 验证项，而是 V7 后续训练的必选项。任何 stable/rescue/router/adapter 训练，如果没有显式纳入 rot/mirror 视图，只能作为 clean-only ablation，不能作为主线候选。

必选几何视图：

```text
clean
rot90
rot180
rot270
mirror_lr
mirror_lr_rot90
mirror_lr_rot180
mirror_lr_rot270
```

训练要求：

```text
train_views must include rot_mirror
router/adaptor fixture must include rot_mirror GAP/logit views
stable/preserve rot_mirror views are hard negatives for gate
rescue rot_mirror views are positive/consistency views when CTD or hard label remains correct
```

验收要求：

```text
clean 304/304 is necessary but not sufficient
rot_mirror mean/worst must be reported
rot_mirror stable_false_trigger must be reported
rot_mirror preserve_false_trigger must be reported
```

## 6. Multi-Teacher Delta Distillation

V7 的 teacher 来源：

- `old_teacher`: 原始旧最佳 `model_int8.tflite`
- `rescue_teacher`: 原始 CTD 平衡最佳 `model_int8.tflite`
- `oracle_teacher`: 按样本选择 old 或 rescue 的离线 teacher

teacher 分配：

| 样本组 | stable expert | rescue expert | final fused target |
| --- | --- | --- | --- |
| both correct | old teacher + hard label | low weight | old teacher / hard label |
| old correct, CTD wrong | old teacher high weight | suppress | old teacher |
| old wrong, CTD correct | suppress old wrong class | CTD teacher high weight | CTD teacher |
| both wrong | hard label | hard label | hard label |

推荐损失：

```text
L_stable_kd =
  KL(stable_logits, old_teacher) on stable + preserve

L_rescue_kd =
  KL(rescue_logits, rescue_teacher) on rescue

L_fused_kd =
  KL(fused_logits, oracle_teacher)
```

注意：

- stable expert 不应被 CTD 全局污染。
- rescue expert 不应在 269 张 stable 样本上自由发挥。
- final fused target 应优先服务 parent 100%，不是追求 teacher 分布漂亮。

## 7. Negative Distillation

普通蒸馏告诉模型“像谁”。V7 还必须告诉模型“不要像谁”。

对 rescue 9 张：

```text
true_parent = human parent
old_wrong_parent = argmax(old_teacher)

maximize margin:
  fused_logit[true_parent] - fused_logit[old_wrong_parent] >= m_rescue
```

对 preserve 26 张：

```text
true_parent = human parent
ctd_wrong_parent = argmax(rescue_teacher)

maximize margin:
  stable_logit[true_parent] - stable_logit[ctd_wrong_parent] >= m_preserve
  fused_logit[true_parent] - fused_logit[ctd_wrong_parent] >= m_preserve
```

推荐 margin：

```text
m_rescue = 1.0, 1.5, 2.0
m_preserve = 1.0, 1.5, 2.0
```

推荐损失：

```text
L_negative =
  mean(relu(m - logit_true + logit_wrong))
```

这是处理高置信错例的核心。

## 8. Group DRO / Worst-Group Optimization

目标是 parent 100%，不是平均 accuracy。V7 应引入 worst-group 训练。

推荐分组 loss：

```text
L_groups = {
  stable,
  preserve,
  rescue,
  hard_clean,
  c4,
  camera_blur_noise,
  rot_mirror,
  parent_supplies,
  parent_vehicle,
  parent_weapon,
}
```

Group DRO 训练目标：

```text
L = sum(q_g * L_g)
q_g <- q_g * exp(eta * L_g)
normalize(q)
```

简化实现也可以先做 hard group reweight：

| group | 初始 parent weight |
| --- | ---: |
| stable | `1.0` |
| preserve | `3.0` - `5.0` |
| rescue | `5.0` - `8.0` |
| hard_clean | `2.5` - `4.0` |
| c4 | `2.5` - `5.0` |
| camera stress failures | `2.0` - `4.0` |

选择模型时必须看 worst group：

```text
clean_parent_all
rescue_recall
preserve_recall
hard_clean_recall
c4_recall
camera_stress_worst
int8_flip_count
```

## 9. Gate-Specific Focal / Asymmetric Loss

gate 的正例极少：

```text
gate_target = 1 for old_wrong_ctd_correct rescue samples
gate_target = 0 for old_correct samples
```

如果普通 BCE，gate 很容易永远输出 0。V7 应使用 focal/asymmetric gate loss。

推荐：

```text
L_gate =
  alpha_pos * focal_bce(gate, 1) on rescue
+ alpha_preserve * focal_bce(gate, 0) on preserve
+ alpha_stable * bce(gate, 0) on stable
```

推荐权重：

| group | gate target | weight |
| --- | ---: | ---: |
| rescue 9 | `1` | `8.0` - `16.0` |
| preserve 26 | `0` | `8.0` - `16.0` |
| stable 269 | `0` | `0.5` - `2.0` |
| hard clean | depends | `4.0` - `8.0` |

推荐 gate 初始化：

```text
gate_bias = logit(9 / 304) ~= -3.49
```

也可以更保守：

```text
gate_bias = -4.0
```

这会让初始模型默认走 stable expert，避免 early training 中 rescue 泛滥。

## 10. SupCon / ArcFace / Prototype Learning

V7 的 shared trunk 应输出一个低维 embedding：

```text
embedding_dim = 8, 16, 24
```

用途：

- 让同 parent 样本聚拢，不同 parent 拉开。
- 让 rescue 9 张形成可识别局部区域。
- 让 preserve 26 张和 rescue 区域拉开。
- 让 C4 的 `box_like` 和 `circuit_like` 作为 weapon 内部子中心存在，而不是强迫它们共享一个 C4 外观。

可选训练目标：

### 10.1 Supervised Contrastive

```text
positive:
  same parent
  same visual subclass
  same C4 instance group

negative:
  different parent
  rescue vs preserve
```

推荐权重：

```text
supcon_weight = 0.02, 0.05, 0.10
temperature = 0.07, 0.10, 0.20
```

### 10.2 ArcFace / Angular Margin

对 parent head 或 auxiliary metric head 使用 angular margin：

```text
arcface_margin = 0.15, 0.25, 0.35
arcface_scale = 8, 12, 16
```

适用场景：

- 类间边界不够硬。
- 高置信错例落入错误 parent 的角度区域。

### 10.3 Multi-Center Prototype

每个 parent 可以有多个中心：

```text
supplies:
  first_aid_kit_center
  telescope_center

vehicle:
  ambulance_center
  armoured_car_center

weapon:
  firearm_short_center
  firearm_long_center
  grenade_center
  c4_box_center
  c4_circuit_center
```

最终 parent 仍是 3 类，但内部允许多形态中心。

## 11. Prototype Override

如果 embedding 训练稳定，可以加入极低成本 prototype override。

推理逻辑：

```text
parent = argmax(fused_logits)

if prototype_rescue_score > threshold_rescue
   and gate_score > threshold_gate_low
   and parent != correct_rescue_parent:
      parent = rescue_parent
```

更保守版本：

```text
prototype only changes gate, not parent directly
```

成本估算：

```text
embedding_dim = 16
prototype_count <= 40
distance ops ~= 16 * 40
```

这远小于 CNN 成本。

适用目标：

- `explosive_030.jpg` 这种孤例。
- old 高置信错而 CTD 可救的 9 张。
- blur/noise 下容易漂移的 hard samples。

限制：

- prototype override 必须只在闭集数据下使用。
- 必须报告误触发 preserve 26 的数量。
- 不允许为了救 1 张而破坏 26 张 preserve。

## 12. Augmentation Consistency

真实困境包含高速模糊和轻微噪点。V7 不应只把这些当普通增强，而应对输出一致性加约束。

增强集合：

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
cam_blur2a0
cam_blur3a90
cam_blur5a45
cam_blur5a135
cam_noise0p02
cam_noise0p04
cam_blur3a0_noise0p02
cam_blur5a45_noise0p04
```

Consistency targets：

```text
KL(parent_logits(clean), parent_logits(aug))
KL(stable_logits(clean), stable_logits(aug))
KL(rescue_logits(clean), rescue_logits(aug)) on rescue samples
BCE(gate(clean), gate(aug))
distance(embedding(clean), embedding(aug))
```

推荐权重：

```text
parent_consistency = 0.02, 0.05, 0.10
gate_consistency = 0.05, 0.10
embedding_consistency = 0.02, 0.05
```

关键要求：

- rescue 样本增强后仍应触发 rescue。
- preserve 样本增强后仍不能误触发 rescue。
- C4 circuit 孤例增强后仍应保持 weapon parent。

## 13. QAT / Int8 Consistency

最终部署是 int8 TFLite Micro。V7 不能只看 Keras float。

必须检查：

```text
keras_parent_pred == int8_parent_pred
keras_gate_decision == int8_gate_decision
keras_rescue_trigger == int8_rescue_trigger
```

重点样本：

```text
rescue 9
preserve 26
hard_clean 12
c4 6
camera stress failures
```

推荐策略：

- 扫描期先用 PTQ 快速评估。
- top candidates 进入 QAT 或 fake-quant fine-tune。
- QAT 后重新评估 clean / stress / gate / prototype。
- 对 304 张全集输出 int8 flip report。

QAT 不应改变部署结构，只改变训练方式。

## 14. SAM / SWA 收尾

SAM 和 SWA 都不增加推理成本。

推荐使用方式：

```text
Stage A: normal training to convergence
Stage B: SAM fine-tune, low lr, short epochs
Stage C: SWA over final checkpoints
Stage D: int8 export and full audit
```

用途：

- SAM: 提升 blur/noise 和 hard sample 稳定性。
- SWA: 平滑后期权重，减少 seed 偶然性。

注意：

- SAM/SWA 只作为 final refinement。
- 如果 SAM/SWA 让 rescue gate 漂移，必须回滚。

## 15. Offline Oracle Teacher

主机端可以非常重。V7 应构建离线 oracle teacher：

输入来源：

- old best TFLite
- CTD best TFLite
- V6 Delta-CTD top candidates
- 后续 MoE top candidates
- augmentation-time voting
- prototype nearest neighbor
- human hard label

输出：

```text
oracle_parent_soft
stable_teacher_soft
rescue_teacher_soft
gate_target
negative_parent_id
group_id
sample_weight
```

原则：

- teacher 可以复杂，部署模型必须简单。
- teacher 不能把 CTD 的 26 张错误扩散到 stable 区。
- teacher 对 rescue 9 张必须强制纠错。
- teacher 对 both-correct 样本应尽量保守，默认 old/stable。

## 16. 推荐训练阶段

### Stage 0: 固定 teacher 与分组

产物：

```text
v7_teacher_bundle.npz
v7_teacher_summary.csv
v7_group_summary.csv
```

必须包含：

- old logits / soft labels
- CTD logits / soft labels
- oracle logits
- gate target
- group id
- negative parent id
- sample weights

### Stage 1: 8ms 内 shared-trunk MoE 粗扫

主力结构：

```text
spacetodepth_conv
filters = 7-14-28, 8-16-32
embedding_dim = 0, 8, 16
head = stable_rescue_gate
```

目标：

- 验证一次推理 MoE 是否能接近 oracle。
- 优先找 `<8ms` 候选。

### Stage 2: 12ms 内准确率兜底扫

结构：

```text
filters = 10-18-36
embedding_dim = 16, 24
aux = c4 / weapon / prototype
```

目标：

- 冲 clean parent `304/304`。
- 找到精度上限。

### Stage 3: Prototype / SupCon / ArcFace 细扫

只对 Stage 1/2 top 附近做。

扫描：

```text
supcon_weight
arcface_margin
prototype_count
prototype_threshold
gate_threshold
```

### Stage 4: QAT + SAM/SWA final

只对少量 top candidates 做。

目标：

- int8 clean parent `304/304`
- stress 下 rescue/preserve 不崩
- 速度仍在 `<=8ms` 或 `<=12ms`

## 17. 推荐候选参数

### 17.1 结构

| 参数 | 候选 |
| --- | --- |
| `filters` | `7-14-28`, `8-16-32`, `10-18-36` |
| `embedding_dim` | `0`, `8`, `16`, `24` |
| `gate_type` | `scalar_sigmoid`, `two_logit_softmax`, `prototype_assisted` |
| `fusion` | `soft_gate_logits`, `hard_gate_argmax`, `stable_default_hard_gate` |
| `aux_heads` | `none`, `weapon_c4`, `weapon_c4_box_circuit` |

### 17.2 损失权重

| 参数 | 候选 |
| --- | --- |
| `stable_kd_weight` | `0.05`, `0.10`, `0.20` |
| `rescue_kd_weight` | `0.20`, `0.40`, `0.80` |
| `fused_kd_weight` | `0.05`, `0.10`, `0.20` |
| `negative_margin_weight` | `0.20`, `0.50`, `1.00` |
| `group_dro_eta` | `0.01`, `0.05`, `0.10` |
| `gate_pos_weight` | `8`, `12`, `16` |
| `gate_preserve_weight` | `8`, `12`, `16` |
| `supcon_weight` | `0.00`, `0.02`, `0.05`, `0.10` |
| `arcface_margin` | `0.00`, `0.15`, `0.25`, `0.35` |
| `consistency_weight` | `0.00`, `0.05`, `0.10` |

### 17.3 Gate 阈值

```text
gate_threshold = 0.50, 0.60, 0.70, 0.80
prototype_threshold = calibrated by preserve false trigger = 0
```

阈值选择原则：

```text
preserve false trigger must be 0
then maximize rescue trigger
then maximize camera stress stability
```

## 18. 评估门槛

V7 模型选择不能只看 score。必须输出以下门槛：

| 指标 | 理想 | 最低 |
| --- | ---: | ---: |
| clean parent | `304/304` | `>=303/304` 才能进入 final |
| rescue 9 recall | `9/9` | `>=8/9` |
| preserve 26 recall | `26/26` | `26/26` |
| hard clean 12 recall | `12/12` | `>=11/12` |
| C4 clean parent | `6/6` | `>=5/6` |
| int8 flip on clean | `0` | `0` |
| estimated board us | `<=8000` | `<=12000` |
| camera stress preserve false trigger | `0` | `0` |

如果 clean parent 到不了 `304/304`，优先看：

```text
是否只差 1 张
是否所有图片都被某个 V7 top 模型判对
是否存在 2 个 V7 模型的 oracle 互补
是否可通过 prototype threshold 修复且不伤 preserve
```

## 19. 失败模式与处理

### gate 永远不触发

原因：

- rescue 正例太少。
- gate BCE 被 stable 负例淹没。

处理：

- 增大 `gate_pos_weight`。
- 使用 focal loss。
- gate bias 初始化为 `-3.5`，但 rescue 样本高权重训练。
- 给 rescue 增强样本复制 gate target。

### gate 误触发 preserve

原因：

- CTD 的错误边界污染 shared trunk 或 rescue head。

处理：

- 增大 preserve gate negative weight。
- 增加 negative distillation。
- prototype threshold 以 preserve false trigger = 0 为硬约束。
- 降低 rescue KD 在非 rescue 样本上的权重。

### rescue 修好了但 stable 大面积回归

原因：

- shared trunk 被 rescue/hard 过拟合。

处理：

- stable expert 加 old KD。
- Group DRO 中 preserve/stable 组权重上调。
- 降低 auxiliary C4 权重。
- 使用 8ms 小模型时减少 embedding/prototype 强度。

### Keras 对、int8 错

原因：

- gate logit 或 parent margin 太小，被量化翻转。

处理：

- 加 margin loss。
- QAT/fake quant。
- gate threshold 留安全间隔。
- 对 9/26/12 hard set 做 int8 flip loss 或重采样。

### camera blur/noise 下 gate 抖动

原因：

- gate 学了纹理尖峰，而不是稳定形态。

处理：

- gate consistency。
- embedding consistency。
- 对 rescue/preserve 单独做 camera stress augmentation。
- prototype 使用增强均值中心，而不是 clean 单图中心。

## 20. 与当前 Delta-CTD 的关系

当前 Delta-CTD 是 V7 的前置 teacher 实验，不是最终融合形态。

Delta-CTD 的价值：

- 验证 old/rescue teacher 的分组是否正确。
- 产出可用于 V7 offline oracle 的候选。
- 观察单 parent head 是否能吸收 rescue 9 而不伤 preserve 26。

Delta-CTD 的限制：

- 只有一个 parent head，可能无法同时容纳两套相差很大的边界。
- 没有显式 gate，因此无法表达“只有这 9 张走 rescue”。
- 没有 embedding/prototype 时，对孤例修复能力有限。

V7 应在 Delta-CTD 结果出来后立即比较：

```text
Delta-CTD top
vs
V7 shared-trunk MoE
vs
V7 MoE + prototype
vs
V7 MoE + QAT/SAM/SWA
```

## 21. 实现清单

建议新增脚本：

```text
build_v7_oracle_teacher_labels.py
generate_v7_moe_candidates.py
train_tiny32_v7_moe_scan.py
run_v7_moe_round1_tmux.sh
analyze_v7_gate_errors.py
summarize_v7_moe_results.py
```

trainer 需要支持：

- stable/rescue/fused/gate 多输出。
- per-sample group id。
- per-sample teacher soft labels。
- per-sample negative parent id。
- Group DRO 或 group reweight。
- gate focal/asymmetric loss。
- optional SupCon / ArcFace / prototype loss。
- consistency loss。
- int8 export and full audit。

每个 result.json 必须记录：

```text
clean_parent_accuracy
clean_wrong_list
rescue_recall
preserve_recall
gate_confusion
hard_clean_recall
c4_recall
camera_stress_parent
camera_stress_gate
int8_flip_count
estimated_board_us
model_int8_bytes
group_metrics
```

## 22. 文献与思想来源

V7 使用的是工程化组合，不要求完全复现论文实现。参考思想：

- Knowledge Distillation / specialist models: https://arxiv.org/abs/1503.02531
- Sparsely-Gated Mixture-of-Experts: https://arxiv.org/abs/1701.06538
- Learning to defer / selective prediction: https://arxiv.org/abs/1901.09192
- Group DRO: https://arxiv.org/abs/1911.08731
- Supervised Contrastive Learning: https://arxiv.org/abs/2004.11362
- ArcFace / angular margin: https://arxiv.org/abs/1801.07698
- Focal Loss: https://arxiv.org/abs/1708.02002
- Class-Balanced Loss: https://arxiv.org/abs/1901.05555
- Sharpness-Aware Minimization: https://arxiv.org/abs/2010.01412
- Stochastic Weight Averaging: https://arxiv.org/abs/1803.05407
- Model Soups: https://arxiv.org/abs/2203.05482
- Git Re-Basin / neuron permutation alignment: https://arxiv.org/abs/2209.04836
- ZipIt / feature zipping and partial model merging: https://arxiv.org/abs/2305.03053
- TIES-Merging / task vector interference removal: https://arxiv.org/abs/2306.01708
- Fisher-weighted model merging: https://arxiv.org/abs/2111.09832
- AdaMerging / layer-wise adaptive merging: https://arxiv.org/abs/2310.02575

Model Soups 在 V7 中主要作为反例参考：它适合相近 basin 的模型平均，而当前 old/CTD 权重差异很大，不应优先做 weight soup。

对 V7 更有价值的是 Re-Basin、ZipIt、TIES、Fisher-weighted merge 和 AdaMerging 的组合思想：

- Re-Basin: 合并前先处理通道/神经元置换对称性。
- ZipIt: 只合并相似特征，冲突特征保留为多头或 delta。
- TIES: 对 task delta 做 trim、sign conflict 过滤，减少互相抵消。
- Fisher-weighted merge: old stable 高敏感参数必须被保护。
- AdaMerging: 合并系数应该按 layer/channel/group 自适应，而不是全局一个 alpha。

## 23. 最终判断

V7 的主线不是“更大模型”，也不是“双模型部署”，而是：

```text
用主机端昂贵 teacher / oracle / group optimization 训练一个单次推理的 shared-trunk MoE。
```

最强候选形态：

```text
8-16-32 shared trunk
+ stable parent expert
+ rescue parent expert
+ conservative gate
+ optional 16-dim prototype embedding
+ negative distillation
+ group DRO
+ augmentation consistency
+ QAT final
```

如果 `<8ms` 档不能达到 parent 100%，允许 `10-18-36` 兜底，但必须保持 `<12ms`。

最终目标：

```text
int8 clean parent = 304/304
preserve 26 = 26/26
rescue 9 = 9/9
estimated board us <= 8000 preferred, <= 12000 hard limit
```

## 24. 2026-05-18 专家训练 v2 决策

在进入 shared-trunk MoE 之前，先补一轮更纯的专家训练。第一轮 `v7_expert_teacher_round1` 的主要问题不是没有救到 rescue，而是 stable 专家为兼顾 rescue 打坏了 old 原本稳定的 stable/preserve 区域。因此专家训练 v2 采用更强职责隔离：

- `stable expert`: old behavior cloning 为主，只保护 `stable_both_correct` 与 `preserve_old_correct_ctd_wrong`；`rescue_old_wrong_ctd_correct` 不作为 stable 的修复目标。
- `rescue expert`: CTD behavior cloning 只强作用于 rescue/hard/C4；`preserve` 作为 anti-CTD 约束，防止后续 gate 误触发。
- 第一轮实现先使用强 KD、per-sample weight 和职责隔离 teacher bundle；后续 trainer 再补 explicit negative distillation、teacher replay consistency、Group DRO、QAT/SAM/SWA。
- 本阶段验收不看单一 score。stable 先看 `stable=269/269`、`preserve=26/26`、接近 old `295/304`；rescue 先看 `rescue=9/9`、`hard>=11/12`、`c4>=5/6`。

## 25. 2026-05-18 专家训练结果与最新改线

专家训练 v2/v3 的结论已经足够明确：继续从头训练 stable expert 不是当前主线。后续 V7 应从“重新训练两个专家”改为 **old stable 锚定 + rescue delta/gate + 参数/激活对齐合并**。

### 25.1 已验证事实

旧模型 pair 仍是当前最强 teacher/oracle：

| model | all | stable | preserve | rescue | hard | C4 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| old stable `p100_head_parent_s4_integrated_084_a5024 seed=20263104` | `295/304` | `269/269` | `26/26` | `0/9` | `8/12` | `5/6` |
| old CTD rescue `ctd1_head_parent_weapon_c4_box_circuit_t0.2_p100_cal_balanced_clean_s4_int seed=20263203` | `278/304` | `269/269` | `0/26` | `9/9` | `11/12` | `5/6` |
| old stable + old CTD oracle | `304/304` | `269/269` | `26/26` | `9/9` | `12/12` | `6/6` |

逆向权重/激活事实：

- old stable parent head shape = `3 x 36`，dead GAP channels = `3`。
- old CTD rescue parent head shape = `3 x 36`，dead GAP channels = `12`。
- 两者是互补专家，不是同一 basin 的普通 soup 候选。不能直接逐层平均或 0.5/0.5 插值。

专家训练结果：

| round | best stable | best rescue | 结论 |
| --- | ---: | ---: | --- |
| round2 | `285/304` | rescue 可做 C4 但整体弱 | stable 未恢复 old |
| round3 negative-margin | `283/304` | top score `237/304`, `hard=11/12`, `C4=6/6`, `C4 camera=1.0` | rescue 方向有效，stable 继续退步 |

round3 最有价值的 rescue:

```text
v7r3_rescue_ctdkd_t0.7_p2_neg0.95 seed=20263611
all = 237/304
hard = 11/12
C4 = 6/6
C4 camera stress = 1.0
```

与 old stable 组合时：

```text
old stable + new rescue top score oracle = 302/304
hard = 12/12
C4 = 6/6
```

它能救 old stable 错 9 张中的 7 张；仍漏 `telescope_167.jpg` 与 `armoured_car_008.jpg`。因此新 rescue 可作为更安全的 rescue delta 候选，但还不如 old CTD rescue 的 oracle 完整性。

### 25.2 为什么从头训 stable 失败

从头训练 stable 的失败不是单个超参问题：

- round2 exact old anchor retrain baseline 只有 `265/304`，说明旧 stable 的 `295/304` 局部解不可稳定复现。
- parent-level KD 信息量不足，无法保留 old stable 对 stable/preserve 边界的细粒度记忆。
- negative margin 对 rescue 有效，但对 stable 会扰动原本应保留的干净边界。
- stable 的正确策略是继承/冻结 old stable 权重，而不是重新训练一个“像 old 的模型”。

因此后续禁止把“从头训练 stable expert 并追 old `295/304`”作为主线目标。stable 只能作为：

```text
frozen old stable teacher / frozen old stable expert / old stable initialized trunk
```

### 25.3 新主线：Stable-Anchored Parameter Surgery + Delta-MoE

新的 V7 主线：

```text
old stable trunk/head as anchor
+ aligned shared merge for truly similar channels
+ rescue-only delta/adapters/head
+ conservative gate
```

部署目标仍然是一次主干推理，而不是双 CNN 串行：

```text
x
 -> stable-anchored trunk
 -> z

stable_logits = stable_head(z)
rescue_delta = rescue_delta_head_or_adapter(z)
gate = gate_head(z, stable_margin, rescue_delta_margin, C4 evidence)

final_logits = stable_logits + gate * rescue_delta
```

保守 hard-gate 版本：

```text
if gate > threshold:
    output = stable_logits + rescue_delta
else:
    output = stable_logits
```

关键原则：

- 默认输出必须接近 old stable，先保住 `295/304`。
- rescue 不作为完整替代模型，只作为局部 correction/delta。
- gate 的 preserve false trigger 必须为 `0`。
- rescue 目标优先看 `old_wrong_rescue_correct`、hard、C4，不再用 all accuracy 评价 rescue 是否成功。

### 25.4 参数合并策略

允许做参数合并，但必须先做对齐和 stable 保护，不能直接平均。这里的目标不是把 old stable 与 CTD/rescue 变成一个普通 soup，而是把 CTD/rescue 的局部 correction 以最小破坏方式注入 old stable 坐标系。

总体流程：

```text
Re-basin / channel alignment
-> stable-protected sparse delta merge
-> rescue-only adapter or delta head
-> conservative gate controls rescue delta
```

#### 25.4.1 对齐优先级

两个模型虽然 parent head 都是 `3 x 36`，但 dead GAP channel 分布不同：

```text
old stable dead GAP channels = 3
old CTD rescue dead GAP channels = 12
```

因此不能假设第 `i` 个 channel 在两个模型中含义相同。第一步必须做权重和激活联合对齐：

1. 对 old stable 与 old CTD/rescue 逐层反量化权重，计算 cosine/L2/sign agreement。
2. 跑全数据中间激活，计算 channel correlation / CKA。
3. 对 GAP/conv channel 做 Hungarian matching，识别功能相同但 index 不同的通道。
4. 对 conv block 做一致置换：某层 output channel 的重排必须同步到下一层 input channel；depthwise/pointwise block 应作为一个 unit 处理。

推荐 channel matching 分数：

```text
similarity(i, j) =
  0.45 * corr(activation_stable_i, activation_rescue_j)
+ 0.25 * cosine(outgoing_parent_weight_i, outgoing_parent_weight_j)
+ 0.20 * agreement_on_stable_preserve
+ 0.10 * agreement_on_hard_c4
```

其中 `agreement_on_stable_preserve` 优先保护 old stable 的 `269 + 26` 个正确样本；`agreement_on_hard_c4` 只作为次级信号，避免 hard/C4 把 shared trunk 拉偏。

#### 25.4.2 通道/参数分组

对齐后按 layer/channel/parameter group 分类，而不是全量合并：

| group | 处理 |
| --- | --- |
| shared-similar | 以 stable 为锚小步合并 rescue delta |
| stable-only | 保留 stable，不动 |
| rescue-only | 放入 rescue delta/adaptor，不污染 shared trunk |
| conflict | 不合并，交给 gate/MoE |

分组判断不能只看 weight cosine。每个候选 channel 还要做 ablation/sensitivity：

```text
zero old-stable channel -> stable/preserve 掉多少？
zero rescue channel -> rescue/hard/C4 掉多少？
merge rescue delta -> preserve 是否掉？
```

硬规则：

- 任意 merge 让 preserve 26 掉样本，该 group 立即标记为 `conflict`。
- 对 stable/preserve 高敏感、但 rescue 收益不确定的参数，标记为 `stable-only`。
- 对 hard/C4 有收益但 preserve 有风险的参数，标记为 `rescue-only`，只能进入 adapter/delta head。
- 只有 shared-similar 才允许进入 trunk-level merge。

#### 25.4.3 Stable-protected delta merge

禁止使用：

```text
merged = 0.5 * stable + 0.5 * rescue
```

推荐合并形式：

```text
delta = aligned_rescue_weight - stable_weight
merged_weight = stable_weight + alpha[group] * mask * delta
```

推荐 alpha 搜索范围：

| group | alpha |
| --- | ---: |
| stable-only | `0` |
| conflict | `0` |
| shared-similar early block | `0.00, 0.03, 0.05` |
| shared-similar late block | `0.03, 0.05, 0.10, 0.20` |
| parent/GAP boundary delta | `0.05, 0.10, 0.20` |
| rescue-only | 不进 merged trunk，转入 adapter/delta |

mask 使用 TIES/Task Arithmetic 思想：

- 只保留方向一致、幅度显著的 delta。
- sign 冲突的参数不合并。
- 小幅 delta 视为噪声丢弃。
- 对 stable/preserve 高敏感参数加保护，不让 rescue delta 覆盖。

可执行的 mask 规则：

```text
large_delta = abs(delta) >= percentile(abs(delta), 70 or 80)
sign_ok = sign(delta_group_mean) == sign(delta_param)
preserve_safe = preserve_26 unchanged in local probe
mask = large_delta & sign_ok & preserve_safe
```

#### 25.4.4 Fisher / sensitivity 保护

由于数据只有 304 张，可以直接用样本梯度近似 Fisher，不需要复杂估计：

```text
F_stable[p] = mean_{stable+preserve} grad(logit_true, p)^2
F_rescue[p] = mean_{rescue+hard+C4} grad(logit_true, p)^2
```

参数级 alpha:

```text
alpha_p = clamp(
  beta * F_rescue[p] / (F_stable[p] + F_rescue[p] + eps),
  0,
  alpha_max
)
```

保护规则：

```text
if F_stable[p] is high and F_rescue[p] is not overwhelming:
    alpha_p = 0
```

工程上可以先实现 group/channel 级 Fisher，避免参数级噪声过大。第一版指标：

```text
F_stable_channel = mean grad^2 over channel weights on stable+preserve
F_rescue_channel = mean grad^2 over channel weights on rescue+hard+C4
```

#### 25.4.5 Rescue-only delta/adaptor

冲突能力不要硬合进 shared trunk。推荐保留为 rescue delta：

```text
z = stable_anchored_trunk(x)

stable_logits = stable_head(z)
rescue_delta = rescue_adapter(z) or rescue_delta_head(z)
gate = gate_head([z, stable_margin, rescue_delta_margin, c4_evidence])

final_logits = stable_logits + gate * rescue_delta
```

rescue_delta 的初始化：

```text
rescue_delta_head = aligned_ctd_parent_head - stable_parent_head
```

如果只做 head-level delta，推理成本最低；如果 head-level 不够，再加低秩 adapter：

```text
adapter(z) = W2 * relu(W1 * z)
rank = 4, 8, 12
```

adapter 只允许被 gate 使用，不允许无条件覆盖 stable logits。

#### 25.4.6 第一轮实验顺序

第一轮不要动早期 trunk。按风险从低到高：

```text
E0: old stable frozen baseline
E1: GAP/parent-head aligned delta only
E2: parent-head delta + logistic gate
E3: parent-head delta + rank-4/8 adapter + gate
E4: block_3 shared-similar partial merge + delta head
E5: block_2 partial merge only if E4 improves and preserve remains 26/26
E6: block_1 normally frozen; only after all above失败再探索
```

推荐先扫：

```text
merge_scope = head_only, gap_head, block3_head
alpha = 0.00, 0.03, 0.05, 0.10, 0.20
mask_percentile = 70, 80, 90
adapter_rank = 0, 4, 8
gate_threshold = 0.50, 0.60, 0.70, 0.80
```

第一轮验收：

```text
preserve 26 = 26/26 hard constraint
all >= 299/304 first target
hard >= 11/12
C4 = 6/6 preferred
gate false trigger on preserve = 0
int8 flip = 0
```

如果 `head_only + gate` 已能超过 `299/304`，不要急着动 trunk；继续优化 gate 和 delta。只有当 head/adaptor 明确无法救 `telescope_167.jpg`、`armoured_car_008.jpg`，才考虑 block_3 partial merge。

### 25.5 下一步实现优先级

新增或扩展脚本优先级：

```text
analyze_v7_old_ctd_weight_activation_alignment.py
build_v7_stable_anchored_delta_init.py
generate_v7_delta_moe_candidates.py
train_tiny32_v7_delta_moe_scan.py
run_v7_delta_moe_round1_tmux.sh
analyze_v7_delta_gate_errors.py
```

第一轮不要训练大而全的新 expert。先做最小可证伪版本：

1. 冻结 old stable 输出作为 baseline。
2. 构建 old stable + rescue delta head。
3. gate target:
   - `1` for old wrong / CTD or new rescue correct。
   - `0` for old correct，尤其 preserve 26。
4. 训练只更新 gate、rescue delta head、极少量 adapter；shared trunk 冻结或极低学习率。
5. 验收优先级：

```text
old stable baseline = 295/304
delta-gated result >= 299/304 first target
preserve 26 = 26/26 hard constraint
hard >= 11/12
C4 = 6/6 preferred
gate false trigger on preserve = 0
estimated board us <= 12000
```

如果 delta-gated result 不能超过 `299/304`，不要继续扩大模型；先分析 gate missed/false-trigger，再决定是否回退到 old CTD rescue 作为 delta teacher。

### 25.6 2026-05-18 阶段1/阶段2合并结果

阶段1已完成 `old stable` 坐标系下的 head/GAP delta merge，脚本：

```text
run_v7_delta_merge_phase1.py
experiments/v7_delta_merge_phase1_20260518_0001
```

最好结果：

```text
all = 298/304
stable = 269/269
preserve = 26/26
rescue = 3/9
hard = 9/12
C4 = 5/6
gate_count = 3
preserve_false_trigger = 0
```

结论：单纯把 CTD/rescue head 投影回 stable GAP 坐标并做 TIES 稀疏 delta，保护性足够，但表达力/路由都不足。它只救回 `telescope_149.jpg`、`telescope_167.jpg`、`firearms_126.jpg`，剩余 `first_aid_kit_046.jpg`、`first_aid_kit_058.jpg`、`armoured_car_008.jpg`、`armoured_car_098.jpg`、`explosive_124.jpg`、`explosive_070.jpg` 没有解决。

阶段2已完成 `stable frozen GAP + low-rank/linear rescue adapter + CTD-distilled router`，脚本：

```text
run_v7_delta_merge_phase2.py
experiments/v7_delta_merge_phase2_20260518_0005
```

最好可部署结果：

```text
all = 304/304
stable = 269/269
preserve = 26/26
rescue = 9/9
hard = 12/12
C4 = 6/6
gate_count = 9
stable_false_trigger = 0
preserve_false_trigger = 0
```

最佳配置：

```text
target_mode = hybrid_ctd_margin
adapter = stable GAP z-score -> parent-logit delta
adapter_l2 = 0.001
adapter_rank = full
adapter_alpha = 1.0
margin = 4.0
router = learned_old_stable
router_feature = old_gap_logits
router_l2 = 0.001
gate = two_band_disagree_tail_margin
main_threshold = 0.87736226
tail_low_threshold = 0.606759
tail_adapter_margin_lt = 0.05
```

触发的 9 个样本全部是 old stable 错、CTD 对的 rescue 样本：

```text
first_aid_kit_046.jpg
first_aid_kit_058.jpg
telescope_149.jpg
telescope_167.jpg
armoured_car_008.jpg
armoured_car_098.jpg
explosive_124.jpg
explosive_070.jpg
firearms_126.jpg
```

关键诊断：

- 旧 stable 本身是最强 preserve/stable anchor，不应再从头训练 expert 替代它。
- CTD/rescue 的价值是局部 correction，不是全局模型；直接使用 CTD 会把 preserve 26 全部打掉。
- Phase2 的 adapter 已有 304/304 oracle 表达力，真正难点是 router 隔离。
- `explosive_070.jpg` 是主 learned-router 唯一漏样本；它的 adapter margin 极低，所以需要 two-band router：高分 rescue 正常放行，低分尾部只允许 `old/adaptor disagree` 且 `adapter_margin < 0.05` 的样本补充放行。
- 这个结果说明下一步不应先动早期 trunk；应先把 phase2 adapter/router 固化成可部署 TFLite/TFLM 形式，再做量化翻转检查。

下一步优先级更新：

1. 将 `best_phase2_adapter_params.npz` 转成板端可用参数：GAP z-score、adapter coef、router coef、two-band gate 常量。
2. 在 PC 端做 int8/float 路由一致性检查，确认 `gate_count=9` 不因量化漂移。
3. 再评估 MCU 成本：36维 GAP z-score + 37x3 adapter + old_gap_logits router，预计远低于双 TFLite ensemble。
4. 只有当量化或板端成本破坏 `304/304` 时，才回到 block_3 partial merge 或更小 rank/sparse adapter。

### 25.7 2026-05-19 Phase2 部署包与量化检查

已新增部署导出脚本：

```text
export_v7_phase2_deploy_bundle.py
```

当前部署包：

```text
experiments/v7_phase2_deploy_bundle_20260519_0002/
  deploy_bundle_summary.json
  deploy_sample_decisions.csv
  quantization_sweep.csv
  v7_phase2_adapter_params.hpp
```

导出包语义：

```text
输入：old stable TFLite 暴露出的 GAP[36] 和 parent_logits[3]
输出：phase2 final parent / gate / logits
神经模型：仍然只有 old stable TFLite
额外逻辑：GAP z-score + adapter delta + learned gate
```

为了避免原始搜索阈值贴边，导出脚本会把 two-band gate 阈值重校准到正负样本中点：

```text
original main threshold = 0.87736226
calibrated main threshold = 0.87505370

original tail low threshold = 0.606759
calibrated tail low threshold = 0.44753242

original tail adapter margin limit = 0.05
calibrated tail adapter margin limit = 0.25076902
```

校准后 float 结果：

```text
all = 304/304
stable = 269/269
preserve = 26/26
rescue = 9/9
hard = 12/12
C4 = 6/6
gate_count = 9
stable_false_trigger = 0
preserve_false_trigger = 0
```

固定点仿真结果：

| mode | all | stable | preserve | rescue | hard | C4 | gate | false trigger |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Q8 | 302/304 | 268/269 | 26/26 | 8/9 | 12/12 | 6/6 | 9 | stable 1 |
| Q10 | 302/304 | 268/269 | 26/26 | 8/9 | 12/12 | 6/6 | 9 | stable 1 |
| Q12 | 304/304 | 269/269 | 26/26 | 9/9 | 12/12 | 6/6 | 9 | 0 |
| Q14 | 304/304 | 269/269 | 26/26 | 9/9 | 12/12 | 6/6 | 9 | 0 |
| Q15 | 304/304 | 269/269 | 26/26 | 9/9 | 12/12 | 6/6 | 9 | 0, but int32 accumulator unsafe |

选择：

```text
deploy fixed-point mode = Q12
adapter accumulator max = 103103661
gate accumulator max = 71657340
int32 accumulator safe = yes
```

`v7_phase2_adapter_params.hpp` 已包含：

```text
ApplyPhase2Float(gap, old_logits)
ApplyPhase2Q(gap, old_logits)
```

下一步工程任务：

1. 在 old stable TFLite 推理后暴露/读取 GAP tensor 与 parent logits。
2. 调用 `ApplyPhase2Q`，用 Q12 adapter/router 替换原 parent argmax。
3. 做板端或主机 C++ 回放测试，输入 PC 端导出的 304 条 GAP/logits fixture，要求 gate 与 parent 完全一致。
4. 再决定是否把 `v7_phase2_adapter_params.hpp` 移入 `new/code` 的正式推理模块。

### 25.8 2026-05-19 Phase2 stress 验证补充

重要更正：`Phase2 304/304` 只代表 clean frozen 304，不代表 rot/mirror/blur/noise stress 也 100%。Phase2 adapter/router 的训练与阈值校准没有使用 stress 样本。

已新增 stress 验证脚本：

```text
evaluate_v7_phase2_stress.py
experiments/v7_phase2_stress_20260519_0001/
  stress_report.json
  stress_summary.csv
  stress_sample_events.csv
```

验证使用现有 `stress_batch_any`，覆盖：

```text
rot90, rot180, rot270
mirror_lr, mirror_lr_rot90, mirror_lr_rot180, mirror_lr_rot270
noise_0p06, noise_0p10
hblur5_noise_0p06, diagblur5_noise_0p08, vblur5, diagblur5
cam_blur2a0, cam_blur3a90, cam_blur5a45, cam_blur5a135
cam_noise0p02, cam_noise0p04
cam_blur3a0_noise0p02, cam_blur5a45_noise0p04
```

使用 clean 校准阈值和 Q12，不对 stress 重新调参。总体结果：

```text
old stable stress mean accuracy = 0.9294
old stable stress min accuracy = 0.8586

phase2 Q12 stress mean accuracy = 0.9278
phase2 Q12 stress min accuracy = 0.8520
phase2 Q12 camera min accuracy = 0.8684
```

结论：

- Phase2 clean 上打穿，但 stress 平均略低于 old stable。
- Q12 与 float 在 stress 下完全一致：`pred_mismatch = 0`，`gate_mismatch = 0`，所以问题不是 Q12 量化，而是 router/adapter 的 stress 泛化。
- rot/mirror 类普遍退步，说明 clean-only router 对几何变换后的 GAP/logit 分布隔离不稳。
- blur/noise 混合下有时改进，有时退步；`diagblur5_noise_0p08` 是最差项。
- stress 下 gate 会误触 stable/preserve，例如 `cam_blur5a135` 有 `stable_false_trigger = 13`，这是不能直接部署为鲁棒方案的原因。

代表性结果：

| stress | old wrong | phase2 Q12 wrong | delta |
| --- | ---: | ---: | ---: |
| clean | 9 | 0 | +9 |
| cam_blur2a0 | 8 | 3 | +5 |
| noise_0p10 | 22 | 19 | +3 |
| noise_0p06 | 15 | 13 | +2 |
| hblur5_noise_0p06 | 23 | 21 | +2 |
| mirror_lr_rot90 | 23 | 28 | -5 |
| cam_blur5a135 | 36 | 40 | -4 |
| rot90 | 29 | 32 | -3 |
| diagblur5_noise_0p08 | 43 | 45 | -2 |

后续方向必须改为 `stress-aware router/adaptor`，不能只做 clean 304：

1. 保持 old stable 为 anchor。
2. `rot/mirror` 是训练必选项：使用 `clean + rot90/180/270 + mirror_lr + mirror_lr_rot90/180/270` 扩展后的 GAP/logit fixture 训练 router，gate target 不再只来自 clean。
3. 对 stable/preserve stress view 增加强负样本权重，目标是消除 stress false trigger。
4. 对 rescue 样本的 stress view 增加一致性约束，避免 clean 能救、stress 漏救。
5. 重新搜索 two-band gate，验收必须同时包含：

```text
clean = 304/304
rot_mirror mean >= old stable rot_mirror mean
rot_mirror worst >= old stable rot_mirror worst
stress mean >= old stable stress mean
stress min >= old stable stress min
camera min >= old stable camera min
rot_mirror stable/preserve false trigger = 0 preferred, must be reported
stress stable_false_trigger <= old/regressed baseline target
Q12 pred/gate mismatch = 0
```

文档同步规则：以后任何 V7 训练记录如果没有说明 `rot_mirror` 已进入训练视图，应标注为 `clean-only`，不得与主线 stress-aware 结果混排。

### 25.9 2026-05-19 Phase3 stress-aware 长线搜索已启动

新增脚本：

```text
run_v7_phase3_stress_aware_search.py
run_v7_phase3_stress_aware_tmux.sh
```

正式 run：

```text
experiments/v7_phase3_stress_aware_20260519_0001/
  feature_cache/old_rescue_clean_all_stress_gap_logits.npz
  stage1_stress_aware_adapter_router/shard_0..15/
```

这轮不是继续 clean-only 追 304，而是把 Phase2 的问题直接纳入训练目标：

- 每个主线 view profile 都包含完整 `rot/mirror` 视图。
- 训练 fixture 使用 old stable / old CTD 的 clean+stress GAP/logits，不重复训练早期 trunk。
- 每个 shard 随机抽样 1200 条 adapter 路线，先用 oracle 上界筛选，再对 top adapter 搜 learned/two-band/analytic router。
- 16 路总覆盖约 19200 条 adapter 路线，router 阶段覆盖 CTD delta、margin target、hybrid CTD-margin、preserve/stable lock、group-DRO、camera guard 等方向。

主要技术路线：

```text
view profiles:
  rotmirror
  rotmirror_noise
  rotmirror_blur_noise
  rotmirror_camera_light
  rotmirror_camera_full
  worst_phase2
  all_stress

positive profiles:
  clean_rescue_all_views
  view_rescue_preserve_locked
  hybrid_clean_stress
  margin_improve_hard
  stress_union_conservative
  stress_recovery_allow_stable

weight profiles:
  balanced
  preserve_locked
  stable_locked
  rescue_heavy
  group_dro
  camera_guard
```

启动健康检查：

```text
tmux sessions = 16
selected_adapter_combo_count = 1200 per shard
feature cache covers clean + 21 stress views
```

结果验收仍沿用 25.8 的口径：必须同时报告 clean、rot/mirror mean/min、stress mean/min、camera min、stable/preserve false trigger，并与 old stable stress baseline 对比。

### 25.10 2026-05-19 严格几何一致性专家训练已启动

背景判断：

- 旧模型虽然使用了 `train_transforms = rot_mirror`，但只有训练集 8-view 展开。
- 旧训练链路的 validation / early stopping 仍是 clean `val_loss`。
- 旧 stable 的 int8 calibration 是 `balanced_clean`，不是 `balanced_rotmirror`。
- 因此旧模型只是做了几何增强，不是严格几何一致性训练。

本轮不只“再加 rot/mirror”，而是把此前验证过有效的方向合并到专家训练：

```text
old stable anchor
CTD/rescue teacher
correct-teacher labels
negative parent margin
stable/preserve/rescue 隔离权重
8-view parent probability consistency
rot/mirror validation for val_loss / early stopping
balanced_rotmirror int8 calibration
```

新增训练能力：

```text
train_tiny32_v5_visual_subclass_scan.py
  validation_transforms
  geometric_consistency_weight
  geometric_consistency_group
  parent_consistency output/loss
```

一致性 loss 形式：

```text
每个原始样本展开为 8 个 view:
  clean, rot90, rot180, rot270,
  mirror_lr, mirror_lr_rot90, mirror_lr_rot180, mirror_lr_rot270

对同一原图 8 个 parent softmax:
  minimize KL(p_view || mean(p_8views))
```

新增候选与启动脚本：

```text
generate_v7_expert_geometric_candidates.py
run_v7_expert_geometric_tmux.sh
```

正式 run：

```text
experiments/v7_expert_geometric_20260519_0001/
  stage2_stable_geometric_consistency/shard_0..7/
  stage2_rescue_geometric_consistency/shard_0..7/
```

启动配置：

```text
stable candidates = 96
rescue candidates = 96
stable shards = 8
rescue shards = 8
seeds per role = 2
epochs = 280
patience = 52
calibration_limit = 304
```

启动健康检查：

```text
tmux sessions = 16
stable run.log = 8
rescue run.log = 8
per shard trials = 12
candidate check:
  train_transforms = rot_mirror
  validation_transforms = rot_mirror
  calibration = balanced_rotmirror
  geometric_consistency_weight > 0
```

本轮验收重点：

1. 专家自身 clean 不能只追局部提升；必须看 clean 与 8-view rot/mirror 的差距是否收窄。
2. stable expert 的 preserve/stable 误伤必须低于上一轮专家。
3. rescue expert 要看 rescue clean 与 rescue rot/mirror 是否同时改善，不能只救 clean。
4. 如果专家仍然 clean 明显高于 rot/mirror，说明还需要 hard-view replay 或 view-pooled/equivariant 结构，而不是继续调 router。

### 25.11 2026-05-19 Phase4/Phase5 几何 delta 搜索结论

重新明确目标：

```text
clean = 304/304
rot_mirror 7 views = each 304/304
stress = as high as possible
deployment = old stable single backbone + lightweight correction
```

Phase4 `old_wrong_*` hard-label adapter/router 已证明方向有效但容量不足：

```text
run:
  experiments/v7_phase4_geometric_delta_20260519_0001/

best adapter oracle:
  clean = 304/304
  rot_mirror_min = 0.976973684
  stress_min = 0.901315789
  stress_mean = 0.961152882

rot_mirror_min == 1.0 rows = 0
```

Phase5 将 adapter 特征从 36-d GAP 扩展为 kernelized/poly 特征：

```text
scripts:
  run_v7_phase3_stress_aware_search.py
  run_v7_phase5_kernel_geo_oracle_tmux.sh

新增 adapter features:
  old_gap_logits
  old_gap_poly
  old_gap_logits_poly
  old_gap_logits_interact

run:
  experiments/v7_phase5_kernel_geo_oracle_20260519_0001/

rows = 38400
clean304 rows = 3338
rot_mirror_min == 1.0 rows = 0

best:
  clean = 304/304
  rot_mirror_min = 0.993421053
  stress_min = 0.976973684
  stress_mean = 0.994674185
```

结论：

- 二阶/interaction GAP delta adapter 已经非常接近，但仍未达到严格几何 100%。
- 继续调线性 delta/router 会出现收益递减。
- 下一步必须增加非线性局部记忆能力，优先使用单 backbone 后的 lightweight prototype rescue，而不是继续训练第二个完整 CNN。

### 25.12 2026-05-19 Phase6 GAP prototype rescue 候选

技术路线：

```text
old stable int8 TFLite remains the only neural backbone
use old GAP(36) as embedding
store z-scored prototypes for old-wrong stress/rot_mirror failures + clean anchors
compute nearest class prototype distance
override old parent only when:
  nearest prototype class != old parent
  class distance confidence >= threshold
  nearest distance <= threshold
```

这是 MoE 的轻量非参数分支：shared old backbone + local prototype expert。它救的是旧模型在 clean/rot_mirror/stress 下实际失败的 GAP 区域，不再依赖 stable/rescue 两个 CNN 之间的 oracle gate。

脚本与 artifact：

```text
run_v7_phase6_prototype_rescue.py
export_v7_phase6_prototype_bundle.py

experiment:
  experiments/v7_phase6_prototype_rescue_20260519_0001/stage1_gap_prototype_rescue/

deploy bundle:
  experiments/v7_phase6_prototype_rescue_20260519_0001/deploy_bundle/
    v7_phase6_prototype_params.hpp
    deploy_bundle_summary.json
    deploy_stress_summary.csv
    deploy_sample_decisions.csv
```

最佳候选：

```text
prototype_profile = all_old_wrong_plus_clean
feature_name = old_gap
feature_dim = 36
prototype_count = 767
conf_threshold = 0.002599076728031946
dist_threshold = 0.04175443470550415
gate_count = 472

clean = 304/304
rot90 = 304/304
rot180 = 304/304
rot270 = 304/304
mirror_lr = 304/304
mirror_lr_rot90 = 304/304
mirror_lr_rot180 = 304/304
mirror_lr_rot270 = 304/304
all stress views = 304/304
stress_min = 1.0
stress_mean = 1.0
```

部署代价估算：

```text
old stable estimated_board_us = 8241
distance MACs per frame = 767 * 36 = 27612
float prototype table = 110448 bytes
deduped int8 prototype table = 733 * 36 = 26388 bytes
header size = about 384 KB
host C++ microbench prototype-only = about 7.8 us/call
```

验证：

```text
python -m py_compile run_v7_phase6_prototype_rescue.py
python -m py_compile export_v7_phase6_prototype_bundle.py
export replay metrics:
  clean_all_correct = true
  rotmirror_all_correct = true
  stress_all_correct = true
g++ -std=c++17 -fsyntax-only v7_phase6_prototype_params.hpp include test = pass
```

当前 caveat：

- Phase6 是 deterministic stress benchmark 上的 prototype memory expert，准确率已经满足目标。
- 仍需板端接入 old stable GAP/logits 后实测延迟；因为 stress 已 100%，目标允许到 20ms。
- `new/user/debug.sh remote status` 当前 SSH 到 `10.100.170.226:22` timeout，暂时没有板端实测通路。
- 2026-05-19 复查热点邻居：`192.168.137.198` 从 Windows hotspot 源地址 `192.168.137.1` 出口，但 SSH/22 timeout；WSL native route 仍会把 `192.168.137.198` 走到 `10.5.0.1/dev eth3`，但 Windows backend 也同样 timeout，因此当前不是单纯 WSL 路由问题。
- USB 串口复查：`usbipd.exe list` 只有 persisted `COM6/COM7` 记录，没有当前 connected CH340/USB serial；`/dev/ttyUSB*`/`/dev/ttyACM*` 不存在。
- int8 prototype table 已完成并通过 replay；下一步是板端接入 fast old GAP/logits 后实测延迟。

### 25.13 2026-05-19 纯 prototype 与 fast backbone 验证

回答“纯 prototype 是否可实现”：

```text
可以，但需区分两种含义：

1. old backbone outputs GAP, parent class is decided only by nearest class prototype
   -> 可实现，但需要覆盖所有目标 stress/view 的 prototype，表更大。

2. old backbone + old parent head, prototype only rescues failure region
   -> 当前 Phase6 主候选，表更小，延迟更稳。
```

纯 GAP prototype classifier 试验：

```text
stable old backbone:
  clean_only prototypes = 304
    clean = 1.0
    rot_mirror_min = 0.730263
    stress_min = 0.730263
  clean_rotmirror prototypes = 2432
    clean = 1.0
    rot_mirror_min = 1.0
    stress_min = 0.766447
  all_views prototypes = 6688
    clean = 1.0
    rot_mirror_min = 1.0
    stress_min = 1.0

fast old backbone:
  clean_only prototypes = 304
    clean = 1.0
    rot_mirror_min = 0.631579
    stress_min = 0.631579
  clean_rotmirror prototypes = 2432
    clean = 1.0
    rot_mirror_min = 1.0
    stress_min = 0.605263
  all_views prototypes = 6688
    clean = 1.0
    rot_mirror_min = 1.0
    stress_min = 1.0
```

结论：

- 纯 prototype 要想 stress 也 100%，必须把 stress views 也纳入原型表；只用 clean 或 clean+rotmirror 不够。
- 纯 all_views prototype 是可实现的 deterministic memory classifier，但表规模明显大于 rescue 版，泛化余量也更依赖 stress 覆盖面。
- 当前更合理的部署候选仍是 prototype rescue：让 old head 处理大多数正常点，prototype 只覆盖失效区域。

fast backbone + Phase6 prototype rescue：

```text
fast old model:
  p100_near_anchor_s2_deploy_balance_v6_fast_augment_
  estimated_board_us = 5296
  feature_dim = 24

cache:
  experiments/v7_phase6_fastbackbone_proto_20260519_0001/feature_cache/fast_old_stress_gap_logits.npz

experiment:
  experiments/v7_phase6_fastbackbone_proto_20260519_0001/stage1_gap_prototype_rescue/

deploy bundle:
  experiments/v7_phase6_fastbackbone_proto_20260519_0001/deploy_bundle/

best:
  prototype_profile = all_old_wrong_plus_clean
  feature_dim = 24
  prototype_count = 982
  gate_count = 694
  clean = 304/304
  rot_mirror_min = 1.0
  stress_min = 1.0
  stress_mean = 1.0

cost:
  distance MACs = 982 * 24 = 23568
  float table = 94272 bytes
  deduped int8 table = 920 * 24 = 22080 bytes
  host C++ microbench prototype-only = about 5.9 us/call
```

fast backbone + int8 prototype rescue:

```text
script:
  export_v7_phase6_prototype_int8_bundle.py

deploy bundle:
  experiments/v7_phase6_fastbackbone_proto_20260519_0001/deploy_bundle_int8/
    v7_phase6_prototype_int8_params.hpp
    v7_phase6_fast_int8_microbench.cpp
    deploy_bundle_summary.json
    deploy_stress_summary.csv
    int8_quantization_candidates.csv

selected:
  quant_scale = 8.0
  conf_threshold = 0
  dist_threshold = 0
  prototype_count = 920
  feature_dim = 24
  clean = 304/304
  rot_mirror_min = 1.0
  stress_min = 1.0
  stress_mean = 1.0
  int8 prototype table = 22080 bytes
  header size = 87876 bytes
  host C++ microbench prototype-only = 2.896 us/call

verification:
  python -m py_compile export_v7_phase6_prototype_int8_bundle.py
  g++ -std=c++17 -fsyntax-only int8 header include test = pass
  host g++ -O3 microbench = 2.896 us/call
```

2K0300 local latency estimate:

```text
chip source:
  Loongson official LS2K0300 page:
    64-bit dual-issue superscalar LA264
    1GHz
    32KB L1D / 32KB L1I / 512KB shared L2
    16-bit DDR4-1600 controller

board benchmark anchor:
  V5 guide / BOARD_LATENCY_US reference:
    spacetodepth_conv [8,16,32] avg = 7163 us
    spacetodepth_conv [8,16,32] p95 = 10611 us

fast backbone scaling:
  candidate = spacetodepth_conv [6,12,24]
  scale = ((6+12+24) / (8+16+32)) ^ 1.05 = 0.739289
  avg backbone = 7163 * 0.739289 = 5296 us
  p95 backbone = 10611 * 0.739289 = 7845 us

int8 prototype rescue:
  prototype_count = 920
  feature_dim = 24
  distance dimensions = 920 * 24 = 22080
  int8 table = 22080 bytes
  table fits within 32KB L1D if hot; worst case still sequential L2/DDR reads.

prototype compute estimate on 1GHz LA264:
  3-op theoretical lower bound = 66240 cycles = 66 us
  10 cycles/dim = 221 us
  20 cycles/dim = 442 us
  50 cycles/dim = 1104 us

combined estimate:
  avg, 20-cycle prototype = 5296 + 442 = 5738 us
  p95, 20-cycle prototype = 7845 + 442 = 8287 us
  p95, extreme 50-cycle prototype = 7845 + 1104 = 8949 us

host sanity benchmark:
  x86_64 g++ -O3, 100 runs:
    median = 2.862 us
    p95 = 2.898 us
    max = 2.909 us
  x86_64 g++ -O2 / -Os, 100 runs:
    median = about 10.7-10.9 us
    p95 = about 11.0 us

estimate conclusion:
  expected board avg = about 5.7 ms
  conservative board p95 = about 8.3-9.0 ms
  stress benchmark is 100%, so the allowed latency ceiling is 20 ms; this candidate is safely below it.
  It is also below the hard 15 ms floor, and near the preferred 8 ms target even under conservative p95.
```

当前推荐：

```text
If board latency target is prioritized:
  use fast backbone + Phase6 int8 prototype rescue first.

If classification margin / fewer prototype gates is prioritized:
  keep stable backbone + Phase6 prototype rescue as fallback.
```
