# Board Ethernet Recovery Log

记录时间：2026-04-24 20:28 CST

## 背景

问题现象：
- 板端网线物理连接正常
- 主机侧有时能看到有线口 `Up`
- 板端 `eth0` 会在启动后掉回 `down/off`
- 有线 `SSH` 不能稳定恢复

本次记录只保留修复结论和验证结果，不重复标准连板步骤。

## 根因判断

板端同时存在两套网络管理路径：
- `systemd-networkd`
- `connman.service`

实际观测到的行为是：
- `systemd-networkd` 能管理 `eth0`
- `eth0-link-up.service` 能把 `eth0` 拉到 `UP`
- `connman.service` 在启动链路里短暂运行后退出
- `connman` 的停止过程会把 `eth0` 一起带下去，导致有线链路失效

因此，问题不在网线硬件本身，也不在 `SSH` 服务本身，而在板端网络管理冲突。

## 板端修复

保留的网络路径：
- `eth0-link-up.service`
- `systemd-networkd`
- `/etc/systemd/network/10-eth0.network`

移除的冲突路径：
- `connman.service`

本次在板端执行的持久化修复：

```sh
systemctl disable --now connman.service
systemctl mask connman.service
systemctl restart systemd-networkd
systemctl restart eth0-link-up.service
systemctl restart wlan0-connect.service
```

修复后的职责分工：
- `eth0-link-up.service`：保证 `eth0` 在启动后保持管理上 `UP`
- `systemd-networkd`：负责 `eth0` 的 `DHCP=ipv4 + LinkLocalAddressing=ipv4`
- `wlan0-connect.service`：负责 `wlan0` 连 Wi-Fi
- `connman.service`：不再参与网络管理

## 重启后验证

重启后检查结果：
- 串口恢复：
  - `usbipd` 设备重新 `Attached`
  - WSL 恢复 `/dev/ttyACM0`
  - 串口可见完整启动日志并可登录
- 板端服务状态正确：
  - `connman.service` 为 `masked`
  - `eth0-link-up.service` 为 `enabled` 且启动成功
- 板端网络状态正确：
  - `eth0 = 169.254.148.181/16`
  - `wlan0 = 10.100.170.226/24`
- 主机侧链路状态正确：
  - Windows 实体网卡 `Up, 1 Gbps`
  - WSL `eth0 = 169.254.6.147/16`
- 两条 `SSH` 路径均恢复：
  - `ssh root@169.254.148.181`
  - `ssh root@10.100.170.226`

## 当前结论

本次修复在重启后已验证自动生效。

当前稳定结论：
- 串口接入可恢复
- Wi-Fi `SSH` 可恢复
- 网线直连有线 `SSH` 可恢复
- `connman` 不应再参与这块板子的网络管理

