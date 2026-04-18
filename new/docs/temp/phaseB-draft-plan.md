  一、核心拆分
  最重要的是守住这三个语义边界：

  - startup/shutdown 继续只负责资源和进程，不负责“发车”。现状是对的 [startup.cpp](/home/madejuele/projects/2K0300/new/code/
  runtime/startup.cpp:249)、[shutdown.cpp](/home/madejuele/projects/2K0300/new/code/runtime/shutdown.cpp:5)。
  - control.veto 继续只表示故障/失效保护，不表示“还没发车”。这个语义现在很干净，别污染 [control_decision.cpp](/home/
  madejuele/projects/2K0300/new/code/runtime/control_decision.cpp:13)。
  - LegacyPidControl / LegacyMotorLogic 继续只做控制律，不吞掉生命周期语义 [pid_control.cpp](/home/madejuele/
  projects/2K0300/new/code/legacy/pid_control.cpp:7)、[motor_logic.cpp](/home/madejuele/projects/2K0300/new/code/legacy/
  motor_logic.cpp:8)。

  old_2 真正该迁移的不是 temp_pwm 这种字面实现，而是“起步/运行/停车/故障恢复是独立语义”这件事 [old_2 isr.cpp](/home/
  madejuele/projects/2K0300/old_2/project/code/isr.cpp:143)、[old_2 main.cpp](/home/madejuele/projects/2K0300/old_2/
  project/user/main.cpp:184)。

  二、建议新增的类型
  最干净的做法是加一组 runtime-owned 类型，不改 adapter 边界：

  - 在 new/code/runtime/ 新增 motion_types.hpp、motion_supervisor.hpp/.cpp。
  - 在 [control_types.hpp](/home/madejuele/projects/2K0300/new/code/port/control_types.hpp:71) 的 RuntimeParameters 里补充
  运动参数，别再开一套第二配置源。
  - 在 [runtime_state.hpp](/home/madejuele/projects/2K0300/new/code/runtime/runtime_state.hpp:13) 里补共享的运动态，但把内
  部计数器尽量留在 MotionSupervisor 私有成员里。

  我会加这些概念：

  enum class MotionPhase {
      kDisarmed,
      kStartRequested,
      kSpinup,
      kRunning,
      kStopping,
      kFailSafeLatched
  };

  struct MotionIntent {
      bool start_requested = false;
      bool stop_requested = false;
      bool reset_fault_requested = false;
  };

  struct MotionDecision {
      MotionPhase phase = MotionPhase::kDisarmed;
      bool hold_disarmed = true;
      bool apply_emergency_stop = false;
      bool reset_control_state = false;
      double effective_speed_target = 0.0;
      int effective_pwm_limit = 0;
      float effective_turn_limit = 0.0F;
      int pwm_step_limit_per_cycle = 0;
  };

  RuntimeState 至少补这些公开状态：

  - motion_phase
  - motion_intent
  - motion_fault_latched
  - last_motion_decision

  不要把 gate_clear_streak、phase_enter_ms 这类内部推进细节也塞进 RuntimeState，它们应该留在 MotionSupervisor 内部。

  三、状态机怎么落
  我建议用下面这条状态线，比直接从 DISARMED -> RUNNING 稳很多：

  DISARMED
    │ start_requested
    ▼
  START_REQUESTED
    │ gate 连续 clear N 个周期
    ▼
  SPINUP
    │ ramp 完成且速度/输出稳定
    ▼
  RUNNING
    │ stop_requested
    ▼
  STOPPING
    │ 速度接近 0 且输出归零
    ▼
  DISARMED

  SPINUP / RUNNING / STOPPING
    │ 任一真实故障 gate.veto
    ▼
  FAIL_SAFE_LATCHED
    │ reset_fault_requested 且 gate 恢复稳定
    ▼
  DISARMED

  关键细节是这几个：

  - START_REQUESTED 必须存在。它的价值是把“操作者想发车”和“系统允许起步”分开，这样才能记录
  motion.start.blocked(reason=...)。
  - FAIL_SAFE_LATCHED 只在已经准备动或正在动时进入。还没发车时 gate 不通，不应该记成“故障后锁死”，只应该记成“start
  blocked”。
  - STOPPING 也必须显式存在，否则 main.cpp 一收到信号就直接退出，根本留不下“正常停车”的证据 [main.cpp](/home/madejuele/
  projects/2K0300/new/user/main.cpp:186)。

  四、ControlLoop 里具体怎么接
  当前 [Tick()](/home/madejuele/projects/2K0300/new/code/runtime/control_loop.cpp:285) 的骨架是对的：先读电源、IMU、编码
  器、感知，再做 gate，再决定命令。完整实现时，我会把它改成这个顺序：

  1. 采样 power/imu/encoder/perception
  2. 调 EvaluateControlGate(...)
  3. 调 MotionSupervisor::Evaluate(...)
  4. 根据 MotionDecision 选路径
  5. 更新 observation / diagnostics / shared state

  对应三条输出路径：

  - gate.veto_active == true
    这里继续走真正的 fail-safe，发 emergency_stop，保留现有 control.veto.* 语义。
  - gate 清了，但 motion_decision.hold_disarmed == true
    这里不要伪装成 fail-safe；应当只 motor->Disable()，并记一个新的 marker，例如 control.apply.hold_disarmed。
  - gate 清了且允许运动
    才进入 PID + mix + slew limit + motor apply。

  这一点非常关键，因为你现在的 ObserveControlCycle() 会把所有 emergency_stop 都解释成 kEmergencyStopApplied
  [control_decision.cpp](/home/madejuele/projects/2K0300/new/code/runtime/control_decision.cpp:43)。如果把“未发车的静止保
  持”也走 emergency_stop，日志会非常脏。

  所以 ControlApplyOutcome 我会再补一个状态，例如：

  - kHeldDisarmed

  这样 B-1/B-3/B-5 的证据就能分清：

  - 现在只是“没发车”
  - 还是“故障停机”

  五、起步和停车不要靠硬编码 PWM 跳变
  new/ 的底层执行器只有左右轮 PWM，没有单独无刷状态机 [bridge.hpp](/home/madejuele/projects/2K0300/new/code/platform/
  true_ls2k0300/bridge.hpp:53)、[motor_bridge.cpp](/home/madejuele/projects/2K0300/new/code/platform/true_ls2k0300/
  motor_bridge.cpp:102)。所以 Phase B 的“缓启动”应该这样实现：

  - 不复刻 old_2 的 run_brushless_start/temp_pwm
  - 改为 supervisor 输出一个 effective_speed_target
  - 再加一个 per-wheel slew limit

  也就是：

  PID 想要的命令
    └─ mean_speed_target 从 0 线性爬到 Speed_base
    └─ turn_output 在 SPINUP 阶段限幅更小
    └─ left/right 再做每周期最大改变量限制

  原因有两个：

  - 只 ramp Speed_base 不够，转向量还是可能跳。
  - 只做 PWM 限步也不够，目标速度会太生硬。

  所以最稳的是两者都做。

  建议新增的参数直接进 RuntimeParameters 和 default_params.json [default_params.json](/home/madejuele/projects/2K0300/new/
  config/default_params.json:1)：

  - motion_unveto_confirm_cycles
  - motion_spinup_ms
  - motion_stop_ms
  - motion_turn_limit_spinup
  - motion_pwm_step_limit
  - motion_stop_encoder_threshold
  - motion_fault_rearm_hold_ms

  param_store.cpp 现在已经有 optional int 解析框架，扩展很顺手 [param_store.cpp](/home/madejuele/projects/2K0300/new/code/
  platform/param_store.cpp:57)。

  六、控制器内部必须可 reset
  如果你不加 reset，Phase B 一旦经历 stop / fault / restart，旧积分会残留，起步一定会脏。

  当前需要补 reset 的地方有两个：

  - [LegacyPidControl](/home/madejuele/projects/2K0300/new/code/legacy/pid_control.hpp:9)：清 camera_error_last_、
  gyro_error_last_、gyro_i_count_、speed_error_last_、speed_i_count_
  - [LegacyAttitudeLogic](/home/madejuele/projects/2K0300/new/code/legacy/attitude_logic.hpp:8)：清 yaw_deg_

  然后在这些状态切换点触发 reset：

  - DISARMED -> SPINUP
  - RUNNING/SPINUP -> FAIL_SAFE_LATCHED
  - STOPPING -> DISARMED

  同时把 RuntimeState::W_Target_last 也清零 [runtime_state.hpp](/home/madejuele/projects/2K0300/new/code/runtime/
  runtime_state.hpp:15)。

  七、main.cpp 该怎么改才不丑
  这里要先把“产品运行时语义”和“测试自动化语义”拆开。当前 main.cpp 的结构性问题不是“没有自动退出”，而是
  SIGINT 或 LS2K_MAX_FRAMES 一到，就直接退出循环，然后关进程 [main.cpp](/home/madejuele/projects/2K0300/new/user/
  main.cpp:187)。这对测试很方便，但对 Phase B 的真实起停语义不够干净，因为你需要先“请求停车”，等 motion.stop.complete，
  再退出。

  所以要分开两个概念：

  - motion_intent.stop_requested
  - runtime_state.stop_requested

  前者是车辆停车，后者是进程退出。

  产品态我建议只保留这一条约束：

  - 收到 stop signal 或 operator stop 后，先发 motion stop request，等 supervisor 回到 DISARMED，再退出进程

  也就是说，真正长期保留在 main.cpp 里的，不应该是 auto-start/auto-stop，而应该只是“退出前先完成受控停车”。

  至于自动启动、自动停车、自动 fault reset，这些都可以明确降级为测试 harness：

  - LS2K_AUTO_START=1 仅用于 Phase B 可重复验证
  - LS2K_AUTO_START_DELAY_MS 仅用于测试脚本稳定复现
  - LS2K_AUTO_STOP_AFTER_MS 仅用于 smoke / bench / 证据采集
  - LS2K_AUTO_RESET_FAULT=1 仅用于故障注入后的自动恢复验证

  如果后续确认实际使用根本不需要这些自动化入口，它们完全可以删去，或者只保留在 smoke 专用 wrapper 中，而不进入长期产品语义。

  所以这一节真正要落的实现点只有两个：

  - main loop 内部把“请求停车”和“结束进程”拆开
  - `run_remote_smoke.sh` / 测试 env 负责注入 bounded run 之类的自动化行为，而不是让 main.cpp 永久承担测试编排职责

  这样 B-1 的“标准起停流程”才能有真实日志，同时又不会把测试期的自动退出逻辑误写成产品长期约束。

  八、B-5 故障注入应该怎么做
  文档已经把注入点给对了 [02-phase-b-low-speed-vehicle-motion.md](/home/madejuele/projects/2K0300/new/docs/race-finish-
  series.zh-CN/02-phase-b-low-speed-vehicle-motion.md:238)。最优雅的方式是只加白名单 env hook，不加第二套测试程序：

  - PerceptionFrontend
    支持短时 drop frame / force stale
  - ImuAdapter
    支持指定周期返回 invalid [imu_adapter.cpp](/home/madejuele/projects/2K0300/new/code/platform/imu_adapter.cpp:78)
  - EncoderAdapter
    支持指定周期返回 invalid [encoder_adapter.cpp](/home/madejuele/projects/2K0300/new/code/platform/
  encoder_adapter.cpp:57)
  - PowerAdapter
    继续复用已有 LS2K_FORCE_LOW_VOLTAGE [power_adapter.cpp](/home/madejuele/projects/2K0300/new/code/platform/
  power_adapter.cpp:74)

  建议 env 名：

  - LS2K_FAULT_INJECT_DROP_FRAME_EVERY_N
  - LS2K_FAULT_INJECT_IMU_INVALID_EVERY_N
  - LS2K_FAULT_INJECT_ENCODER_INVALID_EVERY_N

  然后 run_remote_smoke.sh 只做一件事：把这些白名单 env 透传到远端，不新增分叉入口 [run_remote_smoke.sh](/home/madejuele/
  projects/2K0300/new/user/run_remote_smoke.sh:49)。

  九、日志要补哪些 marker
  Phase B 是否“优雅”其实最后看日志能不能讲清楚。建议最少补这些：

  - motion.phase.transition
  - motion.start.blocked
  - motion.spinup.enter
  - motion.spinup.complete
  - motion.run.enter
  - motion.stop.requested
  - motion.stop.complete
  - motion.failsafe.latched
  - motion.failsafe.reset_ready
  - control.apply.hold_disarmed

  并且继续保留现有：

  - control.veto.*
  - control.apply.drive
  - control.apply.emergency_stop
  - control.arm.transition

  这样 B-2/B-3/B-4/B-5 的视频和日志才能一一对应。

  十、我会按什么顺序实现
  如果现在正式开工，我会按这个顺序，不会乱：

  1. 加 motion_types / motion_supervisor
  2. 扩 RuntimeParameters / param_store / default_params.json
  3. 给 LegacyPidControl / LegacyAttitudeLogic 加 Reset()
  4. 改 ControlLoop::Tick()，接入 gate -> supervisor -> shaping -> apply
  5. 改 main.cpp，把“停车请求”和“进程退出”分开
  6. 加 fault injection hook
  7. 改 run_remote_smoke.sh 做 env 透传
  8. 最后再补文档和 marker 口径
