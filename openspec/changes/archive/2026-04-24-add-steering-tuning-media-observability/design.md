## Context

`new/` 当前已经有一条清晰且已落地的 Phase D accepted surface：

- `new/code/platform/assistant_protocol.*` 冻结了 assistant TCP JSON `command/ack/state/telemetry` contract。
- `new/code/runtime/assistant_service.*` 在前台循环里同时处理 control feedback、wave publish 和 assistant-compatible image publish。
- `new/code/runtime/control_debug_snapshot.hpp` 与 `new/code/runtime/control_debug_reporter.*` 已经能导出 wheel-level tuning state、`raw_turn_output` 和 `applied_turn_output`。
- `new/user/debug.sh tuning ...` 与 `new/user/tune_speed.py` 已经是 accepted host workflow，但当前只消费 control JSON，会生成 CSV 和最小 plotting，不会记录图像与更细的 steering-chain metadata。

当前缺口也很明确：

1. accepted control session 不能承载原始图像或更重的 steering metadata，否则会污染现有 JSON contract 和 ACK path。
2. `ControlDebugSnapshot` 只暴露 wheel-level + high-level tuning flags；`lateral_error`、`threshold_veto`、`resolved_fuzzy_p`、camera/gyro P/D terms、`w_target`、`gyro_error` 等 steering-chain 量仍未形成 project-owned export surface。
3. `LegacyPidControl` 目前只返回 `w_target` 和最终 gyro turn 输出，内部 camera/gyro term 没有结构化观测面。
4. assistant-compatible image publish 仍依赖 vendor helper 语义，不是 project-owned host evidence contract。
5. host workflow 还没有“控制 CSV + 图像/元数据 + board log”同一轮次最小证据包。

### Reference Inventory

Primary implementation references:

- `new/code/platform/assistant_protocol.hpp`
- `new/code/platform/assistant_protocol.cpp`
- `new/code/platform/assistant_link.hpp`
- `new/code/platform/assistant_link.cpp`
- `new/code/platform/true_ls2k0300/assistant_bridge.hpp`
- `new/code/platform/true_ls2k0300/assistant_bridge.cpp`
- `new/code/runtime/assistant_service.hpp`
- `new/code/runtime/assistant_service.cpp`
- `new/code/runtime/control_debug_snapshot.hpp`
- `new/code/runtime/control_debug_reporter.hpp`
- `new/code/runtime/control_debug_reporter.cpp`
- `new/code/runtime/control_loop.hpp`
- `new/code/runtime/control_loop.cpp`
- `new/code/runtime/runtime_state.hpp`
- `new/code/runtime/tuning_state.hpp`
- `new/code/runtime/tuning_state.cpp`
- `new/code/legacy/pid_control.hpp`
- `new/code/legacy/pid_control.cpp`
- `new/code/legacy/camera_logic.cpp`
- `new/code/port/control_types.hpp`
- `new/user/debug.sh`
- `new/user/tune_speed.py`
- `new/docs/race-finish-series.zh-CN/04-phase-d-track-operations-and-tuning.md`

Alignment references for preserved steering semantics:

- `old/code/PID.c`
- `old/code/FUZZY_PID_UCAS.c`
- `old/code/camera.c`
- `old/user/cpu0_main.c`

### Alignment Mapping

| Reference Module | Target Module | Action | Notes |
|---|---|---|---|
| `new/code/platform/assistant_protocol.*` | unchanged control protocol + new `platform/steering_media_protocol.*` | Adapt | 保持 JSON control contract 冻结不变；新增独立 media envelope，不把 image/raw metadata 塞回现有 JSON framing。 |
| `new/code/platform/assistant_link.*` | unchanged control link + new `platform/steering_media_link.*` | Adapt | 继续保留 control transport ownership；新增只读 media transport，连接方向仍为板端主动连上位机。 |
| `new/code/platform/true_ls2k0300/assistant_bridge.*` | control socket helpers + media socket helpers | Adapt | vendor TCP helper 仍留在 owning bridge；media 需要复用同一 host、独立 port、非阻塞发送与丢帧策略。 |
| `new/code/runtime/assistant_service.*` | control-plane owner + new media-side publish owner | Adapt | 前台 sidecar 继续拥有 control plane；media publish 共享前台时序但不得回压 ACK/control。 |
| `new/code/runtime/control_debug_snapshot.hpp` | extended control snapshot family with steering fields | Adapt | 在现有 runtime-owned snapshot 上补 steering-chain observability，而不是让 host 或 sidecar 重新计算。 |
| `new/code/runtime/control_debug_reporter.*` | current `control.snapshot` + new `control.steering_snapshot` export | Adapt | assistant disabled 时也要可验收；新的 steering export 作为 primary evidence surface。 |
| `new/code/legacy/camera_logic.cpp` | steering snapshot perception segment | Replicate | `lateral_error`、`highest_line`、`threshold`、`threshold_veto` 继续来自 project-owned perception result。 |
| `new/code/legacy/pid_control.*` + `old/code/PID.c` + `old/code/FUZZY_PID_UCAS.c` | steering snapshot controller segment | Adapt | 增加 resolved fuzzy P、camera/gyro P/D terms、`w_target`、`gyro_error` 等结构化观测，不改变 turn algorithm ownership。 |
| `new/user/debug.sh` + `new/user/tune_speed.py` | same entrypoint with optional media recorder/alignment path | Adapt | accepted workflow 继续是 `debug.sh tuning ...`，不引入新的 ad hoc socket 主路径。 |
| `old/user/cpu0_main.c` image/threshold observability | new host evidence bundle | Hybrid | 保留“图像 + 阈值 + steering link 同时可见”的调参目标，但实现为 project-owned dual-channel sidecar 和结构化 evidence。 |

### Alignment Coverage

- ✅ 现有 assistant JSON control contract、ACK/state/telemetry framing、`debug.sh tuning` 主入口都已经存在并可作为冻结基线。
- ✅ `PerceptionResult` 已经持有 `lateral_error`、`threshold`、`highest_line`、`threshold_veto`、`capture_time_ms` 和 `frame_id` 等字段。
- ⚠️ `raw_turn_output` / `applied_turn_output` 已可见，但 camera/gyro term、resolved fuzzy P、`w_target`、`gyro_error` 仍未结构化导出。
- ⚠️ assistant-compatible image publish 已存在，但它仍是 vendor-compatible support path，不是 project-owned media contract。
- ❌ 当前没有独立 media socket、长度前缀 envelope、`config_snapshot` / `image_frame` frame family，也没有 time-aligned host evidence bundle。

## Goals / Non-Goals

**Goals:**

- 保持现有 assistant TCP JSON `command/ack/state/telemetry` control plane 冻结不变。
- 新增 project-owned、只读、控制/媒体分离的 steering tuning media sidecar。
- 扩展 runtime-owned steering observability，使 perception、camera outer loop、gyro inner loop、raw/applied turn 和参数白名单快照都能进入 project-owned evidence surface。
- 让 assistant disabled 时仍然可以通过 `control.steering_snapshot` 或等价 export 面验收 steering tuning observability。
- 继续以 `new/user/debug.sh tuning ...` 作为 accepted host workflow，并让 host 可选监听 media port、保存图像/元数据、与 control CSV 时间对齐。
- 为后续自动搜索铺路，但本次只交付稳定观测面和证据面。

**Non-Goals:**

- 不修改 accepted assistant JSON control schema、framing 或 command set。
- 不实现在线 steering `P/D` 写回、在线覆盖 `RuntimeParameters` 或参数文件写回。
- 不把媒体 sidecar 变成可写命令通道。
- 不把 vendor assistant-compatible image publish 当作新的主验收面。
- 不新增一个与 `debug.sh tuning` 平行的 ad hoc host workflow 作为主路径。

## Decisions

### Decision: Freeze The Existing JSON Control Plane And Add A Separate Read-Only Media Sidecar

**Problem being solved**

当前 accepted tuning workflow 已经依赖一条固定的 assistant TCP JSON control session。把 raw grayscale image 或更重的 steering metadata 继续塞进这条连接，会直接污染 `command/ack/state/telemetry` framing，并把媒体链路的拥塞风险带回 ACK/control。

**Alternatives considered**

1. 在现有 JSON session 中增加 base64 image 或大块 steering metadata。
2. 继续使用 vendor-compatible assistant image publish，允许 host 自行猜测其验收含义。
3. 保持现有 JSON control plane 冻结不变，新增 project-owned media sidecar，仍由板端主动连上位机，只做只读 image/metadata push。

**Why this option was chosen**

选项 3 最符合现有 accepted surface：

- control plane 保持既有 JSON-line contract，不需要重定义 host command/ACK/state/telemetry。
- media plane 可以使用更适合 raw bytes 的 envelope，而不把二进制负载强行包装成 control JSON。
- 两条连接都由板端主动连上位机，延续现有 assistant host workflow 的部署方式。
- 媒体链路即使掉线、阻塞或关闭，也不会改变 ACK path 和 motion lifecycle。

**Stack Equivalent**

- Frozen control session owner: `new/code/platform/assistant_protocol.*`, `new/code/platform/assistant_link.*`, `new/code/runtime/assistant_service.*`
- New media protocol owner: `new/code/platform/steering_media_protocol.*`
- New media transport owner: `new/code/platform/steering_media_link.*`
- Vendor-facing socket owner: `new/code/platform/true_ls2k0300/assistant_bridge.*` or a sibling media bridge helper under the same owning boundary
- Host entrypoint: `new/user/debug.sh tuning ...`

**Named Deliverables**

- 新增 project-owned media protocol/module，定义 `config_snapshot` 与 `image_frame`
- 新增 project-owned media link/bridge，使用与 control 相同 host、独立 port
- startup-loaded config surface:
  - `assistant_tcp.host`
  - `assistant_tcp.port`
  - `steering_media_enabled`
  - `steering_media_port`
  - `steering_media_publish_interval_ms`
- `debug.sh assistant local` / `debug.sh tuning` 协同配置 control port 与 media port
- runtime 前台 sidecar 在 media ready 后发布 read-only steering media frames

**Failure Semantics**

- media sidecar disabled, unconfigured, or disconnected: 只记录 diagnosable marker；control plane 和 motion lifecycle 继续。
- media send busy: 只丢媒体帧，不等待、不重传、不阻塞 control ACK。
- control plane disconnected while media still healthy: runtime 仍按现有 control/disconnect clear 语义处理 tuning snapshot；media 不得绕过该边界。

**Boundary Examples**

- Allowed: board opens `host:control_port` for JSON control and `host:media_port` for media push.
- Allowed: host consumes JSON `telemetry` and binary `image_frame` from different sockets.
- Forbidden: `image_frame` bytes or binary envelope fields appear inside the accepted JSON control session.
- Forbidden: media sidecar accepts host commands or mutates runtime tuning state.

**Contrast Structure**

- Chosen: frozen JSON control plane + separate read-only media plane.
- Not chosen: one multiplexed socket carrying both commands and raw image payloads.

**Verification Hook**

- Source-first review verifies existing `assistant_protocol.*` command schema remains unchanged while media logic lives in a separate project-owned module.
- Board smoke captures simultaneous control + media connections and proves control ACK/state survives media churn.

### Decision: Publish One Runtime-Owned Steering Snapshot Family With A Non-Assistant Evidence Surface

**Problem being solved**

steering tuning 需要同时看到 perception、fuzzy-camera outer loop、gyro inner loop 和最终 applied turn，但这些量当前分散在 `PerceptionResult`、`LegacyPidControl` 内部局部变量和 `ControlDebugSnapshot` 的少数字段里。让 host、bridge 或 diagnostics 各自重算会形成不受控的第二套 API。

**Alternatives considered**

1. 让 host 从现有 telemetry 和 board logs 反推 steering-chain 关键量。
2. 让 media sidecar 直接读取 `control_loop.cpp` 局部变量或 `LegacyPidControl` 私有状态。
3. 扩展 runtime-owned snapshot family，统一导出 steering-chain 字段，并由 diagnostics/export 与 media sidecar 共用。

**Why this option was chosen**

选项 3 最符合 project-owned boundary：

- 主控制路径仍然是唯一权威计算方。
- assistant disabled 验收和 media sidecar 都可以消费同一份 snapshot，而不是各自推导。
- `control.steering_snapshot` 能成为非 assistant primary evidence surface。

**Stack Equivalent**

- Snapshot producer: `new/code/runtime/control_loop.cpp`
- Snapshot carrier: `new/code/runtime/control_debug_snapshot.hpp` plus a new steering snapshot sub-structure or equivalent runtime-owned family in `runtime_state.hpp`
- Diagnostics/export consumer: `new/code/runtime/control_debug_reporter.*`
- Media consumer: runtime foreground media publisher

**Named Deliverables**

- 扩展后的 steering snapshot fields:
  - `lateral_error`
  - `highest_line`
  - `threshold`
  - `threshold_veto`
  - `resolved_fuzzy_p`
  - `camera_p_term`
  - `camera_d_term`
  - `w_target`
  - `gyro_z`
  - `gyro_error`
  - `gyro_p_term`
  - `gyro_d_term`
  - `raw_turn_output`
  - `applied_turn_output`
- `control.steering_snapshot` diagnostics/export contract
- runtime-owned parameter snapshot view for:
  - `PID_TURN_CAMERA.D`
  - `PID_TURN_GYRO_CAMERA.D`
  - `P_Mode`
  - `Speed_base`
  - `control_period_ms`
  - media publish interval

**Failure Semantics**

- Missing or invalid steering snapshot for a cycle: media publish skips that frame and diagnostics keep the last valid export cadence; control output ownership is unchanged.
- assistant disabled: `control.steering_snapshot` remains available.
- snapshot evolution: host workflow only depends on the documented project-owned fields, not ad hoc diagnostics strings.

**Boundary Examples**

- Allowed: `resolved_fuzzy_p` is emitted from the same runtime-owned calculation path that resolved camera proportional gain for that cycle.
- Allowed: `threshold_veto` comes from `PerceptionResult`, not from host-side image reprocessing.
- Forbidden: host workflow recomputes `gyro_error` by guessing from raw image and telemetry.
- Forbidden: diagnostics/export reads private `LegacyPidControl` members through backdoor access without a project-owned snapshot contract.

**Contrast Structure**

- Chosen: runtime-owned steering snapshot + non-assistant export.
- Not chosen: assistant-only observability or host-side recomputation.

**Verification Hook**

- Source review checks that steering fields are published through one project-owned snapshot family.
- Assistant-disabled runtime verification proves `control.steering_snapshot` or equivalent export remains sufficient for acceptance.

### Decision: Define One Binary Envelope For `config_snapshot` And `image_frame`

**Problem being solved**

转向调参既需要结构化元数据，也需要原始 `160x128` grayscale bytes。只定义“一个媒体 socket”还不够；必须冻结一版 host/board 都能实现的 frame contract。

**Alternatives considered**

1. 继续使用 assistant-compatible image publish，并另开文本 side-channel 传 metadata。
2. 用 newline-delimited JSON 直接传图像 payload。
3. 使用长度前缀二进制 envelope，内部包含 UTF-8 JSON header 和原始 payload。

**Why this option was chosen**

选项 3 既能保留 project-owned metadata schema，也能直接承载固定大小图像 payload：

- header 仍然是易于检查与演进的 JSON。
- payload 可以是无压缩 raw bytes，不需要 base64。
- host 侧读取逻辑明确，不会与 control JSON parser 混淆。

**Stack Equivalent**

- Frame envelope: `uint32_be header_len` + `uint32_be payload_len` + `header_json_bytes` + `payload_bytes`
- Media frame families:
  - `config_snapshot`
  - `image_frame`
- Host decoder: extended `new/user/tune_speed.py` or helper module under `new/user/`

**Named Deliverables**

- `config_snapshot` header fields:
  - `type="config_snapshot"`
  - `publish_time_ms`
  - `param_snapshot` object with exact keys:
    - `pid_turn_camera_d`
    - `pid_turn_gyro_camera_d`
    - `p_mode`
    - `speed_base`
    - `control_period_ms`
  - `media_publish_interval_ms`
- `image_frame` header fields:
  - `type="image_frame"`
  - `frame_id`
  - `capture_time_ms`
  - `publish_time_ms`
  - `motion_phase`
  - `pixel_format="gray8"`
  - `width=160`
  - `height=128`
  - `steering_snapshot` object with exact keys:
    - `lateral_error`
    - `highest_line`
    - `threshold`
    - `threshold_veto`
    - `resolved_fuzzy_p`
    - `camera_p_term`
    - `camera_d_term`
    - `w_target`
    - `gyro_z`
    - `gyro_error`
    - `gyro_p_term`
    - `gyro_d_term`
    - `raw_turn_output`
    - `applied_turn_output`
- `image_frame` payload:
  - exact `160 * 128 = 20480` grayscale bytes

**Failure Semantics**

- malformed header or payload length mismatch: host rejects that media frame and records a diagnosable parsing error; control plane continues.
- media reconnect: runtime resends one `config_snapshot` when the new media session becomes ready.
- zero payload on `config_snapshot`: accepted; `image_frame` payload MUST be exactly 20480 bytes.

**Boundary Examples**

- Allowed: `config_snapshot` has JSON header with top-level `param_snapshot` plus `media_publish_interval_ms`, and empty payload.
- Allowed: `image_frame` carries raw bytes plus a top-level `steering_snapshot` object.
- Forbidden: compressed image blob with undocumented codec or implicit payload length.
- Forbidden: frame family names outside `config_snapshot` / `image_frame` as first-release accepted behavior.

**Contrast Structure**

- Chosen: binary length-prefixed envelope + JSON header + raw payload.
- Not chosen: second JSON-line stream carrying base64 image data.

**Verification Hook**

- Local protocol tests verify frame length, header parsing, and fixed image payload size.
- Host verification proves `config_snapshot` is sent once per media session and `image_frame` metadata aligns with CSV timestamps.

### Decision: Board-Side Control And Media Wiring Must Use One Project-Owned Configuration Surface

**Problem being solved**

docs 已经要求 control 与 media 共享 accepted host target并使用不同 port，但如果没有一个冻结的 board-side 配置面，后续实现仍可能把 media endpoint 写成硬编码值，或者让 host wiring 停留在临时脚本参数里。

**Alternatives considered**

1. 让实现自由选择 media host/port 的来源，只在 runbook 里描述“配置一下”。
2. 给 media sidecar 新增一套完全独立的 host/port config surface。
3. 复用现有 assistant control host，新增最小的 steering media 参数键，并要求 project-owned CLI 一起维护这组字段。

**Why this option was chosen**

选项 3 能把 wiring contract 收紧到最小且可审计的面：

- control host 继续由 `assistant_tcp.host` 提供，不引入第二个 host 字段。
- media 只增加自己必需的 enable/port/cadence 字段。
- `debug.sh assistant local` 与 `debug.sh tuning` 可以在 accepted workflow 中同时配置和消费这组字段。

**Stack Equivalent**

- Startup-loaded control endpoint:
  - `assistant_tcp.host`
  - `assistant_tcp.port`
- Startup-loaded steering media endpoint:
  - `steering_media_enabled`
  - `steering_media_port`
  - `steering_media_publish_interval_ms`
- Project-owned setup surface:
  - `new/config/default_params.json`
  - `new/code/platform/param_store.cpp`
  - `new/user/debug.sh`

**Named Deliverables**

- parameter schema additions for `steering_media_enabled`, `steering_media_port`, and `steering_media_publish_interval_ms`
- explicit rule that steering media uses `assistant_tcp.host` as the accepted board-side host target
- `debug.sh assistant local` / related helper flow that updates both control and media wiring in one project-owned setup path

**Failure Semantics**

- `steering_media_enabled=false`: control plane remains available; media is not attempted.
- invalid `steering_media_port`: media sidecar is diagnosably unavailable; control plane and startup contract remain unchanged.
- missing or malformed media params: existing startup-loaded param validation remains authoritative; no ad hoc fallback script becomes the accepted setup surface.

**Boundary Examples**

- Allowed: board reads `assistant_tcp.host=192.168.x.x`, `assistant_tcp.port=8888`, `steering_media_port=8890`.
- Allowed: `debug.sh assistant local 8888 8890` or equivalent project-owned helper updates both ports in `default_params.json`.
- Forbidden: media host/port are hardcoded in bridge code or passed only through undocumented shell env.
- Forbidden: a second independent host field drifts away from `assistant_tcp.host` in the accepted first release.

**Contrast Structure**

- Chosen: one shared host + distinct control/media port fields.
- Not chosen: free-form manual setup or a second unrelated host config surface.

**Verification Hook**

- Source review checks that board-side control/media wiring is loaded from project-owned params rather than hardcoded transport constants.
- Runbook review confirms the accepted setup path updates both control and media wiring before tuning starts.

### Decision: Media Publication Must Be Best-Effort, Latest-Frame, And Non-Blocking

**Problem being solved**

图像帧体积远大于 control JSON。如果沿用 ACK-style “必须送达”思路，媒体拥塞会直接拖慢 foreground sidecar 甚至反向影响 control command handling。

**Alternatives considered**

1. 对 media frame 建有界队列并要求最终送达。
2. 允许 media backpressure 反压到 control/service tick。
3. 只保留最新可发布帧；socket 忙时丢旧帧，不重传、不等待。

**Why this option was chosen**

选项 3 与本 change 的 observability-only 目标一致：

- 调参需要最新视图，而不是完整视频可靠传输。
- control path 的 determinism 和 ACK path 优先级高于媒体完整性。
- 丢帧策略更容易证明“不会因为观测面拖慢控制面”。

**Stack Equivalent**

- Media pacing owner: runtime foreground media publisher
- Busy detection: platform media bridge/link IO status
- Queue policy: zero-or-one latest frame, overwrite old pending frame

**Named Deliverables**

- non-blocking media send path
- explicit drop counter or diagnosable marker for busy/drop events
- control-priority publish policy in runtime service

**Failure Semantics**

- socket would-block: drop pending stale media frame and keep only newest frame candidate.
- repeated disconnect/backoff: media remains degraded; control session and `snapshot_cleared` semantics stay unchanged.
- host too slow: evidence bundle may have sparse media frames but still preserves control CSV and session logs.

**Boundary Examples**

- Allowed: media frame `N` dropped while frame `N+1` is published later.
- Allowed: ACK/state/telemetry continue while no media frame is delivered for several intervals.
- Forbidden: control command processing waits for media send completion.
- Forbidden: media disconnect suppresses `ack`, `state`, `snapshot_cleared`, or `FAIL_SAFE_LATCHED` rejection semantics.

**Contrast Structure**

- Chosen: latest-frame best effort.
- Not chosen: reliable media transport with backpressure into the tuning workflow.

**Verification Hook**

- Focused transport tests simulate busy sockets and prove control session behavior is unchanged.
- Board smoke shows ACK chain stays intact while media drop markers appear.

### Decision: Keep `debug.sh tuning` As The Accepted Host Workflow And Extend It For Recording/Alignment Only

**Problem being solved**

仓库已经明确 `new/user/debug.sh tuning ...` 是 accepted host entrypoint。为 steering media 再起一个临时 socket recorder，会把 acceptance surface 分裂成两个 workflow。

**Alternatives considered**

1. 另写一个独立 Python socket 脚本专门录图像。
2. 让 host workflow 继续只保存 control CSV，媒体记录交给人工工具。
3. 扩展现有 `debug.sh tuning` / `tune_speed.py`，让其可选监听 media port、保存图像/metadata，并和控制 CSV 时间对齐。

**Why this option was chosen**

选项 3 保持 accepted workflow 集中：

- 调参入口仍然只有 `debug.sh tuning ...`。
- host 可在一轮 run 中同时得到命令反馈、control CSV、media records 和对齐后的最小证据包。
- 本次仍保持 read-only observability，不把 steering parameter mutation 混进 host tool。

**Stack Equivalent**

- Entry point: `new/user/debug.sh tuning ...`
- Control consumer: existing JSON session logic in `new/user/tune_speed.py`
- Media recorder: new helper in `new/user/` or an extension inside `tune_speed.py`
- Evidence outputs: CSV + media index/metadata + raw frame files + host log

**Named Deliverables**

- `debug.sh tuning` 新参数：
  - media host/port wiring
  - media output directory
  - optional grayscale preview/overlay
- host evidence bundle containing:
  - control CSV
  - board log
  - media frame metadata
  - raw image frame capture
  - time-aligned summary

**Failure Semantics**

- media listener disabled or unavailable: host workflow still preserves control CSV and command logs.
- plotting unavailable: current plotting fallback remains; media recording is independent of plotting dependency.
- steering params remain read-only: host can record `config_snapshot`, but cannot send steering mutation commands.

**Boundary Examples**

- Allowed: `debug.sh tuning --media-port 8890 --media-dir ../verification/phase-d-steering-media`.
- Allowed: host writes CSV and raw frame artifacts from one run.
- Forbidden: a new ad hoc Python script becomes the accepted first-release steering tuning entrypoint.
- Forbidden: `debug.sh tuning` grows a steering `set_pid` command in this change.

**Contrast Structure**

- Chosen: extend the accepted workflow for recording and visualization.
- Not chosen: parallel host workflows or online steering parameter mutation.

**Verification Hook**

- Host evidence review checks that one accepted workflow can record aligned control and media artifacts.
- Runbook verification confirms control-only runs still work when media is disabled.

### Decision: Steering Parameter Exposure Stays Narrow And Read-Only

**Problem being solved**

steering tuning 需要知道当前加载了哪些关键参数，但本 change 明确不做在线写回。如果 host workflow 拿到完整 param mutation surface，就会直接越过 proposal non-goal。

**Alternatives considered**

1. 暴露全部 `RuntimeParameters` 到 media/config header。
2. 允许 host 读写 steering P/D。
3. 只暴露一个小型 read-only whitelist snapshot，并明确不扩展 accepted command set。

**Why this option was chosen**

选项 3 既能满足观测需要，又能防止 contract 漂移：

- host 能知道这轮 run 的 steering context。
- proposal 和 tasks 可以明确本次不包含 steering parameter mutation。
- 后续如果真要加在线写参，需要单独 change 与独立 review。

**Stack Equivalent**

- Param source: startup-loaded `port::RuntimeParameters`
- Publish path: `config_snapshot` header
- Host surface: evidence metadata only

**Named Deliverables**

- read-only whitelist snapshot of:
  - `PID_TURN_CAMERA.D`
  - `PID_TURN_GYRO_CAMERA.D`
  - `P_Mode`
  - `Speed_base`
  - `control_period_ms`
  - media publish interval
- explicit “no online mutation” rule in host workflow docs/specs/tasks

**Failure Semantics**

- malformed runtime params at startup: existing startup/fail-closed rules remain authoritative; no special media fallback creates a second config source.
- host attempts to mutate steering params over control plane: rejected as unsupported command under the frozen control contract.

**Boundary Examples**

- Allowed: host stores one `config_snapshot` alongside the run.
- Forbidden: control command `set_pid_turn_camera_d`.
- Forbidden: media sidecar becomes a writable config socket.

**Contrast Structure**

- Chosen: narrow read-only snapshot.
- Not chosen: general live-parameter tunnel.

**Verification Hook**

- Spec/source review confirms accepted command set is unchanged.
- Host runbook review proves steering parameter evidence comes from snapshot capture, not mutation.

## Independent Verification Plan (STANDARD/STRICT)

Verification uses shared sequence `verify-sequence/default` from:
`openspec/schemas/ai-enforced-workflow/verification-sequence.md`
and shared verification-cycle contracts:

- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`

Stage A flow:

- checkpoints use the same `active/non_active/closed` verification cycle
- docs-first checkpoints use changed `proposal/specs/design/tasks` as the primary surface
- source-first checkpoints use changed code, tests, and directly impacted code as the primary surface
- approved docs remain reference material when source-first review runs
- repository-level `.index/` material is optional background only
- verification continues a usable `active` agent first
- callers prefer `send_input` while that same `active` agent is still open
- callers use `resume` only when that same `active` agent was closed and must be restored
- if no usable `active` agent exists, the orchestrator spawns one
- only `block -> pass` marks an agent `non_active`
- termination depends only on a valid `active` pass

Runtime profile policy:

- Use verifier runtime profile from `.codex/agents/verify-reviewer.toml`.

Loop rule:

- an `active` agent that reports `block` stays authoritative until that same agent returns `pass`
- `close` or `exit` never implies `non_active`
- valid `pass` requires `review_coverage.coverage_status=complete` and `review_coverage.exhaustive=true`
- partial verification requires explicit `review_scope.scope`
- only the main orchestrator may authorize resume/spawn/repair/terminate, and it must not substitute its own judgment for verifier output

## External Repository Index Reference (Optional)

This change does not require `.index/` material. If repository indexing is used later, it remains non-authoritative background only and MUST NOT become a verification prerequisite.

## Review Checkpoints

- Artifact-completion docs-first review
  - Shared sequence reference: `verify-sequence/default`
  - Review goal: `implementation_correctness`
  - Verifier agent path: `.codex/agents/verify-reviewer.toml`
  - Invocation template id: `verify-reviewer-inline-v3`
  - Authoritative findings JSON path:
    `openspec/changes/add-steering-tuning-media-observability/verification/artifact-completion/attempt-<n>/findings.json`
  - Verifier execution evidence JSON path:
    `openspec/changes/add-steering-tuning-media-observability/verification/artifact-completion/attempt-<n>/verifier-evidence.json`
  - Agent table path:
    `openspec/changes/add-steering-tuning-media-observability/verification/artifact-completion/agent-table.json`
  - Continuation target on pass: `openspec-apply-change`

- Checkpoint 1: steering snapshot surface and non-assistant export
  - Findings JSON:
    `openspec/changes/add-steering-tuning-media-observability/verification/checkpoint-1/attempt-<n>/findings.json`
  - Evidence JSON:
    `openspec/changes/add-steering-tuning-media-observability/verification/checkpoint-1/attempt-<n>/verifier-evidence.json`
  - Agent table:
    `openspec/changes/add-steering-tuning-media-observability/verification/checkpoint-1/agent-table.json`
  - Primary surface: `runtime/control_loop.*`, `runtime/control_debug_snapshot.*`, `runtime/control_debug_reporter.*`, `legacy/pid_control.*`

- Checkpoint 2: media transport and protocol
  - Findings JSON:
    `openspec/changes/add-steering-tuning-media-observability/verification/checkpoint-2/attempt-<n>/findings.json`
  - Evidence JSON:
    `openspec/changes/add-steering-tuning-media-observability/verification/checkpoint-2/attempt-<n>/verifier-evidence.json`
  - Agent table:
    `openspec/changes/add-steering-tuning-media-observability/verification/checkpoint-2/agent-table.json`
  - Primary surface: media protocol/link modules, bridge ownership, busy-drop behavior

- Checkpoint 3: runtime dual-channel publish path and control regression
  - Findings JSON:
    `openspec/changes/add-steering-tuning-media-observability/verification/checkpoint-3/attempt-<n>/findings.json`
  - Evidence JSON:
    `openspec/changes/add-steering-tuning-media-observability/verification/checkpoint-3/attempt-<n>/verifier-evidence.json`
  - Agent table:
    `openspec/changes/add-steering-tuning-media-observability/verification/checkpoint-3/agent-table.json`
  - Primary surface: foreground service scheduling, control disconnect clear semantics, `FAIL_SAFE_LATCHED` rejections, snapshot/export behavior

- Checkpoint 4: host workflow extension and evidence bundle
  - Findings JSON:
    `openspec/changes/add-steering-tuning-media-observability/verification/checkpoint-4/attempt-<n>/findings.json`
  - Evidence JSON:
    `openspec/changes/add-steering-tuning-media-observability/verification/checkpoint-4/attempt-<n>/verifier-evidence.json`
  - Agent table:
    `openspec/changes/add-steering-tuning-media-observability/verification/checkpoint-4/agent-table.json`
  - Primary surface: `new/user/debug.sh`, `new/user/tune_speed.py`, host artifacts, runbooks

- Checkpoint 5: final source-first board/runtime verification
  - Findings JSON:
    `openspec/changes/add-steering-tuning-media-observability/verification/checkpoint-5/attempt-<n>/findings.json`
  - Evidence JSON:
    `openspec/changes/add-steering-tuning-media-observability/verification/checkpoint-5/attempt-<n>/verifier-evidence.json`
  - Agent table:
    `openspec/changes/add-steering-tuning-media-observability/verification/checkpoint-5/agent-table.json`
  - Primary surface: changed implementation files, protocol tests, board smoke logs, aligned evidence bundle

## Migration Plan

1. 先扩展 steering snapshot family 与 `control.steering_snapshot`，确保 assistant disabled 时 already 可验收。
2. 在此基础上引入独立 media protocol/link，先完成 local protocol tests 和 busy-drop behavior，再接到 runtime foreground sidecar。
3. 扩展 host workflow，使 `debug.sh tuning` 能同时保存 control CSV 与 media artifacts，但默认仍可 control-only 运行。
4. 更新 runbooks 和 verification docs，要求证据包至少包含 board log、host CSV、media records、time-aligned summary。
5. rollout 期间保留现有 control plane 不变；如果 media path 出现问题，回退方式是禁用 media sidecar，而不是触碰 JSON control contract。

## Open Questions

- 首版 host visualization 是否直接内嵌在 `tune_speed.py` 还是拆成单独 helper module，对 acceptance surface 没有影响；只要 `debug.sh tuning` 仍是唯一 accepted entrypoint 即可。
- 如果后续实测板端 CPU/网络负担偏高，可以只下调默认 media publish interval；这不会改变首版协议或字段集合。

## Risks / Trade-offs

- 双 TCP sidecar 会增加参数和部署复杂度；必须保证 `debug.sh assistant local` 与 `debug.sh tuning` 一起配置，不把 host wiring 留给人工猜测。
- steering snapshot 扩容会让 runtime/legacy observability surface 变大；但这是为了避免 host/diagnostics 各自重算带来的 contract 漂移。
- raw grayscale frames 会放大 evidence bundle 体积；首版以“少量、稳定、可对齐”为目标，不追求完整视频录制。
- media drop policy 会牺牲帧完整性；但这是为保证 control plane determinism 和 ACK path 完整性所必须的 trade-off。
