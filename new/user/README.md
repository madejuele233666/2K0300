# `new/user` 调试入口

统一调试脚本是 `debug.sh`。它把原来的构建上传、助手开关、板端进程控制、运行冒烟合并成一个入口。

## 常用命令

```bash
./debug.sh build
./debug.sh assistant status
./debug.sh assistant local
./debug.sh assistant on 192.168.2.32 8888
./debug.sh assistant off
./debug.sh tuning --sequence 20,40,60,77 --disabled-mode-checks --invalid-target-speed 90
./debug.sh remote start normal
./debug.sh remote start smoke
./debug.sh remote status
./debug.sh remote logs
./debug.sh remote stop
./debug.sh smoke run
./debug.sh smoke local
```

## 命令分组

- `build`：编译 `new/`，并上传二进制、`default_params.json`、`hardware_profile.json` 到板子。
- `assistant`：只修改 `../config/default_params.json` 里的助手相关字段。
- `tuning`：运行主机侧速度调参脚本，监听 assistant TCP 连接，发送命令并保存 CSV。
- `remote`：远程启动、停止、查看板端 `new` 进程。
- `smoke`：执行板端或本地冒烟验证，并生成验证日志。

## 典型流程

```bash
./debug.sh assistant local
./debug.sh build
./debug.sh remote start normal
./debug.sh tuning --sequence 20,40,60,77 --disabled-mode-checks --invalid-target-speed 90
./debug.sh remote logs
```

如果只想做速度调参与证据采集：

```bash
./debug.sh assistant local 8888
./debug.sh build
./debug.sh remote restart normal
./debug.sh tuning --no-plot --csv ../verification/phase-d-speed-tuning.csv
```

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
- `tuning` 只负责运行时目标速度覆盖、启停和证据采集；PID 参数仍然通过 JSON 参数文件修改后重启生效。
- 板端联调建议串行进行，不要同时起多个远程运行实例。
- `smoke` 会占用固定远端临时路径和日志文件，板测时也应串行执行。
- 默认板 IP 是 `10.100.170.226`，可用环境变量覆盖：

```bash
BOARD_IP=10.100.170.226 ./debug.sh remote start normal
```
