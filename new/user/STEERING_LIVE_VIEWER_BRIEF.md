# Steering Live Viewer 简明说明

本文档是实时图像 viewer 的短版说明。完整细节见
[`STEERING_LIVE_VIEWER_README.md`](STEERING_LIVE_VIEWER_README.md)。

## 目标

`start_steering_live_viewer.sh` 启动一个长时本地监听服务，把板端已经存在的
assistant control TCP 和 steering media TCP 回连流接到本机网页。网页用于只读查看
图像、BEV、FSM、参考候选、门控和控制状态，不负责发车、停车或调参。

## 解耦边界

系统按互不知晓原则拆开：

- 板端只知道 host、control port、media port，不知道网页。
- `host_capture.py` 只负责 TCP listener、协议解析和 evidence，不知道发车策略。
- `steering_media_live_server.py` 只负责 HTTP/WebSocket fan-out，不知道 SSH 和板端运行生命周期。
- 浏览器只读消费 live message，不连接板端 TCP，也不发送车辆控制命令。
- 发车脚本只管理 runtime 生命周期，不直接操作网页。

## 链路

```text
board runtime
  | assistant JSON TCP
  | steering media envelope TCP
  v
host_capture.py
  +--> evidence bundle
  +--> SteeringMediaLiveServer
         +--> http://127.0.0.1:8765/
         +--> /ws
         +--> /latest.bin
         +--> /status.json
```

浏览器不直接解析板端裸 TCP raw。板端发送 accepted envelope；host 侧解析 header 和 payload，再把浏览器可读的消息推给网页。`raw`、`gray4_packed`、`gray2_packed`、`gray1_packed` 都由网页按 header 展开显示。

## 一次启动，长期复用

在 `new/user` 下前台启动：

```bash
./start_steering_live_viewer.sh --no-open-browser
```

推荐后台长时启动：

```bash
tmux new-session -d -s ls2k-live-viewer './start_steering_live_viewer.sh --no-open-browser'
tmux capture-pane -pt ls2k-live-viewer -S -120
```

默认行为：

- viewer 地址：`http://127.0.0.1:8765/`
- 监听时长：`86400` 秒
- 自动选择 control/media 端口
- 写回并上传 `default_params.json`
- `--media-record-mode none`，实时显示不写每帧 raw
- `--display-mode bev`，默认显示 host-only BEV 图像
- 不上传二进制、不重启 runtime、不发车

当前实际 control/media 端口以启动日志和 `status.json` 为准，不要写死旧端口。

## 发车方式

viewer 已经启动后，多次发车只复用这个 listener，不需要重复启动 viewer。

确认 listener：

```bash
curl http://127.0.0.1:8765/status.json
./debug.sh assistant status
```

只启动 runtime 做 no-motion 检查：

```bash
./debug.sh remote start normal
```

复用当前 viewer，定时 10 秒发车：

```bash
CONFIRM_POWERED_START=1 LS2K_AUTO_START=1 LS2K_AUTO_STOP_AFTER_MS=10000 ./debug.sh remote restart normal
```

如果使用：

```bash
CONFIRM_POWERED_START=1 ./debug.sh steering drive --drive-s 10
```

它会自己启动 capture listener。已有长时 viewer 时不要同时用它抢同一组端口。

## 常用参数

```bash
./start_steering_live_viewer.sh --display-mode raw
./start_steering_live_viewer.sh --display-mode bev
./start_steering_live_viewer.sh --high-fps-320x240
./start_steering_live_viewer.sh --media-gray-bits 4
./start_steering_live_viewer.sh --media-record-mode all
./start_steering_live_viewer.sh --no-upload-params
./start_steering_live_viewer.sh --live-port 8766
```

`--high-fps-320x240` 会上传高帧率实时显示参数：

- `steering_media_downsample=1`
- `steering_media_publish_interval_ms=20`
- `steering_media_gray_bits=2`
- `steering_media_publish_latest_frame=0`

这仍保持图像与 `control.steering_snapshot` 强绑定。只有显式使用
`--media-latest-frame` 才切到最新相机帧诊断模式。

## 网页显示

左侧显示图像，右侧显示状态。默认 BEV 显示只消费板端已经发送的图像和真实运行参数：

- `config_snapshot.param_snapshot.BEV_PROJECTOR`
- `config_snapshot.param_snapshot.BEV_GEOMETRY`
- 每帧 gray payload
- 每帧 `steering_snapshot`

网页可以直接绘制 `steering_snapshot.visual_reference.path_candidates.items[*]`
里的候选路径事实，包括 `kind/source/reason/confidence/mode` 和 BEV 采样点
`forward_m/lateral_m`。网页不复刻板端候选生成算法，不把显示结果反写回板端。

右侧重点字段：

- transport、帧号、FPS、payload encoding
- motion phase、CircleV2 状态
- safety gate、reference control、degraded
- reference、visual reference、eligibility
- lateral error、yaw control、actuator output
- camera metadata、threshold、pixel stats

## Evidence

默认 `--media-record-mode none` 适合长时实时查看，只写 summary，不写每帧 raw。

需要短时间取证时使用：

```bash
./start_steering_live_viewer.sh --media-record-mode all --duration-s 60
```

典型输出在 `new/verification/host-capture-<timestamp>/`，Windows backend 会先写到 Windows 本地临时目录，再复制回 WSL，避免实时写 WSL 路径拖慢接收。

## 常见问题

网页能打开但没有帧：

```bash
curl http://127.0.0.1:8765/status.json
./debug.sh remote status
./debug.sh assistant status
```

- `messages_published=0`：viewer 正常，但还没收到板端 media。
- runtime stopped：启动 `./debug.sh remote start normal`。
- params 刚上传但 runtime 未重启：重启 runtime 让新 host/port 生效。

端口或绑定错误：

- `WinError 10013` / `10048`：端口被占用或被旧 listener/WSL 预绑定污染。
- `WinError 10049`：绑定了当前 Windows 不存在的旧热点地址。

处理：

```bash
tmux kill-session -t ls2k-live-viewer
./start_steering_live_viewer.sh --no-open-browser
```

仍失败时换 viewer 端口：

```bash
./start_steering_live_viewer.sh --live-port 8766 --no-open-browser
```

## 停止

后台会话停止：

```bash
tmux kill-session -t ls2k-live-viewer
```

停止车辆 runtime 或收车仍使用既有安全入口：

```bash
./stop_car.sh controlled
./stop_car.sh now
```
