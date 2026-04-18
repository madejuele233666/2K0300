# LS2K0300 WSL + 开发板环境配置记录

这份文档记录的是本次已经实际跑通的环境配置流程。

适用范围：
- Windows + WSL2
- WSL 内直接交叉编译
- CH340/CH341 串口接入 WSL
- 通过串口给开发板配网
- 通过 `ssh` / `scp -O` 部署到板卡

和原始 [guide.md](/home/madejuele/projects/2K0300/docx/guide.md) 相比，这份文档只保留已经验证过的步骤，并补上实际环境差异。

## 完成状态总览

以下项目已经完成并验证：
- [x] WSL 自定义内核已启用 CH340/CH341 驱动
- [x] WSL 可识别串口设备并进入板卡控制台
- [x] 可通过串口登录开发板
- [x] 龙芯 LoongArch 交叉编译工具链已安装并可调用
- [x] OpenCV 预编译库已安装到本机
- [x] 开发板 Wi-Fi 驱动可自动加载
- [x] 开发板可自动连接 Wi-Fi
- [x] WSL 可通过 `ssh` 登录开发板
- [x] WSL 可通过 `scp -O` 上传文件到开发板
- [x] OpenCV 运行库目录已部署到开发板
- [x] Wi-Fi 重启 panic 修复已完成
- [x] 用户工程交叉编译已跑通
- [x] 用户工程上传到板卡已跑通

以下项目暂未在本次记录中重新验证：
- [ ] 内核重新编译
- [ ] 新内核刷写到板卡
- [ ] 设备树修改流程

## 1. 当前已跑通的环境状态

WSL 侧：
- 自定义 WSL 内核已启用，版本：`5.15.153.1-ch341`
- CH340/CH341 串口驱动已生效
- 当前已验证可通过 `/dev/ttyACM0` 进入板卡串口控制台调试
- 交叉编译器已安装到 `/opt/ls_2k0300_env`
- OpenCV 预编译库已安装到 `/opt/ls_2k0300_env`
- 用户工程可成功编译并上传到板卡

板卡侧：
- 可通过串口登录 `root`
- Wi-Fi 驱动可自动加载
- Wi-Fi 可自动连接到指定网络
- `ssh` 可从 WSL 直接登录
- `scp -O` 可上传文件
- OpenCV 运行库目录已放到板卡

## 2. WSL 侧依赖

在 WSL 中安装这些工具：

```bash
sudo apt update
sudo apt install -y \
    openssh-client openssh-server \
    net-tools git cmake bison flex libssl-dev libncurses5-dev \
    picocom
```

## 3. WSL 自定义内核：启用 CH341 串口驱动

这一步的目的不是给开发板编内核，而是给 WSL 编内核，让 WSL 能识别 CH340/CH341 串口。

本次实际使用路径：
- 源码目录：`/home/madejuele/src/WSL2-Linux-Kernel`
- 编译产物：`/home/madejuele/src/WSL2-Linux-Kernel/arch/x86/boot/bzImage`
- Windows 内核文件：`C:\Users\27866\.wsl-kernels\bzImage-5.15.153.1-ch341`
- WSL 配置文件：`C:\Users\27866\.wslconfig`

启用的关键配置：
- `CONFIG_USB_ACM=y`
- `CONFIG_USB_SERIAL=y`
- `CONFIG_USB_SERIAL_CH341=y`
- `CONFIG_LOCALVERSION="-ch341"`

`.wslconfig` 示例：

```ini
[wsl2]
kernel=C:\\Users\\27866\\.wsl-kernels\\bzImage-5.15.153.1-ch341
```

修改后在 Windows 执行：

```powershell
wsl --shutdown
```

回到 WSL 验证：

```bash
uname -r
ls /dev/ttyUSB* /dev/ttyACM*
dmesg | grep -i ch341
```

本次已验证现象：
- `uname -r` 显示 `5.15.153.1-ch341`
- 串口设备可能显示为 `/dev/ttyUSB0` 或 `/dev/ttyACM0`
- 本次实际调试使用的是 `/dev/ttyACM0`

## 4. 串口连接开发板

查看串口：

```bash
ls /dev/ttyUSB* /dev/ttyACM*
```

连接串口：

```bash
sudo picocom -b 115200 /dev/ttyACM0
```

说明：
- 这块板卡当前在 WSL 中实际枚举为 `/dev/ttyACM0`
- 如果后续重新插拔后设备名变化，再重新执行一次 `ls /dev/ttyUSB* /dev/ttyACM*`
- 已确认可以通过这个串口进入 `LoongOS login`，并直接做 Wi-Fi 配置与调试

退出：
- `Ctrl + A`
- `Ctrl + X`

注意：
- 板卡重启时，WSL 里的 USB 串口映射有概率断开
- 如果串口设备消失，需要重新把设备附加回 WSL，再重新检查 `/dev/ttyUSB*` 或 `/dev/ttyACM*`

## 5. 交叉编译器和 OpenCV

本次实际安装位置：

```bash
/opt/ls_2k0300_env/loongson-gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc1.6
/opt/ls_2k0300_env/opencv_4_10_build
```

压缩包也保留在：

```bash
/opt/ls_2k0300_env/opencv_4_10_build.tar.xz
/opt/ls_2k0300_env/loongson-gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc1.6.tar.xz
```

验证交叉编译器：

```bash
source ~/.zshrc
loongarch64-linux-gnu-g++ --version
```

## 6. 开发板 Wi-Fi 驱动与自动联网

本次板卡上实际使用的 Wi-Fi 模块：
- `/lib/modules/4.19.190+/aic8800_bsp.ko`
- `/lib/modules/4.19.190+/aic8800_fdrv.ko`

已配置的自动加载脚本：
- `/usr/local/sbin/load-aic8800.sh`

已配置的 systemd 服务：
- `/etc/systemd/system/aic8800-load.service`

本次发现板卡上已经存在自动联网服务：
- `/etc/systemd/system/wlan0-connect.service`

Wi-Fi 配置文件：
- `/etc/wpa_supplicant-wlan0.conf`

当前自动连接优先级：
- 主 Wi-Fi：`madejuele236`，优先级 `100`
- 备用 Wi-Fi：`mch666`，优先级 `10`

本次实际已连上的网络：
- SSID：`madejuele236`

板卡当前曾成功获取到的地址：
- `10.236.192.226/24`

如果后续 IP 变化，请重新在串口里执行：

```bash
ifconfig wlan0
```

推荐的多网络模板：

```conf
network={
    ssid="madejuele236"
    psk=cd2e7812ac7ea0c8478ada2436549705d569144b8de35abec74404409c0d0da2
    priority=100
}

network={
    ssid="mch666"
    psk="mch66666"
    priority=10
}
```

说明：
- `priority` 数值越大，优先级越高
- 当前配置会优先连接 `madejuele236`
- 只有主 Wi-Fi 不可用时，才会考虑连接 `mch666`
- 如果手动连过别的 Wi-Fi，执行 `systemctl restart wlan0-connect.service` 可切回自动优先级逻辑

## 7. SSH 与 SCP

本次已验证：

```bash
ssh root@10.236.192.226
```

可以直接登录。

注意：
- 这块板子上的 `dropbear` 没有 `sftp-server`
- 所以 `scp` 必须使用旧协议选项 `-O`

正确示例：

```bash
scp -O local_file root@10.236.192.226:/home/root/
```

错误示例：

```bash
scp local_file root@10.236.192.226:/home/root/
```

如果不用 `-O`，会遇到类似错误：

```text
/usr/libexec/sftp-server: No such file or directory
scp: Connection closed
```

## 8. OpenCV 运行库在板卡上的处理

板卡上已存在目录：

```bash
/home/root/opencv_4_10_build
```

原始 `guide.md` 里建议使用：

```bash
ldconfig
```

但本次实际确认：
- 这套板卡系统里没有 `ldconfig`

所以改用登录时注入环境变量的方式处理，文件为：
- `/etc/profile.d/opencv.sh`

内容：

```sh
export LD_LIBRARY_PATH=/home/root/opencv_4_10_build/lib:${LD_LIBRARY_PATH}
```

验证方法：

```bash
ssh root@10.236.192.226 "sh -lc '. /etc/profile >/dev/null 2>&1; echo \$LD_LIBRARY_PATH'"
```

## 9. Wi-Fi 重启 panic 修复

按 `guide.md` 的思路，本次已把新的 `aic8800_bsp.ko` 替换到板卡：

本地文件来源：
- [aic8800_bsp.ko](/home/madejuele/projects/2K0300/LS2K0300_Library/buildroot-2405/seekfree_overlay/usr/lib/modules/4.19.190/aic8800_bsp.ko)

板卡目标位置：
- `/lib/modules/4.19.190+/aic8800_bsp.ko`

替换后已执行：

```bash
sync
```

本次实际结果：
- 板卡可以正常走完整的 `reboot` 流程
- 没再出现之前“直接卡在重启阶段无法回来”的阻塞

## 10. 用户工程编译与上传

工程路径：
- [project/user](/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/project/user)

构建脚本：
- [build.sh](/home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/project/user/build.sh)

本次对 `build.sh` 做了两个实际修正：
- 板卡地址改为支持环境变量覆盖
- 上传时使用明确的产物绝对路径

当前默认行为：
- 默认 IP：`10.236.192.226`
- 可通过 `BOARD_IP` 覆盖

推荐用法：

```bash
cd /home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/project/user
BOARD_IP=10.236.192.226 ./build.sh
```

本次已验证结果：
- `cmake` 成功
- `make` 成功
- `scp -O` 上传成功
- 板卡上目标文件存在：`/home/root/project`

## 11. 本次实际验证过的关键命令

验证 WSL 内核和串口：

```bash
uname -r
ls /dev/ttyUSB* /dev/ttyACM*
dmesg | grep -i ch341
```

串口登录：

```bash
sudo picocom -b 115200 /dev/ttyACM0
```

查看板卡 IP：

```bash
ifconfig wlan0
```

SSH 登录：

```bash
ssh root@10.236.192.226
```

SCP 上传：

```bash
scp -O ./your_file root@10.236.192.226:/home/root/
```

编译并上传用户程序：

```bash
cd /home/madejuele/projects/2K0300/LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library/project/user
BOARD_IP=10.236.192.226 ./build.sh
```

## 12. 已知注意事项

1. WSL 串口会在某些板卡重启场景下掉线，这不影响 Wi-Fi 和 SSH。
2. 板卡 `scp` 必须加 `-O`，因为没有 `sftp-server`。
3. 板卡没有 `ldconfig`，OpenCV 依赖通过 `LD_LIBRARY_PATH` 处理。
4. `dropbear` 是 `socket activation` 方式运行，`dropbear.service` 显示 `inactive` 不代表 SSH 不可用，应检查 `dropbear.socket`。
5. 板卡 IP 可能变化，如果变了，先串口查看 `wlan0` 地址，再更新 `BOARD_IP`。

## 13. 结论

当前环境已经满足以下开发闭环：
- WSL 识别 CH340/CH341 串口
- 串口登录板卡
- 板卡自动联网
- WSL 通过 SSH/SCP 部署到板卡
- 本机交叉编译应用
- 编译产物上传到板卡运行

如果后续需要扩展，可以在这份文档基础上继续补：
- 板卡程序运行命令
- 内核编译与刷写
- 设备树修改流程
