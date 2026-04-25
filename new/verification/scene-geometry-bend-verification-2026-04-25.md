# 场景几何泛化与弯道入口修正验证总结

日期：`2026-04-25`

## 1. 目标

本轮工作目标是把当前宽场景/普通弯道几何从“固定三带 + 贴边仍算有效边线”改成：

- 边线状态化：`visible` / `border_truncated` / `missing`
- 主几何改为当前帧有效行分位点自适应锚点
- `bend` 允许使用当前帧短程外推与前 `1-2` 帧历史兜底
- `circle_entry` / `cross` 只允许使用当前帧真实可见证据

用户给出的目标场景固定为普通弯道入口，因此这类“双边可见但一侧贴边”的画面必须进入 `bend`，不得停在 `straight`，也不得误入 `special_wide`、`circle_entry`、`cross`。

## 2. 实现摘要

本轮改动主要落在：

- [new/code/legacy/steering_scene_common.hpp](/home/madejuele/projects/2K0300/new/code/legacy/steering_scene_common.hpp)
- [new/code/legacy/steering_scene_common.cpp](/home/madejuele/projects/2K0300/new/code/legacy/steering_scene_common.cpp)
- [new/code/legacy/steering_scene_circle_entry.cpp](/home/madejuele/projects/2K0300/new/code/legacy/steering_scene_circle_entry.cpp)
- [new/code/legacy/steering_scene_bend.cpp](/home/madejuele/projects/2K0300/new/code/legacy/steering_scene_bend.cpp)
- [new/code/runtime/perception_frontend.cpp](/home/madejuele/projects/2K0300/new/code/runtime/perception_frontend.cpp)
- [new/code/port/control_types.hpp](/home/madejuele/projects/2K0300/new/code/port/control_types.hpp)
- [new/verification/tests/scene_classifier_selftest.cpp](/home/madejuele/projects/2K0300/new/verification/tests/scene_classifier_selftest.cpp)

实现后的规则：

- `border_truncated` 不再被当作真实边线参与 `circle_entry` 的对侧直线证明
- `circle_entry` 的对侧直边证明要求当前帧 `3/3` 可见锚点，且非截断
- `cross` 仍然只看当前帧上方连续横带结构
- `bend` 使用弱补全几何；只有当前帧锚点不足时才允许历史介入
- 运行时状态新增最近两帧可见锚点快照，但只供 `bend` 兜底使用

## 3. 离线 Authority Baseline

离线 `scene_classifier_selftest` 已扩展为 authority baseline 回归，覆盖以下九张样本：

- `circle-1`
- `circle-2`
- `circle-3`
- `cross-1`
- `cross-2`
- `cross-3`
- `bend-1`
- `bend-2`
- `bend-3`

离线结果：

- `circle-1`、`circle-2`：识别为 `circle_entry`
- `circle-3`：不误判为 `cross`
- `cross-1`、`cross-2`：识别为 `cross`
- `cross-3`：不误判为 `circle_entry`
- `bend-1`、`bend-2`、`bend-3`：识别为 `bend`

额外通过的回归项：

- `circle-*` 不误判为 `cross`
- `cross-*` 不误判为 `circle_entry`
- `bend-*` 不误判为 `circle_entry`
- `bend-*` 不误判为 `cross`
- `bend-*` 不进入 `special_wide`
- `border_truncated` 不可直接证明 `circle_entry`
- 历史兜底只在当前锚点不足时触发
- 历史兜底只影响 `bend`，不影响 `circle_entry` / `cross`

执行结果：

- 主机侧本地编译运行：`scene_classifier_selftest passed`
- 交叉构建：`rtk cmake --build new/out-runtime-params-test --target scene_classifier_selftest -- -j2`

## 4. 无电机板端静态验证

### 4.1 弯道入口静态验证一

证据包：

- [new/verification/steering-debug-20260425T085932Z/summary.json](/home/madejuele/projects/2K0300/new/verification/steering-debug-20260425T085932Z/summary.json)

结果：

- `board_steering_snapshot.jsonl`：`106 / 106` 帧为 `bend`
- `steering-media/frame_metadata.jsonl`：`45 / 45` 帧为 `bend`
- `steering_media_alignment.jsonl`：`41 / 41` 对齐帧为 `bend`
- 未出现 `special_wide`
- 未出现 `circle_entry`
- 未出现 `cross`

代表帧：

- [frame-000911.raw](/home/madejuele/projects/2K0300/new/verification/steering-debug-20260425T085932Z/steering-media/frames/frame-000911.raw)

### 4.2 弯道入口静态验证二

证据包：

- [new/verification/steering-debug-20260425T090443Z/summary.json](/home/madejuele/projects/2K0300/new/verification/steering-debug-20260425T090443Z/summary.json)

结果：

- `board_steering_snapshot.jsonl`：`94 / 94` 帧为 `bend`
- `steering-media/frame_metadata.jsonl`：`23 / 23` 帧为 `bend`
- `steering_media_alignment.jsonl`：`12 / 12` 对齐帧为 `bend`
- 未出现 `special_wide`
- 未出现 `circle_entry`
- 未出现 `cross`

代表帧：

- [frame-015978.raw](/home/madejuele/projects/2K0300/new/verification/steering-debug-20260425T090443Z/steering-media/frames/frame-015978.raw)
- [frame-016330.raw](/home/madejuele/projects/2K0300/new/verification/steering-debug-20260425T090443Z/steering-media/frames/frame-016330.raw)

## 5. 执行命令

本轮板端静态验证使用的 accepted workflow：

```bash
cd new/user
./debug.sh assistant local 8888 8890
./debug.sh build
./debug.sh remote restart normal
./debug.sh steering --duration-s 12
```

第二轮静态验证复用了已运行的 normal runtime，仅重新执行：

```bash
cd new/user
./debug.sh steering --duration-s 12
```

## 6. 结论

截至 `2026-04-25`，本轮“场景几何泛化与弯道入口修正”已经满足当前验证目标：

- 离线 authority baseline 九张样本识别完成并通过
- 当前目标类弯道入口不再停在 `straight`
- 当前目标类弯道入口不再误入 `special_wide`
- 当前目标类弯道入口不再误入 `circle_entry`
- 当前目标类弯道入口不再误入 `cross`
- 两轮无电机板端静态验证均稳定进入 `bend`

## 7. 剩余边界

- 本文只覆盖离线样本与无电机静态板端验证，不代表真实跑车动态闭环已经完成
- 仍建议后续继续积累更多不同光照、不同摆位、不同弯道半径的静态证据
- 若后续出现新误判，应优先复核对应证据包中的 `board_steering_snapshot.jsonl`、`frame_metadata.jsonl` 与原始 `raw` 帧
