# V8 Pure Embedding Prototype Roadmap

更新日期：2026-05-21

本文是 V8 训练路线的自洽说明。目标读者不需要先读 V5/V6/V7 文档，也能理解：当前任务是什么、为什么 V7 走到 prototype rescue、为什么还需要 V8、V8 借鉴哪些论文思想、这些思想如何落地成真实训练和验证脚本。

## 1. 一句话结论

V8 不再把问题理解为“再训练一个 parent softmax 分类器”，而是训练一个专门服务于几何一致、局部可分、可压缩检索的 tiny embedding 模型：

```text
32x32 grayscale ROI
  -> tiny CNN / fast backbone
  -> normalized embedding
  -> subclass/cluster prototypes or learned proxies
  -> parent decision
```

部署时优先不使用传统 softmax parent head。训练时可以使用 proxy logits、teacher logits、margin loss、contrastive loss 等辅助信号，但最终推理路径以 embedding 距离和少量 prototype/proxy 为核心。

V8 的核心目标不是再次证明“查表可以 100%”，而是把 V7 Phase6 已经证明有效的 prototype rescue 思想，压缩成一个真正训练出来、几何一致、prototype 数量可控、int8 可部署的纯 embedding 系统。

2026-05-19 后的关键前提修正：当前数据集就是部署全集。因此 V8 的核心问题不再是“对未见 original 泛化”，而是“把一个有限但包含 clean/D4/stress 全部目标视图的闭集全集，编译成最小、最快、int8 稳定、可审计的模型”。这会改变先进思想的用法：true rebuild-LOO 是诊断工具，不是部署门槛；prototype selection、边界修复、量化稳定和训练-部署目标一致性才是主线。

2026-05-20 的工程约束补充：V8 不能只按训练正确率排序，最终导出的 TFLite encoder 必须优先落在本地已验证的推荐算子路径上。此前 V5 将模型约束到推荐算子后获得过数量级级别的性能收益；V8 如果在 embedding 尾部引入 `sqrt/div/reduce` 之类的非主线算子，即使 prototype 数量更少，也可能在板端抵消甚至反转收益。因此后续训练、导出和候选排序必须同时报告准确率、prototype 成本和 TFLite op audit。

2026-05-20 under2 结果更新：V8 已经得到一个板端保守估计 `<=2ms` 的闭集部署候选。它使用严格推荐算子 `SPACE_TO_DEPTH + CONV_2D + MAX_POOL_2D + MEAN + FULLY_CONNECTED` 的 tiny parent-logit encoder，把 int8 TFLite 输出的 3 维 logits 当作闭集检索 embedding，再用 residual exact-memory prototype 表补齐全集边界。当前最佳常规 stress 表为 `2124` 个 3D int8 prototypes，保守估计 `1710us`，clean / rot_mirror / stress 以及对应 int8 replay 全部 100%，且 `margin_min = int8_margin_min = 1`。合并 fixed stress + medium stress 后，同一路线为 `2659` 个 3D int8 prototypes，保守估计 `1742us`，30 个 view 组全部 100%。

2026-05-20 margin 后续实验：对上述 3D parent-logit residual memory 做 prototype 剪枝/合并后，能安全删掉 unused / duplicate 原型并小幅降低表大小，但不能把全局 `int8_margin_min` 从 `1` 抬高。低 margin 的 nearest-wrong prototypes 不是冗余项，它们同时是其他 query 的必要 correct prototypes；因此真正的 margin 修复应进入训练目标或 code 几何，而不是继续只做表后处理。

2026-05-20 stress 边界澄清：额外高强度 stress 的价值是诊断低 margin 是否会真实翻车、并导出可审计错例；它不应直接扩大正式训练分布。正式训练仍应以 clean + D4 +已定义 mild/fixed/medium stress 为部署全集，额外 stress 只用于发现哪些正常全集 view/code 区域需要更厚的 int8 margin。

2026-05-20 高压 boundary-repair 结果：在固定 baseline 选择集上，用 replay-gated greedy 防守 prototypes 可以把高压低组错误率从 `53.82%` 降到约 `46.1-46.3%`，control 从 `11.74%` 降到约 `10.16-10.25%`。primary 版本为 `2646` 个 3D prototypes，combined fixed+medium 版本为 `3172` 个 3D prototypes；两者正常 replay 仍 100%，保守板端估计约 `1741us` / `1773us`，仍在 `<=2ms`。但这不是最终 margin 根治：`int8_margin_min` 仍为 `1`，且候选 prototypes 来自高压诊断错例，不能当作独立泛化证明。它的结论是：小型 boundary repair table 有工程价值，但下一步仍应把这些 replay-confirmed conflict 转回训练侧 code geometry / fake-int8 margin / conflict separation，而不是无界吸收高压错例。

2026-05-20 论文思想落地更新：下一轮研究主线命名为 Quantized Large-Margin Prototype Learning。它把 GLVQ/LVQ 的 nearest-correct vs nearest-wrong margin、ArcFace/Sub-center 的多中心分离、Proxy Anchor 的稳定 proxy 训练、SupCon 的多视图正负样本、QAT/fake-quant 的部署一致性、submodular/set-cover 的 prototype 选择、kNN robustness 的邻域稳定性合成一个闭环：训练只用正常部署全集，loss 直接优化 int8 prototype margin，每轮导出真实 TFLite 后重新 compiler replay，以 `low_margin` 桶、`int8_margin_min`、prototype_count 和板端估时决定是否保留。

2026-05-20 推荐算子高压验证更新：运行时 L2 normalize / metric-normalization TFLite 分支会引入 `DIV/SQRT/SUM/MAXIMUM` 等非主线算子，且本地 int8 interpreter 已出现 `DIV` invoke failure，因此只能作为研究对照，不能进入主候选。改成 raw embedding 输出后，C4/8/16-D24 strict-op encoder 的 TFLite 图只含 `SPACE_TO_DEPTH + CONV_2D + MAX_POOL_2D + MEAN + FULLY_CONNECTED`（`DELEGATE` 仅为 interpreter 记录），正常 clean/D4/shadow-fixed replay 可保持 100%。该路线把高压 low 组错误率从 baseline `53.82%` 降到 `21.96%`，strong synthetic 训练 + 标准部署 gate 后进一步到 `20.54%`，control 降到 `4.97%`；但仍未达到 `<10%`，且 `2900 x 24` distance dims 的保守板端估计约 `4791us`，不满足 `<=2ms`。因此下一步不能继续把 24D/强增强当主线，而应在推荐算子下转向 C2/4/8 tiny backbone + 4/6/8/12D raw code 的 fake-int8 margin / conflict separation。

2026-05-20 超时候选保留规则：对“超时但几何很好看”的 strict-op 候选，不再按失败实验清理。当前保留清单写入 `experiments/v8_timeout_retained_candidates_20260520_0001/retained_candidates.csv`：C4/8/16-D24 strongraw-shadowgate 作为 upper-bound geometry teacher，C2/4/8-D16 PCA teacher 作为当前小骨干几何折中，C2/4/8-D12 PCA teacher 作为 compression near-miss，C2/4/8-D8 PCA teacher 仅作为维度瓶颈诊断；后续又补充 D6 normal-margin / union-stress table-shape 诊断、D7 source-margin side3 的轻微超时容量边界诊断、D7 sourcewinner all-7D 的轻微超时 source/orbit 容量边界诊断、D8 D24-PCA4 shared-head 的轻微超时容量边界诊断、C2/6/12-D8 multi-source qanchor 的超时但结构干净容量边界诊断、C2/6/12-D16 C248 source-block qanchor 的超时 source-block 容量边界诊断、C2/6/12-D4-k5 center-init 的首层感受野容量边界诊断、C2/6/12-D8 source-gate center-margin 的近门槛 source-choice 表征诊断，以及 C2/6/12-D8 source-cluster collapse penalty 的全 code source-choice 表征诊断。清理实验目录时应保留该 manifest 及 CSV 中引用的源实验目录。它们共同约束下一轮 QLMPL 的方向：不能把高压样本加入训练，也不能把超时结果当部署胜出；但可以把这些候选的 strict recommended-op 图、normal int8 replay 100%、高压错例分布和 margin 形态用于教师蒸馏、低维投影、fake-int8 margin target、teacher-source/orbit gate 与 conflict separation。

2026-05-21 保留清单补充：C2/6/12-D8 synthetic source-gate clean-row distillation 的 `5339 x 8` residual 表也被保留为超时诊断。它能让 clean/rotmirror/61-view normal synthetic stress replay 100%、`int8_margin_min=5`，但保守估时 `2748us`，高压为 `29.74% / 6.37%`，说明 synthetic source-choice 信号不能靠普通 source-gate CE 直接压进紧凑单表。后续 rank/center 版把高压推进到 `23.93% / 6.12%`，但仍需要 `5358 x 8`、保守 `2751us`；normal-only set-cover 也只能到 `2447-2581 x 8`、保守 `2285-2306us`，因此同样只作为超时 source-choice preserving 边界诊断保留。

2026-05-21 under2 source-pool 结论更新：当前有效部署 anchor 仍是 C2/6/12-D4 multiteacher compile，`967 x 4`、保守 `1971us`、normal clean/D4/fixed/medium stress int8 replay 100%、`int8_margin_min=4`，高压 `31.30% / 10.80%`，推荐算子仍只含 `SPACE_TO_DEPTH + CONV_2D + MAX_POOL_2D + MEAN + FULLY_CONNECTED`（`DELEGATE` 仅为 interpreter 记录）。source-gate CE、显式 source-gated table、hard-label/class-balanced source gate 均为负例；其中 balanced gate 甚至不能保持正常 int8 replay。新的 C248 五源 under2 池（D4 anchor、C2/4/8-D4、D8 qpair、D6/D7 source-margin）首次给出高压诊断 oracle `9.71% / 1.26%`，说明 under2 source 容量已经足够，但 label-free margin gate、normal routing table 和 local normal-neighborhood gate 仍停在约 `25.5-28.6%` low wrong。下一步方向因此从“继续增大 normal margin 或继续扫 gate 分桶”改为“在不使用高压训练的前提下，学习/压缩 source-orbit 决策本身”：normal-only 多源 embedding side code、预算感知二阶段表，或 compiler-feedback loss，用来惩罚把互补 source decision 折叠成单一 nearest-prototype 几何。

2026-05-21 source-gate qmargin/balance 追测：在 `train_v8_parent_classifier.py` 中加入 normal-only source-gate fake-int8 margin 与 source-prior balance loss 后，轻量设置能经 residual compile 补回 normal replay 100%，但表为 `840 x 7` / `2011us`，剪枝后最优仍约 `802-828 x 7` / `2006-2009us`，高压约 `30.81% / 11.30%`；显式 source-gated table 仍塌缩到 `[3,121,2619,3]`、`2260us`，高压 `30.62% / 11.13%`。强 qmargin/balance 设置在 raw int8 TFLite 阶段已破坏 normal replay（clean `99.67%`、rotmirror/stress min `92.11%`）。结论是：单纯增大 source-gate margin/balance 不是下一步；它证明当前 side dims 没学到可压缩 source decision，应继续转向预算感知 compiler/gate 或新的 source/orbit 表征。

2026-05-21 多源距离融合 / fusion-logit 追测：新增 `analyze_v8_multisource_distance_fusion.py` 直接在 C248 五源的 class-distance 上做 label-free 决策融合；全源 best 为 `sum_class_margin:margin_p90`，高压 `26.19% / 6.01%`，子集 best 为 `d8q+d7sm` softmax-distance fusion，高压 `24.51% / 7.71%`。这说明多源距离本身有有效互补性，但该融合需要运行所有 source class-distance，不能作为 under2 部署路径。随后用 `build_v8_source_logit_teacher.py --aggregate-mode class_margin_sum` 把 `d8q+d7sm` 的 normal-only class-margin 融合蒸馏回 D4 单头，compile 后可得到 `873 x 4`、保守 `1963us`、normal clean/D4/fixed/medium int8 replay 100%，但 `int8_margin_min=1` 且 canonical 高压为 `31.41% / 11.08%`。结论：不要把“多源融合有效”误解为“单头 logit 蒸馏有效”；下一步应保留显式 source/orbit 决策或让 compiler 参与预算约束，而不是继续平均/求和后压回一个 D4 nearest-prototype 空间。

2026-05-21 D4 margin-ceiling / guard 追测：在不使用高压训练样本的前提下，继续验证“增大 margin”这个首要目标。先把 D4 anchor 的 residual compile target 提到 `8/8`，结果 best 仍是 `967 x 4`、`int8_margin_min=4`，连 `exact_all` 也只有 `int8_margin_min=4`，说明当前 D4 code 的全局 normal margin 存在几何上限。C248 source-decision guard prototypes 的 best low 只到 `31.13% / 10.85%`（`1287 x 4`、保守 `1996us`、normal replay 100%），强 toward-wrong 则破坏 normal replay。进一步用 margin-ceiling dynamic qpair teacher（`407` normal events、`wrong_event_count=0`、`neighborhood_margin_min=-54`）训练后，compile 可得到 `897 x 4`、保守 `1965us`、normal int8 replay 100%，但 `int8_margin_min=3` 且 canonical 高压仍为 `31.17% / 10.97%`。结论：当前方向不能再理解为“给同一个 D4 nearest-prototype 空间继续加 residual / guard / scalar qpair margin”。真正的下一步应是预算感知 compiler 或显式二阶段结构，把 source/orbit conflict decision 在压缩前保留下来；或者换成能表达该决策的 normal-only 表征，再用推荐算子和 under2 门槛筛选。

2026-05-21 retained D12/D16 set-cover 压缩追测：新增 `select_v8_setcover_prototypes.py`，只用正常部署全集做 greedy set-cover 子表选择；高压仍只评估。它把 D12 PCA retained teacher 从 `5068` 压到 `2366 x 12`，normal int8 replay 100%、`int8_margin_min=9`，但保守估时仍 `2150us`，canonical 高压退到 `29.39% / 11.52%`；把 D16 PCA retained teacher 从 `4279` 压到 `2218 x 16`，normal int8 replay 100%、`int8_margin_min=9`，但保守估时仍 `2292us`，canonical 高压 `26.70% / 10.86%`。结论：D12/D16 retained 几何不是简单 dead-prototype 过多；正常安全子表压缩会丢掉 control 优势且仍过 `<=2ms`。这些 set-cover 行按“超时但有诊断价值”保留，但下一步不应继续 plain subset pruning，而应把 D16/D24 的几何蒸馏成更低维 source/orbit 表征或预算感知二阶段结构。

2026-05-21 retained D12/D16 center-set-cover 压缩追测：新增并修正 `select_v8_center_setcover_prototypes.py`，先验证了未过滤的 kmeans center / exact fallback 会作为新 wrong prototype 偷走其他 parent 的正常样本，导致 normal replay 只有 `98.36%` 左右；随后加入 normal-only cross-parent reject，禁止生成候选比原 retained 表 nearest-wrong 更靠近任何其他 parent 正常行。修正后 D12 `center_setcover_safe_k512_m8` 为 `2186 x 12`、normal replay 100%、`int8_margin_min=9`、保守 `2107us`，canonical 高压 `29.57% / 11.86%`；D16 `center_setcover_safe_k512_m8` 为 `2026 x 16`、normal replay 100%、`int8_margin_min=9`、保守 `2231us`，canonical 高压 `26.80% / 11.43%`。随后只用正常部署全集追测更大中心池与更低 set-cover margin：D12 `k1024/k2048, m0` 为 `2117/2170 x 12`、保守 `2091/2103us`、`int8_margin_min=1`；D16 `k1024/k2048, m0` 为 `1968/2018 x 16`、保守 `2212/2228us`、`int8_margin_min=1`。结论：generated center 可以小幅压表，但放大中心池或降 margin target 都不能达成保守 `<=2ms`，还会把 normal margin 变薄；该方向只作为“超时但有诊断价值”保留，不应继续扩 kmeans center 网格。

2026-05-21 source-gate center-margin 追测：在 `train_v8_parent_classifier.py` 中新增默认关闭的 `--source-gate-center-*` loss，只用正常部署全集的 `sample_correct_count` source-gate teacher，在 fake-int8 `code[3:8]` 空间做 per-source center separation。`w010_qm0005_b010_cw001_t16` 的 center loss 下降，但 center margin mean 仍为负；compile/prune 可闭合 normal int8 replay 100%，最小干净表为 `803 x 8`、保守 `2022us`、`int8_margin_min=4`，canonical 高压 `31.00% / 11.58%`。按“超时但好看候选”保留该行，但结论是：当前 side-code 即使加 center separation 仍没有学到可压缩 source choice，下一步不应继续加同类 source-gate CE/qmargin/center 标量，而应转向显式二阶段/预算感知 compiler loss 或新的 normal-only source-orbit 表征。

2026-05-21 source-cluster collapse penalty 追测：在 `train_v8_parent_classifier.py` 中新增默认关闭的 `--source-cluster-*` loss，只用正常部署全集的 `sample_correct_count` source-gate teacher，在 full 8D fake-int8 code 里分离同一 parent 下的 `(parent, source_label)` cluster。`w010_qm0005_b010_scw0005_t64` 的 loss 下降（`10.2441 -> 9.8975`），但 cluster margin mean 仍强负（`-375.42 -> -350.96`）；normal-only compile/prune 可闭合 normal int8 replay 100%，最小干净表为 `823 x 8`、保守 `2025us`、`int8_margin_min=4`，canonical 高压 `31.00% / 11.43%`。按“超时但好看候选”保留该行，但结论不变：把 source choice 折回单一最近原型 code 后仍无法迁移到高压鲁棒性；下一步应停止同类 scalar source separation 加权，转向显式事件级 source/orbit gate、预算感知二阶段表，或 compiler/prototype 合并阶段的 source-choice collapse 约束。

2026-05-21 source-cluster set-cover 压缩追测：为验证上述“超时但好看”表是否只是 prototype 冗余过多，复用 `select_v8_setcover_prototypes.py` 对 `823 x 8` source-cluster 表做 normal-only 子表选择，高压仍只作最终复核。`m4` 目标对 parent 0 有 1 个正常行不可覆盖；`setcover_m2/m3` 均能保持 normal clean/D4/fixed/medium stress int8 replay 100%、`int8_margin_min=4`，表压到 `250/248 x 8`，保守估时约 `1933us`，严格推荐算子图不变。但 canonical 高压为 `31.12% / 12.25%` 与 `31.17% / 12.29%`，control 比原 source-cluster 的 `11.43%` 明显退化。结论：source-cluster 表可以被预算化，但 normal-only set-cover 会删掉高压 source-choice 形态需要的原型；这不是部署晋升，后续不应继续 plain subset compression，而应做 source-choice preserving selection 或换事件级 source/orbit 表征。

2026-05-21 source-choice preserving set-cover 追测：新增 `select_v8_sourcechoice_setcover_prototypes.py` 和 `select_v8_sourcechoice_anchor_setcover.py`，继续只用 normal deployment rows 与 normal-only `sample_correct_count` source teacher，高压仍只作最终评估。严格要求每个正常行由同 `(parent, source_label)` 原型覆盖时，即使 `parent_margin=0/source_margin=0` 也有 `223` 个 parent-0/source-0 正常行不可覆盖，说明当前 D8 source-cluster code 的 source choice 已经塌缩；退而改成 parent set-cover 加同源 usage anchors 后，`pm3_anchor16/32/48` 均保持 normal replay 100%、`int8_margin_min=4`，保守估时为 `1964/1984/1999us`，canonical 高压为 `30.98% / 12.07%`、`30.93% / 11.86%`、`30.96% / 11.74%`。结论：bounded source-choice anchors 可以追回 plain set-cover 的一部分 control 损失，但最大 under2 行仍不如原 `823 x 8` 超时表的 `11.43%` control，也远离 `<10%` 目标；后续不应继续在已塌缩的 D8 code 上做 subset/anchor 选择，而应在训练或 compiler 目标中先让 source/orbit decision 可分。

2026-05-21 normal source-policy transfer 追测：新增 `analyze_v8_normal_source_policy_transfer.py`，不用训练小 gate，而是直接把五源 under2 pool 在 normal deployment rows 上得到的 source winner / sample 聚合 / sample-family 聚合策略按 `(sample_index, view_label)` 或 sample key 冻结迁移到同 sample 的高压事件；高压仍只评估，且该 sample-key policy 明确不可部署。最佳 row-level normal winner transfer 为 `26.17% / 9.90%`，`sample_family_min` 为 `28.35% / 11.14%`，`sample_correct_count` 为 `30.38% / 10.47%`。结论：瓶颈不只是“小 gate 学不会 normal source label”，normal source label 自身也不能把 low 组迁移到 `<10%`；后续不要继续扩 sample/family source-label 路由表，应构造不依赖高压标签的 event-level robustness signal，或在表示/编译阶段直接约束 source-choice collapse。

2026-05-21 source-choice pair teacher 追测：新增 `build_v8_source_choice_pair_teacher.py`，只用正常部署全集的 C248 source label 与 reference-code 近邻构造 same-source / different-source 动态 pair，再叠加原 normal-only multiteacher qpair cache；高压仍只作最终评估。`sourcepair_w5e5_light_e120` 训练后 raw int8 normal replay 不闭合，但 normal-only compile/prune 可得到一个“超时但好看”的 `755 x 8` 表：保守 `2014us`、normal int8 replay 100%、`int8_margin_min=6`，canonical 高压 `30.71% / 11.87%`，已写入 timeout retained manifest。对同一分支做 normal-only set-cover 可压到严格 under2 的 `230 x 8`、保守 `1930us`、`int8_margin_min=4`，但高压退到 `31.55% / 12.34%`。结论：source-pair 监督可以增厚 normal margin，却仍没有让 source choice 在单一 D8 nearest-prototype code 中可迁移；不要继续只加 source-pair 权重或 plain subset compression。

2026-05-21 robust-label learned-guard 追测：`build_v8_learned_gate_guard_teacher.py` 新增 `--label-mode`，可用 normal-only `sample_correct_count` / `sample_family_min` 等 robust source label 构造 learned-gate guard teacher；高压仍只作最终评估。`sample_correct_count` 最佳 normal-safe guard 为 `1159 x 4`、约 `1986us`、高压 `31.23% / 10.81%`；`sample_family_min` 边缘细扫最佳为 `1287 x 4`、约 `1996us`、高压 `30.97% / 10.86%`；加入 normal view-family feature 后为 `1159 x 4`、约 `1986us`、高压 `31.01% / 10.70%`。结论：robust normal label 与 view-family context 只能带来很小的局部 fix/break 净收益，仍不能把 C248 event-level oracle 压成 D4 local guard；后续不要继续改 label 聚合或堆 local guards。

2026-05-21 D4 orbit + VICReg anti-collapse 追测：在 `train_v8_parent_classifier.py` 中新增默认关闭的 `--vicreg-*` fake-int8 variance / decorrelation loss，配合已有 `orbit_consistency` 验证“同一样本多视图拉近时防止 code 维度塌缩”是否能厚化 margin；该 loss 只影响训练，不增加推理算子，高压样本仍只作最终评估。`orbit_vicreg_f24_e80` 从当前 C2/6/12-D4 multiteacher anchor 续训 80 epoch，TFLite 图仍只含推荐算子；normal-only compile 后 best 为 `898 x 4`、保守 `1965us`、normal 29-view int8 replay 100%、`int8_margin_min=3`，canonical 高压 `31.25% / 11.21%`。结论：VICReg anti-collapse 能形成一个干净 under2 表，但没有把错误率推出 `31% / 11%` 平台；后续不要继续扫 D4 orbit/VICReg 标量权重，应继续转向 source/orbit decision 保留或预算感知二阶段结构。

2026-05-21 C248 source-decision preserving prune 追测：`prune_merge_v8_logit_prototypes.py` 新增默认关闭的 `--source-decision-preserve`，在 normal replay / int8 margin 接受条件之外，要求剪枝/合并候选不增加 active normal source-decision rows <= target，也不降低 active source-decision min margin。用 C248 source-decision compiler 的最小近门槛表 `c248pool_m32_compile` 做静态覆盖诊断：`2533 x 4` 表中只有 `5` 个 prototype 无 correct-normal 使用、无 low-margin wrong 使用、也不参与 source-decision risk；source-decision 保护下的 keep 下界是 `2528 / 2533`。smoke 运行 `m32_preserve_smoke` 也只删掉这 `5` 个，得到 `2528 x 4`、normal replay 100%、`int8_margin_min=4`、source-decision <= target 仍 `96`、保守仍 `2096us`，不能进 `<=2ms`。结论：C248 source-decision residual 表本身已经被 normal/source-decision 覆盖得很密，保护式 prune/merge 不能把它压回预算；后续不要继续把 source-decision 先膨胀成 D4 residual 大表再期望后处理剪回 under2，应在表示/二阶段结构里预先预算 source/orbit decision。

2026-05-21 hybrid 推荐算子迁移追测：为回应“推荐算子”约束下的预算空间问题，把当前 C2/6/12-D4 multiteacher 配方迁移到 `spacetodepth_hybrid`（首层 `CONV_2D`，后两层 `DEPTHWISE_CONV_2D + 1x1 CONV_2D`），并从当前 D4 anchor partial-init 续训 `160` epoch；训练仍只使用 normal deployment views 与 normal-only dynamic qpair teacher，高压不参与。该结构的 backbone 保守估时约 `1465us`，理论上能释放大量 prototype 预算，但实际在训练端直接 collapse：clean / rotmirror / normal stress / fixed-medium stress 以及 int8 replay 都停在 `34.21%`，无法进入 normal compile 或高压评估。随后复查已有 CE 训练的 `hyb_c2_6_12_s20260933`：用完整 29-view normal stress 编译 residual table，即使 `4665 x 3` 仍只有 int8 clean `99.67%`、stress min `97.70%`，`exact_all 9120 x 3` 也不能闭合，说明 3D hybrid parent-logit int8 code 已有跨类碰撞。进一步用当前 D4 multiteacher int8 code 作 normal-only qanchor teacher，训练 `hyb_c2_6_12_d4_qanchor_w0005_e200`，qanchor loss 虽从 `2543.90` 降到 `2447.27`，但 clean / rotmirror / normal stress / fixed-medium stress 及 int8 replay 仍全部塌到 `31.58%`。结论：hybrid 推荐算子不是当前 D4 multiteacher anchor 的可直接替换加速器，也不能靠更大 D3 residual 表或简单 D4 qanchor 蒸馏补救；若重启该架构，需要先做 depthwise blocks 的专门 pretraining / distillation，或直接输出可压缩的 source/orbit 表征。

2026-05-20 D16 / qcompiled 结果更新：C2/4/8-D16 PCA teacher 分支是当前小骨干中较好的几何折中，normal int8 replay 100%、`int8_margin_min=9`，高压 low/control 为 `24.54% / 8.85%`，但 `4279 x 16` 的保守估时约 `2952us`，仍不满足 `<=2ms`。直接把 C1/2/4-D16 当加速替代会破坏正常 replay，且高压退化到 `48.81% / 21.04%`。新增的 global fake-int8 compiled-teacher margin loss 能在训练日志中把 qcompiled margin mean 拉高到约 `244`，但最终高压反而退化到 `31.62% / 11.37%`，prototype 表也增大到 `4609 x 16`；因此 qcompiled 不能按全局 teacher-pull 使用，后续若继续使用 fake-int8 margin，必须改成 replay-confirmed conflict-pair / low-risk-region selective separation，或与 prototype-count 目标联合约束，而不是全样本拉向 projected teacher prototypes。

2026-05-20 selective qcompiled / stress compaction 结果更新：新增 `build_v8_residual_risk_teacher.py`，从正常全集 residual compiler 的 clean/clean_rotmirror base 失败行导出 risk teacher，明确不使用高压评估样本。D12 normal residual-risk teacher 有 `2636` 行；用 `qcompiled_weight_mode=low_margin_only` 做 selective fake-int8 margin 后，训练侧 risk qcompiled margin mean 可拉到约 `258`，normal int8 replay 仍 100%、`int8_margin_min=9`，表略降到 `4797 x 12`、保守估时 `2734us`，但高压 low/control 退化到 `33.88% / 10.26%`。D8 单纯把 normal stress consistency 加强到 `lambda_stress=4.0` 也未把 stress views 收敛到 clean/D4 表：`exact_clean_rotmirror` 虽在 `1972us` 下但 normal stress_min 只有 `67.76%`，residual 100% 表反而变成 `5703 x 8`、保守估时 `2495us`，高压 low/control `35.88% / 11.45%`。结论：当前失败点不是“qcompiled 只需选择性开关”或“单纯加大 stress pull”能解决；下一步必须转向预算感知的 prototype/compiler 联合目标、D4/shift 等变结构或 top-k/分层检索 ablation，在 normal replay 100% 前提下降低需要 residual 记忆的视图数量。

2026-05-20 top-k per-parent 后处理消融：在不重训、不把高压样本加入训练的前提下，对 D16/D12/D8 PCA teacher 与 D24 strongraw teacher 做 per-parent mean/kth top-k 聚合（`k=2/3/5/8`）。结论是简单 top-k 不能作为下一步主线：除 `k=1` 基线外，所有 `k>1` 都破坏正常全集 int8 replay 100%，并且 high-pressure low 组错误率全部高于对应基线。D24 strongraw 的 `mean k=2` 虽把 control 从 `4.97%` 小幅降到 `4.44%`，但 normal replay 已失败且 low 从 `20.54%` 升到 `21.59%`；D16/D12/D8 也呈现相同趋势。因此若继续做分层检索，不能是直接替换 score 聚合，而必须是以 `k=1` 为主决策、只处理 replay-confirmed tie/conflict 的预算感知二级过滤。

2026-05-20 D6 / qpair conflict 结果更新：补齐 D24 strongraw teacher 的 PCA4/PCA6 投影，并新增 `build_v8_pair_margin_teacher.py` 与训练侧 `qpair` fixed nearest-correct/nearest-wrong int8 pair loss。D6 PCA teacher 分支在 normal replay 上需要 `5809 x 6` residual 表，`int8_margin_min=5`，保守估时 `2280us`，高压 low/control 为 `36.45% / 11.12%`，说明 D6 不是 D8 的 under2 替代。PCA8 clean+D4 base 的 normal-only pair teacher 有 `1146` 个风险视图，其中 `1098` 个在 base 表下已错；D8 qpair 可把 normal 100 表从 D8 baseline 的 `5356 x 8` 降到 `4961 x 8`、保守估时从 `2439us` 降到 `2376us`，高压 low 从 `34.95%` 降到 `29.04%`，但 control 仍约 `11.15%`，离 `<10%` 和 `<=2ms` 都不够。提高 qpair 权重并加大 norm anchor 后反而退化为 `5242 x 8`、`2421us`、高压 `31.17% / 12.77%`。结论：fixed-pair conflict separation 有局部收益，但单独不足以解决目标；下一步若继续，应把 pair loss 做成预算感知/原型选择联合目标，而不是继续加大 qpair 权重。

2026-05-20 D3 parent-logit qpair 追测：把同一类 fixed nearest-correct/nearest-wrong int8 pair loss 移植到 `train_v8_parent_classifier.py`，并用 combined-pruned normal 表构造 `420` 个低 margin pair 事件，仍然不使用高压样本训练。较强设置 `qpair_margin_weight=0.02,target=8` 会把 3D int8 code 推到跨类碰撞：normal int8 replay 不再 100%，`int8_margin_min=0`，高压 low/control 仅为 `53.59% / 12.76%`。较弱设置 `qpair_margin_weight=0.005,target=4` 能保住 clean/D4/fixed/medium 30 视图 float/int8 replay 100%，`2927 x 3`、保守估时 `1758us`、`int8_margin_min=1`，但高压 low/control 只有 `52.06% / 11.49%`，且表比 combined-pruned `2574 x 3` 更大。结论：D3 parent logits 的 normal-only pair margin 只能产生很小的高压收益，不能把 margin 根治到 `<10%`；继续调 D3 qpair 权重不是主线，应把 D3 当 under2 控制组，转向 4/6/8/12D raw code 的预算感知 conflict separation / compiler 联合目标。

2026-05-20 D4 raw / parent-code 追测：C2/4/8-D4 PCA teacher 分支保持 strict recommended ops，但 normal replay 不能闭合到 100%（best residual `7539 x 4`，clean 100%、rotmirror/stress/int8 stress min `99.67%`，`int8_margin_min=0`），高压 low/control 为 `46.14% / 21.64%`，因此不能作为 D8/D12 的压缩替代。另一路把 D3 parent-logit code 扩成带 parent offset 的 D4 teacher，并用 normal-only qanchor 蒸馏；compile 后可得到 `2572 x 4`、保守估时 `1788us`、clean/D4/fixed/medium stress int8 replay 100%、`int8_margin_min=2` 的 under2 表，但高压 low/control 仍为 `52.65% / 12.23%`。结论：单纯给 3D parent logits 增加 parent-coded 第 4 维可以稍微增厚闭集 margin，却没有产生鲁棒几何；D4 后续只有在 true raw-code 训练、D4 orbit/neighborhood margin 与预算感知 compiler 联动下才值得继续，不应继续加大 qanchor。

2026-05-20 C2/6/12-D4 qpair/neighborhood 追测：用 C2/6/12-D3 near-miss 生成 parent-code D4 teacher，再从正常全集 replay 导出 fixed nearest-correct / nearest-wrong / neighborhood margin 事件，仍然不使用高压样本训练。qanchor baseline 可得到 `1260 x 4`、保守估时 `1994us`、normal int8 replay 100%、`int8_margin_min=3`，高压 low/control 为 `32.67% / 11.93%`，是当前 strict-op under2 候选中较好的高压 low 结果，但已经贴近 `<=2ms` 上限。继续加入 qpair/neighborhood loss 后，best 表缩到 `1153 x 4`、保守估时 `1986us`、normal int8 replay 100%、`int8_margin_min=3`，高压 low/control 为 `32.32% / 12.27%`。结论：normal-only qpair/neighborhood 可以小幅压表和略降 low 组，但没有降低 control，也没有把错误率推向 `<10%`；它应保留为 under2 控制组和 compiler-teacher cache 的验证样本，而不是继续单独加权 qpair。

2026-05-20 dynamic qpair 追测：把 `build_v8_pair_margin_teacher.py` 扩展为同时保存 query / nearest-correct / nearest-wrong 的样本与 prototype 索引，并在 `train_v8_end_to_end_embedding.py`、`train_v8_parent_classifier.py` 中加入 normal-only dynamic qpair margin loss，使 loss 在当前 code/fake-int8 code 上实时计算，而不是只拉固定 teacher 坐标。C2/6/12-D4 qanchor 表导出的 dynamic teacher 有 `636` 个正常全集事件、`wrong_event_count=0`、`true_margin_min=3`、`neighborhood_margin_min=-43`，没有使用高压样本。raw D4 的 qproxy / dynamic qpair 分支均为负例：正常 replay 或 `<=2ms` 失败，且高压退化到约 `42-44% / 17-19%`。parent-code D4 dynamic qpair 的 `weight=0.0004,target=32` 是当前较好的 deployable under2 控制组：`942 x 4`、保守估时 `1969us`、normal clean/D4/fixed/medium int8 replay 100%、`int8_margin_min=2`，高压 low/control 为 `31.91% / 11.56%`。但 fixed+dynamic mix 退化到 `32.21% / 11.92%`，更强 `weight=0.0008` 虽把 `int8_margin_min` 拉到 `3`，高压仍为 `32.21% / 12.01%`。结论：dynamic qpair 比 fixed qpair 稍好，但简单加权已经平台化；下一步不能继续只调 pair loss，而要把 normal-only conflict 信号嵌入预算感知 compiler/原型选择联合目标或 D4/orbit 等变结构。

2026-05-20 teacher-oracle / top-k / orbit 追测：对当前 under2 best `v8_parent_c2612_d4_dynqpair_20260520_0001` 做高压错例结构审计。D4 dynamic 本身高压 overall wrong 为 `21.73%`；D24 strongraw teacher 为 `12.75%`，D16 PCA 为 `16.69%`，D12 PCA 为 `18.42%`。同一批高压事件上，D4 dynamic + D24/D16/D12 retained teachers 的 oracle union 只剩 overall `3.04%`、low `5.44%`、control `0.64%`，说明 retained teachers 的几何互补性足够强，但还没有被压缩进 under2 单模型。直接在 D4 dynamic 表上做 gated mean/kth top-k 二级评分不是解法：所有保持 normal replay 100% 的设置都只在 low `31.86-31.96%` 附近波动。新增 normal-only `orbit_consistency` 训练项后，可比的 29-view 正常全集下，`orbit5` 得到 `917 x 4`、`1967us`、normal int8 replay 100%、`int8_margin_min=3`、高压 `31.68% / 11.53%`；`orbit4` 得到 `869 x 4`、`1963us`、normal int8 replay 100%、`int8_margin_min=2`、高压 `31.84% / 11.47%`。结论：orbit consistency 能小幅压表、提高局部 margin、微降高压 overall，但仍是小修小补，远离 `<10%`；下一步主线应转向把 retained teachers 的互补几何用 normal-only multi-teacher distillation / teacher-agreement conflict separation 压缩到单个 strict-op encoder，而不是继续局部调 qpair/orbit/top-k。

## 2. 项目任务与隐含前提

### 2.1 输入与部署约束

当前模型任务是对 32x32 grayscale ROI 做视觉分类，并最终输出 3 个 parent 类之一：

```text
parent classes:
  supplies
  vehicle
  weapon
```

训练数据还带有 8 个 visual subclass，它们映射到 3 个 parent：

```text
VISUAL_TO_PARENT = [0, 0, 1, 1, 2, 2, 2, 2]
```

这意味着 parent 是最终部署指标，subclass 主要是结构化监督、原型分组、候选边界挖掘和 prototype 压缩的依据。

部署环境是小模型 / TFLite Micro / int8 倾向的板端环境。不能用两个完整 CNN 串行推理来做 ensemble，也不能无限扩大 backbone。主机端训练可以很重，但板端推理必须简单、确定、可审计。

### 2.2 正确率指标不等于 clean accuracy

当前不能只看 clean 304 张图。必须同时看：

```text
clean:
  原始 304 张样本

rot_mirror:
  D4 风格的 8 视图，包含 4 个旋转和水平镜像组合

stress:
  blur / noise / camera-like blur-noise 等固定压力视图
```

rot_mirror 在训练中必须作为必选项，而不是 optional augmentation。原因是本任务的 parent 标签理论上应对旋转和镜像保持不变；如果 clean 最高而 rot/mirror 下降，说明模型学到的是有限数据下的视角捷径、校准偏置或量化边界，而不是严格几何一致表示。

### 2.3 为什么 clean 经常最高

尽管理论上 clean 与 rot_mirror 是等价标签，训练结果仍可能 clean 最高，原因包括：

- 数据增强只是采样约束，不是数学上的等变/不变结构；CNN 不会自动保证 D4 一致。
- early stopping、calibration、候选排序往往更偏向 clean 或 clean-like validation。
- 小模型容量有限，可能优先拟合原始拍摄方向的局部纹理。
- int8 calibration 如果主要来自 clean，会让 clean 分布的边界更稳。
- rot/mirror 样本虽然被输入训练，但它们在 batch、loss 权重、hard mining 中未必与 clean 完全对等。
- softmax head 学的是全局线性边界；如果数据中存在局部边界冲突，可能被大多数区域牵制。

V8 因此要把几何一致性写进训练目标和验证门槛，而不仅是“加入增强”。

### 2.4 不预设 hard case

V8 不应先验性假设“必然存在 hard case”。`hard`、`rescue`、`preserve` 只应是从当前模型行为、per-view replay、prototype margin 和错误归因中观测到的诊断标签，而不是数据的内在真理。

默认训练应从不带 hard 假设的 A0/A1 开始：

```text
start:
  parent/subclass labels
  clean + D4 + stress consistency
  compressed prototype objective

only enable dynamic hard weighting if:
  replay shows stable cross-parent confusion
  nearest wrong margin is small
  old/CTD/V7 rescue disagreement is reproducible across views
  no-self/LOO reveals systematic failure clusters
```

如果 A0/A1 表明不存在需要特殊处理的边界，V8 应允许 embedding 自然压缩，而不是强行保留 rescue/preserve/hard 子结构。

## 3. V5/V6/V7 已知事实

### 3.1 旧 stable 与 CTD rescue 的互补性

旧 stable 模型是当前最强稳定基线之一。V7 记录的关键点：

```text
old stable:
  clean parent = 295/304
  stable_both_correct = 269/269
  preserve_old_correct_ctd_wrong = 26/26
  rescue_old_wrong_ctd_correct = 0/9
  hard clean = 10/12
  C4 = 6/6

old CTD rescue:
  clean parent = 278/304
  stable_both_correct = 269/269
  preserve_old_correct_ctd_wrong = 0/26
  rescue_old_wrong_ctd_correct = 9/9
  hard clean = 11/12
  C4 = 5/6

old stable + old CTD oracle:
  clean parent = 304/304
```

当前观测结论：在这组旧模型上，CTD/rescue 表现为局部 correction，而不是全局替代 stable。直接使用 CTD 会破坏当前观测到的 preserve 区域；直接训练新 rescue 又容易把当前 stable 区拖坏。这是本轮实验证据，不是对未来 V8 embedding 空间的先验结构假设。

### 3.2 参数合并为什么没有直接解决

用户提出过一个合理方向：参数相似的部分合并，参数差异大的部分用 MoE 或类似分流方式处理。

这个方向本身正确，但在当前 old stable 与 old CTD 上有硬约束：

- old/CTD 的量化权重方向差异很大。
- 共享卷积层和 parent head 的 cosine 接近 0。
- CTD parent head 形状与 GAP 通道存在明显差异，V7 记录过 `3 x 36` parent head 和 `12` 个 dead GAP channels。
- 直接 weight soup / 线性插值只适合相近 basin 的模型；当前不是这个条件。

V7 已经把高级合并思想尝试过一部分：

- 权重反量化后比较 cosine/L2/sign agreement。
- 用 stable 坐标系保护 stable 区域。
- 尝试 TIES-like sparse delta，把 rescue correction 作为局部参数增量。
- 做 frozen GAP + low-rank/linear rescue adapter + CTD-distilled router。

结果是：保护性还可以，但表达力和路由不足。单纯 GAP adapter / delta merge 没有稳定救全 9 个 rescue 样本，也没有解决 rot_mirror/stress 一致性。

V8 的转向：不再优先在原始 weight space 强行合并两个已观测冲突的边界，而是在 embedding space 学一个可压缩、可验证的度量空间。参数合并思想仍保留为备选，但主路线变成“共享表示 + prototype/proxy 决策”。如果 V8 训练后不再出现这些冲突区域，应允许它们自然消失。

### 3.3 V7 Phase6 已证明 prototype 可救

V7 Phase6 的关键发现：

纯 GAP prototype classifier 可实现 deterministic benchmark 上 100%，但需要把所有目标 view/stress 都收入 prototype 表：

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

这说明 prototype 技术路线可行，但也暴露问题：如果靠 all_views 原型记忆，每增加 stress/view 就增加表规模，泛化也依赖覆盖面。

当前最可部署的 V7 候选是 fast backbone + Phase6 int8 prototype rescue：

```text
backbone:
  p100_near_anchor_s2_deploy_balance_v6_fast_augment_
  estimated_board_us = 5296
  feature_dim = 24

selected int8 prototype rescue:
  prototype_count = 920
  quant_scale = 8.0
  clean = 304/304
  rot_mirror_min = 1.0
  stress_min = 1.0
  stress_mean = 1.0
  gate_count = 694
  estimated_distance_macs = 22080
  estimated_int8_table_bytes = 22080
  host C++ prototype-only microbench = 2.896 us/call
  estimated board avg = about 5.7 ms
  conservative board p95 = about 8.3-9.0 ms
```

V8 必须继承这条证据：prototype/proxy 决策对当前任务有效。但 V8 的成功标准更高：不能只是冻结旧 backbone 后加大原型表，而是训练出更适合 prototype 压缩和几何一致的 embedding。

### 3.4 V8-B 早期实验证据更新

2026-05-19 的 V8 Phase B end-to-end embedding sweep 已经给出一个重要信号：V8 不应被归类为失败路线，而应被视为 early proof-of-signal passed、compression/objective closure 未完成。

当前最强完整配置：

```text
run:
  experiments/v8_phaseB_parallel16_gpu_20260519_0001/
  stageB_parallel16_end_to_end_embedding/c6_12_24_d24_s20260519

backbone:
  spacetodepth_conv [6,12,24]
  embedding_dim = 24

prototype:
  kmeans
  k_per_subclass = 16
  prototype_count = 128
  distance_macs = 3072

result:
  clean = 304/304 = 1.0
  rot_mirror_min = 0.983553
  stress_min = 0.983553
  fixed_stress_min = 0.993421
  int8_stress_min = 0.983553
  float wrong = 22 / 6688
  int8 wrong = 22 / 6688
```

这说明：

- 32x32 raw ROI -> tiny embedding -> prototype parent decision 是有效方向。
- 失败主因不是 int8 量化，也不是 clean 不可分；clean 已经全对。
- 错误高度集中在 D4/rot_mirror 低 margin 边界，尤其 `rot90`、`rot270`、`mirror_lr_rot90`、`mirror_lr_rot270`。
- 现有训练目标和最终 nearest-prototype parent decision 尚未完全对齐；subclass proxy 准确率接近满分，但 parent prototype margin 仍偏薄。

对同一个 embedding 做离线高 K prototype sweep 的临时诊断显示：

```text
c6_12_24_d24 embedding, kmeans:
  k=16   prototypes=128   stress_min=0.983553
  k=24   prototypes=192   stress_min=0.990132
  k=32   prototypes=256   stress_min=0.993421
  k=48   prototypes=384   stress_min=0.996711
  k=96   prototypes=768   float_stress_min=0.996711, int8_stress_min=1.0
  k=192  prototypes=1476  float/closed/int8 stress_min=1.0 in closed replay
```

因此当前 V8-B 的主要缺口不是 embedding 中没有信息，而是：

```text
1. prototype cap 只扫到 k<=16，压缩搜索提前截断；
2. prototype selection 还没有 reserved boundary centers / coreset 策略；
3. 训练 loss 没有直接优化 true-parent vs nearest-wrong-parent prototype margin；
4. kmeans center 的现有 strict_LOO 不是 rebuild-LOO，不能作为最终泛化证据。
```

重要评估修正：当前 `evaluate_v8_embedding_prototypes.py` 对 kmeans center 使用 `prototype_sample_index = -1`，所以 `strict_loo_*` 只能排除 medoid 这种真实样本 prototype；对 kmeans center，它不能移除 hold-out 原图对 centroid 的贡献。若要讨论 unseen-original 泛化，必须增加 true rebuild leave-one-original-out：每次 hold out 一个 original，重新聚类/重建 prototype table，再评估该 original 的所有 clean/D4/stress views。当前全集部署选择只把 true rebuild-LOO 作为 top candidate 诊断。

当前最小下一步不应是继续盲目 16 路随机训练，而是：

```text
V8-C0 diagnostic:
  extended prototype budget curve on existing top embeddings
  k = 16,24,32,48,64,96,128,192
  sources = kmeans, medoid, k-center/coreset, quant-stable medoid, boundary-reserved hybrid
  eval = closed, no-self-view, true rebuild-LOO top candidates, int8 replay

V8-B2 training:
  add parent prototype margin loss
  add replay-confirmed dynamic hard mining
  add late fake-quant / int8 distance margin
  use [6,12,24] as method-control, then compress to [5,10,20] / [4,8,16]

resource schedule:
  GPU is the primary bottleneck and is much faster than CPU for end-to-end embedding training.
  keep GPU-fed Phase B/B2 training heavier than CPU diagnostics
  run CPU prototype sweeps only as low-concurrency side work after GPU memory/utilization are stable
  do not let true rebuild-LOO or large CPU sweeps starve TensorFlow GPU workers
```

### 3.5 true rebuild-LOO 修正后的证据

2026-05-19 后续修正了评估口径。补充约束：当前任务的数据集就是部署全集，因此 V8 的主目标不是 unseen-original 泛化，而是全集 closed-set deterministic 100%、int8 100%、prototype/latency 可部署。

- `strict_loo_*` 旧列保留为兼容输出，但标记为 `deprecated_sample_exclusion_approx`。
- 对 kmeans / centroid，若讨论 unseen-original 泛化，必须看 `true_rebuild_loo_*`：每次 hold out 一个 original 的所有 views，用剩余 originals 重建 prototype table，再评估 hold-out original 的 clean/D4/stress views。
- 但在当前全集部署设定下，`true_rebuild_loo_*` 不是 winner gate；它只用于诊断哪些 originals/views 是几何边界、是否需要 reserved boundary prototypes 或 closed-set margin 修复。
- `run_v8_extended_prototype_sweep.py` 与 `train_v8_end_to_end_embedding.py` 支持 `--true-loo-top`，输出 `true_rebuild_loo_top.csv` 和 `true_rebuild_loo_events.csv`。

当前最强 closed-set 候选：

```text
run:
  experiments/v8_phaseB_focus_tf_function_20260519_0003/
  c6_12_24_d24_s20260522

closed / approx:
  source = kmeans
  k_per_subclass = 64
  prototype_count = 512
  clean = 1.0
  stress_min = 0.996711
  int8_stress_min = 0.996711
  wrong views = 4 / 6688
```

true rebuild-LOO 显示它还不是 unseen-original 泛化闭环：

```text
diagnostic:
  experiments/v8_true_rebuild_loo_c6_12_24_d24_s20260522_20260519_0001

best true rebuild-LOO:
  source = kmeans
  k_per_subclass = 64
  prototype_count = 512
  true_rebuild_loo_clean = 0.986842
  true_rebuild_loo_stress_min = 0.950658
  true_rebuild_loo_int8_stress_min = 0.947368
  true_rebuild_loo_fixed_stress_min = 0.970395
  true_rebuild_loo events = 176 views, 51 unique originals
```

对比 `[5,10,20] d24 seed20260523`：

```text
closed:
  kmeans k=96, prototypes=768
  stress_min = 0.993421
  int8_stress_min = 0.993421

true rebuild-LOO:
  true_rebuild_loo_stress_min = 0.927632
  true_rebuild_loo_fixed_stress_min = 0.940789
```

结论：

- V8 pure embedding 方向的 closed-set 部署信号更强了；`[6,12,24]` 可用 512 prototypes 达到 4/6688 wrong。
- true rebuild-LOO 暴露出当前 embedding/prototype 仍大量依赖同 original 参与聚类；这不阻止全集部署，但说明它不能被宣称为 unseen-original 泛化模型。
- kcenter / medoid / quant_medoid 没有超过 kmeans；prototype source 不是主瓶颈。
- 下一步优先级应改为 closed-set 边界闭合：用事件分析找出剩余 4/6688 错误与低 margin views，优先做 boundary-reserved prototypes / closed-set margin loss / int8-stable margin，而不是把 true rebuild-LOO 当成主优化目标。

### 3.6 先进思想在全集闭集前提下的统一重述

如果数据集就是全集，V8 不应该继续照搬开放集分类范式。更准确的工程表述是：

```text
train a tiny embedding encoder
compile a compact labeled prototype program over the complete deployment universe
distill the compiled decision geometry back into the encoder
export the exact int8 nearest-prototype decision path
```

这不是放弃训练，也不是退化成 6688 条 all_views 记忆表。关键区别在于：

- all_views memory prototype 只是逐样本查表，prototype_count 随 view 数线性膨胀；
- V8 closed-set compiler 要在全集上求一个小的代表集合，使所有 clean/D4/stress views 都被正确覆盖，且 int8 replay 无翻转；
- 训练的目标是让 encoder 主动产出适合这种小表覆盖的 embedding，而不是训练完再被动 kmeans。

先进思想在这里的融合方式如下：

```text
LVQ / large-margin prototype learning:
  直接优化 d(nearest_wrong) - d(nearest_correct)，让闭集边界变厚。

Submodular / coreset selection:
  把 prototype 表构建成覆盖问题，选择最少中心覆盖全部正确视图和低 margin 视图。

Reject / cascade classifier:
  主 prototype 表处理高 margin 大多数样本；低 margin 样本进入小的 boundary repair 表。

Quantization-aware retrieval:
  训练和 compiler 都使用与板端一致的 int8 distance / tie-breaking replay。

VQ / hashing / product quantization:
  若 prototype 表仍大，再把 embedding 或 prototype table 离散化压缩，而不是先牺牲正确率。

Prototype teacher distillation:
  编译出的最强闭集 prototype program 反过来作为 teacher，训练下一代更小 backbone。
```

因此当前主线应该是 `closed-set prototype compiler -> compiler-guided training -> smaller backbone replay`，而不是继续只靠随机种子和 kmeans K 值碰运气。

## 4. 术语表

```text
embedding:
  CNN 输出的低维向量，通常 L2 normalize，用距离或 cosine 做分类。

prototype:
  某个类别、子类、簇或动态发现边界区域的代表向量。可由样本均值、medoid、k-means center 或 learned proxy 得到。

proxy:
  训练中可学习的类别/子类/簇中心。它类似 prototype，但直接作为参数参与 loss。

pure embedding model:
  推理时不依赖 parent softmax head，而是用 embedding 与 prototype/proxy 的距离决策。

prototype rescue:
  旧 head 先判断，大多数样本走旧 head；只有 gate 触发时用 nearest prototype 覆盖旧判断。

pure prototype classifier:
  所有样本都由 nearest prototype 决策，不再使用旧 parent head。

closed-set prototype compiler:
  在全集 clean+D4+stress embedding 上选择一个最小、int8 稳定的 labeled prototype table，并输出可复现的决策、边界事件和训练反馈。

main prototype table:
  覆盖大多数高 margin 样本的基础表，通常来自 kmeans / medoid / learned proxy。

boundary repair table:
  只服务于 replay 证明的 wrong 或低 margin 样本的小表。它不是人工 hard-case 先验，而是 compiler 从当前 embedding 行为中选出的闭集修复项。

margin gate:
  `nearest_wrong_distance - nearest_correct_distance` 的部署侧阈值。高于阈值走主表，低于阈值可进入 boundary repair 或触发更保守 tie-breaking。

CTD:
  本项目中主要指 correct-teacher / CTD rescue 相关路线：用 old stable 与 rescue teacher 的 per-view 正确性生成候选 teacher soft labels、teacher weight、parent weight。是否启用 teacher 信号由当前 replay/margin 决定，而不是由区域名先验决定。

preserve:
  old stable 正确但 CTD/rescue 错误的观测区域。它是诊断标签，不是先验类别；只有 replay 仍支持时才作为约束。

rescue:
  old stable 错误但 CTD/rescue 正确的观测区域。它是候选纠错证据，不应无条件扩展到所有 view/stress。

hard negative:
  由当前 embedding/prototype replay 动态发现的易混异类样本或 prototype。

D4 / rot_mirror:
  4 个旋转和水平镜像组合形成的 8 视图标签不变约束。
```

## 5. 论文思想与本项目落地方式

### 5.1 Prototypical Networks

论文：Snell et al., Prototypical Networks for Few-shot Learning, https://arxiv.org/abs/1703.05175

原思想：学习一个 metric space，让分类通过样本到 class prototype 的距离完成。

落地到 V8：

- 每个 parent 不只一个 prototype，而是先按 visual subclass / view-stress cluster 建多中心 prototype；只有 replay 发现稳定边界冲突时，才增加动态 hard cluster。
- 训练 batch 采用 episodic 结构：每个 episode 采若干原图，每个原图带 clean + D4 + stress views。
- support views 形成 prototypes，query views 必须被最近的正确 prototype 分类。
- validation 不能让 query 自己或同一原图同一 view 泄漏到 prototype 表。

### 5.2 Supervised Contrastive Learning

论文：Khosla et al., Supervised Contrastive Learning, https://arxiv.org/abs/2004.11362

原思想：同类样本在 embedding space 拉近，异类样本推远；多正样本 contrastive 比普通 triplet 更稳定。

落地到 V8：

- 同一原图的 clean / rot / mirror / stress 全部是强 positive。
- 同一 visual subclass 是中等 positive。
- 同一 parent 不同 subclass 是弱 positive，不能拉到完全重合，否则可能损失局部可分结构。
- 不同 parent 是 negative；old stable 的错类、CTD 的错类、top confusion prototype 只作为候选 hard negative，必须由当前 replay/margin 再确认。

### 5.3 ArcFace / Sub-center ArcFace

论文：Deng et al., ArcFace, https://arxiv.org/abs/1801.07698

原思想：在角度空间加入 margin，提高类间间隔；sub-center ArcFace 给每类多个中心，允许噪声或 hard subgroup 自动隔离。

落地到 V8：

- embedding L2 normalize，proxy/prototype L2 normalize，用 cosine/angular margin。
- parent 只作为高层类别，不强制一个 parent 一个中心。
- 每个 visual subclass 使用多个 sub-centers；动态 hard cluster 只在 replay 证明必要时启用。
- 被当前 replay 证明需要隔离的样本允许靠近非主中心，避免污染大多数样本。

### 5.4 Proxy Anchor Loss

论文：Kim et al., Proxy Anchor Loss for Deep Metric Learning, https://arxiv.org/abs/2003.13911

原思想：proxy-based loss 收敛快，pair-based loss 能利用样本间细粒度关系；Proxy Anchor 结合两者。

落地到 V8：

- 对小数据集，learned proxies 比纯 pair/triplet 更稳定。
- 初期用 proxies 训练 embedding，后期把 proxies 与真实样本 medoids 合并成部署 prototype 表。
- proxy 不一定等于 parent；推荐粒度为 `parent/subclass/cluster`。

### 5.5 Multi-Similarity Loss

论文：Wang et al., Multi-Similarity Loss, https://openaccess.thecvf.com/content_CVPR_2019/html/Wang_Multi-Similarity_Loss_With_General_Pair_Weighting_for_Deep_Metric_Learning_CVPR_2019_paper.html

原思想：通过 mining + weighting 选出信息量最大的 positive/negative pairs。

落地到 V8：

- 每个 batch 内只对当前 replay 证明有信息量的 pairs 加大权重，避免 easy 样本淹没真实边界信号。
- hard pair 候选来源：
  1. old stable wrong / CTD correct 的 9 个 rescue 原图及其 D4/stress views。
  2. old stable correct / CTD wrong 的 26 个 preserve 原图，作为 anti-CTD 候选。
  3. both wrong 或历史 hard clean 样本。
  4. prototype 边界最近的异类样本。
- 上述候选不能直接变成固定边界标签；必须经过当前 embedding/prototype replay、per-view correctness、nearest-wrong margin 再筛选。

### 5.6 VICReg / Barlow Twins

论文：VICReg, https://arxiv.org/abs/2105.04906；Barlow Twins, https://arxiv.org/abs/2103.03230

原思想：让不同 view 的 embedding 一致，同时通过 variance / covariance 或 redundancy reduction 防止 collapse。

落地到 V8：

- D4/stress views 必须 embedding 一致，但不能把所有样本坍缩成同一个点。
- 对同一原图的 views 使用 invariance loss。
- 对 batch embedding 加 variance lower bound 和 covariance decorrelation。
- 这是 pure embedding 训练必须加入的稳定项，尤其在样本少、positive 约束强时防 collapse。

### 5.7 SwAV / online clustering

论文：Caron et al., SwAV, https://arxiv.org/abs/2006.09882

原思想：不同增强 view 之间预测一致的 cluster assignment，避免只做 pairwise instance comparison。

落地到 V8：

- 对每个原图的 clean/D4/stress views，要求它们分配到同一个 subclass/cluster prototype 或同 parent 下的等价 prototypes。
- 训练中做 balanced assignment，避免动态发现的边界样本全部挤到一个 proxy。
- 后处理可以用 SwAV-like assignment 结果决定 prototype compression。

### 5.8 Group Equivariant CNN

论文：Cohen & Welling, Group Equivariant Convolutional Networks, https://arxiv.org/abs/1602.07576

原思想：网络结构对旋转/反射群保持等变，从结构上降低样本复杂度。

落地到 V8：

- 第一阶段不直接换成 G-CNN，因为板端算子和 TFLite Micro 风险更高。
- 但训练目标采用 D4 orbit consistency，等价于用 loss 逼近 D4 invariance。
- 如果 pure embedding 仍无法稳定，V8-B 可以尝试轻量 D4 weight sharing：同一输入生成 8 view embeddings 后做 orbit pooling，但这会增加推理成本，必须单独做板端评估。

### 5.9 Model Soups / Task Arithmetic / TIES / Git Re-Basin

论文：

- Model Soups, https://arxiv.org/abs/2203.05482
- Task Arithmetic, https://arxiv.org/abs/2212.04089
- TIES-Merging, https://arxiv.org/abs/2306.01708
- Git Re-Basin, https://arxiv.org/abs/2209.04836
- Fisher-weighted averaging, https://arxiv.org/abs/2111.09832

原思想：多个模型如果处在可对齐的 basin 或共享初始化的相近区域，可以通过权重平均、任务向量、符号冲突处理、通道重排、Fisher 权重等方式合并能力。

落地到本项目：

- 这些思想解释了为什么“参数相似部分合并、差异部分分流”是合理方向。
- 但当前 old stable/CTD 的实测权重差异过大，不能把 naive soup 当主线。
- V8 可借鉴其原则，而不是照搬到权重：
  - 先对齐表示空间，再合并决策。
  - 对一致区域做共享 prototype/proxy。
  - 对符号冲突或边界冲突区域保留多个 sub-centers。
  - 用 Fisher/importance 思想给 replay-confirmed 高影响样本动态权重。

### 5.10 Learning Vector Quantization / relevance metric learning

论文：Adaptive Distance Measures in Relevance Learning Vector Quantization, https://link.springer.com/article/10.1007/s13218-012-0188-1

原思想：prototype 不只是后处理中心，而是可训练分类器的一部分；距离度量本身也可以学习。Generalized / Matrix Relevance LVQ 用最近正确 prototype 与最近错误 prototype 的 margin 形成优化目标，并允许不同维度、低秩投影或局部度量有不同重要性。

落地到 V8：

- 不再只把 kmeans 当离线聚类；把 nearest-correct 与 nearest-wrong 的距离差写进训练 loss。
- 先从低风险版本开始：学习 diagonal relevance / per-dim scale，而不是一开始引入完整矩阵度量。
- 对 16/24/32 维 embedding，per-dim relevance 可以直接折叠进 prototype 或 embedding scale，部署侧仍是 int8 squared L2 / dot product。
- 若闭集错样本集中在少数维度关系上，可尝试 low-rank metric head，但必须确认导出后不会增加板端不可接受的矩阵乘开销。

### 5.11 Submodular selection / coreset / set cover

论文：apricot: Submodular Selection for Data Summarization in Python, https://jmlr.csail.mit.edu/papers/v21/19-467.html

原思想：代表点选择可以视为覆盖和多样性优化，而不是只靠 kmeans 均方误差。facility location、set cover、diversity-aware selection 这类目标适合从有限全集中选择少量代表点。

落地到 V8：

- 当前 6688 个 deployment views 是有限全集，可以显式优化“哪些 prototype 覆盖哪些 view”。
- kmeans 给基础覆盖，set-cover/coreset 给低 margin 与边界样本补点。
- compiler 的候选池不应只来自 cluster center，还应包括 wrong view exact embedding、低 margin view、同 original D4 orbit mean、同 parent 防守 medoid、nearest-wrong conflict pair 的 correct-parent 反制 prototype。
- 选择目标不是最小 reconstruction error，而是最小 prototype_count 下的 deterministic 100%、int8 100%、margin risk 最小。

### 5.12 Product Quantization / learning to hash

论文：Product Quantization for Nearest Neighbor Search, https://doi.org/10.1109/TPAMI.2010.57；Learning to Hash survey, https://link.springer.com/article/10.1007/s10115-022-01734-0

原思想：nearest-neighbor 检索可以通过向量离散化、子空间量化、二值 hash 等方式降低存储与距离计算成本。

落地到 V8：

- 首轮不应先上 PQ/hash，因为当前 prototype_count 还没闭合到 100%；先保证 float/int8 决策完全一致。
- 如果闭集 100% 需要的 prototype 表仍偏大，再考虑把 24/32 维 embedding 拆成子空间码本，或训练 binary/hash head。
- 对 TFLite Micro，最可控的第一步是 int8 dot/L2 replay；PQ/hash 只能作为表规模或距离成本仍过高时的 Phase C/D 压缩手段。
- hash loss 的有效部分可提前借鉴：增加 quantization loss，让 embedding 接近 int8/bin code 的稳定格点，降低边界翻转。

### 5.13 Reject option / margin cascade

论文：Classification with reject option performance analysis, https://www.sciencedirect.com/science/article/abs/pii/S0031320319302870

原思想：分类器可以对低置信样本拒识或转入更强决策路径，用错误率和拒识率共同评价。

落地到 V8：

- 板端不需要“拒识”，但可以使用同构思想做 cascade：高 margin 样本走小主表，低 margin 样本走小 boundary repair 表。
- gate 不能是 softmax 置信度，必须是部署距离 margin：`d_wrong - d_correct`。
- boundary repair 表只对低 margin 集合生效，降低主表为了少数样本增加大量 K 的需求。
- compiler 必须同时报告 gate 触发率、主表正确率、repair 后正确率、int8 后 gate 稳定性。

### 5.14 VQ-VAE / discrete representation

论文：Neural Discrete Representation Learning, https://papers.neurips.cc/paper/7210-neural-discrete-representation-learning.pdf

原思想：用 codebook 把连续表示离散化，模型学习把输入映射到有限 code，再用 code 表表达离散语义。

落地到 V8：

- V8 的 prototype table 本质上就是 supervised codebook，但 code label 是 parent/subclass/cluster/boundary。
- 不建议首轮加入完整 VQ-VAE 重构分支；当前任务不是重建 32x32 ROI，而是闭集 parent 决策。
- 可借鉴 commitment/codebook 思想：训练时让 embedding 靠近当前 compiler 选中的 prototype，并惩罚频繁跳到 wrong-parent code。
- 如果后续需要更小 backbone，可让 encoder 直接预测 code id 或 coarse code，再用小表 refine。

### 5.15 融合原则：先编译，再训练，再压缩

这些思想不能平均用力。当前 V8 的最有效融合顺序应是：

```text
1. 用当前最强 embedding 运行 closed-set prototype compiler。
2. 得到主表、boundary repair 表、错误/低 margin 清单和 int8 翻转清单。
3. 把 compiler 输出变成训练信号：
   - active positive prototype
   - nearest wrong prototype
   - boundary sample weight
   - int8 distance margin
   - gate target
4. 用这些信号训练下一代 encoder。
5. 再次 compiler replay，比较 prototype_count、wrong_count、margin、latency。
```

这条路线把“论文思想”变成可验证闭环：每个新 loss 都必须减少 closed-set wrong、增厚 int8 margin、降低 prototype_count 或降低延迟，否则就不保留。

## 6. V8 模型定义

### 6.1 推理结构

V8 推荐推理结构：

```text
input x
  -> encoder f_theta(x)
  -> projection head g_phi(...)
  -> raw embedding h = g_phi(f_theta(x))
  -> optional offline/training-only normalization convention
  -> distance to int8 prototype table
  -> parent = parent_of(nearest valid prototype)
```

部署时可以移除训练用 projection MLP 的一部分，只保留最小可部署 embedding head。历史写法中常把 embedding 直接记为 `L2Norm(g_phi(...))`，这对 metric learning loss 很自然，但不应默认把 L2 normalize 留在 TFLite runtime。严格推荐算子模式下，最终 TFLite encoder 应输出 raw dense embedding；归一化、prototype 缩放、距离等价变换应尽量在训练 loss、离线 prototype compiler 或板端 C++ int8 距离循环中显式处理。

闭集 compiler 成熟后，推荐推理结构升级为两层但仍保持同一种 nearest-prototype 机制：

```text
input x
  -> encoder + int8 embedding
  -> main prototype table
  -> if int8 margin >= gate_threshold:
       return main_parent
     else:
       search boundary repair table
       return repaired_parent
```

这不是两个 CNN ensemble，只是把一个 prototype table 分成高覆盖主表和小型边界修复表。主表与 repair 表可物理合并为一个序列化表，gate 只影响搜索范围和 tie-breaking；如果 microbench 显示分支成本不值得，也可以退化为单表全搜索。

### 6.1.1 严格推荐算子约束

V8 的最终部署 encoder 继承 V5 的推荐算子白名单思路。严格主线只允许下列结构算子进入 TFLite 图：

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

V8 pure embedding 的默认严格导出路径应更窄：

```text
gray32
  -> SPACE_TO_DEPTH
  -> CONV_2D (+ folded BN / fused RELU6)
  -> MAX_POOL_2D
  -> CONV_2D (+ fused RELU6)
  -> MAX_POOL_2D
  -> CONV_2D (+ fused RELU6)
  -> MEAN
  -> FULLY_CONNECTED
  -> raw embedding
```

`SOFTMAX` 不属于 pure embedding 决策路径；prototype distance 和 argmin 不应伪装成 TFLite 子图，而应作为生成的 C++ int8 loop 和可复现 replay 逻辑进入部署 bundle。

当前 `train_v8_end_to_end_embedding.py` 的手写 L2 normalization：

```text
values / max(norm(values), eps)
```

导出时会展开为类似：

```text
MUL, SUM, SQRT, MAXIMUM, DIV
```

这不满足严格推荐算子路线：`DIV`、`MAXIMUM`、`SUM` 不在 V5 推荐白名单，`SQRT` 也没有本地完整 green-op 证据。其他归一化写法也不能自动通过：

```text
tf.nn.l2_normalize -> L2_NORMALIZATION
UnitNormalization -> SQUARE, SUM, RSQRT, MINIMUM, MUL
```

`L2_NORMALIZATION` 在本地 full-ops 记录中可用，但它仍不是 V5 strict whitelist 的主线算子；只能作为明确标注的 fallback，而不是默认 winner。`UnitNormalization` 会引入更多非白名单 reduce / reciprocal-sqrt 算子，默认排除。

因此 V8 后续训练应采用“训练可归一化，部署导出 raw”的分离策略：

```text
training:
  raw_h = encoder(x)
  z = normalize(raw_h) only inside loss if angular/proxy loss needs it
  optimize proxy / contrastive / compiler margin losses

compiler and replay:
  use the same feature convention intended for deployment
  if strict export uses raw_h, prototypes must be compiled and int8-replayed in raw_h space
  any offline scale, per-dim relevance, or normalization approximation must be serialized

deployment TFLite:
  output raw int8 embedding using only strict recommended ops
  run prototype distance in C++ int8 path with deterministic tie-breaking
```

候选排序新增硬字段：

```text
exported_tflite_ops
strict_recommended_ops_pass
non_recommended_ops
embedding_export_mode: raw | l2_normalization_fallback | custom_norm_rejected
prototype_distance_mode: int8_squared_l2 | int8_dot | other
```

只有 `strict_recommended_ops_pass=true` 的候选才能作为 V8 主部署候选；带 `L2_NORMALIZATION` 或其他非白名单算子的候选可以保留为研究对照，但必须单独估计板端 op 成本，不能与严格路线混在一起排名。

### 6.1.2 2026-05-20 latency-first 修正

“推荐算子”不能只理解为“不越过白名单”。本轮目标改为板端保守估计 `<=4ms`，最好接近 `<=2ms`，因此推荐算子路线必须服务于实际吞吐：

```text
primary speed tools:
  SPACE_TO_DEPTH: 低成本降空间，优先替代盲目加宽
  DEPTHWISE_CONV_2D + 1x1 CONV_2D: 用推荐算子扩大容量而不是堆普通 3x3 Conv
  MAX_POOL_2D / MEAN: 保留低成本下采样和 GAP
  RELU6: 量化友好，当前作为 fast4ms 主激活
  raw FULLY_CONNECTED embedding: TFLite 图到 Dense 截止，prototype replay 走 C++ int8
```

新增脚本：

```text
model_training/estimate_v8_board_time.py
model_training/run_v8_fast4ms_tmux.sh
model_training/run_v8_recommended_backbone_cpu_tmux.sh
```

`estimate_v8_board_time.py` 使用本地 `full_ops_20260506_185857` 的 modelbench 标定，并按实际 MAC 关系估计新 V8 backbone。排序必须使用：

```text
board_total_avg_us
board_total_conservative_us = backbone_estimate * 1.25 + prototype_distance_estimate
under_4ms_conservative
under_2ms_avg
clean/rotmirror/stress + int8 replay
```

2026-05-20 的重要负例：

```text
experiments/v8_recommended_backbone_20260520_0001/
  stageB_recommended_operator_backbone/double_s2d_c8_16_32_d24_s20260705

accuracy:
  clean = 100%
  rot_mirror_min = 100%
  stress_min = 100%
  int8 clean/rot_mirror/stress = 100%

but:
  board_total_conservative_us ~= 19.7ms by current estimator
  reason: second SPACE_TO_DEPTH makes the following 3x3 Conv much wider
  status: useful teacher / upper-bound evidence, not a deploy candidate
```

因此后续主资源必须优先投向真正可能通过 4ms 的窄模型：

```text
fast4ms GPU run:
  experiments/v8_fast4ms_20260520_0001/stageB_fast4ms_embedding
  s2d [3,6,12] / [4,8,16]
  double_s2d [3,6,12] / [4,8,16]
  depthwise_pool [3,6,12] / [4,8,16]
  spacetodepth_depthwise [4,8,16]

fast4ms CPU runs:
  experiments/v8_fast4ms_cpu_20260520_0001/stageB_fast4ms_cpu_embedding
  experiments/v8_fast4ms_cpu2_20260520_0001/stageB_fast4ms_cpu_embedding
```

资源策略：

```text
GPU: 负责 8 路 fast4ms 主训练，优先保证 GPU 利用率
CPU: 跑互补 seed / dim / speed arch，但保留内存和 host 线程给 GPU data path
stop rule: 若候选结构保守估计明显 >4ms，除非已产出可作为 teacher 的完整 100% 结果，否则停止占用主资源
```

### 6.1.3 2026-05-20 fast4ms 达标结果

本轮达标模型不再依赖泛化式 LOO，而是按“数据集就是全集”的部署前提，把低 margin / 误判 view 编译进 prototype 表。最终主候选：

```text
encoder:
  experiments/v8_fast4ms_20260520_0001/stageB_fast4ms_embedding/s2d_c4_8_16_d24_s20260802/embedding_model.keras

prototype table:
  experiments/v8_fast4ms_repairs_20260520_0001/stageB_prototype_repair/
    s2d_c4_8_16_d24_s20260802_existing_repair/best_v8_repaired_joint2_params.npz

architecture:
  backbone_architecture = spacetodepth_conv
  filters = [4, 8, 16]
  embedding_dim = 24
  activation = relu6
  pool = max
  embedding_output_mode = raw

prototype_count:
  base kmeans128 = 1022
  original clean+D4+stress repair = +47
  medium stress repair = +9
  original regression repair after medium = +2
  final joint2 = 1081
```

验证产物：

```text
original clean/rot_mirror/stress replay:
  experiments/v8_fast4ms_repairs_20260520_0001/stageB_prototype_repair/
    s2d_c4_8_16_d24_s20260802_existing_repair/training_stress_eval_joint2_final/medium_stress_summary.json

medium stress replay:
  experiments/v8_fast4ms_repairs_20260520_0001/stageB_prototype_repair/
    s2d_c4_8_16_d24_s20260802_existing_repair/medium_stress_eval_joint2_repaired/medium_stress_summary.json

board estimate:
  model_training/experiments/v8_fast4ms_final_joint2_board_time_20260520_0001.csv

TFLite op audit:
  experiments/v8_fast4ms_repairs_20260520_0001/stageB_prototype_repair/
    s2d_c4_8_16_d24_s20260802_existing_repair/tflite_export_joint2/tflite_export_summary.json
```

结果：

```text
original clean accuracy = 100%
original rot_mirror_min_accuracy = 100%
original stress_min_accuracy = 100%
original int8 clean/rot_mirror/stress = 100%

medium clean accuracy = 100%
medium stress_min_accuracy = 100%
medium int8 stress_min_accuracy = 100%

prototype_count = 1081
estimated_distance_macs = 25944
board_backbone_avg_us = 2719
board_backbone_conservative_us = 3399
board_total_avg_us = 3238
board_total_conservative_us = 3918
under_4ms_conservative = true
under_2ms_avg = false
```

TFLite 导出结果：

```text
float_unique_ops = CONV_2D, DELEGATE, FULLY_CONNECTED, MAX_POOL_2D, MEAN, SPACE_TO_DEPTH
int8_unique_ops = CONV_2D, DELEGATE, FULLY_CONNECTED, MAX_POOL_2D, MEAN, SPACE_TO_DEPTH
float_non_recommended_ops = []
int8_non_recommended_ops = []
```

注意事项：

- `DELEGATE` 是 Python TFLite interpreter 注入的运行时 delegate 记录，不是模型 FlatBuffer 中需要新增支持的业务算子。
- d28 备选 `s2d_c4_8_16_d28_s20260825_existing_repair` 也能 clean/rot/stress/int8 全 100%，但保守估算约 `3980-3981us`，离 4ms 边界过近。d24 joint2 的 `3918us` 更适合作为当前主候选。
- 这一路线证明推荐算子不是“在白名单内就行”，而是要用 `SPACE_TO_DEPTH + 窄 CONV_2D + MAX_POOL_2D + MEAN + FC` 把 backbone 压到约 3.4ms conservative，再把全集边界通过少量 prototype 修复补齐。

### 6.1.4 2026-05-20 under2 新目标

新的硬目标是板端保守估计 `<=2ms`，最好逼近 `1ms`，并且 clean / rot_mirror / stress 在 float 与 int8 replay 中仍全部 100%。这改变了资源分配：

```text
current fast4ms candidate:
  s2d_conv [4,8,16] d24
  backbone conservative ~= 3399us
  prototype distance ~= 519us
  total conservative ~= 3918us

under2 implication:
  prototype 修复不是主瓶颈
  必须把 backbone 换成更小的推荐算子组合
  prototype_count 仍要受 latency budget 约束
```

首轮 under2 主线：

```text
launcher:
  model_training/run_v8_under2_teacher_tmux.sh

teacher:
  experiments/v8_fast4ms_repairs_20260520_0001/stageB_prototype_repair/
    s2d_c4_8_16_d24_s20260802_existing_repair/best_v8_repaired_joint2_params.npz

training idea:
  用已经达标的 d24 prototype table 作为 compiled teacher
  tiny encoder 仍输出 d24 raw embedding
  loss = proxy subclass CE + D4/stress consistency + VICReg-style var/cov + norm + compiled prototype margin/pull
  目标不是泛化，而是把全集闭集 decision geometry 蒸馏到更小 backbone
```

优先候选和预算：

```text
spacetodepth_hybrid [2,4,8] d24:
  backbone conservative ~= 1420us
  under2 prototype budget ~= 1200 prototypes
  risk: capacity very low, but prototype budget most宽

spacetodepth_conv [2,4,8] d24:
  backbone conservative ~= 1582us
  under2 prototype budget ~= 869 prototypes
  risk: prototype budget tighter, but all-conv path may learn stronger embedding

spacetodepth_hybrid [2,6,12] d24:
  backbone conservative ~= 1465us
  under2 prototype budget ~= 1114 prototypes
  risk: middle layer wider, but still enough budget for V8-style closed-set repair

spacetodepth_hybrid [3,6,12] d24:
  backbone conservative ~= 1855us
  under2 prototype budget ~= 303 prototypes
  risk: previous裸训准确率不足，只有 teacher-guided training 明显改善才值得继续
```

判定顺序：

```text
1. 先看 raw candidate_results 是否有接近 100 的低 prototype_count 候选。
2. 对 top under2 候选跑 closed-set compiler / repair，而不是直接加到 all_views 表。
3. 若 under2 只差少量 view，允许 exact boundary prototypes；若需要超过预算，转入更小 dim 或新的 closed-set head。
4. 若 tiny backbone 无法形成可压缩 embedding，下一步改成 parametric distillation head 或二级 cascade，而不是继续扩大 prototype 表。
```

### 6.1.5 2026-05-20 under2 已达成候选

实验路径：

```text
parent classifier training:
  model_training/run_v8_parent_classifier_tmux.sh
  experiments/v8_parent_classifier_20260520_0002/
    stageD_parent_classifier/s2d_c2_4_8_s20260931/

logit-memory compiler:
  model_training/compile_v8_parent_logits_memory.py

regular fixed-stress output:
  experiments/v8_parent_logits_memory_20260520_0003/
    s2d_c2_4_8_s20260931/

combined fixed + medium stress output:
  experiments/v8_parent_logits_memory_combined_stress_20260520_0001/
    s2d_c2_4_8_s20260931/
```

结构：

```text
encoder:
  spacetodepth_conv [2,4,8]
  output = 3 parent logits
  deployed feature source = parent_int8.tflite raw int8 output

runtime decision:
  int8 TFLite logits as 3D embedding
  squared-L2 to int8 residual prototype table
  parent = nearest prototype parent
```

常规 fixed stress 达标结果：

```text
prototype_source = exact_clean_residual
prototype_count = 2124
feature_dim = 3
estimated_distance_macs = 6372
estimated_int8_table_bytes = 6372

clean_accuracy = 1.0
rotmirror_min_accuracy = 1.0
stress_min_accuracy = 1.0
int8_clean_accuracy = 1.0
int8_rotmirror_min_accuracy = 1.0
int8_stress_min_accuracy = 1.0
margin_min = 1
int8_margin_min = 1

board_backbone_avg_us = 1266
board_backbone_conservative_us = 1582
board_total_avg_us = 1393
board_total_conservative_us = 1710
under_2ms_conservative = true
```

合并 fixed stress + medium stress 结果：

```text
stress view count = 30 groups including clean
prototype_source = exact_clean_residual
prototype_count = 2659
feature_dim = 3
estimated_distance_macs = 7977
estimated_int8_table_bytes = 7977

clean_accuracy = 1.0
rotmirror_min_accuracy = 1.0
stress_min_accuracy = 1.0
int8_clean_accuracy = 1.0
int8_rotmirror_min_accuracy = 1.0
int8_stress_min_accuracy = 1.0
margin_min = 1
int8_margin_min = 1

board_total_avg_us = 1425
board_total_conservative_us = 1742
under_2ms_conservative = true
```

TFLite op audit:

```text
unique ops:
  CONV_2D
  FULLY_CONNECTED
  MAX_POOL_2D
  MEAN
  SPACE_TO_DEPTH

runtime Python delegate record:
  DELEGATE
```

解释：

- 这是一个“parametric tiny encoder + closed-set residual memory”的路线，而不是传统泛化分类器。
- 因为数据集就是部署全集，logit-memory residual table 是合法主线；它把全集边界编译进一个很小的 3D int8 表，而不是依赖泛化式 LOO。
- `[2,6,12]` direct classifier 的 clean-only residual 表虽然只需 `671` prototypes 且保守估计 `1934us`，但 int8 TFLite logits 存在一个 rot/mirror 碰撞，full exact 表也只能到 `0.9967105`，因此不作为当前最佳。
- `exact_all` 在 `[2,4,8]` 上也能 100%，但常规 stress 下 `6688` prototypes 的保守估计为 `1984us`，太贴近 2ms 边界；residual 表以更少 prototypes 和正 margin 成为主候选。
- 下一步应从训练搜索转向部署打包、板端端到端 benchmark、C/C++ int8 prototype replay 实现和 tie/margin 审计。

### 6.1.6 2026-05-20 prototype 剪枝/合并与 margin 诊断

实验脚本：

```text
model_training/prune_merge_v8_logit_prototypes.py

output:
  experiments/v8_parent_logits_prune_merge_20260520_0001/
    primary_quick/
    combined_quick/
    board_time.csv
```

实验目标不是再次验证 100%，而是回答：`margin_min = 1` 是否由少数冗余 wrong-neighbor prototypes 造成，能否通过剪枝/合并直接变厚。

常规 fixed stress 表的基线：

```text
input:
  experiments/v8_parent_logits_memory_20260520_0003/
    s2d_c2_4_8_s20260931/best_parent_logits_memory_params.npz

base prototype_count = 2124
int8_margin_min = 1
low_margin <=1 / <=2 / <=4 / <=8 = 23 / 66 / 122 / 295
zero nearest-correct prototypes = 75
unique low-margin wrong prototypes <=8 = 226
low-margin wrong prototypes with zero correct usage = 0
```

常规 fixed stress 剪枝/合并结果：

```text
prune_unused_le8:
  prototype_count = 2049
  clean/rot_mirror/stress/int8 = all 1.0
  int8_margin_min = 1
  low_margin <=1 / <=2 / <=4 / <=8 = 23 / 66 / 122 / 295
  board_total_conservative_us = 1705

prune_unused_then_low_margin_wrong_le8:
  prototype_count = 2042
  clean/rot_mirror/stress/int8 = all 1.0
  int8_margin_min = 1
  low_margin <=1 / <=2 / <=4 / <=8 = 23 / 68 / 124 / 293
  board_total_conservative_us = 1705

merge/duplicate:
  duplicate same-parent int8 codes = 75 removable extras
  near merge d0/d1/d4 accepted only table-cleanup moves
  no candidate raised int8_margin_min above 1
```

对照诊断：

```text
remove all low-margin wrong prototypes:
  remove = 226
  remaining prototypes = 1898
  result = not correct
  wrong views = 118

remove all unused prototypes:
  remove = 75
  remaining prototypes = 2049
  result = all correct
  low-margin counts unchanged
```

combined fixed + medium stress 表的基线：

```text
input:
  experiments/v8_parent_logits_memory_combined_stress_20260520_0001/
    s2d_c2_4_8_s20260931/best_parent_logits_memory_params.npz

base prototype_count = 2659
int8_margin_min = 1
low_margin <=1 / <=2 / <=4 / <=8 = 29 / 87 / 170 / 422
zero nearest-correct prototypes = 81
unique low-margin wrong prototypes <=8 = 305
low-margin wrong prototypes with zero correct usage = 0
```

combined 剪枝/合并结果：

```text
prune_unused_then_low_margin_wrong_le8:
  prototype_count = 2574
  clean/rot_mirror/stress/int8 = all 1.0
  int8_margin_min = 1
  low_margin <=1 / <=2 / <=4 / <=8 = 28 / 86 / 167 / 420
  board_total_conservative_us = 1737

prune_unused_le8 / duplicate:
  prototype_count = 2578
  clean/rot_mirror/stress/int8 = all 1.0
  int8_margin_min = 1
  low-margin counts unchanged from base

remove all low-margin wrong prototypes:
  remove = 305
  remaining prototypes = 2354
  result = not correct
  wrong views = 157
```

结论：

- prototype 剪枝/合并是有效的表清理：常规 stress 可安全从 `2124` 降到 `2049` 或更小的 `2042`，combined 可从 `2659` 降到 `2574`，板端保守估计仍稳在 `<=2ms`。
- 它不是有效的 margin 根治：所有低 margin wrong prototypes 都有 correct-usage，说明它们不是可删冗余项，而是 3D int8 code 空间中的真实相邻边界。
- 继续扩大剪枝半径可能只会在局部降低表大小，或用新的低 margin/tie 换旧的低 margin/tie；不能作为安全性主线。
- 下一步若要解决 `margin_min = 1`，应让 encoder 训练阶段直接优化部署距离：`d_nearest_wrong - d_nearest_correct`、fake-int8 margin、conflict-pair code separation，而不是只在 compiler 末端删表。

### 6.1.7 2026-05-20 low-margin 真实压力错例

实验脚本：

```text
model_training/stress_test_v8_low_margin.py

output:
  experiments/v8_low_margin_stress_test_20260520_0001/
    primary_cleaned/
    combined_cleaned/
```

这轮压力测试不是重新跑固定 stress，而是在当前已经 100% 的 base views 上，对 `int8_margin <= 8` 的 query 叠加额外扰动：

```text
extra perturb:
  noise 0.02..0.10
  motion blur length 3/5/7 at 0/45/90/135 degrees
  blur + noise
  brightness +/- 0.04/0.08/0.12
  contrast +/- 0.10/0.20
  1px/2px shifts
  shift + noise
```

脚本同时抽取 `base_margin >= 128` 的 high-margin control，并导出所有真实错分的 stress 图片。注意导出的不是原图，而是进入 `parent_int8.tflite` 的扰动后 32x32 输入：

```text
primary wrong images:
  experiments/v8_low_margin_stress_test_20260520_0001/
    primary_cleaned/wrong_stress_images/
  exported = 7930 png

combined wrong images:
  experiments/v8_low_margin_stress_test_20260520_0001/
    combined_cleaned/wrong_stress_images/
  exported = 10561 png
```

primary 表结果：

```text
low group:
  base int8_margin <= 8
  rows = 295 base queries * 41 perturb = 12095 events
  wrong = 6510
  wrong_rate = 53.82%

control group:
  base int8_margin >= 128
  rows = 295 base queries * 41 perturb = 12095 events
  wrong = 1420
  wrong_rate = 11.74%

margin bucket wrong rate:
  <=1:  594 / 943  = 62.99%
  <=2:  998 / 1763 = 56.61%
  <=4: 1263 / 2296 = 55.01%
  <=8: 3655 / 7093 = 51.53%
```

combined 表结果：

```text
low group:
  base int8_margin <= 8
  rows = 420 base queries * 41 perturb = 17220 events
  wrong = 8839
  wrong_rate = 51.33%

control group:
  base int8_margin >= 128
  rows = 420 base queries * 41 perturb = 17220 events
  wrong = 1722
  wrong_rate = 10.00%

margin bucket wrong rate:
  <=1:  696 / 1148  = 60.63%
  <=2: 1287 / 2378  = 54.12%
  <=4: 1764 / 3321  = 53.12%
  <=8: 5092 / 10373 = 49.09%
```

最容易真正翻车的 base view：

```text
primary low wrong by view:
  rot90             1133
  rot270             845
  mirror_lr_rot90    684
  mirror_lr_rot270   644
  mirror_lr_rot180   550
  rot180             532
  cam_blur5a135      280
  mirror_lr          214
  vblur5             210

combined low wrong by view:
  rot90             1201
  rot270            1016
  mirror_lr_rot270   786
  mirror_lr_rot180   707
  mirror_lr_rot90    701
  rot180             606
  cam_blur5a135      290
  vblur5             276
  cam_bright0p04_contrast0p10 267
```

最容易触发错分的额外扰动：

```text
primary low top perturb:
  blur7a135                 205
  bright_m0p12              192
  contrast_m0p20            186
  blur7a45                  182
  blur5a135_noise0p04       182
  bright_m0p08              181
  shift_l1_noise0p04        179
  noise_0p08                178

combined low top perturb:
  blur7a135                 266
  bright_m0p12              260
  shift_l1_noise0p04        256
  shift_ul1                 250
  contrast_m0p20            245
  shift_u2                  244
  blur7a45                  239
  noise_0p08                236
```

结论：

- `int8_margin <= 8` 不是纯统计噪声；它在额外 stress 下确实显著更容易错，低 margin 组错误率约为 control 的 `4.6x` 到 `5.1x`。
- `margin <= 1` 是最高风险档，但 `<=2/<=4/<=8` 也明显危险，不能只盯全局最小值。
- 风险主要落在 D4 方向视图，尤其 `rot90/rot270/mirror_lr_rot90/mirror_lr_rot270`；这说明下一轮训练应把 D4 consistency 与 closed-prototype int8 margin 合在一起优化。
- 导出的 wrong stress 图片和 `wrong_events.csv` / `first_failure_by_base.csv` 是诊断证据，不应直接作为正式训练样本池扩张。它们的正确用法是定位正常全集中哪些 clean/D4/fixed/medium view 的 base margin 太薄，再把训练目标转向 D4 consistency、fake-int8 margin、nearest-wrong separation 和 code geometry 修复。

### 6.1.8 2026-05-20 margin8 residual compiler 结果

用户判断：这些额外高强度 stress 不必纳入训练，但它们提醒我们必须控制 margin。按这个边界，本轮没有把 extra stress 加入训练分布，而是在原有 fixed stress / medium stress 全集上尝试 table-side margin control：

```text
script:
  model_training/compile_v8_parent_logits_memory.py

output:
  experiments/v8_parent_logits_memory_margin8_20260520_0001/

compiler target:
  --residual-target-int8-margin 8
  --residual-target-margin 8
```

常规 fixed stress margin8 表：

```text
params:
  experiments/v8_parent_logits_memory_margin8_20260520_0001/
    primary/best_parent_logits_memory_params.npz

prototype_count = 2239
clean/rot_mirror/stress/int8 = all 1.0
margin_min = 1
int8_margin_min = 1
low_margin <=1 / <=2 / <=4 / <=8 / <=16 / <=32 =
  20 / 62 / 112 / 276 / 585 / 1223
board_total_conservative_us = 1717
```

对比常规 fixed stress 原表：

```text
prototype_count = 2124
low_margin <=1 / <=2 / <=4 / <=8 / <=16 / <=32 =
  23 / 66 / 122 / 295 / 618 / 1261
board_total_conservative_us = 1710
```

combined fixed + medium stress margin8 表：

```text
params:
  experiments/v8_parent_logits_memory_margin8_20260520_0001/
    combined/best_parent_logits_memory_params.npz

prototype_count = 2833
clean/rot_mirror/stress/int8 = all 1.0
margin_min = 1
int8_margin_min = 1
low_margin <=1 / <=2 / <=4 / <=8 / <=16 / <=32 =
  24 / 80 / 154 / 383 / 813 / 1599
board_total_conservative_us = 1752
```

对比 combined 原表：

```text
prototype_count = 2659
low_margin <=1 / <=2 / <=4 / <=8 / <=16 / <=32 =
  29 / 87 / 170 / 422 / 864 / 1647
board_total_conservative_us = 1742
```

margin8 表的额外 stress 诊断只做 no-export replay：

```text
primary_margin8_noexport:
  low rows = 276
  low events = 276 * 41 = 11316
  low wrong = 6228
  low wrong_rate = 55.04%
  control wrong_rate = 11.59%

combined_margin8_noexport:
  low rows = 383
  low events = 383 * 41 = 15703
  low wrong = 8214
  low wrong_rate = 52.31%
  control wrong_rate = 10.09%
```

解释：

- residual compiler 的 margin target 有方向性收益：`<=8` 低 margin query 从 `295 -> 276`、`420 -> 383`，额外 stress 下的低 margin 总 wrong 也随风险面缩小而下降。
- 但它不是根治：留下的低 margin query 单点风险率没有下降，`int8_margin_min` 仍为 `1`，p01/p05/p10 仍约 `3/9/17`。
- 原因与剪枝/合并结论一致：3D parent-logit int8 code 空间已经把不同 parent 的必要 correct prototypes 放得太近；只在 residual table 末端补点，无法把这些 code 本身推远。
- 下一步不应继续堆 extra-stress training set，而应做 encoder/code 几何修复：在训练阶段加入与部署一致的 fake-int8 nearest-correct/nearest-wrong margin loss、D4 orbit consistency、conflict-pair separation，或提高/重构 embedding code 维度后再编译表。

### 6.1.9 2026-05-20 parent-logit margin training smoke

在不引入额外高强度 stress 的前提下，补了 `model_training/train_v8_parent_classifier.py` 的训练入口：

```text
new experimental knobs:
  --prototype-teacher-npz
  --prototype-margin-weight
  --prototype-margin-target
  --prototype-margin-alpha
  --prototype-output-scale
  --prototype-output-zero
  --prototype-code-anchor-weight
```

这允许用 compiled prototype table 做 teacher，在 full-batch `@tf.function` 训练中加入 nearest-correct / nearest-wrong margin loss，并可选择把输出拉向 teacher 的 raw int8 code。两条 smoke 结果都是负例：

```text
raw int8 code-anchor smoke:
  output:
    experiments/v8_parent_margin_code_20260520_0001/smoke_raw_anchor/
  idea:
    treat model output directly as raw int8 code
    add code-anchor to teacher embedding_int8
  result:
    tflite_int8_clean_accuracy = 0.9474
    tflite_int8_rotmirror_min_accuracy = 0.7303
    tflite_int8_stress_min_accuracy = 0.7303
  conclusion:
    directly forcing old raw code destroys the classifier geometry

old-output-scale STE margin smoke:
  output:
    experiments/v8_parent_margin_code_20260520_0001/fine_oldscale_margin/
    experiments/v8_parent_margin_code_20260520_0001/fine_oldscale_margin_compile/
  idea:
    initialize from current best parent classifier
    use old TFLite output scale / zero for fake-int8 margin
  compiled result:
    prototype_count = 2871
    clean = 1.0
    rot_mirror_min = 1.0
    stress_min = 0.9967105263
    int8_stress_min = 0.9967105263
    margin_min = int8_margin_min = 0
```

解释：

- 这两个结果都不能替代当前 `2124/2659` prototype 的 under2 主候选。
- 负例本身有价值：Keras float logits + 旧 output scale 不是板端真实 int8 code，直接 code-anchor 到旧 raw int8 也会破坏 CE / D4 几何。
- 因此 3D parent-logit 路线如果继续做 margin training，必须把训练-导出-真实 int8 replay 放进闭环，而不能只看训练时 fake margin。
- 更可能的正解是重构 code 空间，而不是继续拧 3D parent logits：例如保留严格推荐算子，尝试 4/6/8/12 维 raw code head、D4 orbit margin、conflict-pair separation，再用真实 TFLite residual compiler 验证 prototype_count / latency / `int8_margin_min`。

### 6.1.10 2026-05-20 论文思想到真实训练的完整落地

本节把第 5 章论文思想收敛成一个可实现训练方案，而不是继续停留在“可以借鉴”的层面。目标不是把所有论文 loss 全部堆进脚本，而是把它们分别放到最适合的位置：

```text
core objective:
  Quantized Large-Margin Prototype Learning

deployment distribution:
  clean + D4 + fixed/medium stress

diagnostic only:
  extra high-strength stress, exported wrong images

hard gate:
  exported TFLite int8 replay, not Keras-float training metrics
```

论文思想到工程组件的映射：

```text
Prototypical Networks:
  推理路径固定为 encoder -> embedding/code -> distance to prototypes。
  训练、compiler、验证都围绕同一距离决策，不再把 softmax accuracy 当最终目标。

GLVQ / relevance LVQ:
  主 loss 直接优化 d_nearest_wrong - d_nearest_correct。
  先做 squared-L2 int8 margin；per-dim relevance 只允许作为离线可折叠 scale。

ArcFace / Sub-center ArcFace:
  warmup 阶段用 subclass/subcenter proxies 建几何坐标系。
  不做 3 个 parent 单中心；每个 visual subclass 允许 2-8 个 sub-centers。

Proxy Anchor / soft orthogonal proxies / ETF:
  proxies 用于稳定收敛和全局分散。
  对 proxy/prototype 加正交或 simplex-like 分散约束，避免不同 parent/subclass 中心挤在同一小区域。

SupCon:
  clean+D4+正常 stress 的同 original 视图是 strong positive。
  同 subclass 是 medium positive，同 parent 不同 subclass 是 weak positive，不同 parent 是 negative。

QAT / fake quantization:
  训练中用 STE/fake-int8 计算 prototype distance margin。
  但训练侧 fake quant 不能代替真实 TFLite；每个候选必须重新 export + compiler replay。

Submodular / coreset / set cover:
  prototype compiler 选择覆盖全集且降低 margin risk 的候选，而不是最小 reconstruction error。
  score 同时考虑 wrong 修复、low-margin 改善、prototype_count 和 latency。

kNN robustness / margin certification:
  对低 margin query 不只看原始 int8 code，还看 code 邻域内是否稳定。
  这比把 extra stress 加进训练更直接，因为真实翻车来自 code 点跨过 nearest-prototype 边界。
```

推荐训练闭环：

```text
round 0:
  take current best TFLite encoder + compiled prototype table
  export teacher cache:
    embedding_int8
    prototypes_int8
    prototype_parent
    nearest_correct_proto
    nearest_wrong_proto
    int8_margin
    low_margin_bucket
    conflict_proto_usage

round 1 warmup:
  train raw code head with CE/subcenter proxy/SupCon/D4 losses
  target dims: 3 control, 4, 6, 8, 12
  keep strict recommended TFLite ops

round 2 margin:
  freeze or slow-update active prototype teacher
  add fake-int8 nearest-correct/nearest-wrong margin loss
  increase weights only for replay-confirmed low-margin/conflict views

round 3 export:
  export real parent_int8.tflite or embedding_int8.tflite
  run compiler on true TFLite raw int8 outputs
  run prune/merge and board-time estimator

round 4 decision:
  keep only candidates that improve one of:
    low_margin <=1/2/4/8
    int8_margin_min or p01/p05/p10
    prototype_count
    conservative board time
  while preserving 100% normal deployment replay
```

推荐实验矩阵：

```text
M0 baseline:
  current 3D parent-logit encoder + existing residual table
  purpose: control

M1 3D QLMPL:
  same output dim, add int8 margin/neighborhood loss
  purpose: test whether 3D code still has unused separability

M2 4D/6D raw code:
  final Dense outputs raw code, no runtime normalization
  use C2/4/8 tiny backbone first; only widen backbone if replay proves capacity-bound
  purpose: relieve 3D geometric crowding with small distance-cost increase

M3 8D/12D raw code:
  wider code only if M2 cannot remove low-margin buckets
  still prefer C2/4/8 backbone because C4/8/16-D24 already exceeds <=2ms
  purpose: upper-bound margin fix while keeping recommended ops

M4 top-k class distance:
  keep same table, compare min distance vs top-k average/vote per parent
  k in {1,2,3,5}
  purpose: reduce single wrong-neighbor brittleness without changing encoder

M5 relevance scale:
  learn per-dim scale or integer weights, fold into prototype/code offline
  purpose: GLVQ-style relevance without adding TFLite runtime ops
```

验收指标必须固定为：

```text
normal deployment:
  clean = 100%
  D4 / rot_mirror = 100%
  fixed/medium stress = 100%
  real TFLite int8 replay = 100%

margin:
  int8_margin_min
  low_margin <=1 / <=2 / <=4 / <=8 / <=16 / <=32
  margin p01 / p05 / p10
  conflict-prototype count and usage

deployment cost:
  prototype_count
  feature_dim
  prototype_count * feature_dim
  estimated board avg/conservative us
  op whitelist pass

diagnostic only:
  extra-stress wrong count
  exported wrong stress images
```

重要边界：

- extra high-strength stress 仍只做诊断，不进入正式训练全集。
- 训练增强和部署 gate 必须分离；strong synthetic stress 可以用于训练鲁棒表示，但候选排序仍按 clean + D4 + fixed/medium deployment replay 收口。
- 负例 smoke 已经证明，直接把 Keras float logits 按旧 scale 当真实 int8 code 不可靠；所有训练指标必须由真实导出的 TFLite replay 收口。
- 3D parent logits 是当前 under2 主候选和控制组，不是必须坚持的最终几何。若 3D 的 `int8_margin_min=1` 无法消除，应优先尝试 4/6/8/12 维 raw code，而不是继续扩大 residual table。
- 不为 margin 在 TFLite encoder 图里引入 `sqrt/div/L2_NORMALIZATION` 等非主线算子；归一化、scale、relevance 尽量离线折叠或在 C++ prototype replay 中处理。

### 6.2 Prototype 层次

不要只做 3 个 parent prototypes。当前数据存在明显多模态结构：

```text
parent
  -> visual subclass
    -> cluster/proxy center
      -> optional dynamically discovered boundary center
```

推荐 prototype 粒度：

```text
K per visual subclass:
  K in {1, 2, 4, 8, 16}

dynamic boundary overrides:
  only if replay confirms a stable boundary conflict
  conflict clusters can receive extra centers
  teacher-disagreement clusters can receive guard centers
```

最终部署要扫 prototype_count：

```text
target:
  <= 96 prototypes: aggressive compression
  <= 192 prototypes: preferred first deploy target
  <= 384 prototypes: acceptable if board latency still passes
  > 512 prototypes: only temporary diagnostic, not V8 success
```

V7 fast int8 prototype rescue 的 920 prototypes 是强可用 fallback，但 V8 应努力把纯 embedding prototype 数量压到更小。

### 6.3 距离函数

首选：

```text
cosine distance on L2-normalized int8 embedding
```

备选：

```text
squared L2 on z-scored embedding
```

选择标准：

- cosine 更适合 ArcFace/SupCon/proxy。
- squared L2 更贴近 V7 Phase6 z-scored GAP prototype 实现。
- 最终以 int8 replay 和板端 microbench 为准。

## 7. 训练目标

V8 训练不应只有一个 loss。推荐总损失：

```text
L =
  lambda_proxy   * L_proxy_anchor_or_subcenter_arcface
+ lambda_supcon  * L_supervised_contrastive
+ lambda_d4      * L_d4_orbit_consistency
+ lambda_stress  * L_stress_consistency
+ lambda_vicreg  * L_vicreg_or_barlow_regularization
+ lambda_ms      * L_multi_similarity_hard_pairs
+ lambda_ctd     * L_ctd_region_weighting
+ lambda_qat     * L_quantization_robustness
+ lambda_lvq     * L_closed_set_prototype_margin
+ lambda_teacher * L_compiled_prototype_teacher
```

### 7.1 Proxy / angular margin loss

目的：给 embedding 一个可分的全局坐标系。

建议：

```text
subcenter_arcface:
  centers per visual subclass: 2-8
  angular margin: small first, then sweep
  scale: sweep with int8 robustness
```

不要只对 parent 做 3-center ArcFace。3 个 parent 太粗，会把同 parent 的多模态结构压成一团，导致 hard boundary 更差。

### 7.2 SupCon positive/negative 定义

推荐层次：

```text
strong positive:
  same original image across clean/D4/stress views

medium positive:
  same visual subclass

weak positive:
  same parent but different visual subclass

negative:
  different parent

candidate hard negative:
  old wrong parent if replay confirms it remains confusing
  CTD wrong parent if replay confirms it remains confusing
  nearest wrong prototype
  confusion pairs from V7 reports, rechecked under current embedding
```

同 parent 不同 subclass 只做弱 positive，不应强行完全重合。否则 prototype 压缩时容易丢掉局部边界。

### 7.3 D4 几何一致性

训练 batch 必须按 orbit 组织：

```text
for each original image i:
  views = [
    clean,
    rot90,
    rot180,
    rot270,
    mirror_lr,
    mirror_lr_rot90,
    mirror_lr_rot180,
    mirror_lr_rot270,
  ]
```

loss：

```text
z_bar = stop_gradient(mean(normalized embeddings of all D4 views))
L_d4 = mean(max(0, tau_d4 - cosine(z_view, z_bar)))
```

同时要求：

```text
prediction(view_j) == prediction(clean)
parent(view_j) == y_parent
```

这不是普通 augmentation，而是等价类约束。

注意：D4 consistency 不应无限强拉近。只要同一原图 8 个 view 已经达到 `tau_d4` 一致性，就应让额外容量服务 parent/subclass margin 和动态发现的边界 margin。否则可能把同 parent 的多模态结构或已观测局部边界抹平。

### 7.4 Stress 一致性

stress views 包括 noise、blur、camera blur/noise 等。训练时推荐两层约束：

```text
same-original stress consistency:
  stress view embedding close to clean/D4 orbit center

decision consistency:
  nearest prototype parent unchanged
```

但 stress 不应过强到抹掉真实可分特征。推荐规则：

```text
D4:
  strict decision invariance
  medium/high embedding consistency

stress:
  strict parent decision invariance
  weaker embedding consistency
  margin-based consistency, not full collapse
```

也就是说，blur/noise/camera stress 的主要硬门槛是 parent 决策不变；embedding 只需接近 clean/D4 orbit center 到足够 margin，不应被强制完全重合。stress consistency 权重应低于 D4 consistency，并按 mild -> full fixed stress -> boundary stress 做 curriculum。

### 7.5 CTD 的正确用法

CTD 不应作为全局 teacher，也不应被当成先验 hard-case 定义。它只应作为候选区域权重和动态 mining 信号：

```text
stable_both_correct:
  candidate low teacher weight
  preserve old behavior only if replay still supports it

preserve_old_correct_ctd_wrong:
  candidate parent-weight increase
  candidate anti-CTD negative constraint
  activate only if current replay confirms CTD would hurt this view/cluster

rescue_old_wrong_ctd_correct:
  candidate hard-weight increase
  candidate positive correction target
  allow extra local centers only if current replay confirms a repeated failure

both_wrong:
  use label only
  no teacher trust unless later evidence proves a teacher is reliable
```

V8 中 CTD 的作用更像 curriculum / sample importance / hard-pair candidate oracle，而不是要复制 CTD 的 softmax 分布，也不是要固定一套 preserve/rescue 子空间。

实现时必须区分 clean-region 与 view-region：

```text
region_clean_id:
  based on clean old/CTD correctness
  records coarse diagnostic group only; does not force a training policy

region_view_id:
  based on each clean/D4/stress view old/CTD correctness
  records per-view diagnostic group for dynamic mining and teacher trust checks

old_pred_per_view / ctd_pred_per_view:
  required for checking whether clean-region assumptions still hold after D4/stress

v7_rescue_gate_per_view:
  optional reference from V7 Phase6 rescue behavior
```

不要把 clean 上 `old wrong / CTD correct` 的关系无条件扩展到所有 rot_mirror/stress views。CTD 只能在对应 view 也可信、且当前 embedding/prototype replay 也显示有必要时，才作为 positive correction；否则只保留为候选 hard negative 或 sample-importance 信号。

### 7.6 Quantization-aware robustness

最终部署需要 int8。因此训练或后处理至少要覆盖：

```text
fake quantization on embedding
fake quantization on prototypes/proxies
distance tie margin loss
int8 replay after every top checkpoint
```

距离 tie margin：

```text
d_nearest_wrong - d_nearest_correct >= margin_q
```

这个 margin 要在 int8 embedding/prototype 上重算，而不是只看 float。

### 7.7 Closed-set prototype margin / LVQ-style loss

目的：让训练目标直接对齐最终 nearest-prototype parent decision。

对每个训练 view，在当前 active prototype/proxy 表上计算：

```text
d_pos = min distance to prototypes with correct parent
d_neg = min distance to prototypes with wrong parent
margin = d_neg - d_pos

L_closed_proto = softplus(alpha * (target_margin - margin))
```

实现建议：

- warmup 阶段用 learned subcenter proxies，避免早期 kmeans 表太不稳定；
- 每隔若干 epoch 用全集 embedding 重建 compiler 表，然后把 active prototypes 冻结为下一阶段 teacher；
- 对 wrong / low-margin / int8-flip views 提高权重；
- margin 同时在 float 与 fake-int8 路径上计算，最终以 int8 margin 为准。

这相当于把 LVQ 的大间隔原型学习思想放进 TensorFlow 训练循环，而不是只在训练结束后做 kmeans。

### 7.8 Compiled prototype teacher loss

目的：把 closed-set compiler 找到的最强决策程序蒸馏回 encoder，让下一轮模型更容易用更少 prototype 覆盖全集。

teacher 来自当前 best compiled prototype table：

```text
teacher signals:
  active correct prototype id
  nearest wrong prototype id
  float distance margin
  int8 distance margin
  gate region: high_margin / boundary_repair
  selected boundary candidate kind
```

训练 loss：

```text
L_teacher =
  CE(parent_logits_or_proxy_logits, y_parent)
  + KL(softmax(-teacher_distances / T), softmax(-student_distances / T))
  + margin_matching_loss(student_margin, teacher_margin)
  + gate_consistency_loss(student_gate, teacher_gate)
```

如果最终推理不保留 parent softmax head，`parent_logits_or_proxy_logits` 只是训练辅助项；导出时仍以 prototype distance 为准。

### 7.9 Quantized Large-Margin Prototype Learning

这是下一轮 margin closure 的主 loss。它要直接优化板端会执行的 int8 squared-L2 决策，而不是优化 float softmax 间隔。

训练侧近似：

```text
z_float = encoder(x)
z_q = clip(round(z_float / s + zp), -128, 127)
z_ste = z_float + stop_gradient((z_q - zp) * s - z_float)

p_q = active compiler prototypes in int8 code
d_c(z) = min squared_l2(z_q, p_q where parent == c)
d_pos = d_y
d_neg = min d_c for c != y
margin_q = d_neg - d_pos

L_q_margin = softplus(alpha * (target_margin_q - margin_q))
```

如果采用 raw-code head，让 Dense 直接输出近似 int8 code，则可用更简单的 STE：

```text
z_q = clip(round(z_float), -128, 127)
z_ste = z_float + stop_gradient(z_q - z_float)
```

但 raw-code head 必须在导出后用真实 TFLite output quantization 重放；训练中的 raw-code 数值不等于部署 output tensor，除非 converter calibration 后实际输出 scale/zero 与训练假设一致。

低 margin 加权：

```text
sample_weight =
  base_weight
  + w1 * I[int8_margin <= 1]
  + w2 * I[int8_margin <= 2]
  + w4 * I[int8_margin <= 4]
  + w8 * I[int8_margin <= 8]
  + w_conflict * I[nearest_wrong_proto is also necessary correct proto]
```

权重只来自当前 compiler replay，不来自人工 hard-case 先验。每轮训练后必须重新计算权重。

### 7.10 Int8 code-neighborhood margin

`int8_margin=1` 的本质风险是 code 点附近一个量化格点跳动就可能越过决策边界。因此只约束原点 margin 仍不够，下一轮应加入邻域 margin：

```text
N_D(r) = {delta in Z^D | ||delta||_inf <= r}
margin_neighborhood = min_delta margin_q(clip(z_q + delta))
L_neighborhood = softplus(alpha * (target_margin_q - margin_neighborhood))
```

计算策略：

```text
D <= 4:
  enumerate delta in {-1,0,1}^D

D in {6,8,12}:
  sample axis deltas, random corner deltas, and current nearest-wrong direction deltas

low-margin only:
  apply full neighborhood loss only to margin <= 8 or conflict views
```

这个 loss 的作用是直接增厚部署 code 空间的局部安全半径。它替代“把 extra high-strength stress 加入训练”的冲动：extra stress 只告诉我们哪里脆，真正训练时约束的是正常全集 view 在 int8 code 网格上的稳定性。

### 7.11 Proxy geometry regularization

Proxy/ArcFace/SupCon 的落地目标不是最终分类，而是给 code 空间提供不会挤成一团的几何先验。

推荐正则：

```text
proxy_norm:
  normalize proxies during loss computation

proxy_separation:
  for different parent proxies:
    cosine(proxy_i, proxy_j) <= tau_neg

soft_orthogonal_proxy:
  minimize ||P_norm P_norm^T - I|| off diagonal

subcenter_balance:
  avoid all samples of one subclass collapsing to a single active center
```

部署时不需要保留这些 proxy；它们只服务于训练，使 compiler 更容易用少量 int8 prototypes 覆盖全集。

### 7.12 Top-k prototype decision as robustness ablation

当前 nearest-min 决策会被单个 wrong-neighbor prototype 影响。kNN robustness 相关思想提示：可以测试每个 parent 的 top-k 距离聚合，而不是只取一个最近点。

候选决策：

```text
min:
  D_c = min distance to parent c

topk_mean:
  D_c = mean of k smallest distances to parent c

topk_trimmed:
  D_c = mean of smallest k distances, but ignore exact duplicate/tie artifacts

vote:
  among global top K prototypes, vote by parent, distance as tie-break
```

约束：

- 这是 ablation，不应先替代主路线。
- 必须用同一 prototype table 先离线 replay，看 low-margin 桶是否下降。
- 若 top-k 需要额外排序成本，C++ 实现只能维护 per-parent 小顶/小数组，不允许引入复杂 runtime 结构。
- 如果 top-k 明显降低 low-margin 但增加可接受的 replay 成本，再进入板端 microbench。

## 8. 数据与 batch 设计

### 8.1 Episode batch

推荐 batch 单位不是单张图，而是原图组：

```text
episode:
  select N original images
  include clean + all D4 views
  include M stress views per original
  optionally include current replay-mined boundary candidates
```

这样可以确保每个 batch 内有足够 positive、negative 和 orbit consistency；边界候选只在当前 replay 已经发现时加入。

### 8.2 Dynamic boundary sampling

默认不做 hard-group oversampling。只有当当前 embedding/prototype replay 发现可重复边界风险时，才启用动态采样权重：

```text
repeated failure or low-margin wrong-neighbor:
  increase sample weight

old/CTD disagreement confirmed on current view:
  increase contrastive/mining priority

both teachers unreliable:
  use label only; do not trust either teacher

otherwise:
  normal sampling
```

权重应由 replay 指标驱动，例如 nearest-wrong margin、per-view correctness、int8 flip、no-self/LOO failure，而不是由旧模型区域名固定决定。

### 8.3 Stress curriculum

建议先后顺序：

```text
stage warmup:
  clean + D4

stage consistency:
  add mild noise/blur

stage robustness:
  add full fixed stress list

stage boundary:
  focus on stress views where prototype margin is smallest
```

如果一开始把所有 stress 高权重压入，容易造成 embedding collapse 或过度平滑。

## 9. Prototype 构建与压缩

### 9.1 从训练到部署的 prototype 来源

可选来源：

```text
learned proxies:
  训练中作为参数，部署时直接导出

sample medoids:
  每个 cluster 选择真实样本 embedding 中最居中的点

k-means centers:
  对同 subclass/parent embedding 聚类；动态边界区域只有在 replay 证明必要时单独聚类

hybrid:
  learned proxies 初始化，medoid 校正，k-means 压缩
```

推荐首轮 hybrid：

```text
1. 用 subcenter/proxy loss 训练 embedding 和 proxies。
2. 在 clean+D4+stress 上提取 embedding。
3. 按 parent/subclass 分层聚类；对 replay 证明必要的动态边界候选单独聚类。
4. 每个 cluster 选 medoid 或 quantization-stable center。
5. 扫 K 和 threshold，导出 int8 replay。
```

### 9.2 Closed-set prototype compiler

在“数据集就是全集”的前提下，prototype 构建应显式进入 compiler 阶段。compiler 输入是某个 encoder 的全集 embedding cache，输出是部署候选表和训练反馈。

推荐候选池：

```text
base prototypes:
  kmeans / medoid / quant-stable medoid at k=32,48,64,96

boundary candidates:
  exact embeddings of current wrong views
  exact embeddings of top low-margin correct views
  clean+D4 orbit mean for wrong or low-margin originals
  same-parent medoids near wrong-neighbor conflict pairs
  correct-parent defense prototype for each nearest-wrong conflict
  optional V7 rescue prototype analog if replay proves useful
```

greedy 目标：

```text
score(add candidate) =
  fixed_float_wrong * A
  + fixed_int8_wrong * B
  + improved_low_margin * C
  - introduced_new_float_wrong * D
  - introduced_new_int8_wrong * E
  - prototype_count_cost
  - latency_cost
```

硬约束：

```text
no new wrong views
no int8 flip relative to float winner
deterministic tie-breaking stable
prototype_count lower than V7 fallback unless used only as diagnostic
```

compiler 产物：

```text
compiled_candidate_results.csv
compiled_boundary_events.csv
best_compiled_v8_prototype_params.npz
selected_candidate_trace.json
int8_replay_failures.csv
```

compiler 不只是后处理。它的 selected candidates、nearest wrong prototypes、low-margin views 和 int8 flip points 必须回流到下一轮训练，形成 `L_closed_proto`、动态采样权重和 boundary curriculum。

### 9.3 不能用泄漏式 prototype 评估

V7 all_views prototype 能 100%，但那是 deterministic memory classifier。V8 评估必须区分：

```text
closed-set memory:
  prototype table contains the same original image or same view

no-self-view:
  query view 不能使用同一原图同一 view 的 prototype

approx_leave-one-original-out:
  query 原图的所有 views 都从 prototype table 移除
  prototype/proxy/cluster table 仍可能来自全数据训练或全数据聚类

compressed prototype:
  prototype_count limited, not one view one prototype

strict_leave-one-original-out:
  query 原图不参与 prototype/proxy/cluster 构建
  需要重新构建或增量更新 prototype table
  这是衡量泛化的更强指标
```

如果目标是 unseen-original 泛化，只有 compressed + no-self-view / strict_leave-one-original-out 仍稳定，才说明 embedding 学到了可泛化结构。当前目标是全集闭集部署，因此 compressed closed-set 100% + int8 100% 是主 gate；approx_LOO / true rebuild-LOO 用于诊断边界质量、过拟合程度和下一轮训练权重，不能再反过来淘汰一个闭集部署候选。

### 9.4 Prototype selection objective

压缩时不能只最大化 clean accuracy。目标函数：

```text
score =
  clean_all_correct_bonus
  + rotmirror_all_correct_bonus
  + stress_all_correct_bonus
  - prototype_count_penalty
  - int8_flip_penalty
  - board_latency_penalty
  - margin_risk_penalty
```

必须硬约束：

```text
clean = 304/304
rot_mirror_min = 1.0
stress_min = 1.0
int8 replay = 100%
```

如果为了压缩 prototype 导致任一固定 stress view 下降，不应作为主候选。

## 10. 验证体系

V8 的验证要分 8 个维度。当前闭集全集前提下，closed/int8/compiler replay 是部署主 gate；LOO 系列是诊断维度。

### 10.1 Float closed-set sanity

目的：确认训练没有失败。

要求：

```text
clean = 304/304
rot_mirror_min = 1.0
stress_min = 1.0
```

这只是 sanity，不是成功。

### 10.2 No-self-view replay

目的：排除“同 view 记忆”。

要求：

```text
for each query view:
  remove prototype from the exact same original + same transform
  classify by remaining prototypes
```

### 10.3 Leave-one-original-out

目的：排除“同原图其他 view 泄漏”，或在当前闭集前提下诊断哪些 original 几何上依赖同源覆盖。

要求：

```text
for each original image:
  remove all prototypes from that original
  classify all its clean/D4/stress views
```

这项可能比 deterministic benchmark 更难。当前不把它作为部署 gate，但它仍然重要：如果 LOO 失败高度集中，说明这些 original 需要 boundary repair、orbit consistency 或更厚 parent margin。

注意：如果 prototype 是 kmeans/centroid，简单按 `prototype_sample_index` 排除同原图是不充分的，因为 centroid 已经吸收了 hold-out 原图的信息。对 kmeans/learned center 的最终 LOO 必须使用 rebuild-LOO：

```text
for each held-out original:
  remove all views of that original from prototype-building pool
  rebuild prototypes/clusters from remaining originals
  classify all views of held-out original
```

现有 approx_LOO 仍可用于快速 ranking，但不能作为 unseen-original 泛化闭环；true rebuild-LOO 是诊断项，不是当前全集部署的淘汰项。

### 10.4 Prototype compression sweep

目的：证明 embedding 可压缩。

要求扫描：

```text
K per subclass:
  first pass: 1, 2, 4, 8, 16
  V8-B evidence pass: 24, 32, 48, 64, 96, 128, 192
prototype_count caps: 48, 96, 192, 384, 512, 768, 1024
distance: cosine, squared_l2
quant scale: 4, 8, 12, 16, 24, 32, 48, 64, 96, 128
prototype source:
  learned proxy
  k-means center
  medoid
  quantization-stable medoid
  k-center / farthest-first coreset
  replay-confirmed boundary reserved center
  hybrid proxy-init + medoid-snap
```

排序时不要把 `k<=16` 当成先验上限。当前 V8-B 结果显示 `k=16` 是有信息但不足的压缩点；`k=24..64` 是判断能否进入 `<=384 prototypes` 主线的关键区间，`k=96..192` 是上限诊断和 teacher/prototype-distill 生成区间。

### 10.5 Closed-set compiler replay

目的：验证 prototype compiler 是否真的把全集编译成可部署表，而不是只在 summary 指标上看起来更好。

要求：

```text
base table result:
  float wrong count
  int8 wrong count
  min/percentile margins

compiled table result:
  selected reserved prototypes
  fixed wrong views
  introduced new wrong views
  gate trigger count if using cascade
  float/int8 decision agreement
  deterministic tie-breaking trace
```

必须输出事件级 CSV。任何“闭合到 100%”都要能追溯到具体 prototype 和具体 view，否则不能进入训练 teacher。

### 10.6 Int8 replay

目的：提前发现量化翻转。

要求：

```text
float embedding -> int8 embedding
float prototype -> int8 prototype
distance in int32 accumulator
same decision as deployment C++ path
```

replay 必须覆盖与部署一致的后处理：

```text
if cosine:
  strict path must not put runtime sqrt/div/reduce normalization into TFLite
  prefer offline-normalized prototypes + int8 dot approximation or switch to raw squared L2
  replay the exact int8 dot-product or normalized approximation used in C++

if squared_l2:
  replay z/int8 feature quantization, int32 distance, tie-breaking order

tie breaking:
  fixed and serialized in deploy bundle
  no Python-only argmin behavior hidden from C++ path
```

### 10.7 Board latency / microbench

目的：确认不只是主机可行。

要求：

```text
prototype distance MACs
prototype table bytes
header size
host C++ microbench
board-side timing if available
estimated total latency
```

V7 fast int8 prototype rescue 的距离成本是 `920 * 24 = 22080` distance dimensions，V8 pure embedding 目标应低于这个数量级。

V8 不能只比较 prototype MAC，还必须比较完整推理成本：

```text
total_latency =
  backbone_tflite_us
  + embedding_head_us
  + normalization_us  # should be 0 in strict raw-export path
  + prototype_distance_us
  + decision_overhead_us
```

如果使用 cosine，部署侧应避免在 TFLite encoder 内运行 `sqrt/div/reduce` 归一化。推荐训练时使用 L2 normalize / angular loss，导出时输出 raw embedding，再通过离线 prototype compiler、int8 dot product 或 squared L2 的等价近似完成部署路径，并通过 C++ replay 确认没有 int8 flip。

### 10.7.1 TFLite operator audit

每个进入 top list 的 V8 checkpoint 都必须导出一次 float/int8 TFLite，并记录实际 op sequence。op audit 与 accuracy 同级，不是事后附录。

严格通过条件：

```text
all exported ops are in the strict recommended whitelist
normalization tail is absent from the TFLite graph
BatchNorm/activation are folded or fused as expected
prototype decision is outside TFLite and covered by exact C++ replay
```

必须重点拦截的非主线尾部：

```text
manual L2 norm:
  MUL, SUM, SQRT, MAXIMUM, DIV

tf.nn.l2_normalize fallback:
  L2_NORMALIZATION

UnitNormalization-style fallback:
  SQUARE, SUM, RSQRT, MINIMUM, MUL
```

`MUL` 本身在白名单内，可以用于明确收益的缩放或融合结构；但若它与 `SUM/SQRT/DIV/MAXIMUM` 共同构成 runtime normalization，应整体判为 strict op fail。`L2_NORMALIZATION` 可以单独记录为 board-supported fallback，但不能替代严格推荐算子的默认结论。

### 10.8 Pure prototype 下的 backbone 压缩空间

V7 本质上是在旧 fast softmax/GAP 空间上做 int8 prototype rescue。它已经达到 deterministic 100%，但 backbone 仍是旧 `[6,12,24]` fast 模型，prototype 只是补丁式后处理。V8 pure prototype 如果成功，理论价值不只是压缩 prototype 表，还包括让 backbone 不再承担完整 softmax 线性边界，而只负责输出稳定、可检索、几何一致的 embedding。

客观约束：

```text
V7 fast baseline:
  spacetodepth_conv [6,12,24]
  backbone avg ~= 5296 us
  backbone p95 ~= 7845 us
  total avg with int8 prototype rescue ~= 5.7 ms
  conservative total p95 ~= 8.3-9.0 ms

V8 pure prototype target:
  keep deterministic clean/D4/stress 100%
  keep int8 replay 100%
  prototype_count should drop below 920
  closed/no-self margins should be stable
  true rebuild-LOO is diagnostic unless future requirements change
```

基于本地 `spacetodepth_conv [8,16,32]` board benchmark 和当前 estimator 的 `sum(filters)^1.05` 缩放，纯 backbone 速度估计如下：

```text
[8,16,32]: avg ~= 7163 us, p95 ~= 10611 us
[7,14,28]: avg ~= 6226 us, p95 ~= 9223 us
[6,12,24]: avg ~= 5296 us, p95 ~= 7845 us
[5,10,20]: avg ~= 4373 us, p95 ~= 6478 us
[4,8,16]:  avg ~= 3460 us, p95 ~= 5125 us
[3,6,12]:  avg ~= 2558 us, p95 ~= 3789 us
[2,4,8]:   avg ~= 1671 us, p95 ~= 2475 us
```

越小的模型，估计越可能乐观，因为 TFLite Micro 的 operator overhead、memory movement、quant/dequant 和调度开销不会完全随 filters 缩放。因此 `[3,6,12]` 以下只能作为探索上限，不能作为默认可达结论。

加入 V8 prototype 后，如果能压到 `192 x 16`，prototype 距离约为：

```text
192 * 16 = 3072 distance dimensions
20 cycles/dim @ 1GHz ~= 61 us
50 cycles/dim @ 1GHz ~= 154 us
```

这说明 V8 的速度主变量仍是 backbone，不是 prototype 后处理。关键分档：

```text
conservative realistic:
  [5,10,20] + <=192/384 prototypes
  avg ~= 4.4-4.6 ms
  p95 ~= 6.5-6.7 ms

aggressive primary target:
  [4,8,16] + <=192/384 prototypes
  avg ~= 3.5-3.8 ms
  p95 ~= 5.2-5.5 ms

exploratory lower bound:
  [3,6,12] + <=384/512 prototypes
  avg ~= 2.6-2.8 ms
  p95 ~= 3.8-4.1 ms
  risk: capacity loss, prototype_count inflation, closed margin thinning, int8 margin thinning

not a default mainline:
  [2,4,8]
  speed estimate is attractive, but likely requires large memory-style prototype table
```

因此 V8 Phase B 的客观主攻目标不是“越小越好”，而是先验证：

```text
first realistic replacement:
  [5,10,20], embedding_dim 16/24, <=192 or <=384 prototypes

best useful upgrade if it passes:
  [4,8,16], embedding_dim 16/24, <=384 prototypes

only explore after above:
  [3,6,12], allow <=512 prototypes, closed/int8 margin must be watched closely
```

如果 `[4,8,16]` 能保持 deterministic 100%、int8 replay 100%、closed/no-self margin 稳定，则 V8 从 V7 的 `~5.7ms avg / ~8.3-9.0ms p95` 降到约 `~3.5ms avg / ~5.3ms p95`，这才是纯 prototype 路线相对 V7 的真正工程收益。true rebuild-LOO 仍继续报告，但只作为未来泛化需求的风险说明。

## 11. V8 分阶段计划

### Phase A: Frozen fast backbone metric head

目的：最快验证 pure embedding 是否比 V7 GAP prototype 更可压缩。

做法：

```text
freeze fast old backbone
take GAP feature dim=24

A0 no-training compression baseline:
  use frozen fast GAP directly
  run whitening / diagonal metric / medoid / k-means compression
  measure <=96/192/384 prototype upper bound before training anything

A1 shallow metric calibration:
  train only diagonal metric or tiny projection 24 -> 16/24/32
  losses = proxy/subcenter + D4 consistency
  no full stress, no CTD teacher, no QAT

A2 dynamic boundary structure:
  add replay-confirmed sample weighting
  add dynamic hard negative mining
  reserve extra centers only for replay-confirmed boundary candidates

A3 stress + int8 closure:
  add stress curriculum
  add fake quant / int8 replay
  export C++ prototype table and microbench
```

优点：

- 实验快。
- 可直接对比 V7 Phase6 fast backbone prototype rescue。
- 如果 frozen GAP 上能把 920 prototypes 压到 <=192 且保持 100%，说明 V8 方向很强。
- 每个子阶段只引入少量变量，失败时更容易归因。

风险：

- 原 GAP 不是为 embedding 训练的，压缩上限可能有限。
- 如果 Phase A 失败，不代表 V8 失败，只代表需要端到端训练。
- 如果 A0/A1 已经显示 frozen GAP 无法压缩，应尽早停止 Phase A 全家桶调参，转入 Phase B。

### Phase B: End-to-end tiny embedding backbone

目的：训练真正的 V8 encoder，使 embedding 更适合 compressed prototype decision。

做法：

```text
initialize from fast/stable backbone if compatible
replace parent softmax head with embedding/proxy training heads
train clean + D4 + stress orbit batches
use QAT/fake-quant late-stage fine-tuning
```

候选结构：

```text
backbone candidates:
  conservative: spacetodepth_conv [5,10,20]
  primary aggressive: spacetodepth_conv [4,8,16]
  exploratory: spacetodepth_conv [3,6,12]
  lower-bound probe only: spacetodepth_conv [2,4,8]

embedding_dim: 16, 24, 32
training projection: GAP -> dense -> optional loss-only L2Norm
strict export projection: GAP -> dense(raw embedding)
optional bottleneck: depthwise separable conv + GAP
```

成功标准：

```text
clean/rot/stress deterministic 100%
compressed prototype <=192 preferred
int8 replay 100%
strict recommended TFLite ops pass
board_total_conservative_us <= 4000
board_total_avg_us <= 2000 preferred stretch target
total estimated latency must include embedding head / normalization fallback if any / prototype distance
```

### Phase B2.5: Closed-set prototype compiler and boundary closure

目的：在当前最强 encoder 上先把全集 closed-set wrong 归零，再把 compiler 结果反向喂给下一轮训练。

当前触发条件已经满足：

```text
best current candidate:
  [6,12,24] d24 seed20260522
  kmeans k=64
  prototype_count=512
  wrong views=4/6688
  int8 wrong views=4/6688
```

做法：

```text
1. build base tables:
   source = kmeans, medoid, quant_medoid
   k = 32,48,64,96

2. collect boundary candidate pool:
   current wrong views
   top low-margin correct views
   same-original clean+D4 orbit means
   same-parent defense medoids near nearest-wrong conflicts

3. greedy select reserved prototypes:
   maximize fixed wrong/int8 wrong
   increase minimum margin on low-margin views
   reject candidates that introduce any new wrong

4. export compiled table:
   main table + reserved boundary table
   deterministic tie-breaking
   int8 replay trace

5. feed back to training:
   boundary sample weights
   active correct prototype ids
   nearest wrong prototype ids
   target int8 margins
```

成功标准：

```text
closed clean/D4/stress = 100%
int8 replay = 100%
prototype_count < 920 preferred, <=512 acceptable for method proof
remaining low-margin views explicitly listed
```

这一步优先于继续盲目扩大 GPU random sweep。原因是现有 embedding 只差 4 个 view，先用 compiler 证明这些错误能否由少量 reserved prototypes 修复，才知道下一轮训练应该优化什么。

### Phase C: Prototype compression and rescue fallback comparison

目的：决定 V8 是否超过 V7 fallback。

比较对象：

```text
V7 fast int8 prototype rescue:
  920 prototypes
  24 dim
  22080 distance dimensions
  int8 table = 22080 bytes
  estimated board avg = about 5.7 ms
  conservative board p95 = about 8.3-9.0 ms
  deterministic 100%

V8 pure embedding prototype:
  target <=192 prototypes
  16-32 dim
  deterministic 100%
  closed/no-self margin stable
  total estimated latency <= V7 fallback, or clearly within the original 15/20ms target
```

如果 V8 pure embedding 达不到 deterministic 100%，但 compiler 指出只需极少 boundary prototypes，可继续作为训练方向；如果 prototype_count 接近或超过 V7 fallback 且延迟无优势，部署仍使用 V7 fast int8 prototype rescue。

### Phase D: Distill prototype decision to parametric head

目的：如果 prototype table 仍然太大，则把 prototype decision distill 回一个小 parametric head。

做法：

```text
teacher = V8 prototype classifier decisions + margins
student = same tiny embedding backbone + small parent/subclass head
loss = ground-truth parent + prototype soft target + margin matching + D4/stress consistency
```

这个阶段是备选，不是第一优先级。原因是 parametric head 正是旧路线失败的地方，必须先有强 prototype teacher 再蒸馏。

### Phase E: Board export

目的：生成可部署 bundle。

产物：

```text
v8_embedding_model_int8.tflite
v8_prototype_params.hpp
v8_embedding_microbench.cpp
deploy_bundle_summary.json
deploy_stress_summary.csv
prototype_compression_sweep.csv
int8_replay_failures.csv
```

## 12. 建议脚本与产物布局

建议新增：

```text
model_training/train_v8_end_to_end_embedding.py
model_training/evaluate_v8_embedding_prototypes.py
model_training/run_v8_extended_prototype_sweep.py
model_training/run_v8_closed_set_prototype_compiler.py
model_training/generate_v8_embedding_candidates.py
model_training/export_v8_embedding_prototype_bundle.py
model_training/run_v8_phaseB_focus_tmux.sh
model_training/run_v8_extended_sweep_tmux.sh
```

其中前三个已经是当前主线脚本；`run_v8_closed_set_prototype_compiler.py` 是下一步应补的关键脚本。

建议实验目录：

```text
model_training/experiments/v8_pure_embedding_YYYYMMDD_NNNN/
  launch_config.json
  stageA_frozen_fast_metric/
  stageB_end_to_end_embedding/
  prototype_sweeps/
  deploy_bundle_int8/
  reports/
```

核心中间文件：

```text
v8_region_bundle.npz:
  paths
  y_parent
  y_sub
  old_pred
  ctd_pred
  old_pred_per_view
  ctd_pred_per_view
  v7_rescue_gate_per_view
  region_clean_id
  region_view_id
  sample_weight
  dynamic_negative_parent
  dynamic_negative_weight

v8_embedding_cache.npz:
  paths
  transform_id
  stress_id
  embedding_float
  embedding_int8
  parent
  subclass

v8_prototype_table.npz:
  prototypes_float
  prototypes_int8
  prototype_parent
  prototype_subclass
  prototype_cluster
  quant_scale
  distance_metric

v8_compiled_prototype_table.npz:
  main_prototypes_float/int8
  boundary_prototypes_float/int8
  prototype_parent
  prototype_source_kind
  prototype_source_path
  selected_candidate_trace
  gate_threshold
  tie_break_policy
  float_margin
  int8_margin
```

## 13. 第一轮默认实验矩阵

首轮不要开过宽，先验证关键假设。

### 13.1 Phase A frozen fast backbone

```text
backbone:
  fast old model from V7 Phase6

A0 no-training baselines:
  raw GAP + cosine / squared_l2
  z-scored GAP + squared_l2
  whitened GAP
  diagonal Mahalanobis / per-dim reliability weighting
  LDA/NCA-like linear metric if stable

A1 shallow projection:
  embedding_dim = 16, 24, 32
  metric = diagonal or 24 -> embedding_dim projection
  losses = proxy/subcenter + D4 margin consistency + VICReg

A2 dynamic boundary:
  add CTD region weighting
  add dynamic hard negative mining
  reserve extra centers only for replay-confirmed boundary candidates

A3 stress/int8:
  add stress consistency curriculum
  add fake quant
  run int8 replay every top checkpoint

prototype compression:
  K per subclass = 1, 2, 4, 8, 16, 24, 32, 48, 64, 96
  caps = 96, 192, 384, 512, 768
  sources = learned proxy, k-means, medoid, quant-stable medoid, hybrid

quant:
  int8 embedding/prototype replay every top checkpoint
```

### 13.2 Phase B end-to-end

Phase B 已经启动并证明方向有效。后续不再把 Phase A 作为阻塞项，而是用 `[6,12,24]` method-control + compiler 找到训练目标，再向 `[5,10,20]` / `[4,8,16]` 压缩。

```text
architectures:
  spacetodepth_conv [5,10,20] + embedding head
  spacetodepth_conv [4,8,16] + embedding head
  spacetodepth_conv [3,6,12] + embedding head as exploratory lower bound
  keep [6,12,24] as method-control before compressing

training:
  warmup clean+D4
  add stress
  add closed-set prototype margin after compiler table exists
  add dynamic boundary mining only from replay/compiler events
  late fake quant

prototype caps:
  [5,10,20]: <=192 preferred, <=384 acceptable
  [4,8,16]: <=384 acceptable if closed/int8 margin pass
  [3,6,12]: <=512 diagnostic only; not a mainline winner without strong closed/int8 margin
  [6,12,24]: method-control only; prove margin/prototype strategy before speed compression

resource envelope:
  allocate more queued jobs to GPU Phase B/B2/B2.5 training than to CPU sweeps
  use low CPU thread counts per TensorFlow worker to avoid starving GPU input/host scheduling
  CPU C0/compiler sweeps should be bounded to top embeddings first, especially when GPU memory is near full
```

### 13.3 Phase B2.5 compiler matrix

首轮 compiler 不追求宽，而追求闭合当前最强候选：

```text
input params:
  experiments/v8_phaseB_focus_tf_function_20260519_0003/
  stageB_focus_end_to_end_embedding/c6_12_24_d24_s20260522/
  best_v8_embedding_prototype_params.npz

base sources:
  kmeans
  medoid
  quant_medoid

base k:
  32, 48, 64, 96

candidate pool:
  current wrong views = all
  low-margin correct views = top 64 or top 128
  orbit means = wrong/low-margin originals
  defense medoids = nearest-wrong conflict neighborhoods

reserved budget:
  4, 8, 16, 32

required reports:
  compiled_candidate_results.csv
  compiled_boundary_events.csv
  selected_candidate_trace.json
  best_compiled_v8_prototype_params.npz
```

### 13.4 Selection priority

排序顺序：

```text
1. clean/rot/stress deterministic 100%
2. int8 replay 100%
3. prototype_count lower
4. no-self-view and low-margin stability higher
5. total latency lower
6. margin larger
7. true rebuild-LOO diagnostic not catastrophically worse, if future unseen-original requirements might matter
```

clean-only 最好不能作为 winner。

## 14. 失败判据与回退策略

### 14.1 不能宣布成功的情况

以下都不是 V8 成功：

- 只用 all_views memory prototype 做到 100%，但 prototype_count 接近 6688。
- clean 100 但 rot_mirror_min < 1.0。
- float 100 但 int8 replay 有 flip。
- prototype table 或 embedding head 太大，完整板端估计超过 V7 fallback 或原始延迟目标。
- 在全集闭集上不能达到 clean/D4/stress 100%，却用 true rebuild-LOO 或 clean accuracy 转移主指标。
- no-self-view 一移除就大幅下降，说明表结构高度依赖同 view 记忆；这不必然淘汰闭集部署候选，但必须在风险报告中说明。
- CTD-derived weighting makes any previously correct replay region worse.

### 14.2 如果 V8-A 失败

可能原因：

- frozen GAP 本身不适合压缩。
- projection head 太弱。
- D4/stress consistency 权重过强导致 collapse。
- positive 定义太粗，把不同 subclass 拉得过近。

处理：

```text
reduce lambda_d4/lambda_stress
increase VICReg variance weight
split parent into subclass/subcenter proxies
switch to end-to-end Phase B
```

### 14.3 如果 V8-B 失败

可能原因：

- backbone capacity 不够同时满足 D4/stress margin 和 replay-confirmed boundary margin。
- int8 embedding 维度太小。
- prototype caps 太激进。
- 训练目标没有直接优化 true-parent prototype 与 nearest-wrong-parent prototype 的部署距离 margin。
- compiler 候选池没有覆盖 wrong/low-margin views。
- kmeans/centroid 的 approx_LOO 被误读为 true rebuild-LOO 泛化证据。

处理：

```text
increase embedding_dim from 16/24 to 32/48
allow <=384 prototypes
add parent prototype margin loss
run closed-set prototype compiler
add replay-confirmed boundary reserved centers
distill from compiled prototype teacher
run true rebuild-LOO only as diagnostic on top candidates
keep V7 fast int8 prototype rescue as deployment fallback
```

## 15. 与当前部署候选的关系

当前可部署优先级已经改变：

```text
new primary candidate:
  V8 parent-logit residual memory
  spacetodepth_conv [2,4,8] parent_int8.tflite
  deterministic clean/rot/stress 100%
  int8 replay clean/rot/stress 100%
  2124 prototypes, dim=3
  int8 prototype table=6372 bytes
  estimated board avg=1393us
  conservative board estimate=1710us

medium-inclusive primary variant:
  fixed stress + medium stress all 100%
  2659 prototypes, dim=3
  int8 prototype table=7977 bytes
  conservative board estimate=1742us

table-cleaned variants:
  regular fixed stress prune_unused = 2049 prototypes, dim=3
  regular fixed stress smallest quick cleanup = 2042 prototypes, dim=3
  medium-inclusive cleanup = 2574 prototypes, dim=3
  all keep deterministic/int8 100%
  all keep int8_margin_min=1

fallback deploy candidate:
  V7 fast backbone + Phase6 int8 prototype rescue
  deterministic clean/rot/stress 100%
  920 prototypes, dim=24
  int8 prototype table=22080 bytes
  estimated board avg=about 5.7 ms
  conservative board p95=about 8.3-9.0 ms
```

V8 现在已经从“下一代训练路线”变成新的主部署候选。V7 fast int8 prototype rescue 仍保留为 fallback，而不是被否定。

V8 应在下面任一条件满足时成为新主线：

```text
condition A:
  deterministic 100%
  int8 replay 100%
  prototype_count <= 192
  latency lower than V7 fallback

condition B:
  deterministic 100%
  int8 replay 100%
  prototype_count <= 384
  closed/no-self margins stable
  true rebuild-LOO reported as diagnostic
  estimated latency <= V7 fallback
  and definitely <= 15ms, or <=20ms only if stress remains 100%
```

上述旧条件中的 prototype_count 上限是按 d24 embedding 成本写的。parent-logit memory 的 feature_dim 只有 3，因此 `2124 x 3 = 6372` distance MACs 反而小于 V7 的 `920 x 24 = 22080`，也小于早期 V8 d24 表。后续排序必须按 `backbone latency + prototype_count * feature_dim` 和实测板端时间，而不是单看 prototype_count。

true rebuild-LOO 可作为额外研究指标，但在“数据集就是全集”的设定下不作为部署淘汰门槛。

## 16. 最小可执行下一步

2026-05-20 更新：under2 目标已经由 parent-logit residual memory 路线达成。prototype 剪枝/合并又确认了一个边界：表清理可以小幅降低 prototype_count，但不能根治 `int8_margin_min = 1`。因此完整 closed-set compiler 仍可继续优化表规模和事件审计，但当前 margin 主线应转向训练阶段的 int8 code 几何。

```text
model_training/train_v8_parent_classifier.py
model_training/run_v8_parent_classifier_tmux.sh
model_training/compile_v8_parent_logits_memory.py
model_training/prune_merge_v8_logit_prototypes.py
model_training/estimate_v8_board_time.py

primary deployment candidate:
  experiments/v8_parent_logits_memory_20260520_0003/
    s2d_c2_4_8_s20260931/
      best_parent_logits_memory_params.npz

primary table-cleaned candidate:
  experiments/v8_parent_logits_prune_merge_20260520_0001/
    primary_quick/
      best_pruned_merged_parent_logits_params.npz

medium-inclusive candidate:
  experiments/v8_parent_logits_memory_combined_stress_20260520_0001/
    s2d_c2_4_8_s20260931/
      best_parent_logits_memory_params.npz

medium-inclusive table-cleaned candidate:
  experiments/v8_parent_logits_prune_merge_20260520_0001/
    combined_quick/
      best_pruned_merged_parent_logits_params.npz
```

当前最小下一步已经从“训练能否达到 2ms + 100%”转为“部署打包和板端实测”：

```text
1. 把 `parent_int8.tflite`、`best_parent_logits_memory_params.npz` 或剪枝后的 `best_pruned_merged_parent_logits_params.npz` 转成板端 C/C++ 可加载资源。
2. 在板端实现 int8 squared-L2 replay：3D int8 logits feature、3D int8 prototype table、parent label table。
3. 固定 tie policy；当前主候选已有 `int8_margin_min = 1`，但板端仍应保持与 Python replay 一致的 nearest-parent 规则。
4. 用真实 2K0300 board 跑 encoder + prototype replay 端到端 benchmark，替代当前 `1705us / 1737us` 到 `1710us / 1742us` 的保守估算区间。
5. 如果实测 >2ms，优先压 prototype replay 的 memory layout、loop unroll、parent-wise min update；其次考虑只部署常规 stress 2049/2124 表，而不是回到更宽 CNN。
6. 如果目标是提高安全 margin，不再继续单纯扩大剪枝/合并搜索；转入 parent-logit encoder 的 closed-prototype margin / fake-int8 code separation 训练。
```

2026-05-19 的 V8-B 结果已经证明 pure embedding + prototype 方向有信号，而且用户已明确“数据集就是全集”。2026-05-20 的 under2 结果进一步说明：在闭集前提下，最有效部署形态是“tiny 推荐算子 encoder + 低维 int8 residual memory”，而不是继续扩大 embedding 维度或追求泛化式 LOO。

当前研究后续应升级为 Quantized Large-Margin Prototype Learning + compiler replay。它已经从“证明能否闭合”的主线 gate 变成“把 `margin_min=1` 变厚、减少低 margin 事件、继续压表”的优化项：

```text
V8 QLMPL margin closure:
  use all originals because deployment set is complete
  use current int8 prototype table as active compiler teacher
  identify low-margin clean+D4+stress views, not only wrong views
  optimize true-parent vs nearest-wrong-parent int8 distance margin
  add int8 code-neighborhood margin for low-margin/conflict views
  use proxy/subcenter/SupCon only as geometry warmup, not as deploy classifier
  penalize conflict pairs by moving code geometry, not by deleting necessary prototypes
  evaluate raw code dims 3 control, 4, 6, 8, 12
  treat parent-code qanchor D4 as a negative control, not as the D4 mainline
  keep simple top-k per-parent scoring closed as a negative ablation
  revisit hierarchy only as budget-aware second-stage conflict filtering
  keep strict recommended TFLite ops in the encoder

V8-B3 original-disjoint episodic training:
  optional research branch only if future requirements reopen unseen-original generalization
```

更新后的最小闭环按顺序执行：

```text
1. 保留 `prune_merge_v8_logit_prototypes.py` 作为表清理和低 margin 事件审计工具。
2. 新增或扩展 teacher-cache 导出：对每个 view 记录 nearest correct / nearest wrong prototype、int8 margin、low-margin bucket、conflict-prototype usage。
3. 在下一版训练脚本中实现 `L_q_margin = softplus(alpha * (target_margin_q - (d_wrong - d_correct)))`，并在 STE/fake-int8 code 上计算。
4. 对 margin <=8 和 conflict views 加 `L_neighborhood`，先在 D=3/4 枚举 `{-1,0,1}^D`，D=6/8/12 采样邻域。
5. 用 subcenter proxy / Proxy Anchor / SupCon / D4 consistency 做 warmup，让 raw code 空间先具备可分结构。
6. 扫 raw code dim：3 control、4、6、8、12；D4 不再用 parent-code qanchor 放大，改为真实 raw-code head + orbit/neighborhood margin；每个候选都 export 真实 TFLite 后再 compiler replay。
7. 将简单 top-k per-parent distance 标记为负例关闭；若继续做分层检索，只允许在 `k=1` 主决策后对 replay-confirmed tie/conflict 做预算感知二级过滤。
8. 继续使用严格推荐算子 encoder；不要为了 margin 在 TFLite 图里加入 `sqrt/div/L2_NORMALIZATION/reduce`。
9. 每轮训练后重新运行 compiler + prune/merge replay，比较 `prototype_count`、`low_margin <=1/2/4/8/16`、`int8_margin_min`、p01/p05/p10、板端估算。
10. 最终与 V7 fast int8 prototype rescue 做完整对比：prototype_count、feature_dim、table bytes、distance MACs、backbone latency、int8 replay、margin risk。
```

首轮结果应该回答：

```text
Q1: QLMPL 能否把 primary 的 low_margin <=1 从 23 降到 0？
Q2: combined 表的 low_margin <=1/2/4/8 能否同步下降，而不增加或少量增加 prototype_count？
Q3: 3D parent logits 是否有足够 code 空间，还是必须升到 4D/6D/8D 才能稳定增厚 margin？
Q4: int8-neighborhood margin 是否能降低 extra-stress diagnostic wrong count，同时不把 extra-stress 纳入训练？
Q5: 预算感知二级过滤是否能在不替换 `k=1` 主决策的前提下减少 single wrong-neighbor brittleness？
Q6: total estimated latency 是否仍 `<=2ms conservative`，而不是只改善训练侧 margin？
```

2026-05-20 追测后的优先级修正：D4/PCA、D4 parent-code qanchor、D4 qpair/neighborhood、dynamic qpair、gated top-k、orbit consistency 都不能把高压错误率推进到目标区间，D3 qpair 也只有微弱收益；因此下一轮不应继续局部加权这些 loss。优先做 compiler teacher cache + budget-aware QLMPL：用 normal replay 产生 nearest-correct / nearest-wrong / low-margin / neighborhood 训练信号，用 D24/D16/D12 retained timeout candidates 提供 teacher/projection/upper-bound 几何参考，然后在 strict-op encoder 内做 normal-only multi-teacher distillation / teacher-agreement conflict separation，硬筛 normal int8 replay 100%、`<=2ms conservative` 和高压诊断错误率。第一轮 normal-only multi-teacher teacher cache 使用 retained D24/D16/D12 对正常部署行投票，产生 `2306` 个动态 qpair 事件，不读取高压样本；续训后得到当前 best valid under2 点：`967 x 4`、`1971us`、normal int8 replay 100%、`int8_margin_min=4`、高压 `31.30% / 10.80%`，并保持推荐算子集合 `SPACE_TO_DEPTH + CONV_2D + MAX_POOL_2D + MEAN + FULLY_CONNECTED`（`DELEGATE` 仅为 interpreter 记录）。这证明 multi-teacher conflict separation 是比单独 qpair/orbit/top-k 更有效的主线，但直接变体已经平台化：加 orbit 变成 `882 x 4`、`1964us`、高压 `31.55% / 11.06%`；把 qpair 权重加到 `0.00025` 变成 `869 x 4`、`1963us`、高压 `31.75% / 11.25%`；单票高置信 `vote1_t128` 变成 `864 x 4`、`1963us`、高压 `31.48% / 11.50%`；D24-only teacher 变成 `839 x 4`、`1961us`、高压 `31.51% / 11.36%`。D5 partial-init 虽能把 normal `int8_margin_min` 拉到 `7` 且仍为 `881 x 5`、`1982us` under2，但高压仍为 `31.58% / 11.38%`，说明单纯增厚 normal int8 margin 不等于高压泛化。2026-05-21 继续追测后，这个结论更强：D6 partial-init 把 normal `int8_margin_min` 拉到 `11`，`848 x 6`、`1995us` 仍卡在 under2，但高压变成 `31.15% / 11.62%`，control 退化；D6 的 `exact_clean_rotmirror_residual` 是一个很好看的超时候选（`2464 x 6`、`2189us`、normal replay 100%、`int8_margin_min=11`），已作为 table-shape/margin diagnostic 保留，但不是部署候选。normal-only conflict-family guard 也只给出微弱收益：exact-query guard 最多到约 `31.23% / 10.82%`，toward-wrong `alpha=0.40` 的最好同时点约 `31.29% / 10.77%`，远离 `<10%`。后续不应把高压样本加入训练，也不应继续拧 scalar qpair/orbit 权重；应把 retained teachers 的互补决策按 conflict family / teacher source / view orbit 分开，做预算感知的多分支蒸馏、门控二级过滤或 compiler-feedback 训练，让单个 strict-op encoder 或轻量 cascade 保留 oracle union 的互补性。C2/6/12-D4 orbit5 是上一 under2 控制组：`917 x 4`、`1967us`、normal int8 replay 100%、`int8_margin_min=3`、高压 `31.68% / 11.53%`；orbit4 是更小表/更低 control 的控制组：`869 x 4`、`1963us`、高压 `31.84% / 11.47%`；原 dynamic qpair 控制组为 `942 x 4`、`1969us`、高压 `31.91% / 11.56%`。这些都不是达标方案，teacher-oracle union 的 `5.44% / 0.64%` 才是下一轮应压缩的几何目标。

2026-05-21 后续结论：normal-teacher conflict cascade 已实测为过稀疏，最佳 normal-replay-valid gate 只触发 `12 / 24190` 个高压事件，修正/破坏 `10 / 2`，高压仅到 `31.23% / 10.81%`；family-balanced teacher cache 也不是出路，cap32 为 `996 x 4`、`1973us`、高压 `31.42% / 11.18%`，cap64 为 `1001 x 4`、`1974us`、高压 `31.69% / 10.88%`。D8 qpair 通过降低 normal residual margin target + low-use prune 被压到第一个严格 under2 D8 点：`2608 x 8`、`2000us`、normal replay 100%、`int8_margin_min=2`，但高压是 `29.48% / 14.73%`，低组几何更好、control 明显更差。该结果说明“不同 geometry 确实互补”，但也证明不能直接以 D8 替换 D4；下一步应做 teacher-source/orbit-aware 的共享 backbone 多头或真正预算可行的二阶段策略，而不是继续单表 nearest-prototype 或只按 normal qpair 重排事件。

2026-05-21 teacher-source oracle 追测：新增 `analyze_v8_teacher_source_oracle.py`，只把高压事件作为诊断对齐，比较当前 D4 multiteacher best、D24/D16/D12 retained teachers 和 D8 qpair diagnostic。单源高压分别为 D4 `31.30% / 10.80%`、D24 `20.54% / 4.97%`、D16 `24.54% / 8.85%`、D12 `27.84% / 9.00%`、D8 qpair `29.48% / 14.73%`；event-level oracle-any 只剩 `3.95% / 0.36%`，其中非 D4 选择 `4570` 次（D24 `3377`、D16 `701`、D12 `282`、D8 qpair `210`）。但按 `group`、`view_label`、`perturb_family`、`selection_margin_bucket` 做 best-by-field gate 基本退化为 D24 行为，不能复现 event-level 互补性。结论：下一步必须学习 normal-only teacher-source/orbit 决策信号，不能用高压字段人工 gate，也不能只靠粗分组。

2026-05-21 source-confidence / D8 teacher injection 追测：新增 `analyze_v8_source_confidence_gate.py`，把高压事件只作诊断，评估不使用标签的 source 选择/聚合。retained D4+D24+D16+D12+D8qpair 上，单源 max-margin gate 最好为 overall/low/control `11.31% / 19.26% / 3.36%`，label-free `log_margin_sum` 聚合最好为 `10.41% / 17.73% / 3.09%`，已经接近 overall `<10%` 但 low 仍不够；true-parent oracle 为 `2.16% / 3.95% / 0.36%`。仅 under2 候选池上，即便 true-parent oracle 也只有 `7.97% / 12.82% / 3.13%`，label-free margin-sum 只有 `18.96% / 28.57% / 9.36%`，说明当前 under2 多头互补性不足以解决 low group。把 D8 qpair 作为第四个 normal-only teacher 加入 dynamic qpair cache 后，事件数从 `2306` 增到 `2927`，但训练出的 D4 `plus_d8qpair` 分支只是 `1007 x 4`、`1974us`、normal replay 100%、`int8_margin_min=2`，高压 `31.52% / 11.23%`，不如当前 best。结论：D8 的互补性不能直接通过 qpair vote 注入；下一步应压缩 retained-teacher 的聚合 logits / class-distance 结构，而不是把已压缩 under2 表再做简单 gate 或继续扩大 qpair cache。

2026-05-21 source-logit teacher 追测：新增 `build_v8_source_logit_teacher.py`，只用正常部署行对齐 D24/D16/D12 retained teachers 与 D8 qpair diagnostic，把各 source 的 parent class distance 按正常 margin p90 归一化后聚合成 soft parent probabilities；高压样本不参与 teacher cache、训练或 prototype 编译。该 cache 在 D4 base 的 `9120` 行中激活 `6992` 行，所有激活行都有 4 个 source 支持，mean true-parent target probability 约 `0.499`。用它叠加原 dynamic qpair teacher 训练 D4，得到 `logitteacher_w005`：`915 x 4`、保守 `1967us`、normal replay 100%、`int8_margin_min=1`、推荐算子集合仍为 `SPACE_TO_DEPTH + CONV_2D + MAX_POOL_2D + MEAN + FULLY_CONNECTED`，但高压为 `31.52% / 12.28%`，比当前 D4 multiteacher best 更差。结论：retained-source 的平均 class-distance logits 过早抹平了 event-level source/orbit 分歧，不能把 oracle complementarity 压进单一 D4 nearest-prototype 几何；下一步应保留 source decision 本身，例如 normal-only teacher-source gate、轻量 cascade、按 orbit/family 的二级表，或让 compiler-feedback loss 直接惩罚会破坏互补 source 决策的压缩。

2026-05-21 source-margin side-head 追测：新增 `build_v8_source_margin_qanchor_teacher.py`，只用正常部署行把 D24/D16/D12/D8qpair retained-source 的 margin/confidence 向量压成 side dims，并以 D4 base code + side dims 构成 qanchor teacher。side2 teacher 激活 `6992 / 9120` 行，4 个 source 均参与，PCA side2 解释约 `70.2%` 的 source-confidence 变化；高压样本仍只作诊断。D6 训练后最佳 under2 表为 `803 x 6`、保守 `1990us`、normal replay 100%、`int8_margin_min=11`、推荐算子集合不变，高压为 `30.73% / 11.97%`；其 `head4_side1` 子空间为 `844 x 5`、保守 `1977us`、`int8_margin_min=10`、高压 `30.68% / 11.80%`。继续扩到 side3/D7 后，all7 表为 `794 x 7`、保守 `2005us`、normal replay 100%、`int8_margin_min=10`、高压 `30.39% / 11.70%`，仅超保守预算 `5us`，已按“超时但好看”规则保留；normal-only `merge_mean_d32` prune 可把它压成严格 under2 的 `746 x 7`、约 `1997us`、`int8_margin_min=10`，但高压基本不变为 `30.43% / 11.71%`；严格 under2 的最佳 D7 子空间 `head4_side2` 为 `855 x 5`、`1978us`、`int8_margin_min=7`、高压 `30.52% / 11.57%`。为了保留 source 决策本身，又加入 `winner_simplex` side-mode，把 normal-only source winner 编成 3D simplex qanchor；低权重 D7 为 `766 x 7`、约 `2001us`、`int8_margin_min=9`、高压 `30.81% / 11.48%`，高权重 `0.00015` 为 `850 x 7`、`2012us`、`int8_margin_min=11`、高压 `30.91% / 11.54%`。这说明 source-margin side dims 能在 under2 内继续降低 low group，但只是把错误从 low/control 间重新分配；硬 source-id qanchor 也没有转移 retained teacher 的互补几何。D4 multiteacher 与 D6 all6 的高压标签 oracle 可到 `27.72% / 9.29%`，D4+D6h4s1+D7h4s2 的 under2 source oracle 也只有 `26.84% / 9.05%`；label-free source gate 仍在 `30.5% / 11.2%` 左右，说明互补性存在但当前 gate 信号不足。后续若继续这一路，应训练真正的 source/orbit gate 或 compiler-feedback gate，而不是继续把 side dims 并入单一 nearest-prototype 表。

2026-05-21 normal-region guard cascade 追测：新增 `evaluate_v8_region_guard_cascade.py`，用 normal-only multi-teacher conflict rows 生成小型 guard 表，并给每个 guard 加正常非本类 safe radius，保证 normal deployment replay 不被改坏；高压只用于评估。最佳 under2 有效点是 `256` guards，等效 `1223 x 4`、约 `1991us`、normal replay 100%，高压仅从当前 best 的 `31.30% / 10.80%` 微降到 `31.20% / 10.72%`，触发 `574` 次、修正/破坏 `265 / 244`，净收益只有 `21` events。结论：局部 normal-safe guard 比最近原型 conflict cascade 更密，但仍被高压误触发抵消；除非有 learned source/orbit gate 配合，不应继续单独调这类规则。

2026-05-21 共享 head qanchor 追测：新增 `build_v8_composite_qanchor_teacher.py`，只用正常部署行构造 `D4 head + retained-teacher PCA side head` 的 qanchor，不读取高压样本。D8-PCA2 侧头得到 `915 x 6`、`2003us`、normal replay 100%、`int8_margin_min=9`，高压 `31.43% / 11.24%`，既略超预算又退化。D24 strongraw PCA2 侧头更有诊断意义：只取 D4/D24 共同 normal 行 `6992 / 9120` 生成 qanchor，得到严格 under2 的 `802 x 6`、`1990us`、normal replay 100%、`int8_margin_min=14`，但高压仍为 `31.26% / 11.88%`；D24-PCA4 扩到 D8 后是 `807 x 8`、`2023us`、高压 `31.25% / 11.36%`。结论：PCA-compressed shared head 可以继续增厚 normal margin，甚至给出当前最厚的 under2 normal margin，但没有转移 D24 retained teacher 的高压几何；这把“增大 normal margin”从主因降级为必要但不足的约束。下一步若继续，应显式保留 teacher-source/orbit 决策结构，例如 teacher-source gate、按 view orbit 的小型分支表、或训练一个 normal-only gate 预测何时采用哪类 teacher geometry，而不是把 teacher 几何先 PCA 压扁再交给单一 nearest-prototype 表。

2026-05-21 shared-head 子空间诊断：新增 `analyze_v8_shared_head_subspaces.py`，在 D8 D24-PCA4 输出内只用正常行重编译 `head4`、`head4+side01`、`head4+side23`、`all8` 子空间 residual 表。有效 under2 子空间分别为 `856 x 4`、`1961us`、高压 `31.69% / 11.48%`；`836 x 6`、`1993us`、`31.58% / 11.40%`；`802 x 6`、`1989us`、`31.44% / 11.47%`。`all8` 最好但超预算：`807 x 8`、`2022us`、`31.25% / 11.36%`。高压标签只作诊断 oracle 时，有效 under2 子空间任意选择的上限也只有 `28.85% / 9.94%`（从 all8 出发）或 `29.25% / 10.10%`（从 head4 出发），simple margin gate 几乎不动。因此 PCA shared-head 内部 gate 的 low-group coverage 不足，不能作为主线；真正需要的是保留 teacher-source/orbit 分歧本身，而不是在压扁后的子空间中再找门控。

2026-05-21 sourcewinner shared-head gate 追测：复用 `analyze_v8_shared_head_subspaces.py`，把 D7 `winner_simplex` sourcewinner side-head 拆成 `head4`、`head4+side_i`、`head4+side_ij`、`all7` 子空间，并修正该脚本导出的 normal params，使 normal-only source gate 可按 `(sample_index, view_label)` 对齐。低权重 sourcewinner 的最佳有效单子空间为 `head4_side12`：`767 x 6`、保守 `1985us`、normal replay 100%、`int8_margin_min=9`，但高压仍是 `30.66% / 11.58%`；normal-only routing table 的 q20 最好 low/control 也只有 `30.57% / 11.37%`，label-free margin gate 只有 `30.61% / 11.43%`。高压标签只作诊断 oracle 时，同一共享 head 的有效 under2 子空间任意选择上限为 `28.04% / 9.84%`。较高 sourcewinner 权重更差，normal-only gate 最好 `30.72% / 11.67%`，oracle `28.24% / 10.27%`。结论：把 source winner 编进 side dims 仍没有形成可恢复的 source/orbit gate 信号；后续不要继续扫这类 routing-table quantile 或 simple margin 聚合，应转向单独可训练 gate objective 或更高容量但可预算的显式二阶段表示。

2026-05-21 normal union-stress qanchor 追测：在 D4+D24-PCA2 shared-head 上扩大正常 stress 编译集合（D4 29 views 与 D24 42 views 的 union，共 49 stress names），仍不把高压样本加入训练或原型编译。该分支能用 `exact_clean_rotmirror_residual` 得到一个很完整但超时的表：`4339 x 6`、conservative `2414us`、clean/rotmirror/union-stress int8 replay 100%、`int8_margin_min=3`，已按“超时但好看”规则保留为 `v8_parent_c2612_d6_d24pca2_unionstress_qanchor_20260521_0001/KEEP_TIMEOUT_CANDIDATE.md`。但高压低组/control 为 `32.61% / 10.87%`，比当前 under2 best 更差。结论：把 normal stress 全部补到 replay clean 会迅速变成残差表膨胀，且不自动转化为高压鲁棒性；normal-stress replay 是硬门槛/诊断，不应成为单独优化目标。

2026-05-21 normal-only source gate 追测：新增 `analyze_v8_normal_source_gate.py`，只用正常 deployment params 学 source-routing table，高压仍只作 evaluation。retained D4+D24+D16+D12+D8qpair 源池上，normal gate 退化为全选 D24，高压就是 D24 的 `20.54% / 4.97%`，没有学到何时选择 D16/D12/D8 的互补几何。deployable 源池 D4+D6h4s1+D7 merge_mean_d32 上，q1/q5/q20 只给出微弱改善：best-low 为 q5 `view_label+pred_parent:per_sqrt_dim`，高压 `30.16% / 11.43%`；best-overall/control 为 q20 `view_label:per_dim`，高压 `30.31% / 10.92%`。这说明“正常集学一个查表 gate”不足以压缩 retained-teacher oracle，下一步需要更强的 normal-only source/orbit 表征或 compiler-feedback gate，而不是继续调 routing table 分桶。

2026-05-21 local source-neighborhood gate 追测：新增 `analyze_v8_local_source_gate.py`，用每个 source 的正常 int8 code 近邻、normal margin / source-advantage 作为无标签 source reliability，再在高压事件上只用 feature/margin 做选择，不使用高压标签训练或选源。结果仍为负：deployable 源池 D4+D6h4s1+D7 merge_mean_d32 的 best-low 只有 `30.53% / 11.42%`，不如粗 normal routing 的 `30.16% / 11.43%`；retained 源池 D4+D24+D16+D12+D8q 的 quick local gate best-low 也只有 `23.15% / 5.49%`，差于 D24 单源 `20.54% / 4.97%`，更差于 retained `log_margin_sum` 的 `17.73% / 3.09%`。结论：当前正常局部邻域 reliability 不能复原 teacher-source/orbit 互补性；下一步应停止扩大查表/近邻 gate 网格，转向训练侧 compiler-feedback/source-decision 表征，例如让 encoder 直接保留 retained source aggregation 的决策 margin，或在 normal replay compiler 中惩罚会把互补 source 决策折叠掉的 prototype 合并。

2026-05-21 source-decision margin 追测：新增 `build_v8_source_decision_margin_teacher.py`，只用正常 deployment rows 对齐 D4 base、D24/D16/D12 retained teachers 和 D8 qpair diagnostic，生成 `6992 / 9120` 个 active rows 的 wrong-parent 决策 margin teacher，要求学生在 fake-int8 parent logits 上满足 `q(parent) - q(wrong_parent) >= 8`；高压样本仍只在最后评估。对应在 `train_v8_parent_classifier.py` 新增 `--source-decision-teacher-npz` / `--source-decision-margin-weight` 等 loss。单独 source-decision 60 epoch 续训后可编译出 `969 x 4`、保守 `1971us`、normal replay 100%、`int8_margin_min=3`，高压为 `31.24% / 11.24%`；叠加原 dynamic qpair teacher 的 120 epoch combo 得到更小的 `932 x 4`、保守 `1968us`、normal replay 100%、`int8_margin_min=3`，高压仍为 `31.24% / 11.24%`。结论：均匀的 normal-only wrong-parent 决策 margin 可以压表并微降 low，但会把 control 从当前 best 的 `10.80%` 拉坏到 `11.24%`，不应晋升为主候选；后续若继续 source-decision，应让目标按 teacher source / view orbit / conflict family 分化，或把 loss 接到 compiler-feedback 合并惩罚，而不是对所有 active rows 施加同一个 wrong-parent margin。

2026-05-21 focused source-decision 追测：新增 `filter_v8_source_decision_teacher.py`，在不读取高压样本的前提下，把 source-decision teacher 缩到更聚焦的正常边界行，验证“覆盖太宽导致 control 退化”的假设。`base_margin<=256 && aggregate_margin<=0.40` 的 904-row teacher 训练出 `1008 x 4`、保守 `1974us`、normal replay 100%、`int8_margin_min=2`，高压为 `31.44% / 11.01%`；只按 `aggregate_margin<=0.30` 的 1216-row teacher 训练出 `940 x 4`、保守 `1969us`、normal replay 100%、`int8_margin_min=2`，高压为 `31.48% / 11.23%`。结论：聚焦筛行可以减少 uniform source-decision 的 control 破坏，但 low 组收益消失且仍远离 `<10%`；parent-logit-only 的 source-decision margin 应降级为负样本，下一步应把 retained-source 几何保留到 prototype/compiler 空间，例如按 source/orbit 输出辅助 code、compiler 合并时惩罚 source decision collapse，或使用可预算的显式二级几何，而不是继续筛同一类 parent-logit margin 行。

2026-05-21 source-decision compiler-feedback 追测：把同一个 normal-only source-decision teacher 接入 `compile_v8_parent_logits_memory.py`，让 normal replay compiler 在原型表阶段惩罚 retained-source wrong-parent 决策 margin 过低的行，高压样本仍只作最终诊断。per-row target 版本保持当前 anchor 的 `967 x 4`、保守 `1971us`、normal int8 replay 100%、`int8_margin_min=4`，高压仍为 `31.30% / 10.80%`。继续把 compiler source-margin threshold 加到 `32/64/128` 会把表增厚到 `1040/1134/1318 x 4`，保守估时 `1977/1984/1999us`，正常 replay 仍全 100%，但高压分别只有 `31.19% / 10.80%`、`31.22% / 10.86%`、`31.24% / 10.87%`。结论：在 D4 parent-logit 空间里靠 residual table 增长保护 source-decision margin，不能把 retained teacher 的互补几何迁移到高压集；这个方向作为负结果保留，后续不要继续加同类残差原型，应改为显式 source/orbit/family code、预算感知二级表或可训练 gate/cascade。

2026-05-21 source-gate CE 追测：新增 `build_v8_source_gate_teacher.py`，只用正常部署行把 retained-source `source_margin_scores` 转成 soft source label，并在 `train_v8_parent_classifier.py` 中加入 `--source-gate-teacher-npz` / `--source-gate-weight` / `--source-gate-start` 等 CE side loss。teacher cache 激活 `6992` 行，4 个 source 的 normal winner 计数为 D24 `2939`、D16 `1069`、D12 `1634`、D8 qpair `1350`，高压样本不参与 teacher、训练或 prototype 编译。C2/6/12-D7 `source_gate_weight=0.01` 得到 `832 x 7`、保守 `2010us`、normal replay 100%、`int8_margin_min=1`，高压 `31.20% / 11.42%`；normal-only aggressive prune 可到 `762 x 7`、四舍五入 `2000us`，但 exact conservative gate 仍略超，高压 `31.17% / 11.41%`。把权重加到 `0.03` 后为 `850 x 7`、`2012us`、`int8_margin_min=1`，高压 `31.67% / 11.22%`；继续加到 `0.10` 后为 `824 x 7`、`2009us`、`int8_margin_min=3`，高压退到 `31.48% / 11.92%`。结论：单独可训练的 normal-only source-gate CE 也不能把 retained teacher 的 source/orbit 互补性压进单一 nearest-prototype 表；它没有形成值得保留的厚 margin 超时候选。后续不要继续只扫 CE weight/temperature，应转向 compiler-feedback gate、显式二阶段小表或更高容量 source/orbit 表征，并继续把高压样本限制为诊断评估。

2026-05-21 source-gated table 追测：新增 `evaluate_v8_source_gated_table.py`，把 D7 source-gate side dims 当成真实部署决策使用：正常 deployment rows 按 `argmax(code[3:7])` 切成分源 residual prototype table，高压样本只在正常 replay 100% 后评估。`w010_t0` 得到 source table `[3,490,3,1757]`、合计 `2253` prototypes、最大分源 `1757`、保守 `2139us`、`int8_margin_min=20`，高压 `31.00% / 11.46%`；`w030_t0` 得到 `[3,1933,3,1239]`、保守 `2164us`、`int8_margin_min=3`，高压 `31.24% / 11.18%`；`w100_t0` 得到 `[96,9,2571,3]`、保守 `2253us`、`int8_margin_min=14`，高压 `31.24% / 11.40%`。这证明显式 source-gated table 也会 gate collapse：normal margin 可以变厚，但最大分源表超预算，高压仍在 `31% / 11%` 平台。后续不要继续调 source-gate CE 或分源 table margin target，除非训练/编译里加入预算感知的 gate-collapse penalty 或更强的 source/orbit 表征。

2026-05-21 hard-balanced source-gate 追测：为排除 source-gate CE 只是类别不均衡的问题，`build_v8_source_gate_teacher.py` 新增 hard label、label smoothing、inverse-label class balance 支持。hard-balanced teacher 仍只用正常部署行，source label 计数为 D24 `2939`、D16 `1069`、D12 `1634`、D8 qpair `1350`，class weights 为 `0.595 / 1.635 / 1.070 / 1.295`，高压样本不参与。对应 C2/6/12-D7 `source_gate_weight=0.10` 训练后，推荐算子图仍正确，但正常 int8 rotmirror/stress min 退到 `94.41%`，fixed-stress min `98.68%`，没有资格进入部署编译；TFLite gate 诊断显示 active rows 几乎全被预测为 source 2（`6985 / 6992`），weighted gate accuracy 只有约 `0.247`。结论：当前 tiny side-head 不是简单调 label balance 就能学会 source 决策，source-gate CE 分支应停止。

2026-05-21 C248 under2 source-pool 追测：新增 C2/4/8-D4、D8 qpair、D6 source-margin、D7 source-margin 与当前 D4 anchor 组成五源 under2 池，所有 source 都先满足正常部署约束，高压仍只作诊断评估。单源中 D8 qpair low 较好但 control 坏（`29.48% / 14.73%`），D7 source-margin 为 `30.45% / 11.72%`，当前 D4 anchor 为 `31.30% / 10.80%`。label-free `max_stress_margin` 只能到 `28.47% / 6.99%`，`margin_sum` 为 `28.63% / 7.47%`；但高压标签诊断 oracle-any 首次在 under2-only source pool 内达到 `9.71% / 1.26%`。正常集 source routing table 最好 low 约 `27.67% / 11.66%`，local normal-neighborhood gate 最好 low 约 `25.51% / 10.90%`，仍远离 oracle。这个结果把下一步方向钉死：不应继续把高压样本加入训练、不应继续只扫 CE/gate quantile，也不应只追求更厚 normal margin；应把五源池的互补决策作为 normal-only teacher，训练一个可预算的多源 side code / source-orbit gate / compiler-feedback 二阶段表，在 `<=2ms` 和推荐算子约束下压缩 event-level source choice。

2026-05-21 multi-source composite qanchor 追测：`build_v8_composite_qanchor_teacher.py` 已扩展到重复 `--pca-source-npz`，并用 normal-only source embedding、class-distance delta、source margin 组成 `41` 维 source feature，再压成 4 个 PCA side dims；teacher 行数 `9120`、target dim `8`、高压使用为 `none`。C2/4/8-D8 分支 normal replay 可闭合，但 pruning 后仍为 `2881 x 8`、约 `2043us`、`int8_margin_min=1`，canonical 295-row 高压为 `49.71% / 11.98%`，明确失败。C2/6/12-D8 分支得到一个应保留的超时诊断候选：`2459 x 8`、平均 `1908us`、保守 `2287us`、normal clean/D4/fixed/medium stress int8 replay 100%、推荐算子集合不变、`int8_margin_min=6`，canonical 高压为 `30.85% / 11.96%`。该候选已按“超时但好看”规则保留为 `v8_parent_c2612_d8_multisource_qanchor_20260521_0001/KEEP_TIMEOUT_CANDIDATE.md`；它的 self-selected 高压只有 `1` 个 low row 和 `1` 个 control row，因此 `2.44% / 4.88%` 只能说明自身 normal margin 被压厚，不能作为同口径高压达标证据。结论：多源 PCA side-code 可以保留正常 replay 形态，但不能从压扁后的 source feature 中恢复五源 event-level source choice；下一步应显式训练/编译 source-orbit decision、预算感知二级表或 compiler-feedback gate，而不是继续扩大单一 nearest-prototype side head。

2026-05-21 C248 source-decision compiler-feedback 追测：复用五源 under2 池的 normal-only 证据，构建 `d4base_c248pool_c248d4_d8q_d6sm_d7sm_t8` source-decision teacher，激活 `9119 / 9120` 正常行，高压使用仍为 `none`。把该 teacher 接入 D4 normal replay compiler 后，`m32/m64/m128` 三档都保持 normal clean/rotmirror/stress/fixed-stress int8 replay 100%，推荐算子集合不变，表大小分别为 `2533/2635/2836 x 4`，保守板端估时 `2096/2104/2120us`，`int8_margin_min=4`；canonical 高压分别为 `31.11% / 10.96%`、`31.16% / 10.89%`、`31.15% / 10.91%`。因此这些只作为 near-2ms 负向边界证据保留，不能晋升候选。另用高压标签仅作诊断拟合的粗字段 source oracle 显示，按 `perturb_family` 最好也只有 `27.12% / 11.66%`，按 `view_label` 为 `27.93% / 11.01%`，远离 event-level oracle 的 `9.71% / 1.26%`。结论进一步收敛：单一 D4 residual 表无法靠 normal source-decision margin 增厚恢复五源互补性；下一步要做的是可预算的 event-level source/orbit gate、显式二阶段小表，或能防止 source-choice collapse 的 compiler-feedback，而不是继续给同一 parent-logit 空间加原型。

2026-05-21 C248 source-decision guard cascade 追测：新增 `build_v8_source_decision_guard_teacher.py`，把 C248 source-decision teacher 转成 normal-only region guard rows，只保留 D4 base margin `<=256` 且 source support `>=3` 的正常行，得到 `1939` 个 guard，仍不读取高压样本。用 `evaluate_v8_region_guard_cascade.py` 做二阶段 normal-safe 修补后，所有有效行都保持 normal replay 100%，推荐算子主体不变，guard 作为额外 4D 表计入预算。小网格 best 为 `1095 x 4`、保守 `1981us`、高压 `31.01% / 10.81%`；细扫 best 为 `1127 x 4`、保守 `1984us`、高压 `30.96% / 10.76%`，fix/break 为 `169 / 123`；扩大到 `1191 x 4`、`1989us` 后 low 又回到 `31.01%`。结论：C248 source-decision 信号有很小的 local repair 价值，但 normal-safe guard trigger 仍会把修复和破坏一起带进来，无法接近 `<10%`；这一路只作为负向二阶段表诊断保留，后续不要只增加 guard 数，而要训练真正的 event-level gate 或在训练/编译目标中直接惩罚 source-choice collapse。

2026-05-21 diagonal relevance metric 追测：新增 `search_v8_diag_metric_weights.py` 与 `greedy_v8_diag_metric_mask.py`，把文档推荐的 Relevance-LVQ / per-dim scale 先以最低风险的 post-hoc int8 diagonal metric 落地。TFLite encoder 图不变，payload 只新增 `metric_weights_int32`，权重选择只看 normal replay 与 normal int8 margin，高压仍只作最终复核。D4 当前 anchor 在权重和 `<=5` 下最优仍是原始 `[1,1,1,1]`：`967 x 4`、保守 `1971us`、normal replay 100%、`int8_margin_min=4`，canonical 高压复核 `31.30% / 10.80%`；任何非平凡 under-budget 权重都会使 rotmirror/stress 低于 100% 或把 margin 变薄。D8 robust `sample_correct_count` strict-under2 表在权重和 `<=9` 下同样只保留 all-ones：`470 x 8`、`1969us`、`int8_margin_min=3`，非平凡权重会破坏 normal replay。D16 center-setcover retained 表用 greedy mask 试图把有效维度从 16 降到 10，但删任意一维就破坏 normal replay 或 margin；它只能保持 `2026 x 16`、`2231us`、`int8_margin_min=9` 的原始超时形态。结论：当前 payload 上的离线 diagonal relevance 不是可用 margin 杠杆；若继续 Relevance-LVQ，应把 metric 放进训练/原型编译闭环，而不是后处理扫 per-dim 权重。

2026-05-21 D4 proto-key source gate 追测：新增 `analyze_v8_proto_source_gate.py`，只用正常 deployment params 学一个以 D4 primary nearest-prototype / margin bucket 为 key 的 source routing table，再在 C248 五源高压事件上评估。normal-only 最好行是 `proto_parent+pred+margin_bucket:raw:q5:min1`，mapping size `26`，选择 D4/D8q/D6sm/D7sm 约 `14119/3982/4584/1505` 次，高压为 `28.35% / 9.83%`，主要改善 control，low 仍远离目标。为了判断 key 本身上限，又用高压标签仅作诊断拟合同一 key：`proto+margin_bucket` oracle 也只有 `18.88% / 5.76%`，仍远高于 event-level oracle `9.71% / 1.26%`。结论：D4 nearest-prototype cell 比粗 view/margin gate 更有信息，但静态 proto-key routing 仍不足以恢复五源互补性；下一步不能只做查表 key 扩展，应转向真正学习 event-level gate 或在表示/编译目标中保留 source choice。

2026-05-21 learned D4 source gate 追测：新增 `analyze_v8_learned_source_gate.py`，只用正常 deployment params 训练一个小型连续 source selector，输入为 D4 code / D4 class distances / D4 margin / pred，标签为 C248 五源 normal source-margin winner，高压仍只作评估。p90-normalized 小网格最好为 `code:per_sqrt_dim:hidden8`，source-label train/val accuracy `50.2% / 50.3%`，高压 `28.15% / 10.67%`；关闭 p90 归一化并把 hidden 扩到 32/64 后，最好为 `code+dist+margin:log:hidden32`，train/val `48.6% / 46.4%`，高压 `28.33% / 9.68%`。随后给同一脚本加入 normal `view_label` / view-family one-hot 特征，source-label val 最好到 `52.8%`，best-control 为 `28.62% / 9.41%`，best-low 为 `28.57% / 10.05%`，仍远离 `<10%` low 目标。再加入 normal-confidence fallback cascade：阈值只来自正常验证集 confidence quantile，最佳 `base_adv q0.5` 在高压冻结评估中到 `26.70% / 9.24%`，说明“只在高置信非 D4 时路由”有真实修补价值，但仍需要在多个 under2 source 之间调度，且 low 组距离 `<10%` 很远。结论：从当前 D4 表示后验训练一个小 gate 并不能恢复五源 event-level oracle；正常 orbit/置信拒绝可调 control 并小幅降低 low，但 source choice 在 D4 embedding 中已经大幅折叠。下一步应训练表示本身保留 source choice，或者在 compiler/training 目标中加入 source-choice preserving / collapse penalty，而不是仅在 D4 输出后面加 selector、fallback 阈值或继续添加后验 gate 特征。

2026-05-21 learned-gate guard cascade 追测：新增 `build_v8_learned_gate_guard_teacher.py`，把上一段 normal-only learned fallback cascade 转成 D4 region-guard teacher。teacher 只用 normal params 训练和定阈，导出 `3058` 个正常 guard 行，预测 source 主要为 D8q/D7sm/D6sm；高压使用仍为 `none`。再用 `evaluate_v8_region_guard_cascade.py` 做 normal-safe guard 表评估，最佳 under2 行为 `b128_r512_gap64_pm128_competitor`：`1095 x 4`、保守 `1981us`、normal replay 100%、高压 `31.02% / 10.86%`，fix/break `162 / 136`。结论：learned fallback 的 `26.70% / 9.24%` 诊断收益依赖真正路由到其它 source 表；把同一信号压回 D4 局部 guard 表后基本回到 `31% / 11%` 平台。后续不要继续给 D4 增加 learned-gate local guards，应把 source-choice 保留放到表示训练或 compiler/prototype collapse penalty 中。

2026-05-21 robust-label learned-gate guard 追测：为验证“normal source label 太局部”是否也是 local guard 失败原因，`build_v8_learned_gate_guard_teacher.py` 增加 `--label-mode`，复用 `sample_correct_count` / `sample_family_min` / `sample_family_correct_count` 这类 normal-only 聚合 source label；高压不参与 teacher、gate 训练、阈值、guard 排序或 setting selection。`sample_correct_count` teacher 选出 `2903` 个 guard row，最佳 normal-safe 表为 `1159 x 4`、约 `1986us`、高压 `31.23% / 10.81%`，fix/break `126 / 119`；`sample_family_min` teacher 选出 `2905` 个 guard row，预算边缘细扫最佳为 `1287 x 4`、约 `1996us`、高压 `30.97% / 10.86%`，fix/break `259 / 226`；加入 normal view-family 特征后，最佳为 `1159 x 4`、约 `1986us`、高压 `31.01% / 10.70%`，fix/break `188 / 141`。结论：robust normal label 和 orbit context 能改变 low/control 取舍，但 local D4 guard 的误触发仍抵消大部分修复；这条路不能靠继续改 label 聚合、加 guard 数或放大 safe radius 达成 `<10%`。真正缺口仍是训练/编译阶段保留 source/orbit decision，或显式预算一个能路由到不同几何的二阶段结构。

2026-05-21 C248 source-gate side-head 追测：用正常行构建 C248 五源 source-gate teacher（`9120` 行，source label 计数 `[966,1638,3944,1231,1341]`，高压使用 `none`），再训练 C2/6/12-D8 parent+5-source-gate side-head。严格推荐算子保持不变，但 learned side gate 没有学会 source label：normal raw / weighted source-label accuracy 只有 `14.86% / 13.18%`，预测计数 `{0:15,1:3068,2:37,3:256,4:5744}`。单一 nearest-prototype compile 经 normal-only prune 可得到严格 under2 的 `640 x 8`、保守 `1996us`、normal clean/D4/fixed/medium stress int8 replay 100%、`int8_margin_min=2`，但 canonical 高压退化为 `31.33% / 11.38%`。显式 source-gated table 虽把低组小幅降到 `30.50%`，但 source tables `[3,875,3,35,2027]`、保守 `2218us`，control 仍 `11.05%`。结论：把 C248 source choice 作为 side-head CE/qmargin/balance 训练仍会 collapse；下一步不能继续扫 source-gate loss 权重，而应换成更强的 source/orbit 表征、预算感知二阶段结构，或在 compiler/training 中直接约束 event-level source-choice collapse。

2026-05-21 C248 source-gate robust-label / pairwise-rank 追测：用真实 C248 源参数重建 `sample_correct_count` source-gate teacher（`9120` 正常行，source label 计数为 `{d4best:3210,c248d4:1380,d8q:150,d6sm:1560,d7sm:2820}`，高压使用 `none`），再训练 D8 side-head。CE/qmargin/balance 分支的 learned gate 仍塌缩，normal source-label raw/weighted 只有 `14.98% / 14.98%`；normal-only prune 可以得到严格 under2 的 `merge_mean_d128`：`470 x 8`、保守 `1969us`、normal replay 100%、`int8_margin_min=3`，canonical 高压 `31.04% / 11.50%`；同一分支还有一个“超时但好看”的 `prune_unused_then_low_margin_wrong_le8`：`776 x 8`、`2018us`、normal replay 100%、`int8_margin_min=9`、高压 `31.15% / 11.48%`，已按 retained timeout 规则保留为诊断，不晋升部署。为避免仅 CE 形式导致塌缩，又在 `train_v8_parent_classifier.py` 加入默认关闭的 pairwise source-rank loss，读取 normal-only teacher 的 `score_matrix` 并约束 fake-int8 gate logits 的源顺序；light rank `rw0020_b005` 把 normal source-label accuracy 提到 `27.05%`，但 rank-pair gap16 只有 `49.64%` 且预测仍集中到 source 4，raw int8 normal min 只有 `96.05%`；strong rank 退到 `6.75%` source-label accuracy。light rank 经 normal-only compile/prune 后，严格 under2 的 `merge_mean_d128` 为 `485 x 8`、`1971us`、normal replay 100%、`int8_margin_min=1`，canonical 高压仍为 `31.11% / 11.40%`。结论：更鲁棒的 normal source label 与 pairwise rank 都不能把五源 event-level source choice 保留下来；margin 厚但略超时的 samplecc prune-le8 值得保留作诊断，而 rank-loss 分支 margin 太薄，不进入 timeout retained manifest。后续应停止在 collapsed side-head 上继续扫 CE/rank/qmargin 权重，转向显式 source/orbit gate、source-choice preserving 表征或预算感知二阶段表。

2026-05-21 C248 source-gated residual table 追测：为区分“source-gated table 只是表形态超预算”还是“gate 本身不可用”，新增 `evaluate_v8_source_gated_residual_table.py`，运行时使用共享 clean base 表 + 当前 source 的 residual 表，按 `base_count + max_source_residual` 计最坏距离预算；高压仍只作评估。基于同一 D8 source-gate 模型，`target_margin=0` 可闭合 normal replay，effective `574 x 8`、保守 `1985us`、`int8_margin_min=4`，canonical 高压 `31.04% / 11.31%`；`target_margin=8` 仍 under2，effective `590 x 8`、保守 `1988us`、`int8_margin_min=13`，但高压仍为 `31.04% / 11.24%`。这说明预算感知表结构可以解决 full source-gated table 的 `2218us` 问题，却无法解决当前 side gate 的 source-choice collapse；normal margin 再次增厚但没有转成高压鲁棒性。后续不要继续在这个 collapsed gate 上调 residual margin target，而要换 source/orbit 表征或让 gate 训练本身变成事件级可分。

2026-05-21 C248 sourcewinner qanchor 追测：为排除 CE/qmargin 形式本身的问题，直接用同一五源 normal-only teacher 的 `winner_simplex` side-code 训练 C2/6/12-D8 qanchor（`qanchor_weight=0.0001`，高压使用 `none`）。训练后推荐算子集合保持不变，但 raw int8 rotmirror/stress min 只有 `95.39%`；normal-only residual compile 可补回 `800 x 8`、保守 `2021us`、`int8_margin_min=1`，clean-rotmirror residual 为 `2457 x 8`、`2287us`。进一步 prune/merge 后没有任何 normal replay 100 的严格 under2 行，最接近的是 `merge_medoid_d64`：`682 x 8`、保守 `2003us`、`int8_margin_min=1`，canonical 高压 `30.67% / 12.00%`。直接把 `code[4:8]` 映射回最近 simplex vertex 的 normal source-label accuracy 也只有 `18.34% / 18.26%`，预测几乎全塌到 source 1。因此它不是“超时但好看”保留对象；结论是：直接 winner-simplex qanchor 也没有把 C248 event-level source choice 保留下来，后续不应继续扫同类 qanchor 权重，而要把 source/orbit gate 变成显式事件级目标或预算感知二阶段结构。

2026-05-21 C248 winner-logit teacher 追测：`build_v8_source_logit_teacher.py` 新增 `--aggregate-mode winner`，不再平均 source logits，而是用五源 normal-only 最大 source-margin winner 的 parent-distance logits 做 soft teacher（`9120 / 9120` 行，高压使用 `none`，source 计数 `[966,1638,3944,1230,1342]`，true-parent target prob mean `0.613`）。C2/6/12-D4 `logit_teacher_weight=0.02` 训练后推荐算子保持不变，但 raw int8 rotmirror/stress min 只有 `96.71%`；normal-only residual compile 得到严格 under2 的 `841 x 4`、保守 `1961us`、normal replay 100%、`int8_margin_min=2`，canonical 高压为 `31.03% / 11.97%`。该结果比当前 D4 anchor 的 margin 和 control 都更差，说明把 source winner 的 parent logits 直接折进单一 D4 head 仍会丢掉 event-level source choice；后续不要继续扫 winner-logit weight/temperature，应转向显式 event-level source/orbit gate 或预算感知二阶段结构。

2026-05-21 C248 source-block qanchor 追测：新增 `build_v8_source_block_qanchor_teacher.py`，把当前 D4 base embedding 与四个非 base C248 source 的 class-distance blocks 合成 D16 qanchor teacher（`9120 / 9120` 正常行，高压使用 `none`，target dim `16`，`target_abs_p99=48`）。C2/6/12-D16 `qanchor_weight=0.0001` 训练后推荐算子保持不变，但 raw int8 rotmirror/stress min 只有 `97.04%`；normal-only residual compile 得到 `768 x 16`、保守 `2139us`、normal replay 100%、`int8_margin_min=1`，canonical 高压 `30.43% / 11.75%`。进一步 normal-only prune 可到 `757 x 16`、保守 `2136us`、`int8_margin_min=4`，高压仍为 `30.43% / 11.75%`。因此该候选按“超时但好看”规则保留为 `v8_parent_c2612_d16_c248_sourceblock_qanchor_20260521_0001/KEEP_TIMEOUT_CANDIDATE.md`。随后 direct side-block probe 把后 12 维按四个 3 类 source-distance blocks 直接 `argmin` 或聚合，最好也有 normal wrong `55.94%`、高压 `61.94% / 58.21%`，说明 side blocks 本身不是可读 source/orbit 决策。为验证是否只是 qanchor 过弱，`train_v8_parent_classifier.py` 又加入默认关闭的 `--source-block-margin-weight`，直接约束每个 3 类 block 的 fake-int8 true-vs-wrong margin；小续训 `w002_t16_from_d16` 只把 direct block normal wrong 降到 `54.12%`，normal replay 表反而变为 `800 x 16`、保守 `2149us`、`int8_margin_min=2`，exact-clean under2 行 normal stress min 仍只有 `93.09%`。该分支证明单纯把 source class-distance blocks 塞进一个 nearest-prototype code 或用简单 block margin loss 都无法恢复五源 event-level source choice；下一步不应继续加大 D16 表、只调 qanchor/block-margin 权重或直接读取 side blocks，而要做可预算的显式二阶段 source/orbit gate、source-choice collapse penalty，或把 prototype/compiler 目标改成事件级 source decision 保留。

2026-05-21 source-decision center-collapse 追测：`train_v8_parent_classifier.py` 新增默认关闭的 `--source-decision-center-weight`，用 normal-only source-decision teacher 的 wrong parent 在当前 fake-int8 embedding 几何里约束 true-parent center 与 teacher-wrong center 的距离 margin，高压仍只评估。先用当前 C2/6/12-D4 anchor 做短续训 `w002_t4096`（`weight=0.02`、`target=4096`、`alpha=0.001`）；raw int8 rotmirror/stress min 只有 `95.72%`，normal-only residual compile 可闭合为 `894 x 4`、保守 `1965us`、normal replay 100%、推荐算子不变，但 `int8_margin_min=1`，canonical 高压退化到 `31.59% / 11.27%`。另对 C248 source-decision guard 做 support=4、weight>=1.90 高置信过滤，得到 `494` 个 guard，best under2 行为 `1223 x 4`、约 `1991us`、高压 `31.04% / 10.91%`，fix/break `209 / 190`，control 明显回退。结论：当前 D4 几何的正常 source-decision 信号可闭合 normal replay，但无法通过简单 center-collapse loss 或高置信 guard filter 恢复五源 event-level source choice；后续不要继续扫这些 scalar target/weight，应换成显式事件级 source/orbit gate 或把多源选择作为预算感知结构，而不是折回单一 parent-center 几何。

2026-05-21 first-kernel 架构探针：考虑高压主要仍是 shift/blur-heavy，尝试只增大第一层 receptive field，同时保持推荐算子集合，且高压仍只作评估。为避免初始化假负例，`train_v8_parent_classifier.py` 的 partial init 已扩展为能把 3x3 conv kernel 中心拷贝到 5x5，并在缩小 filters 时裁剪通道。未中心拷贝的首版 C2/6/12-D4-k5 只能到 clean `58.88%`，确认初始化方式会污染结论；中心初始化后，C2/6/12-D4-k5 可用 normal-only residual compile 闭合正常 replay，得到 `801 x 4`、`int8_margin_min=3`、推荐算子不变，但保守估时因 k5 backbone 增至 `3252us`，canonical 高压只有 `31.06% / 11.05%`，low 微降而 control 退化。该中心初始化点按“超时但好看”的规则保留为 `v8_parent_c2612_d4_archshift_20260521_0001/KEEP_TIMEOUT_CANDIDATE.md`，只作为 first-kernel receptive-field 边界记录，不晋升部署候选。为了压回 under2，又试 C1/4/8-D4-k5；该配置 backbone 预算可行，但 final clean 只有 `33.88%`、stress min `25.66%`，表示能力丢失。结论：单纯扩大 first kernel 不是当前 margin 主线；足容量版本超预算且高压不突破，预算版无法保持正常 replay。后续不要继续做 scalar kernel sweep，应把精力放回 source/orbit event-level gate、source-choice preserving loss 或预算感知二阶段表。

2026-05-21 multi-source event gate 追测：新增 `analyze_v8_multisource_event_gate.py`，把五个 under2 source 的事件级 int8 code / class distance / margin / pred 同时作为 gate 输入，标签仍只来自正常部署行的 source-margin winner，高压只作冻结评估。即使使用运行时不可部署的 all-source 诊断特征，最佳也只是 `score:raw:hidden0`：normal source-label val `88.80%`，但高压只有 `27.47% / 6.90%`；加入 all-source code/dist/pred/family 的小 MLP 也停在约 `27.99-28.32%` low。该结果说明问题不是 D4 后验特征太少：normal source-margin winner 这个监督本身对高压 source choice 迁移很弱，不能靠读取更多 source 事件特征直接补到 oracle。后续不要继续扩展 all-source 后验 gate 网格；它只能作为诊断上界，不是部署路径。

2026-05-21 multi-source event gate robust-label 追测：`analyze_v8_multisource_event_gate.py` 新增 `--label-modes`，把 normal-only source label 从逐行 winner 扩展到 `sample_min`、`sample_correct_count`、`sample_family_min`、`sample_family_correct_count`，即按同一样本或样本+view-family 的正常视图最差 margin / 正确覆盖数选 source，高压仍只作冻结评估。即使用 all-source score / family / code+dist+margin+pred 诊断特征，最好也只是 `sample_correct_count:score:raw:hidden0`，高压 `26.02% / 7.97%`；`sample_family_min` 为 `26.52% / 8.17%`，分数尺度 `log/per_sqrt_dim` 没有改进。结论：失败不只是 per-row source-margin label 太局部；正常样本/家族鲁棒 source label 也不能迁移到 event-level 高压 source choice。后续不要继续扩 all-source 后验 gate 或 label 聚合网格，应在训练/编译阶段直接保留 source-choice 信息，或者换更结构化的 source/orbit 表征。

2026-05-21 normal source-policy transfer 追测：`analyze_v8_normal_source_policy_transfer.py` 进一步去掉 gate 学习误差，直接用 normal params 为五源 under2 pool 生成 row/source winner、sample 聚合、sample-family 聚合 source policy，再按高压事件的同 sample/base view key 冻结查表评估；高压标签不参与任何选择，且该 sample-key lookup 仅是不可部署诊断。最佳 `row_winner:raw/log` 只有 `26.17% / 9.90%`，虽然 control 进入 10%，low 仍比 `<10%` 目标差很多；`sample_family_min` 为 `28.35% / 11.14%`，`sample_correct_count` 为 `30.38% / 10.47%`。这把问题进一步收窄：normal source-label teacher 本身对高压 low 组迁移不足，不只是后验 gate 容量不够。后续若继续 source/orbit 路线，必须引入不使用高压标签的 event-level robustness proxy 或直接在表示/编译目标中保留 source-choice margin，而不是继续扩 normal sample/family label 聚合。

2026-05-21 source-choice pair teacher 追测：新增 `build_v8_source_choice_pair_teacher.py`，把 C248 normal-only source teacher 转成动态 pair 约束：同 parent/same source 近邻为 positive，同 parent/different source 近邻为 negative，并追加原 normal-only multiteacher qpair rows。首版 teacher 只在 retained source-cluster reference params 上对齐 `6688 / 9120` 正常行，输出 `8994` 个 pair row；高压样本不参与 teacher、训练、compile、prune、merge 或 set-cover 选择。`sourcepair_w5e5_light_e120` raw int8 normal replay 不闭合，source-cluster margin 也没有真正变正；normal-only compile/prune 后的 retained timeout 行为 `755 x 8`、保守 `2014us`、normal replay 100%、`int8_margin_min=6`，canonical 高压 `30.71% / 11.87%`。进一步 normal-only set-cover 得到严格 under2 `setcover_m3`：`230 x 8`、保守 `1930us`、normal replay 100%、`int8_margin_min=4`，但 canonical 高压退化到 `31.55% / 12.34%`。结论：source-pair supervision 能形成干净 near-gate margin-boundary artifact，已按“超时但好看”规则保留；但它仍把 source choice 折叠进一个 D8 nearest-prototype geometry，不能作为 `<10%` 主线。后续应停止只加 source-pair/source-cluster scalar loss 或 plain set-cover，转向 event-level normal-only robustness proxy、显式 source/orbit gate、预算感知二阶段表或 compiler/prototype 合并阶段的 source-choice collapse 约束。

2026-05-21 robust-label source-gate side-head 追测：新增 `build_v8_multisource_gate_teacher.py`，直接从 C248 五源正常 params 构建 `sample_correct_count` source-gate teacher（`9120` 正常行，label 计数 `3210/1380/150/1560/2820`，高压使用 `none`），再训练 C2/6/12-D8 parent+5-source-gate side-head。严格推荐算子保持不变，但 side gate 仍 collapse：`code[:,3:8]` 的 normal source-label raw/weighted accuracy 只有 `14.98% / 14.98%`，预测几乎集中在 `d6sm` 和 `d8q`。normal-only compile 可闭合为 `789 x 8`、`2020us`、`int8_margin_min=2`；进一步 prune/merge 得到 strict-under2 `merge_mean_d128`（`470 x 8`、`1969us`、normal replay 100%、`int8_margin_min=3`），canonical 高压 `31.04% / 11.50%`。同一分支的 `prune_unused_then_low_margin_wrong_le8` 把 normal `int8_margin_min` 增到 `9`，但保守估时 `2018us` 且高压仍 `31.15% / 11.48%`，已按“超时但好看”规则保留。结论：正常标签更鲁棒、正常 margin 更厚，都没有解决 source-choice collapse；后续不要继续扫同形态 source-gate CE/qmargin/balance，而应把 source/orbit decision 作为显式事件级结构或 compiler-feedback 约束来保留。

2026-05-21 standalone source/orbit gate 追测：新增 `train_v8_standalone_source_gate.py`，用独立 C2/6/12 strict-op CNN 只训练 C248 五源 normal-only source-gate teacher，再在高压事件上选择五源结果。soft teacher 版本的 TFLite 图只含推荐算子，但 gate 保守估时已约 `1893us`，normal int8 source-label acc `43.27%`，几乎全选 `d8q`，高压退到 `29.54% / 14.73%`；加 inverse-label class balance 后 normal int8 source-label acc `42.52%`，高压更差为 `32.24% / 12.83%`。这排除了“把 source gate 从 prototype head 里拿出来就能学会”的简单解释：tiny 图像侧 source/orbit gate 本身也会 collapse，且单独运行已接近 2ms 预算。下一步应停止 CE/label-balance/后验 gate 路线，转向更结构化的 source-choice preserving 目标或预算感知二阶段结构，直接约束 compiler/prototype 合并不要抹掉 event-level source decision。

2026-05-21 D4 dynamic qpair axis margin 追测：在 `train_v8_parent_classifier.py`
加入默认关闭的 `--dynamic-qpair-axis-*` loss，只用 normal-only dynamic qpair
teacher 约束每个 int8 维度的 nearest-correct / nearest-wrong axis margin；高压仍只作
最终评估。该分支 raw int8 normal replay 不闭合（rotmirror/stress min `95.72%`），
normal-only compile 后 strict-under2 `exact_clean_residual` 为 `816 x 4`、保守
`1959us`、normal replay 100%、`int8_margin_min=2`，canonical 高压
`31.33% / 11.18%`。另一个按用户要求保留的“超时但好看”候选是
`exact_clean_rotmirror_residual`：`2451 x 4`、平均 `1711us`、保守 `2090us`、
normal replay 100%、`int8_margin_min=1`，canonical 高压 `31.27% / 11.05%`。
结论：把 dynamic qpair margin 分摊到每个 D4 轴可以形成 clean 的 near-gate 表，
但高压仍停在 `31% / 11%` 平台，不能作为 `<10%` 方向；该候选已进入
timeout retained manifest，只作为 D4 margin-thickening 边界诊断。后续不要继续在
同一 D4 parent-logit 最近原型空间里只做 scalar/axis margin 加权，应转向
source-choice preserving 表征、事件级 source/orbit gate，或能在 compiler/prototype
合并阶段约束 source-decision collapse 的预算感知结构。

2026-05-21 D4 shift-stress 续训追测：为验证“高压主要 shift/blur-heavy，直接加入通用 synthetic shift 视图能否增强鲁棒性”，从当前 C2/6/12-D4 multiteacher anchor 续训 `120` epoch，训练视图加入 `cam_shiftu1/shiftd1/shiftl1/shiftr1/shiftul1/shiftdr1/shiftu2/shiftl2/shiftdr1_noise0p04/shiftu1_noise0p04`，仍只使用 normal-only dynamic qpair teacher，高压事件不参与训练或编译。raw int8 shift 视图确有上升，但 rotmirror 被拉低到 `92.11%`；normal 30-view residual compile 可以闭合为 `1035 x 4`、保守 `1976us`、normal int8 replay 100%，但 `int8_margin_min=1`，canonical 高压退化到 `31.53% / 11.38%`。结论：plain synthetic shift-view expansion 不是 margin 增厚主线；它会牺牲 D4/rotmirror geometry，最终不降低高压错误。后续不应继续只加更多 scalar synthetic stress views，而应把 source/orbit decision 保留在预算感知表示、gate 或 compiler/prototype 结构中。

2026-05-21 D4 orbit + VICReg anti-collapse 追测：为验证推荐论文清单中的 VICReg / Barlow Twins anti-collapse 思路是否能补上单独 orbit consistency 的缺陷，`train_v8_parent_classifier.py` 新增默认关闭的 `--vicreg-var-weight`、`--vicreg-cov-weight`、`--vicreg-variance-floor`、`--vicreg-start-epoch`。该 loss 在 fake-int8 code 上做 variance floor 与 decorrelation，只影响训练，不增加推理算子；高压样本不参与训练、teacher、compile、prune 或阈值选择。`orbit_vicreg_f24_e80` 从当前 C2/6/12-D4 multiteacher anchor 续训 80 epoch，使用 normal-only dynamic qpair teacher 与 29-view normal stress 集。raw int8 normal replay 不闭合；normal-only `exact_clean_residual` compile 后得到 `898 x 4`、保守 `1965us`、normal 29-view int8 replay 100%、`int8_margin_min=3`，推荐算子集合保持 `SPACE_TO_DEPTH + CONV_2D + MAX_POOL_2D + MEAN + FULLY_CONNECTED`（`DELEGATE` 仅为 interpreter 记录），canonical 高压为 `31.25% / 11.21%`。结论：anti-collapse 可以得到一个 compact under2 表，但本质仍是在单一 D4 nearest-prototype 空间内调 orbit/margin 标量；它不能恢复 source/orbit event-level 互补性，后续不应继续扫 D4 VICReg/Barlow 权重，而应继续做 source-choice preserving 表征、显式事件级 gate、预算感知二阶段表或 compiler/prototype 合并约束。

2026-05-21 C248 source-decision preserving prune 追测：为验证“compiler/prototype 合并阶段约束 source-decision collapse”是否能挽救 C248 compiler-feedback 大表，`prune_merge_v8_logit_prototypes.py` 新增默认关闭的 source-decision preserve 接受条件。输入 `c248pool_m32_compile` 是当前最小 C248 source-decision compiler 表：`2533 x 4`、normal replay 100%、`int8_margin_min=4`、保守 `2096us`、source-decision active rows `9119`、<= target rows `96`。一次性 normal-use 诊断显示只有 `5` 个 prototype 同时满足“无 correct usage、无 low-margin wrong usage、无 source-decision risk usage”，保护后的理论最小 keep 已是 `2528`；`m32_preserve_smoke` 实测也只接受这 `5` 个删除，得到 `2528 x 4`、保守仍 `2096us`，无法接近 under2。结论：source-decision-preserving prune/merge 本身不是出路，因为表在 normal/source-decision 约束下已经几乎不可删；下一步应避免先把 source-choice 膨胀为 D4 residual 表，而是在 encoder 表征、显式二阶段 gate/table 或预算感知 compiler 里直接限制 source/orbit decision 的容量。

2026-05-21 C2/6/12-D4 hybrid 推荐算子迁移追测：为了利用文档推荐的 `DEPTHWISE_CONV_2D + 1x1 CONV_2D` 加速空间，测试了 `v8_parent_c2612_d4_hybrid_multiteacher_20260521_0001/hyb_c2_6_12_d4_partial_e160`。它保持当前 D4 multiteacher 的 normal stress 列表和 normal-only dynamic qpair teacher，从现有 C2/6/12-D4 anchor partial-init，唯一关键变化是 `spacetodepth_hybrid` backbone；该 backbone 估计为 `1172us` avg / `1465us` conservative，理论上比当前 `spacetodepth_conv` 留出约 `428us` 保守距离预算。实测失败很早：160 epoch 后 clean / rotmirror / normal stress / fixed-medium stress 以及 int8 TFLite replay 全部只有 `34.21%`，TFLite op audit 虽然包含推荐算子 `CONV_2D + DEPTHWISE_CONV_2D + FULLY_CONNECTED + MAX_POOL_2D + MEAN + SPACE_TO_DEPTH`，但没有资格进入 residual compile 或高压评估。为区分“partial-init collapse”与“hybrid 只需大表”的可能性，又复查已有 CE-trained `hyb_c2_6_12_s20260933`，用当前完整 29-view normal stress 编译 D3 parent-logit residual 表；最优有效表形为 `4665 x 3`、估计约 `1745us`，但 int8 clean 只有 `99.67%`、stress min `97.70%`、`int8_margin_min=0`，甚至 `exact_all 9120 x 3` 也不能闭合 normal replay。这说明低时延 hybrid D3 code 存在跨类 int8 碰撞，不能靠 residual memory 修复。再用当前 D4 multiteacher int8 code 作为 normal-only qanchor teacher，叠加 dynamic qpair teacher 训练 `hyb_c2_6_12_d4_qanchor_w0005_e200`；虽然 qanchor loss 从 `2543.90` 降到 `2447.27`，但 clean / rotmirror / normal stress / fixed-medium stress 与 int8 replay 全部为 `31.58%`，仍无法进入 residual compile 或高压评估。结论：低时延 hybrid backbone 不是当前 D4 anchor 的 drop-in 加速版本；直接 partial-init 会把 source/orbit 表示打散，旧 CE hybrid 受 D3 code collision 限制，简单 D4 qanchor 蒸馏也不能救回 depthwise middle blocks。后续若还要利用 depthwise 推荐算子，应先做针对 depthwise blocks 的专门 pretraining / distillation，或输出更强的 source/orbit 表征，再进入 prototype compiler。

2026-05-21 proto-key source residual table 追测：新增 `evaluate_v8_proto_key_source_residual_table.py`，把 C248 五源 pool 压成更可部署的二阶段表：共享当前 C2/6/12-D4 clean base table，然后只按 normal-only D4 `proto_parent+pred+margin_bucket` source map 访问一个 source residual table。高压样本仍只作最终评估，不参与 routing map、compile、prune、target-margin 或阈值选择。`target_margin=0/8/16` 三档都能闭合 normal replay 且保持 under2：分别为 `584 x 4`、`596 x 4`、`618 x 4`，保守估时 `1940/1941/1943us`，`int8_margin_min=1/4/4`。但 canonical 高压仍为 `31.14% / 11.17%`、`31.10% / 11.21%`、`31.11% / 11.24%`，source 路由也显示 `c248d4=0`，没有恢复五源 event-level oracle。结论：把 gate 换成 D4 proto-key routing、再加大 normal margin target，并不能突破高压平台；后续不应继续只扩同形态 local proto-key/residual 表，而应在表示、显式 source/orbit 结构或 compiler/prototype 合并阶段阻止 source-choice collapse。

2026-05-21 normal cross-view source-policy 追测：`analyze_v8_normal_source_policy_transfer.py` 新增 `row_peer_*`、`row_other_family_*`、`sample_family_other_*` 策略，专门测试“不看当前 normal 行，而用同一样本其它 view / 其它 view family 的最差 margin 或正确覆盖数”能否成为不使用高压样本的 event-level robustness proxy。高压仍只作冻结评估。结果比原来的 row-local winner 更差：`row_winner:raw` 仍是最佳 `26.17% / 9.90%`，`row_peer_min:raw` 为 `29.36% / 12.35%`，`row_other_family_correct_count:raw` 为 `30.54% / 10.81%`，`sample_family_other_correct_count:raw` 同为 `30.54% / 10.81%`。结论：正常其它视图/leave-family 源选择不能解释高压 low 组，后续不要继续扩 normal sample/family/peer source-label 聚合；若要 source/orbit 路线，必须把 source-choice 信息在表示或 compiler/prototype 合并前保留下来，而不是从正常聚合标签里事后恢复。

2026-05-21 non-high-pressure synthetic source event gate 追测：新增 `analyze_v8_synthetic_source_event_gate.py`，只从当前 D4 anchor 的 normal deployment row 里选低 margin 样本，并显式排除 canonical high-pressure 的 `base_query_index`；再在这些非高压 row 上生成 synthetic perturb 事件，用 C248 五源 pool 的正常标签和 source margin winner 训练一个小 source selector，高压仍只作最终冻结评估。13 个默认扰动的完整诊断已把总高压错误降到 `8.06%`，但 low/control 为 `13.78% / 2.33%`；覆盖全部 41 个高压 perturb 算子后，最佳 `code+dist+margin+pred+wrong_parent+family:hidden64` 达到总错误 `5.87%`、low/control `10.29% / 1.46%`。继续把 normal margin 上限放宽到 `256`、加入 exact perturb one-hot 或扩 hidden 到 `128` 都没有把 low 稳定压过 `<10%`。结论：不使用高压样本的 synthetic event-level proxy 是目前最强的 source-choice 诊断，证明问题不是 C248 源池没有互补性，而是该信号尚未压成部署结构；它运行时读取五个 source embedding，不能直接晋升为 under-2ms 单 backbone。下一步应以它为 teacher/target，做预算感知二阶段 source/orbit gate、single-encoder source-choice preserving distillation，或 compiler/prototype source-choice collapse penalty；不应继续只扩 D4 local guard、static proto-key routing、normal sample/family 聚合或普通 synthetic view expansion。

2026-05-21 synthetic source-gate 单 encoder 蒸馏追测：新增 `build_v8_synthetic_source_gate_teacher.py`，把上述 synthetic source winner 转成 `train_v8_parent_classifier.py` 可读的 normal-only `source_gate_teacher.npz`，再从当前 C2/6/12-D4 anchor partial-init 训练 C2/6/12-D8 单 encoder。teacher 使用 normal clean rows 与 synthetic high-pressure-like views，并排除 canonical high-pressure base clean rows；canonical 高压仍只作最终冻结评估。结果显示最小蒸馏切口失败：raw int8 TFLite normal replay 不闭合（rotmirror min `88.49%`、stress min `69.74%`），source-gate loss 只从 `3.4588` 降到 `3.4176`，side margin mean 仍约 `-31`；normal-only residual compile 虽能把 clean/rotmirror/61-view stress replay 闭合到 100%、`int8_margin_min=5`，但表膨胀为 `5339 x 8`、保守 `2748us`，canonical 高压只有 `29.74% / 6.37%`。该行已按“超时但好看”规则保留为 synthetic source-choice distillation boundary，不是部署晋升。结论：synthetic event-level signal 不能靠 clean-row source-gate CE 直接压进一个紧凑 nearest-prototype code；下一步应先让 source/orbit decision 在预算内可分，例如显式二阶段 gate/table、source-choice preserving distillation loss，或 compiler 期间的容量约束，而不是继续扫 source-gate CE 权重或普通 synthetic view expansion。

2026-05-21 synthetic source-gate rank/center 蒸馏追测：在同一 `source_gate_teacher.npz` 上训练 C2/6/12-D8 `rw005_cw001_mw0002_b005_ce002_cleanrow_e80`，加入 pairwise source-rank、source-center、小 CE、qmargin 与 balance；高压仍不参与训练、teacher、compile、set-cover 或阈值选择。raw int8 TFLite 仍不能闭合 normal replay（rotmirror min `88.16%`、stress min `70.39%`），side rank/center margin 到 epoch 80 仍为负（`-7.66` / `-42.31`）。normal-only residual compile 可闭合为 `5358 x 8`、保守 `2751us`、`int8_margin_min=5`，canonical 高压改善到 `23.93% / 6.12%`；这说明 rank/center 比 CE 保留了更多 synthetic source-choice 几何，但仍必须用超大表表达。进一步 normal-only set-cover 中 `m5` 对 parent 0 有 12 个正常行不可覆盖；有效 `m4` 为 `2581 x 8`、`2306us`、高压 `26.89% / 7.49%`，最小 `m0` 为 `2447 x 8`、`2285us`、高压 `27.71% / 7.31%`，全部仍超出保守 `<=2ms`。该分支按“超时但好看”规则保留为 source-choice preserving compression boundary；下一步不应继续普通 side-head rank/center 权重扫，而应把 source/orbit decision 先预算化，例如显式二阶段 gate/table、source-choice-aware set-cover/merge，或训练时直接约束可压缩的 source decision。

2026-05-21 synthetic source-choice guard cascade 追测：新增 `evaluate_v8_synthetic_guard_cascade.py`，不训练新 encoder，而是在当前 C2/6/12-D4 部署表上追加少量来自 non-high-pressure synthetic teacher 的 guard prototypes；guard 选择只用 synthetic teacher 与 D4 synthetic TFLite 特征，normal replay 必须保持 100%，高压仍只作最终冻结评估。最佳 under2 点为 `target_score_all_b360/best_valid`：`128` 个 guards，运行时 `1095 x 4`、保守 `1981us`、normal clean/D4/fixed/medium stress int8 replay 100%，但高压只到 `31.15% / 10.81%`（baseline `31.30% / 10.80%`）。放宽到 `360` guards、无 safe-radius 预筛且仍保持 normal replay 时，也只有 `31.19% / 10.82%`。结论：synthetic source-choice 信号在当前 D4 code 里不是局部 guard 可恢复的结构；不要继续只加 synthetic guard prototypes 或扫更大半径。下一步仍应转向能在表示层/显式 gate 层保留 source-orbit decision 的结构，而不是 D4 最近原型空间的后置 guard。

2026-05-21 source-choice anchor set-cover 复评：为了验证“compiler 阶段先保留少量 source-choice anchors”能否比后置 guard 更有效，复评已有 `select_v8_sourcechoice_anchor_setcover.py` 输出，并给 `stress_test_v8_low_margin.py` 增加 `--allow-missing-selection-rows`，使 base D4 canonical low/control 选样在目标表缺少个别 normal view 行时仍可通过目标 TFLite 重新生成高压特征；高压事件仍只作最终评估，不参与选样、训练、set-cover 或阈值。三个代表点 normal replay 均为 100%、推荐算子仍为 `SPACE_TO_DEPTH + CONV_2D + MAX_POOL_2D + MEAN + FULLY_CONNECTED`：`pm3_anchor0` 为 `248 x 8`、`int8_margin_min=4`，base-canonical 高压 `185/656`、low/control `51.22% / 5.18%`；`pm3_anchor32` 为 `566 x 8`、高压 `185/656`、low/control `51.83% / 4.57%`；`pm3_anchor64` 为 `729 x 8`、高压 `184/656`、low/control `51.52% / 4.57%`。结论：只在 parent set-cover 上追加正常 source-label usage anchors 会显著恶化 low 组，不能恢复 synthetic/source oracle 互补；即使 anchors 保留了正常 source 分布，事件级高压路由仍没有被表达。后续不要继续扩大同形态 anchor cap；若继续 compiler 路线，必须把 non-high-pressure synthetic event teacher 的 source-choice 约束直接放入覆盖目标，或者采用 Hydra-style shared trunk multi-head / BranchyNet-style conditional cascade，让 source/orbit decision 在结构上保留，而不是作为少量普通 prototype anchor 混入单一 parent 表。

2026-05-21 synthetic-teacher source-choice anchor 复评：进一步把 `select_v8_sourcechoice_setcover_prototypes.py` / `select_v8_sourcechoice_anchor_setcover.py` 扩展为可读取 `source_gate_teacher.npz`，并显式要求 `--allow-missing-source-labels --allow-evaluation-only-teacher`；当前 teacher 为 `experiments/v8_synthetic_source_gate_teacher_20260521_0001/cleanrow_all_margin_all41_extra29/source_gate_teacher.npz`，其 `high_pressure_usage=excluded_base_clean_rows_evaluation_only`，因此 canonical 高压仍没有参与训练、teacher、set-cover、阈值或剪枝。用 sourcecluster under2 表作为输入，`anchor_cap=0/16/32/48/64/96` 分别得到 `248/433/526/539/539/539` prototypes，normal replay 均为 100%、推荐算子不变；但 base-canonical 高压复评仍失败：`pm3_anchor32` 为 `184/656`、low/control `51.52% / 4.57%`，`pm3_anchor48` 为 `185/656`、low/control `51.83% / 4.57%`。结论：即使 anchor 标签来自 non-high-pressure synthetic event teacher，只要它仍被压平成单一 parent prototype table 的普通 anchors，就不能表达 oracle 的 source-choice 互补；下一步不能再扫 anchor cap，而要把 synthetic event coverage 做成结构性路由目标，或直接训练/编译 shared-trunk multi-head 与预算内 conditional path。

2026-05-21 restricted-runtime synthetic source gate 复评：给 `analyze_v8_synthetic_source_event_gate.py` 增加 `--runtime-feature-sources`，把训练标签仍设为五源 non-high-pressure synthetic row-winner，但 gate 输入限制为单一 `d4best` 的 code/dist/margin/pred/wrong-parent；canonical 高压仍只作冻结评估，未进入训练标签、阈值或剪枝。`d4only_runtime_highpress_perturbs` 用 `32800` 个 synthetic events 训练，`runtime_source_order=["d4best"]`，纯 base 可部署特征 best 为 `code+dist+margin+pred+wrong_parent:hidden16`，高压 `3069/24190`、low/control `19.76% / 5.61%`；加入 `perturb` 诊断特征的 best 为 `code+dist+margin+pred+wrong_parent+perturb:hidden32`，高压 `2901/24190`、low/control `18.86% / 5.13%`，但 perturb 类型不是板端可用输入，不能作为部署候选。该结果比 normal-label learned gate 的约 `26-28%` low 明显更好，证明 synthetic event winner 标签确实携带可迁移 route 信息；同时它也证明单个 D4 后验只能恢复约一半 oracle 互补，离 `<10%` low 仍很远。注意：这里限制的是 gate 输入，被选择的五源 prediction 仍来自不同 source/backbone，因此这是 route 学习上界诊断，不是 `<=2ms` 完整部署路径。

2026-05-21 d4 proto-key synthetic source gate 复评：同一脚本继续加入 `proto` / `second_proto` / `bucket` 特征，把 `d4best` 最近预测原型 id 与 margin bucket 暴露给 synthetic-label gate。`d4proto_runtime_highpress_perturbs` 的 best 为 `proto+code+dist+margin+pred+wrong_parent+bucket:hidden8`，高压 `3369/24190`、low/control `21.46% / 6.39%`；纯 `proto+pred+bucket` 系列低组约 `29-31%`。结论：nearest-prototype cell 本身太粗且容易过拟合，不能作为恢复五源 source-choice 的主线；后续不要把 D4 prototype id 查表扩展当作核心 route 方案。真正下一步应把 route/source choice 放进单次 backbone 的结构中，例如 shared-trunk multi-head / source-orbit side code / 预算感知二阶段表，而不是用 D4 后验去选择外部 source 的预测。

2026-05-21 single-backbone synthetic route-head smoke：新增 `train_v8_synthetic_route_head.py`，尝试把前 3 维作为 parent logits、后 5 维作为 source/route logits，用 non-high-pressure synthetic row-winner 训练单 backbone route head，高压仍只作最终冻结评估。`smoke_e2_m40` 验证了流水线和推荐算子导出可行，int8 ops 为 `SPACE_TO_DEPTH + CONV_2D + MAX_POOL_2D + MEAN + FULLY_CONNECTED`（另有 interpreter `DELEGATE` 记录）；但 naive 联训会破坏 normal replay（raw clean `99.01%`、rotmirror min `87.50%`；int8 clean `99.34%`、rotmirror min `86.51%`），route head 也塌缩为全选 `d6sm`，高压 route-selected `5165/24190`、low/control `30.73% / 11.97%`，parent head 高压 `5441/24190`、low/control `32.39% / 12.60%`。结论：单 backbone route-head 方向仍值得保留，但不能继续 naive 联训；下一次必须冻结/保护已闭合的 parent 表达，只训练可预算化 route/source 维，或改成 shared-trunk separate-head，使 parent replay 先验闭合不被 route loss 拖坏。

2026-05-21 frozen-prefix route-head 追测：给 `train_v8_synthetic_route_head.py` 增加 `--freeze-backbone`、`--freeze-parent-output-dims`、`--route-start-dim`、`--freeze-prefix-dims`、`--separate-route-head` 和 `--route-hidden-dim`，验证“保护 parent，只训练 route/source”的最小切口。单 Dense route-only 的 `frozen_smoke_e20_m80` 保持推荐算子集合，但 route 仍弱，高压 `5227/24190`、low/control `29.81% / 13.41%`；修正为保留 D4 前 4 维、route 放到第 4 维后的 `frozen_d4prefix_e20_m80` 更差，高压 `5712/24190`、low/control `35.32% / 11.91%`，synthetic source val 退到 `7.5%`。小 MLP separate-head `separate_d4prefix_h16_e30_m80` 也失败，高压 `6007/24190`、low/control `36.55% / 13.11%`，且 int8 图新增 `CONCATENATION`，不满足当前推荐算子约束。结论：固定 D4 trunk/GAP 不能线性或小 MLP 地读出足够 source/orbit 决策；route-head 方向若继续，必须让表示训练本身保留 route 信息，而不是后接冻结读头。短期主线应转向 source-choice-aware compiler/set-cover，把 non-high-pressure synthetic event winner 直接作为覆盖约束，而不是继续在固定 D4 表示后面加 head。

2026-05-21 synthetic source-choice direct set-cover 追测：给 `select_v8_sourcechoice_setcover_prototypes.py` 补齐 `--allow-missing-source-labels` / `--allow-evaluation-only-teacher`，直接用 `source_gate_teacher.npz` 的 non-high-pressure synthetic winner 做严格 `(parent, source_label)` 覆盖。source-cluster D8 输入在最宽松 `pm0/sm0` 已失败：`parent 0 / source 0` 有 `72` 行不可由同源同类原型覆盖；rank/center 超时边界表同样在 `pm0/sm0` 失败，且有 `606` 行不可覆盖。结论：这不是 anchor cap 或普通 subset 选择问题，而是现有单表几何没有把 synthetic source-choice 做成可覆盖结构；继续在单一 nearest-prototype 表里做 source-choice set-cover 不会达标。下一步若走 compiler 路线，必须改成真正二阶段/多表结构（base parent replay + source-specific residual），或在训练阶段从表示层生成可覆盖的 source/orbit 子空间，而不是事后要求 collapsed 表满足同源覆盖。

2026-05-21 source-gated residual oracle-label 诊断：给 `evaluate_v8_source_gated_residual_table.py` 增加 evaluation-only `--stress-source-events`，并支持正常编译阶段用 `--normal-source-gate-teacher-npz` 读取 synthetic teacher source label。仅把高压评估 source label 换成五源 oracle 时，`w010_qm0005_b010_clean_t8_hporacle` 仍为 `590 x 8`、保守 `1988us`、normal replay 100%，但高压退到 `5370/24190`、low/control `32.54% / 11.86%`；正常编译也改用 synthetic teacher label 并对缺失行 fallback 到 gate 后，`w010_qm0005_b010_clean_t8_synthnormal_hporacle` 为 `569 x 8`、保守 `1984us`、normal replay 100%，高压仍是 `5361/24190`、low/control `32.69% / 11.63%`。结论：当前 D8 source-gated residual 表即使给高压 oracle source label 也无法承载五源互补，因为各 source residual 仍来自同一个 collapsed embedding 空间；问题已经不是单独的 gate，而是“source-specific 表内容”没有源自各自互补 source 的几何。后续二阶段若继续，source residual 必须来自真实 source-specific embedding/prototype 空间或训练出等价的独立子空间，而不是在同一 D8 code 上按 source 分桶。

2026-05-21 synthetic source parent-block 诊断：新增 `train_v8_synthetic_source_blocks.py`，让单个 strict-op encoder 一次输出 `parent prefix + 5 x 3 source parent-logit blocks`，每个 source block 蒸馏对应 source 在 non-high-pressure synthetic events 上的 parent class-distance 行为；canonical 高压仍只作最终冻结评估，未进入训练、teacher、阈值或剪枝。脚本 smoke `smoke_e2_m30` 验证了导出流水线，int8 ops 仍为 `SPACE_TO_DEPTH + CONV_2D + MAX_POOL_2D + MEAN + FULLY_CONNECTED`（`DELEGATE` 仅为 interpreter 记录）。正式诊断 `frozen_d4prefix_dist_e60_m400` 冻结 D4 trunk 和 parent prefix，只训练 source blocks：synthetic selected-block parent acc 到 `62.43%`，但 margin-policy source acc 只有 `20.58%`；高压 int8 best 为 block-sum `5784/24190`、low/control `36.72% / 11.10%`，即使用 evaluation-only external oracle source 去选 block 也只有 `5769/24190`、low/control `35.82% / 11.88%`，远差于同一五源 external oracle 的 `1327/24190`、`9.71% / 1.26%`。继续允许 trunk 适配的 `unfreeze_prefixguard_dist_e80_m400` 更差，高压 int8 best `6904/24190`、low/control `42.33% / 14.75%`；加入显式 selected-source top-logit margin 的 `unfreeze_choice_top_e60_m400` 也只把 synthetic top-policy source acc 推到约 `25.7%`，高压 int8 退到 `9342/24190`、low/control `46.03% / 31.21%`。结论：把五源 parent 行为塞进单个 tiny shared trunk 的输出分区，并不能复现 source-specific 几何；即使外部 oracle source 已知，distilled block 本身也不可靠。后续不要继续扫同形态 block-distill / top-choice scalar 权重。若继续二阶段，应直接使用真实 source-specific embeddings/tables 做 <=8ms 边界，或训练具备更强独立容量的条件结构；strict under2 主线则必须在 compiler/representation 中保留 source-specific 几何，而不是仅用单 FC 输出分块模拟五个 source。

这些问题服务于 margin 增厚；板端导出和 C++ prototype replay 已经可以并行推进，不必等待所有研究项完成。

## 17. 文档中的关键承诺

V8 后续实现必须遵守：

- rot_mirror 是训练必选项，也是验证硬门槛。
- stress blur/noise 是验证硬门槛，不能只作为附加报告。
- CTD 只作为动态候选信号，不能先验性强作用；任何 CTD-derived weighting 都必须通过当前 replay 证明不伤害已正确区域。
- prototype 成功必须带 compression 和 int8 replay。
- pure embedding 成功必须优于“all_views 查表记忆”。
- 每个 top checkpoint 都要输出 clean、rot_mirror、stress、prototype_count、int8 flip、no-self/LOO、total latency cost。

如果实现偏离这些承诺，应先更新本文档并解释原因，再启动长线训练。

## 18. 参考论文索引

本节只列与 V8 直接相关的论文和在本项目中的用途。

| 方向 | 论文 | V8 用途 |
| --- | --- | --- |
| Prototype / metric classifier | Prototypical Networks, https://arxiv.org/abs/1703.05175 | 支撑“embedding + nearest prototype”主推理结构 |
| Supervised contrastive | Supervised Contrastive Learning, https://arxiv.org/abs/2004.11362 | 构造同原图、多 view、同 subclass、同 parent 的分层 positive/negative |
| Angular margin / sub-centers | ArcFace, https://arxiv.org/abs/1801.07698 | 用角度 margin 和多中心隔离动态发现的多模态或边界区域 |
| Proxy metric learning | Proxy Anchor Loss, https://arxiv.org/abs/2003.13911 | 小数据下用 learned proxies 稳定收敛，再压缩到部署 prototype |
| Hard pair mining | Multi-Similarity Loss, https://openaccess.thecvf.com/content_CVPR_2019/html/Wang_Multi-Similarity_Loss_With_General_Pair_Weighting_for_Deep_Metric_Learning_CVPR_2019_paper.html | 对 replay-confirmed boundary pairs 做 mining + weighting |
| View-invariance anti-collapse | VICReg, https://arxiv.org/abs/2105.04906 | D4/stress 一致性下防止 embedding collapse |
| Redundancy reduction | Barlow Twins, https://arxiv.org/abs/2103.03230 | 作为 VICReg 的替代或补充，约束 embedding 维度不要冗余塌缩 |
| Online clustering | SwAV, https://arxiv.org/abs/2006.09882 | 用多 view cluster assignment consistency 辅助 prototype compression |
| Geometry/equivariance | Group Equivariant CNN, https://arxiv.org/abs/1602.07576 | 为 D4 几何一致性提供结构性参考，暂不作为首轮板端实现 |
| Prototype metric learning | Matrix/Relevance LVQ, https://link.springer.com/article/10.1007/s13218-012-0188-1 | 把 nearest-correct vs nearest-wrong prototype margin 写入训练，支持 per-dim/low-rank relevance |
| Coreset / subset selection | apricot / submodular selection, https://jmlr.csail.mit.edu/papers/v21/19-467.html | 把 prototype 表选择改成全集覆盖和多样性优化，而不是只看 kmeans reconstruction |
| Retrieval quantization | Product Quantization, https://doi.org/10.1109/TPAMI.2010.57 | 若 prototype 表仍大，用子空间量化降低表规模和距离成本 |
| Hash retrieval | Learning to Hash survey, https://link.springer.com/article/10.1007/s10115-022-01734-0 | 借鉴 quantization/hash loss 稳定离散 embedding，作为后续压缩方向 |
| Margin cascade | Reject-option classifier analysis, https://www.sciencedirect.com/science/article/abs/pii/S0031320319302870 | 把低 margin 样本转入 boundary repair table，避免主表为少数边界过度膨胀 |
| Ensemble diversity distillation | Hydra, https://arxiv.org/abs/2001.04694 | 用 shared trunk + multi-head 保留多源 teacher 的差异，避免把 oracle 互补平均进单一 head |
| Perturb-to-reveal diversity | Diversity Matters When Learning From Ensembles, https://arxiv.org/abs/2110.14149 | 用非高压 perturb 暴露 teacher disagreement，对应当前 synthetic event gate 的有效信号 |
| Dynamic / early-exit inference | BranchyNet, https://arxiv.org/abs/1709.01686 | 为预算内 conditional cascade 提供结构参考：简单样本走 base path，低 margin/不稳定事件走受限二阶段 path |
| Discrete codebook | VQ-VAE, https://papers.neurips.cc/paper/7210-neural-discrete-representation-learning.pdf | 借鉴 codebook/commitment 思想，让 encoder 贴近 compiler prototype code |
| Weight merging boundary | Model Soups, https://arxiv.org/abs/2203.05482 | 解释为什么只有同 basin/相近模型才适合直接 soup |
| Task vector merge | Task Arithmetic, https://arxiv.org/abs/2212.04089 | 支撑“局部 correction 作为 delta”的思想，但 V8 转到 embedding space 实现 |
| Merge interference | TIES-Merging, https://arxiv.org/abs/2306.01708 | 对应 old/CTD sign conflict 的诊断思路，转化为按 replay 结果动态保留多 sub-centers |
| Permutation alignment | Git Re-Basin, https://arxiv.org/abs/2209.04836 | 解释权重合并前需要通道/表示对齐 |
| Importance-weighted merging | Fisher-weighted averaging, https://arxiv.org/abs/2111.09832 | 借鉴 importance weighting，但权重由当前 replay/margin 动态决定，而非固定旧模型区域标签 |
