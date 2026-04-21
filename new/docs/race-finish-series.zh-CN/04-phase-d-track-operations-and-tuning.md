# Phase D：赛道调参与运营流程

## 1. 阶段目标

把系统从“技术上可跑”推进到“现场可调、可复现、可回滚”。

比赛里大量失败并不是因为代码完全不会动，而是因为：

1. 参数无法快速切换
2. 现场问题无法定位
3. 一次调坏后无法恢复
4. 操作流程不稳定

所以这一阶段要补的是工程化和运营能力。

## 1A. 本阶段继承的运维与日志原则

本阶段承接自 debugging / hardware-matrix 的核心思想：

1. 调试 marker 是现场问题定位的主证据，不是附属信息。
2. 参数文件和 hardware profile 是正式运行输入，不是临时测试材料。
3. 现场流程必须比单次开发调试更严格，因为比赛时不能边跑边猜。
4. 每次赛道测试都必须能回溯“参数、配置、marker、失败位置和修复动作”。
5. 图片识别和未来距离传感器如果进入策略层，也必须留下独立的使能状态和证据，不允许混在“车今天好像跑得还行”的模糊结论里。

## 1B. `old_2` 暴露出的现场调参事实

`old_2/` 说明历史上真正可用的赛道系统，并不是“写死参数后反复重新编译”，而是已经有一套轻量车上调参 workflow：

1. `old_2/project/code/save.cpp` 使用 `settings.txt` 持久化关键参数。
2. `old_2/project/code/key.cpp` 提供参数浏览、切换、模式调整和显示交互。
3. 这套机制虽然不应原样拷贝进 `new/`，但它证明现场调参必须足够快，否则赛道阶段会被操作成本拖死。

因此，Phase D 不只是做“参数文件版本化”，还要把以下能力作为明确目标：

1. 能快速切换一组已知可用参数。
2. 能在车边确认当前启用的是哪组参数和关键阈值。
3. 能把一次现场微调以 project-owned 方式落盘和回滚。

## 2. Phase D 任务清单

## 2A. Assistant 双向速度调参 accepted workflow

本阶段接受一条 project-owned 的最小现场调参 workflow：

1. 板端通过 assistant TCP sidecar 主动连到上位机。
2. 上位机只使用一条 newline-delimited JSON session，同时消费 `ack` / `state` / `telemetry`。
3. 允许的首发命令仅限：
   - `enable_tuning_mode`
   - `disable_tuning_mode`
   - `start`
   - `stop`
   - `set_turn_suppressed`
   - `set_target_speed`
4. PID 参数本身仍然只通过 JSON 参数文件加重启更新，不允许在线写回。

accepted 现场步骤：

```bash
(cd new/user && ./debug.sh assistant local 8888)
(cd new/user && ./debug.sh build)
(cd new/user && ./debug.sh remote restart normal)
(cd new/user && ./debug.sh tuning \
  --sequence 20,40,60,77 \
  --ttl-ms 2500 \
  --step-dwell-ms 1200 \
  --disabled-mode-checks \
  --invalid-target-speed 90 \
  --csv ../verification/phase-d-speed-tuning.csv)
```

accepted 最小调参序列必须是：

1. `enable_tuning_mode`
2. `start`
3. 多个 `set_target_speed`
4. `stop`
5. `disable_tuning_mode`

accepted rejection coverage：

1. tuning mode 关闭时发送 `set_target_speed`
2. tuning mode 关闭时发送 `set_turn_suppressed`
3. 发送超出 `Speed_base` 的无效 target speed

accepted 证据：

1. `ack` 明确区分 `accepted` 与 `rejected`
2. `state` 明确覆盖 `input_rejected` / `override_cleared` / `snapshot_cleared`
3. `telemetry` 明确包含左右目标、左右测速、左右 PWM、`raw_turn_output`、`applied_turn_output`
4. CSV 可直接归档为重复调参 run 的主证据

不允许的 shortcut：

1. 用 `stop` 代替 `disable_tuning_mode`
2. 直接改 runtime phase 或 actuator 输出来模拟 remote start/stop
3. 在线热写 PID 参数并把它包装成调参 workflow
4. 用临时 Python socket 草稿替代 `new/user/tune_speed.py`

### D-1 参数集版本化

目标：
把“当前能跑的参数”升级成可追溯、可切换、可回滚的参数体系。

任务：

1. 定义多套参数集：
   - baseline
   - 低速验证
   - 赛道 A
   - 赛道 B
   - 阴影环境
   - 强反光环境
2. 规范参数文件命名和切换方式。
3. 记录每次变更的理由和效果。
4. 显式验证有效参数加载路径。
5. 显式验证缺失参数文件时的 defaults/fail-safe 行为。
6. 显式验证 malformed 参数文件时的 defaults/fail-safe 行为。
7. 显式验证 startup-critical 字段非法时，系统不会进入执行器解锁，例如非法 `P_Mode` 或 `exp_light`。
8. 显式验证“语法合法但 direct-match 不支持”的非默认 `exp_light` 分支。
9. 参考 `old_2` 的 `settings.txt` 模式，为当前项目设计“快速切换 preset + 留痕回滚”的 project-owned 工作流，但不直接复用旧文件格式作为长期契约。

验收标准：

1. 任意测试可以追溯使用的是哪套参数。
2. 出现退化时能快速回滚。

触达文件：

- `new/config/default_params.json`
- 参数版本目录
- `new/code/platform/param_store.cpp`

当前运行时参数文件中的执行器相关字段至少应明确包含：

1. `pwm_limit`：PWM 上限。
2. `pwm_floor`：非零 PWM 命令的最小绝对值；设为 `0` 表示关闭该钳制。
3. `prohibit_reverse_pwm`：禁止反转；开启后负向 PWM 会被截成 `0`。
4. `motion_pwm_step_limit`：单周期 PWM 斜率限制。

其中 `pwm_floor` 的目标是把“参数调节”留在配置文件，而不是把最小起转占空比写死在控制代码里。

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-d-parameter-failure.log ./run_remote_smoke.sh)
rm -f /tmp/ls2k-params-missing.json
printf '{ invalid json }\n' >/tmp/ls2k-params-malformed.json
sed '/"P_Mode":/d' new/config/default_params.json >/tmp/ls2k-params-missing-field.json
sed 's/"P_Mode": 3/"P_Mode": 99/' new/config/default_params.json >/tmp/ls2k-params-invalid-critical.json
sed 's/"exp_light": 65/"exp_light": 66/' new/config/default_params.json >/tmp/ls2k-params-valid-nondefault-exposure.json
sed 's/"exp_light": 65/"exp_light": 9999/' new/config/default_params.json >/tmp/ls2k-params-invalid-exposure.json
(cd new/user && LS2K_PARAMS_PATH=/tmp/ls2k-params-missing.json LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-d-parameter-failure.log 2>&1)
(cd new/user && LS2K_PARAMS_PATH=/tmp/ls2k-params-malformed.json LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-d-parameter-failure.log 2>&1)
(cd new/user && LS2K_PARAMS_PATH=/tmp/ls2k-params-missing-field.json LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-d-parameter-failure.log 2>&1)
(cd new/user && LS2K_PARAMS_PATH=/tmp/ls2k-params-invalid-critical.json LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-d-parameter-failure.log 2>&1)
(cd new/user && LS2K_PARAMS_PATH=/tmp/ls2k-params-valid-nondefault-exposure.json LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-d-parameter-failure.log 2>&1)
(cd new/user && LS2K_PARAMS_PATH=/tmp/ls2k-params-invalid-exposure.json LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-d-parameter-failure.log 2>&1)
```

预期 marker：

- `params.loaded`
- `params.missing`
- `params.parse`
- `params.critical.apply`
- `params.critical.exp_light`
- `camera.exposure.unsupported`

证据文件：

- `phase-d-parameter-matrix.md`
- `phase-d-params-index.md`
- `phase-d-parameter-failure.log`
- `phase-d-parameter-failure.md`

失败回退方向：

- 参数 schema 或 startup-critical 行为异常：回 Phase A
- 参数版本与回滚流程不清：留在 Phase D

### D-2 启动与切换流程标准化

目标：
把现场操作从“靠经验”升级成稳定的标准步骤。

任务：

1. 写清现场上电到发车的步骤。
2. 定义参数切换步骤。
3. 定义失败后的回滚步骤。
4. 定义急停、复位和重启步骤。
5. 把“看当前参数是否真的生效”写成操作步骤，而不是假定操作者记得上一次改了什么。

验收标准：

1. 同一流程可被不同操作者重复执行。
2. 操作错误不会直接导致不可恢复状态。

触达文件：

- `new/docs/race-finish-series.zh-CN/04-phase-d-track-operations-and-tuning.md`
- 现场操作 checklist 文档

运行命令：

```bash
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-d-ops-drill.log ./run_remote_smoke.sh)
```

预期 marker：

- `startup.complete`
- `shutdown.complete`

证据文件：

- `phase-d-ops-checklist.md`
- `phase-d-ops-drill.md`
- `phase-d-ops-drill.log`

失败回退方向：

- 流程与软件状态不一致：留在 Phase D

### D-3 日志与关键观测项固化

目标：
把现场最小观测面固定下来，使每次测试都能快速定位问题。

任务：

1. 明确比赛和调试时必须观察的 marker：
   - `startup.complete`
   - `params.critical.apply`
   - `imu.detect`
   - `control.start`
   - `control.veto`
   - `encoder.baseline`
   - `encoder.delta.jump`
   - `power.low_voltage.transition`
   - `shutdown.complete`
   - `semantic.image.decision`
   - 如果未来已接入距离传感器，还要观察 `semantic.distance.decision`
2. 对关键延迟、周期、失败原因做集中记录。
3. 把板测日志和实车日志归档到统一位置。
4. 为现场快速判读预留最小观测面，例如当前 preset、关键曝光/阈值、主要赛道状态是否切换。

验收标准：

1. 现场出现问题时，可以从日志快速定位到传感器、感知、策略或执行器。

触达文件：

- `new/docs/race-finish-series.zh-CN/06-implementation-and-verification-contract.md`
- 日志模板与观测模板目录

运行命令：

```bash
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-d-log-example.log ./run_remote_smoke.sh)
```

预期 marker：

- `startup.complete`
- `control.start`
- `control.veto`
- `shutdown.complete`

证据文件：

- `phase-d-marker-template.md`
- `phase-d-log-example.log`

失败回退方向：

- 日志无法支撑定位：留在 Phase D

### D-4 赛道分阶段测试流程

目标：
把赛道测试扩张过程标准化，避免跨阶段乱测。

任务：

1. 先跑直线段
2. 再跑简单弯道
3. 再跑带复杂元素的局部赛段
4. 最后跑完整路线

每一步都要记录：

1. 参数集
2. 图片识别是否参与
3. 距离判定是否参与
4. 失效位置
5. 失败模式
6. 修复动作

验收标准：

1. 测试范围逐步扩大，不跨阶段跳测。
2. 每次失败都能落到明确原因和处理动作。

触达文件：

- 赛道测试记录目录
- 参数版本目录
- `new/docs/race-finish-series.zh-CN/05-phase-e-finish-gate.md`

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-d-track-record.log SMOKE_ENABLE_MOTOR=1 ./run_remote_smoke.sh)
```

预期 marker：

- `control.start`
- `control.veto`
- `semantic.image.decision`
- `semantic.distance.decision`

证据文件：

- `phase-d-track-record.log`
- `phase-d-track-record.md`
- `phase-d-track-failures.md`

失败回退方向：

- 赛道问题归因到硬件/闭环基础：回 Phase A 或 B
- 赛道问题归因到感知/语义：回 Phase C
- 赛道问题归因到流程/参数：留在 Phase D

### D-5 时序与性能稳定性

目标：
确认新增语义输入后，感知与控制链路仍然满足可接受的时序预算。

任务：

1. 量化控制环周期抖动。
2. 量化感知耗时和 freshness 退化情况。
3. 量化图片识别算法加入后的 CPU 负载。
4. 如果已接入距离传感器或其判定逻辑，也要量化其对控制周期和策略延迟的影响。
5. 评估是否需要异步化、缓冲或调度优化。

验收标准：

1. 图片识别接入后，不会明显破坏控制实时性。
2. 如果已接入距离传感器，其加入后也不会明显破坏控制实时性。
3. 感知与控制链路保持稳定。

触达文件：

- `new/code/runtime/control_loop.cpp`
- `new/code/runtime/perception_frontend.cpp`
- 图片识别与距离判定的策略接入代码

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-d-latency-samples.log SMOKE_ENABLE_MOTOR=1 ./run_remote_smoke.sh)
```

预期 marker：

- `control.start`
- `main.frame_limit`

证据文件：

- `phase-d-performance.md`
- `phase-d-latency-samples.md`
- `phase-d-latency-samples.log`

失败回退方向：

- 时序问题源于控制或感知接口设计：回 Phase C
- 时序问题源于现场组织与参数：留在 Phase D

## 3. Phase D 必须产出的证据

1. 参数版本目录
2. 赛道测试记录表
3. 失败案例和修复记录
4. 时序与性能测试记录
5. 现场操作流程文档
6. 关键 marker 观测模板
7. 当前启用的语义输入清单：
   - 基础循迹
   - 图片识别
   - 距离判定

## 4. Phase D 退出条件

只有同时满足以下条件，才允许进入 Phase E：

1. 参数切换和回滚流程可用
2. 赛道测试已从局部段扩展到完整路线
3. 图片识别接口已实装到策略层
4. 如果已采用距离传感器，其接口也已按统一策略层落地
5. 时序和性能在目标负载下可接受

## 5. 本阶段完成后的结构性结论

Phase D 通过后，项目不应再依赖“作者本人临场经验”才能运行，而应具备：

1. 参数版本化
2. 现场启动/切换/回滚规范
3. 可重复的赛道测试步骤
4. 可追溯的日志和结论
