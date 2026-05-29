# `new/user` 调试入口

统一调试脚本是 `debug.sh`。它把原来的构建上传、助手开关、板端进程控制、运行冒烟合并成一个入口。

参数说明与调参攻略见 [`../config/default_params.md`](../config/default_params.md)。

实时图像显示系统短版说明见 [`STEERING_LIVE_VIEWER_BRIEF.md`](STEERING_LIVE_VIEWER_BRIEF.md)，完整说明见 [`STEERING_LIVE_VIEWER_README.md`](STEERING_LIVE_VIEWER_README.md)。

## 常用命令

```bash
./debug.sh build
./debug.sh assistant status
./debug.sh assistant on 192.168.137.1 39011 39012
./debug.sh assistant off
./debug.sh tuning --sequence 20,40,60,100 --disabled-mode-checks --invalid-target-speed 170 --listen-port 39011 --media-listen-port 39012
./debug.sh steering --duration-s 20
./start_steering_live_viewer.sh
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
- `assistant`：修改 `../config/default_params.json` 里的 control/media wiring，包含 `assistant_tcp.*`、`steering_media_enabled`、`steering_media_port`、`steering_media_publish_interval_ms`、`steering_media_downsample`。
- `tuning`：运行主机侧 accepted workflow，监听 assistant control TCP，并可选录制 steering media 的 metadata/raw frame 与对齐摘要。
- `steering`：运行主机侧转向调试 workflow。默认 `./debug.sh steering` 等价于 `./debug.sh steering host-capture`，只负责 assistant control 和 steering media 两条板端回连流；旧的板端 SSH tail workflow 已归档到 `new/user/archive/`，需要时用 `./debug.sh steering legacy`。
- `remote`：远程启动、停止、查看板端 `new` 进程。
- `smoke`：执行板端或本地冒烟验证，并生成验证日志。
- `start_with_upload.sh`：一键停旧进程、上传最新参数和程序，然后以 no-motion 默认启动；显式 `drive` 模式才会请求自动发车。
- `start_with_params_upload.sh`：只上传最新 `default_params.json`，不重新编译，然后以 no-motion 默认启动；显式 `drive` 模式才会请求自动发车。
- `start_steering_live_viewer.sh`：一键启动 host 侧 steering/media capture 和只读本地网页 viewer；默认会同步并上传所选 control/media 端口参数，但不上传程序、不重启板端、不发车。
- `stop_car.sh`：停车入口。默认 `now` 为立即停运行时并关执行器；低速测试的正常收车使用 `controlled`，超时会回退到 `now`。

## 启动安全语义

`debug.sh` 会在 `BOARD_IP` 未显式设置时自动从 Windows 热点邻居里寻找可 SSH 的板端；热点网段 `192.168.137.x` 默认使用 Windows OpenSSH/SCP，绕开 WSL 原生路由走错网卡的问题。需要强制指定时仍可用 `BOARD_IP=<ip>`，需要强制后端时可用 `LS2K_REMOTE_BACKEND=auto|native|windows`。

正常 profile 的启动默认不发车。`./debug.sh remote start normal`、`./debug.sh remote restart normal`、`./start_with_upload.sh` 和 `./start_with_params_upload.sh` 都应先用于 no-motion 检查：运行时可以初始化 camera / IMU / encoder / motor / timer，但不会自动请求 motion start。

如果确实要使用 harness 的自动发车能力，必须同时满足两个条件：

```bash
CONFIRM_POWERED_START=1 LS2K_AUTO_START=1 ./debug.sh remote restart normal
CONFIRM_POWERED_START=1 ./start_with_upload.sh drive
CONFIRM_POWERED_START=1 ./start_with_params_upload.sh drive
```

在参数、标定或场景证据未知时，不要使用 `drive` 模式；先用 steering-media 和 `control.steering_snapshot` 确认 BEV 观测、门控和 0 PWM 状态。

`./start_with_upload.sh drive` 和 `./start_with_params_upload.sh drive` 是普通一键发车入口，不继承当前终端残留的 `LS2K_AUTO_STOP_AFTER_MS`，因此不会自动定时停车；需要定时停车和证据采集时使用 `./debug.sh steering drive --drive-s <seconds>`。

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
./debug.sh assistant on 192.168.137.1 39011 39012
./debug.sh build
./debug.sh remote start normal
./debug.sh tuning --sequence 20,40,60,77 --disabled-mode-checks --invalid-target-speed 170 --listen-port 39011 --media-listen-port 39012
./debug.sh remote logs
```

如果只想采集 steering media 板测证据：

```bash
./debug.sh assistant on 192.168.137.1 39011 39012
./debug.sh build
./debug.sh remote restart normal
./debug.sh tuning --csv ../verification/phase-d-speed-tuning.csv --listen-port 39011 --media-listen-port 39012
```

如果要做真实转向调试而不是 runtime tuning：

```bash
./debug.sh assistant on 192.168.137.1 39011 39012
./debug.sh build
CONFIRM_POWERED_START=1 ./debug.sh steering drive --drive-s 10
```

这条 `steering` 路径不会发送 `enable_tuning_mode` 或目标速度覆盖命令，因此不会把运行时切进 `turn_suppressed=true` 的动态调参模式。`steering drive` 默认使用 `host-capture` 后端：WSL 能直接绑定目标 host 时在 WSL 内监听，否则用 Windows Python 在 Windows 热点地址上直接监听。Windows 后端会先写入 Windows 本地临时目录，再把 evidence 复制回 WSL 输出目录，避免实时写 `\\wsl.localhost\...` 拖慢媒体接收。若需要归档的板端 SSH tail evidence，可显式设置 `LS2K_STEERING_CAPTURE_BACKEND=legacy` 或运行 `./debug.sh steering legacy ...`。

`steering drive` 是受控发车采集入口：它先启动 assistant/steering-media listener 并确认端口已绑定，再启动 normal runtime 的 `LS2K_AUTO_START=1` 和 `LS2K_AUTO_STOP_AFTER_MS=<drive-s>`；输出默认落在 `../verification/controlled-drive-<drive-s>s-<timestamp>/`。不要用两个独立终端手工拼接 listener 与 `remote restart normal`，否则板端可能在 listener 未就绪时先连接，日志表现为 `assistant.backoff Connection refused` / `steering_media.backoff Connection refused`。

需要实时看 steering media 图像时，可在 host-capture 路径上打开只读本地网页：

```bash
./start_steering_live_viewer.sh
./debug.sh steering host-capture --live-web --live-host 127.0.0.1 --live-port 8765 --duration-s 20
```

一键脚本默认监听 `127.0.0.1:8765`、长时采集 86400 秒并尝试打开浏览器，适合启动一次后覆盖多轮发车；在 Windows host-capture backend 下会自动检测写给板端连接的 `advertise_host`，本地 listener 默认绑定 `0.0.0.0`，避免热点关闭或网段变化后继续绑定旧 `192.168.137.1` 导致 `WinError 10049`。脚本会自动选择 control/media 端口、写回 `default_params.json` 并上传参数；可用 `--no-auto-ports` 或 `--no-upload-params` 禁用，也可用 `--advertise-host` / `--capture-bind-host` 显式覆盖连接地址和本地绑定地址。长时 viewer 默认 `--media-record-mode none`，只保留 live/summary，不把每帧 320x240 raw 写盘；默认 `--display-mode bev`，网页显示按真实 `BEV_PROJECTOR`/`BEV_GEOMETRY` 变换后的 BEV 图像，需要原始相机图像时使用 `--display-mode raw`。需要取证时使用 `--media-record-mode all`。需要高帧率 320x240 时使用 `--high-fps-320x240`，等价于 `steering_media_downsample=1`、`steering_media_publish_interval_ms=20`、`steering_media_gray_bits=2` 且保持 snapshot-aligned；近距离需要更清晰实时画面时追加 `--media-gray-bits 4`，需要原始 gray8 时显式加 `--media-gray-bits 8`。`--duration-s`、`--live-host`、`--live-port` 可覆盖默认值，其他 `host_capture.py` 参数放在 `--` 后透传。

`--live-web` 只在主机侧增加 HTTP/WebSocket viewer；板端仍然只连接既有 steering media TCP 端口并发送 accepted envelope。浏览器端输入不会变成 assistant 命令。实时显示与 evidence 写盘由 `--media-record-mode` 解耦：`all` 写 raw+metadata，`metadata` 只写 frame metadata，`none` 只更新 live hub 和 summary。

网页 BEV 显示只读消费真实 `config_snapshot` 里的 `BEV_PROJECTOR`/`BEV_GEOMETRY` 和每帧 gray payload，按 `STEERING_LIVE_VIEWER_README.md` 中记录的单应矩阵算法反投影采样；每帧 media header 同步携带 `steering_snapshot.visual_reference.path_candidates` 里的板端候选路径事实，网页直接把这些事实点绘制到 canvas 上，不在侧栏显示候选摘要，也不复刻板端候选路径算法。CircleV2 的独立几何中间点如果没有出现在发送端合同中，网页不推断、不绘制。

新的 `host-capture` evidence bundle 包含：

- `assistant_control.csv`
- `assistant_control.jsonl`
- `assistant_summary.json`
- `steering-media/`
- `summary.json`

旧 `LS2K_STEERING_CAPTURE_BACKEND=legacy` evidence bundle 额外包含：

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
- `steering_media_downsample`

其中 `steering_media_publish_interval_ms`、`steering_media_downsample`、`steering_media_gray_bits` 和 `steering_media_publish_latest_frame` 由板端启动参数读取，普通 host capture 只读取和记录；`start_steering_live_viewer.sh --high-fps-320x240` 会在上传参数前显式覆盖为 320x240、20ms、gray2、snapshot-aligned。默认图像显示与 `control.steering_snapshot` 强绑定；只有显式使用 `--media-latest-frame` 才会改为最新相机帧诊断模式。

如果只想跑 headless 调试而不保留 plotting fallback 证据，可以显式关闭绘图：

```bash
./debug.sh tuning --no-plot --csv ../verification/phase-d-speed-tuning-headless.csv --listen-port 39011 --media-listen-port 39012
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
- 默认会自动发现 Windows 热点板端 IP；发现失败时回退到 `10.100.170.226`。可用环境变量覆盖：

```bash
BOARD_IP=192.168.137.198 ./debug.sh remote start normal
LS2K_REMOTE_BACKEND=windows BOARD_IP=192.168.137.198 ./debug.sh remote status
```
