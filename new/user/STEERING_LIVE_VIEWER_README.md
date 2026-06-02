# Steering Media Live Viewer 系统 README

本文档说明板端 steering/media 图像发送到本地网页实时显示的系统。系统目标是把板端已存在的 assistant control 与 steering media TCP 回连链路接入一个只读本地网页 viewer，便于调车时实时查看图像、FSM、门控、参考线和控制状态。

## 系统边界

本系统只负责主机侧监听、证据采集和网页显示：

- 不直接控制车辆。
- 不向浏览器暴露发车、停车或调参命令。
- 不改变板端发送协议，板端仍按既有 accepted envelope 发送 steering media。
- 不要求每次发车都重启网页服务，长时启动一次即可覆盖多轮发车。

涉及发车的动作仍走 `debug.sh remote ...` 或 `debug.sh steering drive ...` 的安全入口，并由 `CONFIRM_POWERED_START=1` 显式确认。

## 架构

```text
board runtime
  |
  | assistant JSON TCP
  v
host_capture.py AssistantJsonListener
  |
  | steering media TCP, raw/image envelope
  v
host_capture.py SteeringMediaListener
  |
  +--> evidence bundle on host
  |
  +--> SteeringMediaLiveServer
         |
         +--> http://127.0.0.1:8765/
         +--> /ws
         +--> /latest.bin
         +--> /status.json
```

主机侧 `host_capture.py` 是唯一接收板端 control/media 回连的组件。网页 viewer 只订阅 `host_capture.py` 已接收到的帧，不知道板端、不知道 SSH、不知道发车流程。

## 关键文件

- `start_steering_live_viewer.sh`：一键启动长时 host capture 和本地网页 viewer。
- `host_capture.py`：主机侧 canonical capture，负责 assistant JSON、steering media、evidence 和 live publish。
- `steering_media_live_server.py`：只读 HTTP/WebSocket viewer server。
- `debug.sh`：统一调试入口，负责 assistant 参数、板端启动停止、host capture、受控发车。
- `../config/default_params.json`：板端读取的 assistant/media 连接参数。
- `../verification/`：WSL 侧 evidence 输出目录。

## 一键启动

在 `new/user` 目录执行：

```bash
./start_steering_live_viewer.sh
```

默认行为：

- 本地网页地址：`http://127.0.0.1:8765/`
- 采集时长：`86400` 秒
- 自动选择 control/media 端口并写回 `default_params.json`
- 默认上传参数到板端
- 默认 `--media-record-mode none`，实时显示不写每帧 raw
- 默认 `--display-mode bev`，网页显示 BEV 变换后的图像
- 不上传程序
- 不重启板端 runtime
- 不发车

常用覆盖：

```bash
./start_steering_live_viewer.sh --duration-s 120
./start_steering_live_viewer.sh --no-open-browser
./start_steering_live_viewer.sh --display-mode raw
./start_steering_live_viewer.sh --no-upload-params
./start_steering_live_viewer.sh --high-fps-320x240
./start_steering_live_viewer.sh --media-record-mode all
./start_steering_live_viewer.sh --live-host 0.0.0.0 --live-port 8765
```

其他 `host_capture.py` 参数放在 `--` 后透传：

```bash
./start_steering_live_viewer.sh -- --output-dir ../verification/live-test
```

`--media-record-mode` 控制 host 侧证据写盘量：

- `none`：只收 TCP、推 live hub、写 summary；长时实时查看默认用这个模式。
- `metadata`：不写 raw，只写 `frame_metadata.jsonl`。
- `all`：写 `frames/frame-*.raw` 和 `frame_metadata.jsonl`，用于需要归档图像证据的短采集。

`--high-fps-320x240` 只覆盖板端启动参数里的图像发送配置：`steering_media_downsample=1`、`steering_media_publish_interval_ms=20`、`steering_media_gray_bits=2`、`steering_media_publish_latest_frame=0`。它保持图像与 `control.steering_snapshot` 强绑定，不改变网页协议，也不把浏览器逻辑引入板端。需要近距离更清晰画面时追加 `--media-gray-bits 4`；需要诊断最新相机帧而不要求快照对齐时，显式追加 `--media-latest-frame`；需要原始 gray8 时追加 `--media-gray-bits 8`。

长时后台启动建议使用 tmux：

```bash
tmux new-session -d -s ls2k-live-viewer './start_steering_live_viewer.sh --no-open-browser'
tmux capture-pane -pt ls2k-live-viewer -S -120
tmux kill-session -t ls2k-live-viewer
```

## 发车方式

如果 viewer 已经长时启动，不需要再次启动 `start_steering_live_viewer.sh`。多次发车复用同一个 listener 即可。

先确认 listener READY：

```bash
curl http://127.0.0.1:8765/status.json
./debug.sh assistant status
```

如果只想启动板端 runtime 做 no-motion 检查：

```bash
./debug.sh remote start normal
```

如果要定时 10 秒发车，并复用当前 viewer：

```bash
CONFIRM_POWERED_START=1 LS2K_AUTO_START=1 LS2K_AUTO_STOP_AFTER_MS=10000 ./debug.sh remote restart normal
```

如果需要一次性完成受控发车和 evidence 采集，而不是复用长时 viewer：

```bash
CONFIRM_POWERED_START=1 ./debug.sh steering drive --drive-s 10
```

注意：`debug.sh steering drive` 会自己启动 capture listener，因此不要和已有长时 viewer 同时抢同一组 control/media 端口。

## 端口和 Windows 热点

Windows 热点常用 host 地址是 `192.168.137.1`。板端连接这个地址上的两个 TCP 端口：

- assistant control port
- steering media port

`start_steering_live_viewer.sh` 默认会从候选端口里选择一组可用端口，写回 `default_params.json` 并上传到板端。当前实际端口以启动日志和 `./debug.sh assistant status` 为准。

脚本区分“写给板端连接的地址”和“host capture 本地绑定地址”：默认会检测当前 Windows 到板端路由的源地址作为 `advertise_host`，同时让 Windows host capture 绑定 `0.0.0.0`。如果现场网络不是 Windows 热点，或热点地址从 `192.168.137.1` 变成 WLAN 地址，这可以避免 Windows 因绑定旧地址报 `WinError 10049`。必要时可显式覆盖：

```bash
./start_steering_live_viewer.sh --advertise-host <BOARD_CAN_REACH_HOST_IP> --capture-bind-host 0.0.0.0
```

热点地址 `192.168.137.*` 下必须直接使用 Windows host-capture backend。WSL 先试绑端口会污染 Windows 后续 bind，表现为：

- `WinError 10013`
- `WinError 10048`
- 网页开了但 control/media listener 没有 READY

当前 `debug.sh` 对热点地址默认绕开 WSL 预绑定，直接使用 Windows Python 启动 capture。

## 浏览器能否解析 raw / gray4

板端发送的是 accepted envelope，图像负载可以是 raw gray8 或 `gray4_packed` / `gray2_packed` / `gray1_packed`。浏览器不直接解析裸 TCP raw。解析链路是：

1. `host_capture.py` 接收 steering media envelope。
2. `SteeringMediaListener` 解析 header 和 payload。
3. `SteeringMediaLiveServer` 把 header/payload 封装成浏览器可读的 live message。
4. 网页 JS 根据 header 中的宽高、像素格式、帧信息渲染到 canvas；packed gray 会在浏览器侧展开为 8-bit 灰度显示。

因此浏览器不需要直接理解板端 TCP 协议，也不直接连接板端。

## 网页显示内容

网页左侧显示图像帧，右侧显示运行状态。状态来自 steering media header 和 assistant snapshot，包括：

- transport 状态、帧号、尺寸、source、display FPS
- motion FSM、circle FSM、circle reason
- safety gate、reference control、degraded 状态
- reference mode/source、visual reference、eligibility
- lateral error、turn output、actuator output
- camera source、V4L2 seq、timing、buffer
- pixel stats、threshold

左侧图像默认显示 host-only BEV 变换图像；旧的元素框、中心线和横向误差 overlay 默认关闭。BEV 显示只消费板端已经发送的图像和真实运行参数，不向板端回写，也不重新定义控制决策：

- `config_snapshot.param_snapshot.BEV_PROJECTOR` 提供真实运行参数里的四点标定。
- 网页用与 `vision/bev/bev_projector.cpp` 相同的 DLT 8 元线性方程和 3x3 单应矩阵，把每个 BEV 像素 `(lateral_m, forward_m)` 反投影到源图像，再从当前收到的 gray payload 做双线性采样。
- BEV 显示不再把 `BEV_GEOMETRY.SEARCH_LATERAL_LIMIT_M` 当作左右显示边界。网页会沿 `FORWARD_SAMPLE_0..FORWARD_SAMPLE_23` 抽样，计算每个前向截面能反投影到源图内的 lateral 范围，再用这些范围合成完整相机可见梯形，避免远端左右被控制搜索宽度裁掉。
- BEV 显示宽度以 `DEBUG_GRID_WIDTH` 为基础并加宽到可见梯形范围，前向范围来自 `BEV_GEOMETRY.FORWARD_SAMPLE_*`；如果找不到有效可见范围，才退回 `SEARCH_LATERAL_LIMIT_M`。
- 如果 `config_snapshot` 尚未到达或 projector 参数无效，网页自动退回 raw 显示并在 Display 字段标出 fallback。

启动参数 `--display-mode bev|raw` 控制默认显示图像；等价环境变量是 `LS2K_LIVE_DISPLAY_MODE`。`steering_snapshot.visual_reference.path_candidates` 会随每帧 media header 发送板端已经构建的候选路径事实：`kind/source/reason/confidence/mode` 和各个 `sampled_path` 有效点的 `(forward_m,lateral_m,confidence,source)`。网页直接把这些只读事实点绘制到 canvas 上，不再在侧栏显示候选摘要；绘制不得在网页里复刻一份无人维护的感知算法。CircleV2 的独立几何中间点如果没有出现在发送端合同中，网页不推断、不绘制。

如果右侧 `messages_published` 为 0，说明主机 viewer 正常，但还没有收到板端 media 帧。

## Evidence 输出

host capture 会写一份证据 bundle。默认路径在：

```text
new/verification/host-capture-<timestamp>/
```

Windows backend 会先写到 Windows 本地临时目录，再复制回 WSL，避免实时写 `\\wsl.localhost\...` 拖慢媒体接收。

典型内容：

- `assistant_control.csv`
- `assistant_control.jsonl`
- `assistant_summary.json`
- `steering-media/frames/frame-*.raw`
- `steering-media/frame_metadata.jsonl`
- `summary.json`

## 解耦原则

本系统按“互不知晓”设计：

- 板端只知道 host、control port、media port，不知道网页。
- `host_capture.py` 只知道 TCP listener 和 evidence，不知道发车策略。
- `steering_media_live_server.py` 只知道 frame hub，不知道板端、不知道 SSH。
- 浏览器只知道 HTTP/WebSocket，不知道板端 TCP 协议。
- 发车脚本只负责 runtime 生命周期，不直接操作网页。

这种拆分保证长时 viewer 可以独立运行，发车可以多次复用同一个显示服务，也避免浏览器成为车辆控制面。

## 常见问题

### 网页能打开但一帧都没有

检查：

```bash
curl http://127.0.0.1:8765/status.json
./debug.sh remote status
./debug.sh assistant status
```

如果 `runtime_status=stopped`，板端没有运行，自然不会有帧。启动 no-motion runtime：

```bash
./debug.sh remote start normal
```

如果 runtime 在跑但仍无帧，确认板端参数里的 host/port 与 viewer 日志一致，并重启 runtime 让新参数生效。

### `WinError 10013`、`WinError 10048` 或 `WinError 10049`

`10013`/`10048` 通常是 Windows 端口绑定被预占用、被系统拒绝或被 WSL 预绑定污染。`10049` 通常是配置里的 assistant host 已经不是当前 Windows 网卡地址，例如脚本还在绑定 `192.168.137.1`，但热点已关闭或当前走 WLAN。处理顺序：

```bash
tmux kill-session -t ls2k-live-viewer
./start_steering_live_viewer.sh --no-open-browser
```

如果仍失败，换网页端口：

```bash
./start_steering_live_viewer.sh --live-port 8766 --no-open-browser
```

不要同时启动多个 host capture 抢同一组 control/media 端口。

### WebSocket 不可用

网页有 `/latest.bin` 轮询兜底；WebSocket 正常时前端不会持续轮询。优先看 `/status.json`：

```bash
curl http://127.0.0.1:8765/status.json
```

如果 `client_errors` 增长但 `messages_published` 也增长，说明数据链路在工作，浏览器可能走了兜底路径。

### 帧率低

先区分板端发送慢还是主机接收慢：

- 看 `steering_media_publish_interval_ms`
- 看 `summary.json` 里的 `effective_fps`
- 看网页右侧 display FPS

当前常用参数是：

```text
steering_media_publish_interval_ms=20
steering_media_downsample=1
steering_media_gray_bits=2
steering_media_publish_latest_frame=0
```

长时实时查看建议保持 `--media-record-mode none`。如果 `effective_fps` 明显低于板端 `steering_media.summary image_sent`，优先检查 host 是否仍在全量写 raw；短采集需要证据时再切到 `--media-record-mode all`。

热点吞吐不足时优先保持 320x240 但使用 `--media-gray-bits 2` 或 `--media-gray-bits 1`；如果仍不稳，再增大 downsample，例如 `4`，此时发送 80x60。

### 距离和 WiFi 链路

5-6m 半径下先看物理链路，不要只调图像参数。板端可用下面命令看热点信号和 RTT：

```bash
/mnt/c/Windows/System32/OpenSSH/ssh.exe root@192.168.137.50 'cat /proc/net/wireless; ping -c 20 -W 2 192.168.137.1'
```

经验判断：

- `/proc/net/wireless` 里 `level` 低于约 `-70 dBm` 时已经偏弱，320x240 高帧率会明显受影响。
- Windows 移动热点 5GHz 近距离吞吐更好，适合 `gray2_packed` 高帧率；2.4GHz 可能改善信号强度，但在拥挤环境中延迟和排队可能更差。
- Windows 电源计划里的 Wireless Adapter Power Saving Mode 应设为 Maximum Performance。不要盲目修改网卡高级属性；`LowPowerEnable`、`Miracast prefer band` 这类项会重置热点，且不同驱动版本效果不稳定。
- 若 5-6m 仍低帧率，优先改善摆位、天线朝向、遮挡和干扰；软件侧再降为 `--media-gray-bits 1` 或增大 `--media-downsample`。

## 推荐日常流程

启动一次长时 viewer：

```bash
./start_steering_live_viewer.sh --no-open-browser --high-fps-320x240
```

打开网页：

```text
http://127.0.0.1:8765/
```

做 no-motion 检查：

```bash
./debug.sh remote start normal
```

确认 powered start 后定时发车 10 秒：

```bash
CONFIRM_POWERED_START=1 LS2K_AUTO_START=1 LS2K_AUTO_STOP_AFTER_MS=10000 ./debug.sh remote restart normal
```

收车或应急停止：

```bash
./stop_car.sh controlled
./stop_car.sh now
```
