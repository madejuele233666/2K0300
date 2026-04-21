# Speed Tuning 协议精简说明

基于实际的 **上位机单客户端 + 顺序发送控制指令** 的交互场景（此时上位机可能是一个 Python 脚本，由人工操作或 AI 接管），我们对首发的 JSON-Line 通信协议进行了大幅度"减负"。核心目标是**大幅降低板端 C++ 在处理和序列化各类异常分支时的实现复杂度和认知负担**，且不损失任何实际有用的反馈信息。

以下是协议精简前后的核心变更及理由：

## 1. ACK outcome 字段精简

*   **之前：** 5 种 (`accepted`, `rejected`, `ignored`, `cleared`, `superseded`)
*   **之后：** 2 种 (`accepted`, `rejected`)
*   **精简依据：**
    *   **`superseded` (被取代)**：假设上位机单客户端顺序发送指令，且必须等到 ACK 才进行下一步或基于超时重发，就不存在多客户端同时覆盖相同配置导致新旧指令抢占。不需要。
    *   **`ignored` (被忽略)**：此前设计用于处理重复包。对于基于 TCP 传输且自行维护 sequence ID 递增状态机的主控程序，完全可以在协议层面交由上层状态机忽略，没必要将 `ignored` 提升到独立的回应类别，若因格式不对拒绝可以直接归类到 `rejected` 并带原因。
    *   **`cleared`**：这是一个异步的状态清空事件，用对命令的 synchronous ACK 回应机制去承载异步清空行为是在滥用。将其放到 State 帧才是恰当的设计。

## 2. 详细的 Rejection Reason 去除硬编码 Code

*   **之前：** 使用 `reason_code` 枚举（针对 missing, non-numeric, negative, above-range 分别独立一个 Error Code 分配下发）。
*   **之后：** 改用必填的 `reason` 字符串域代替。
*   **精简依据：** Python/AI 的消费端在接收到无效参数回应时，往往不需要在代码做 Switch/Case 针对 `above-range` 和 `negative` 去分别触发不同的恢复逻辑（通常 AI 工作流也只是抛错或者打印日志告知模型自己犯蠢了）。因此直接返回 `reason="invalid target speed: value exceeds Speed_base"` 等人类可读错误不仅能给调试带来巨大帮助，也能免除两端各自维护一套 error code 数据表的包袱。

## 3. State Event 事件简化

*   **之前：** 4 种 event（`tuning_mode_enabled`, `tuning_mode_disabled`, `target_speed_override_cleared`, `tuning_snapshot_cleared`）。
*   **之后：** 2 种 event（`override_cleared`, `snapshot_cleared`）。
*   **精简依据：**
    *   **开启/关闭行为隐式确认**：当上位机下发开启 `enable_tuning_mode` 或关闭 `disable_tuning_mode` 时，只要得到了 ACK `accepted`，本身就已经确认了模式切换完成。没必要再触发一个多余的 State 事件做双重播报。
    *   **保留生命周期变更**：保留过期清理(`override_cleared`)和整体快照清零（如遇意外断联兜底，`snapshot_cleared`），因为这是板端由板内定时器（或者底层网络层库）异步发起的行为，上位机无法精准预知，所以必须作为推送事件告诉上位机。

## 4. State Frame Sequence ID 要求剥离

*   **之前：** 状态帧若由指引触发还需要冗余带上该 `seq` ID。
*   **之后：** 剔除上述要求。
*   **精简依据：** ACK 已经携带了 `seq` 用以跟踪原指令，分离的纯异步 State Frame 更应该聚焦自己"状态变更"的核心，而不必越权作为 Command 追踪系统的附属层。

## 5. 多客户端及旧版/非法命令包拦截要求（Stale / Duplicate）

*   **之前：** 有专门的 Scenario 保证必须明确识别乱序和重复并独立给出反应。
*   **之后：** 删除。
*   **精简依据：** 过度防御。只要不破坏内存，处理掉这些冗余设计能够极大地简化 `command decoder` 的实现。 

---

### 精简总结效益

这五项重构将极大地减少板端需要铺设的 `if/else`, `switch` 的样板判断代码。根据板端的 C++ 实现评估：
1. 抹掉近 4 种 JSON 结构发包逻辑路径以及枚举。
2. 抹除校验阶段细分错误的反馈分类分支，一条验证通不过统一 reject 返回错误 string 即可。
3. 使得板端工程师在编写协议收发时的心智负担减少约 70%，完全符合“**首版仅提供必要功能**”的原则。
