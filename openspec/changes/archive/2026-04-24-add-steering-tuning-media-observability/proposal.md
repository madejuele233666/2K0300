## Why

当前 accepted `new/user/debug.sh tuning` workflow 已经能通过 assistant TCP JSON `command/ack/state/telemetry` 做速度调参，但它仍然主要暴露轮级结果，无法把赛道灰度图、感知阈值状态、相机外环和陀螺内环关键量按时间对齐地呈现给上位机。转向调参和后续自动搜索需要一条 project-owned、只读、控制/媒体分离的观测链路，而不是继续把图片塞进现有控制连接或依赖 vendor-only 图传界面。

## What Changes

- 新增一条 project-owned `steering-tuning-media-observability` capability，定义独立于 assistant JSON control contract 的媒体 sidecar、转向观测快照、结构化 diagnostics/export 面和 host evidence bundle。
- 修改 `assistant-telemetry-sidecar`，冻结现有 assistant TCP JSON `command/ack/state/telemetry` 为唯一 accepted control plane，并明确图片与转向元数据不得复用这条连接的 framing。
- 修改 `runtime-speed-tuning-surface`，要求 accepted host workflow 继续使用 `new/user/debug.sh tuning ...`，但可选扩展为同时监听媒体端口、记录图像/元数据并与控制 CSV 对齐。
- 为 steering tuning 增加只读参数白名单快照与图像帧协议：板端主动连接上位机，连接就绪后发送一次 `config_snapshot`，运行时持续发送 `image_frame`。
- 明确本 change 不包含在线 steering `P/D` 写回、在线覆盖 `RuntimeParameters` 或新的 ad hoc socket workflow；首版重点是稳定 observability 和可验收证据面。

## Capabilities

### New Capabilities
- `steering-tuning-media-observability`: 定义 steering tuning 所需的 project-owned 图像/元数据 sidecar、转向链观测字段、非 assistant 证据面和 host recording/alignment contract。

### Modified Capabilities
- `assistant-telemetry-sidecar`: 保持 accepted assistant TCP JSON control session 冻结不变，并把 steering media/image observability 从该 control session 中分离出来。
- `runtime-speed-tuning-surface`: 扩展 accepted host tuning workflow，使 `debug.sh tuning` 可选监听媒体 sidecar、记录 steering evidence，并明确本次只读 observability 不开放 steering parameter mutation。

## Risk Tier

- `STRICT`: 该 change 同时触达 `platform/` TCP bridge、`runtime/` 调试快照与前台 sidecar、`legacy/` steering-chain observability、`config/` host/port 参数面，以及 `new/user/debug.sh` / `tune_speed.py` 的 accepted host workflow。错误设计会直接破坏现有 JSON control contract、使媒体链路拖慢 ACK/control 路径，或让 steering tuning 重新依赖 vendor-only 图传面，因此需要显式 design/spec/tasks 和 docs-first/source-first 验证。

## Impact

- Affected code: `new/code/platform/assistant_link.*`, `new/code/platform/assistant_protocol.*`, `new/code/platform/true_ls2k0300/assistant_bridge.*`, 新增的 project-owned media protocol/link modules, `new/code/runtime/assistant_service.*`, `new/code/runtime/control_debug_snapshot.hpp`, `new/code/runtime/control_debug_reporter.*`, `new/code/runtime/control_loop.*`, `new/code/runtime/runtime_state.hpp`, `new/code/legacy/pid_control.*`, `new/user/debug.sh`, `new/user/tune_speed.py`, 以及新的 verification/runbook 资料。
- Affected contracts: accepted assistant TCP control framing, runtime steering observability snapshot, non-assistant diagnostics/export surface, project-owned media envelope, host tuning evidence bundle。
- Affected dependencies and systems: board-to-host dual TCP connections, raw `160x128` grayscale image capture/export, startup-loaded param whitelist snapshot, board/host artifact alignment。
- Participating skills: `openspec-propose`, `openspec-align`, `openspec-artifact-verify`, 后续实现阶段使用 `openspec-apply-change`、`openspec-verify-change` 和必要时的 `openspec-repair-change`。
