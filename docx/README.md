# 2K0300 文档索引

本目录集中放置当前仓库的环境与使用文档。

## 标准连板入口

- [board-access skill](/home/madejuele/projects/2K0300/.codex/skills/board-access/SKILL.md)
  - 统一的标准接入链路
  - 覆盖 `SSH 优先 -> 网线兜底 -> 串口辅助 -> 回到 SSH`
  - 需要恢复 SSH、验证直连网线链路、或通过串口补板端网络配置时，优先使用这份 skill

## 文档列表

- [ENV_SETUP_WSL_BOARD.md](/home/madejuele/projects/2K0300/docx/ENV_SETUP_WSL_BOARD.md)
  - 当前已跑通环境的事实记录
  - 保留实际环境状态、工具链路径、Wi-Fi 服务、运行库处理和已知注意事项
  - 不再重复写标准连板步骤
- [board-ethernet-recovery-log-2026-04-24.md](/home/madejuele/projects/2K0300/docx/board-ethernet-recovery-log-2026-04-24.md)
  - 板端 `eth0` 恢复问题的单独排障与修复记录
  - 记录 `connman` 与 `systemd-networkd` 冲突、板端修复动作和重启后验证结果
- [guide.md](/home/madejuele/projects/2K0300/docx/guide.md)
  - 较完整的 WSL 版扩展说明
  - 重点保留工具链、OpenCV、内核和设备树等后续操作
  - 标准连板步骤改为引用 skill

## 当前结论

已完成并验证：
- [x] 串口接入 WSL
- [x] 龙芯 LoongArch 交叉编译工具链安装
- [x] OpenCV 预编译库安装
- [x] SSH / `scp -O` 到板卡
- [x] Wi-Fi 重启 panic 修复
- [x] 用户工程本地交叉编译
- [x] 用户工程上传到板卡

暂未在本轮重新验证：
- [ ] 内核重新编译
- [ ] 新内核上传与刷写
- [ ] 设备树修改流程
