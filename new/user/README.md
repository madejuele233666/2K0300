# `new/user` 调试入口

统一调试脚本是 `debug.sh`。它把原来的构建上传、助手开关、板端进程控制、运行冒烟合并成一个入口。

参数说明与调参攻略见 [`../config/default_params.md`](../config/default_params.md)。

## 常用命令

```bash
./debug.sh build
./debug.sh assistant status
./debug.sh assistant local 8888 8890
./debug.sh assistant on 10.100.170.115 8888 8890
./debug.sh assistant off
./debug.sh tuning --sequence 20,40,60,100 --disabled-mode-checks --invalid-target-speed 170 --media-listen-port 8890
./debug.sh steering --duration-s 20
CONFIRM_POWERED_START=1 ./debug.sh steering drive --drive-s 10
./debug.sh remote start normal
./debug.sh remote start smoke
./debug.sh remote status
./debug.sh remote logs
./debug.sh remote stop
./stop_car.sh controlled
./start_with_upload.sh
CONFIRM_POWERED_START=1 ./start_with_upload.sh drive
./start_with_params_upload.sh
./stop_car.sh
./debug.sh smoke run
./debug.sh smoke local
```

## 命令分组

- `build`：编译 `new/`，并上传二进制、`default_params.json`、`hardware_profile.json` 到板子。
- `assistant`：修改 `../config/default_params.json` 里的 control/media wiring，包含 `assistant_tcp.*`、`steering_media_enabled`、`steering_media_port`、`steering_media_publish_interval_ms`。
- `tuning`：运行主机侧 accepted workflow，监听 assistant control TCP，并可选录制 steering media 的 metadata/raw frame 与对齐摘要。
- `steering`：运行主机侧被动转向调试 workflow，在正常运行态下同时采集 assistant control 连接、steering media 和板端 `control.steering_snapshot`。
- `remote`：远程启动、停止、查看板端 `new` 进程。
- `smoke`：执行板端或本地冒烟验证，并生成验证日志。
- `start_with_upload.sh`：一键停旧进程、上传最新参数和程序，然后以 no-motion 默认启动；显式 `drive` 模式才会请求自动发车。
- `start_with_params_upload.sh`：只上传最新 `default_params.json`，不重新编译，然后以 no-motion 默认启动；显式 `drive` 模式才会请求自动发车。
- `stop_car.sh`：停车入口。默认 `now` 为立即停运行时并关执行器；低速测试的正常收车使用 `controlled`，超时会回退到 `now`。

## 启动安全语义

正常 profile 的启动默认不发车。`./debug.sh remote start normal`、`./debug.sh remote restart normal`、`./start_with_upload.sh` 和 `./start_with_params_upload.sh` 都应先用于 no-motion 检查：运行时可以初始化 camera / IMU / encoder / motor / timer，但不会自动请求 motion start。

如果确实要使用 harness 的自动发车能力，必须同时满足两个条件：

```bash
CONFIRM_POWERED_START=1 LS2K_AUTO_START=1 ./debug.sh remote restart normal
CONFIRM_POWERED_START=1 ./start_with_upload.sh drive
CONFIRM_POWERED_START=1 ./start_with_params_upload.sh drive
```

在参数、标定或场景证据未知时，不要使用 `drive` 模式；先用 steering-media 和 `control.steering_snapshot` 确认 BEV 观测、门控和 0 PWM 状态。

正常低速测试结束时优先使用：

```bash
./stop_car.sh controlled
```

需要应急立即停时使用：

```bash
./stop_car.sh
./stop_car.sh now
```

## 典型流程

```bash
./debug.sh assistant local 8888 8890
./debug.sh build
./debug.sh remote start normal
./debug.sh tuning --sequence 20,40,60,77 --disabled-mode-checks --invalid-target-speed 170 --media-listen-port 8890
./debug.sh remote logs
```

如果只想采集 steering media 板测证据：

```bash
./debug.sh assistant local 8888 8890
./debug.sh build
./debug.sh remote restart normal
./debug.sh tuning --csv ../verification/phase-d-speed-tuning.csv --media-listen-port 8890
```

如果要做真实转向调试而不是 runtime tuning：

```bash
./debug.sh assistant local 8888 8890
./debug.sh build
CONFIRM_POWERED_START=1 ./debug.sh steering drive --drive-s 10
```

这条 `steering` 路径不会发送 `enable_tuning_mode` 或目标速度覆盖命令，因此不会把运行时切进 `turn_suppressed=true` 的动态调参模式。passive capture 输出 evidence bundle 默认落在 `../verification/steering-debug-<timestamp>/`，其中包含：

`steering drive` 是受控发车采集入口：它先启动 assistant/steering-media listener 并确认端口已绑定，再启动 normal runtime 的 `LS2K_AUTO_START=1` 和 `LS2K_AUTO_STOP_AFTER_MS=<drive-s>`；输出默认落在 `../verification/controlled-drive-<drive-s>s-<timestamp>/`。不要用两个独立终端手工拼接 listener 与 `remote restart normal`，否则板端可能在 listener 未就绪时先连接，日志表现为 `assistant.backoff Connection refused` / `steering_media.backoff Connection refused`。

- `assistant_control.csv`
- `assistant_summary.json`
- `board_runtime.log`
- `board_steering_snapshot.jsonl`
- `steering_media/`
- `steering_media_alignment.jsonl`
- `summary.json`

`board_steering_snapshot.jsonl` 与 steering media header 现已共同公开分组转向合同：`perception_health.*`、`element_evidence.cross_exit.*`、`reference.{mode,source}`、`eligibility.*`、`lateral_error.*`、`reference_control.*`、`safety_gate.*`、`degraded.*`、`yaw_control.turn_output_target`、`actuator.{raw_turn_output,applied_turn_output}`。旧的 near/far 误差派生字段和旧扁平 reference/control 字段已经从协议中移除。

如果 steering media 已启用，`tuning` 会额外写出一组 sibling evidence：

- `config_snapshot.json`
- `frame_metadata.jsonl`
- `frames/frame-*.raw`
- `frame_control_alignment.jsonl`
- `summary.json`
- `alignment_summary.json`

accepted control/media wiring 的冻结键集合是：

- `assistant_tcp.host`
- `assistant_tcp.port`
- `steering_media_enabled`
- `steering_media_port`
- `steering_media_publish_interval_ms`

其中 `steering_media_publish_interval_ms` 由板端启动参数读取，host workflow 只读取和记录，不在线改写。

如果只想跑 headless 调试而不保留 plotting fallback 证据，可以显式关闭绘图：

```bash
./debug.sh tuning --no-plot --csv ../verification/phase-d-speed-tuning-headless.csv --media-listen-port 8890
```

这条 `--no-plot` 路径不应作为 checkpoint-4 的 plotting fallback 证据；那部分证据应保留独立 host transcript。

只做安全诊断时：

```bash
./debug.sh build
./debug.sh remote start smoke
```

执行一轮远程冒烟时：

```bash
VERIFY_LOG_PATH=../verification/runtime-smoke.log \
SMOKE_ENABLE_MOTOR=0 \
SMOKE_AUTO_START=1 \
SMOKE_AUTO_START_DELAY_MS=200 \
SMOKE_MAX_FRAMES=80 \
./debug.sh smoke run
```

只跑本地兼容架构冒烟时：

```bash
./debug.sh smoke local
```

## 兼容入口

以下旧脚本仍可用，但现在只是转发到 `debug.sh`：

- `build.sh`
- `switch_assistant_mode.sh`
- `start_remote_runtime.sh`
- `run_remote_smoke.sh`

## 注意

- `assistant` 子命令会写同一个 `default_params.json`，不要并行执行。
- `tuning` 只负责运行时目标速度覆盖、启停和只读证据采集；steering `P/D` 仍然通过 JSON 参数文件修改后重启生效。
- `steering` 适用于真实转向观察，不会驱动 start/stop 或速度覆盖；开始和停止由正常运行态与人工赛道操作决定。
- plotting fallback 的 accepted 证据建议单独保留一份 host-only transcript，不要和 headless `--no-plot` 运行混在一起解释。
- 板端联调建议串行进行，不要同时起多个远程运行实例。
- `smoke` 会占用固定远端临时路径和日志文件，板测时也应串行执行。
- `assistant off` 会同时关闭 steering media；如需 control-only，可显式传 `STEERING_MEDIA_ENABLED=0 ./debug.sh assistant local ...`。
- 默认板 IP 是 `10.100.170.226`，可用环境变量覆盖：

```bash
BOARD_IP=10.100.170.226 ./debug.sh remote start normal
```
