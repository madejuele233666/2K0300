可以。下面我把教程改成 **WSL 版**，并且从你已经执行完：

```bash
sudo apt update
sudo apt upgrade -y
```

之后开始写。
你已经把主机串口映射到 WSL，串口设备可能出现在 **`/dev/ttyUSB0`** 或 **`/dev/ttyACM0`**。本次实际已验证可通过 **`/dev/ttyACM0`** 进入板卡控制台调试。这样后面看串口日志、配置板卡网络都会更顺。原资料里后续主要是：板卡联网、`ssh/scp` 传文件、安装交叉编译工具链与 OpenCV、修复 `reboot` panic、编译应用和内核、以及更新 `vmlinuz`。 

## 当前完成标记

已完成并验证：
- [x] 串口接入 WSL
- [x] 交叉编译工具链安装
- [x] OpenCV 预编译库安装
- [x] SSH / `scp -O` 到板卡
- [x] Wi-Fi 重启 panic 修复
- [x] 用户工程本地交叉编译
- [x] 用户工程上传到板卡

暂未在本轮重新验证：
- [ ] 内核重新编译
- [ ] 新内核上传与刷写
- [ ] 设备树修改流程

已经跑通并收敛后的记录见：[ENV_SETUP_WSL_BOARD.md](/home/madejuele/projects/2K0300/docx/ENV_SETUP_WSL_BOARD.md)

---

# 2. WSL 中安装后续会用到的工具

原教程里有 `openssh-client/openssh-server`、`net-tools`、`git`、`cmake`、`bison`、`flex`、`libssl-dev`、`libncurses5-dev` 等依赖；`open-vm-tools` 那部分是给虚拟机复制粘贴用的，迁移到 WSL 后可以不装。 

在 WSL 里执行：

```bash
sudo apt install -y \
    openssh-client openssh-server \
    net-tools git cmake bison flex libssl-dev libncurses5-dev \
    picocom
```

说明：

* `openssh-client/openssh-server`：后面要用 `ssh/scp`
* `net-tools`：为了使用 `ifconfig`
* `git/cmake/bison/flex/libssl-dev/libncurses5-dev`：编译开源库和内核要用
* `picocom`：用于在 WSL 里直接打开映射过来的串口

---

# 3. 在 WSL 中确认串口并连接板卡

先看串口设备：

```bash
ls /dev/ttyUSB* /dev/ttyACM*
```

可能会看到：

```bash
/dev/ttyUSB0
/dev/ttyACM0
```

本次实际调试使用的是：

```bash
/dev/ttyACM0
```

然后打开串口终端：

```bash
sudo picocom -b 115200 /dev/ttyACM0
```

退出 `picocom`：
按 `Ctrl + A`，再按 `Ctrl + X`

之所以先准备串口，是因为原资料后面板卡联网、看 IP、`reboot` 出问题时看日志，都依赖串口侧观察；资料里的启动配置截图也能看出串口控制台是 `ttyS0,115200`。本次也已经确认，可以通过这个串口直接登录 `LoongOS` 并调试 Wi-Fi 自动连接。

---

# 4. 拉取源码

原教程后面需要用到 `LS2K0300_Library`。

在 WSL 中执行：

```bash
cd ~
git clone https://gitee.com/seekfree/LS2K0300_Library.git
```

---

# 5. 准备交叉编译器和 OpenCV 预编译库

原教程要求把这两个压缩包放到 `/opt/ls_2k0300_env/` 再解压：

* `opencv_4_10_build.tar.xz`
* `loongson-gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc1.6.tar.xz`

在 WSL 中：

```bash
cd /opt
sudo mkdir -p ls_2k0300_env
```

如果这两个文件在 Windows 下载目录，假设在 `C:\Users\你的用户名\Downloads`，那么在 WSL 里复制：

```bash
sudo cp /mnt/c/Users/你的用户名/Downloads/opencv_4_10_build.tar.xz /opt/ls_2k0300_env/
sudo cp /mnt/c/Users/你的用户名/Downloads/loongson-gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc1.6.tar.xz /opt/ls_2k0300_env/
```

然后解压：

```bash
cd /opt/ls_2k0300_env

sudo tar -xvf opencv_4_10_build.tar.xz
sudo tar -xvf loongson-gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc1.6.tar.xz
```

这一步对应原教程“ubuntu 安装 2k0300 交叉编译器和 OpenCV 库”的内容，只是把虚拟机复制文件改成了从 Windows 挂载盘 `/mnt/c/...` 复制。

---

# 6. 先让板卡联网

原资料给了两种方式：手动连接 Wi-Fi，以及在 `/etc/rc.local` 里设置开机自动连接。命令分别是 `wpa_supplicant` 和 `udhcpc`，而且自动连接那两条命令后面必须带 `&`，否则可能卡开机。 

## 6.1 手动连 Wi-Fi

在你刚才打开的串口终端里，进入板卡 shell 后执行：

```bash
wpa_supplicant -B -i wlan0 -c <(wpa_passphrase "你的WiFi名" "你的WiFi密码")
udhcpc -i wlan0
ifconfig
```

看 `wlan0` 的 IP，记下来。
后面我统一记作：

```bash
BOARD_IP=192.168.x.x
```

---

## 6.2 设置开机自动连 Wi-Fi（可选，但推荐）

原教程是在 `/etc/rc.local` 里加下面几行，并明确提醒要带 `&`。 

在板卡串口里：

```bash
vi /etc/rc.local
```

把原来启动热点那几行相关内容按教程删掉后，在 `do_start()` 里加入：

```bash
wpa_supplicant -B -i wlan0 -c <(wpa_passphrase "你的WiFi名" "你的WiFi密码") &
sleep 3
udhcpc -i wlan0 &
sleep 3
```

注意：

* `wpa_supplicant ... &`
* `udhcpc ... &`

这两个末尾都要有 `&`。

如果你希望配置“主 Wi-Fi + 备用 Wi-Fi”的优先级方案，可以直接使用下面这种 `wpa_supplicant` 模板：

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

* `priority` 越大越优先
* 这组配置会优先连接 `madejuele236`
* 只有 `madejuele236` 不可用时，才会考虑 `mch666`

---

# 7. 用 WSL 的 ssh/scp 和板卡互传文件

原教程后面所有“用 MobaXterm 拖文件”的地方，本质都能用 `scp -O` 替代；资料里本来就给了 `scp -O` 的写法。 

先在 WSL 测试 SSH：

```bash
ssh root@192.168.x.x
```

第一次会提示确认指纹，输入：

```bash
yes
```

退出板卡：

```bash
exit
```

---

## 7.1 测试传一个文件

原教程先传了一个 `SEEKFREE_APP` 文件做测试。

在 WSL 中：

```bash
cd ~
echo 125 > SEEKFREE_APP
scp -O SEEKFREE_APP root@192.168.x.x:/home/root/
```

然后登录板卡检查：

```bash
ssh root@192.168.x.x
ls -l /home/root/
cat /home/root/SEEKFREE_APP
```

---

# 8. 把 OpenCV 运行库传到板卡

原教程是在 Ubuntu 里把 `opencv_4_10_build` 整个目录递归传到板卡 `/home/root/`。

在 WSL 中执行：

```bash
cd /opt/ls_2k0300_env
scp -Or ./opencv_4_10_build root@192.168.x.x:/home/root/
```

传完后登录板卡，按原教程添加动态库索引：

```bash
ssh root@192.168.x.x
cd /etc
mkdir -p ld.so.conf.d
cd ld.so.conf.d
vi opencv.conf
```

文件内容写成：

```bash
/home/root/opencv_4_10_build/lib
```

保存后执行：

```bash
ldconfig
```

---

# 9. 修复 `reboot` 卡死 / panic（推荐尽早做）

原资料单独有一节“关机或重启久久派卡 panic 解决方案”，核心就是把资料里的 `aic8800_bsp.ko` 替换到板卡 `/lib/modules/4.19.190+`，并且替换后一定执行 `sync`。 

假设你的修复文件在 WSL 里这个位置：

```bash
~/LS2K0300_Library/[软件] 交叉编译工具 上位机等/旧世界WiFi驱动修复重启不panic/aic8800_bsp.ko
```

先找到它：

```bash
find ~/LS2K0300_Library -name aic8800_bsp.ko
```

然后传到板卡：

```bash
scp -O /找到的实际路径/aic8800_bsp.ko root@192.168.x.x:/lib/modules/4.19.190+
```

登录板卡执行：

```bash
ssh root@192.168.x.x
sync
```

原教程说明：没替换前直接 `reboot` 可能会卡住，需要按板上复位键；替换并 `sync` 后可缓解这个问题。

---

# 10. 编译开源库应用程序

原资料说应用工程目录在：

```bash
LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/project/user
```

并给了两种编译方式：
一是 `export PATH` + `cmake .` + `make`，二是直接 `./build.sh`。

在 WSL 中：

```bash
export PATH=/opt/ls_2k0300_env/loongson-gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc1.6/bin:$PATH

cd ~/LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/project/user
./build.sh
```

如果你想手动编译：

```bash
export PATH=/opt/ls_2k0300_env/loongson-gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc1.6/bin:$PATH

cd ~/LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/project/user
cmake .
make
```

你主要改代码的地方通常就是：

```bash
main.cpp
```

这也是原教程对 `project/user` 目录的说明。

---

# 11. 如需编译内核

原教程明确说：**如果你不改设备树或驱动，其实不一定需要自己编内核**，直接用现成内核也可以；只有需要改引脚、驱动时，再去编。

如果你要编：

```bash
export PATH=/opt/ls_2k0300_env/loongson-gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc1.6/bin:$PATH

cd ~/LS2K0300_Library/ls2k0300_linux_4.19
make -j$(nproc)
```

说明：

* 原教程示例是 `make -j12`，并提醒线程数要按主机能力来分配。
* 在 WSL 下我建议直接用：

```bash
-j$(nproc)
```

编完后会生成：

```bash
vmlinuz
```

这正是原教程说的内核文件。

---

# 12. 更新板卡内核

原教程说板卡内核放在 `/boot/vmlinuz`，更新方法就是把新编出来的 `vmlinuz` 用 `scp -O` 传到 `/boot`，然后一定 `sync`，再 `reboot`；否则可能卡 PMON 或启动失败。 

在 WSL 中执行：

```bash
cd ~/LS2K0300_Library/ls2k0300_linux_4.19
scp -O vmlinuz root@192.168.x.x:/boot
```

然后登录板卡：

```bash
ssh root@192.168.x.x
cd /boot
ls -l
sync
reboot
```

这里建议你一边开着串口，一边重启，这样如果启动失败，马上能看到日志。

---

# 13. VSCode 在 WSL 下的正确打开方式

原 PDF 后半段有一大段“Windows 用 VSCode + Remote-SSH 连接 Ubuntu 虚拟机”的步骤。那一段在 WSL 场景里可以整体替换掉，因为你现在不是“Windows → SSH 到 Ubuntu VM”，而是“Windows 直接打开 WSL 环境”。

你的做法应该改成：

1. Windows 安装 VSCode
2. 安装 **WSL** 扩展
3. 在 WSL 终端里进入工程目录
4. 执行：

```bash
code .
```

例如：

```bash
cd ~/LS2K0300_Library
code .
```

之后你就在 VSCode 里直接编辑 WSL 里的源码，再在 VSCode 终端里运行上面的编译命令即可。

---

# 14. 如果后面要改设备树

原资料后面设备树部分仍然有效，和是不是 WSL 没关系。里面有几个关键点： 

* 当前内置设备树名在 `.config` 里是：

```bash
seekfree_smart_car_pai_99
```

* 图形化配置：

```bash
export PATH=/opt/ls_2k0300_env/loongson-gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc1.6/bin:$PATH
cd ~/LS2K0300_Library/ls2k0300_linux_4.19
make menuconfig
```

* 设备树目录：

```bash
~/LS2K0300_Library/ls2k0300_linux_4.19/arch/loongarch/boot/dts/loongson
```

* 一般真正改的是：

```bash
seekfree_smart_car_pai_99.dts
```

---

# 15. 给你一份“从 update 之后开始”的最简执行顺序

直接照着跑就行：

## WSL 侧

```bash
sudo apt install -y \
    openssh-client openssh-server \
    net-tools git cmake bison flex libssl-dev libncurses5-dev \
    picocom

sudo picocom -b 115200 /dev/ttyACM0
```

## 板卡串口侧：连 Wi-Fi

```bash
wpa_supplicant -B -i wlan0 -c <(wpa_passphrase "你的WiFi名" "你的WiFi密码")
udhcpc -i wlan0
ifconfig
```

记下板卡 IP，比如 `192.168.x.x`

## WSL 侧：拉源码

```bash
cd ~
git clone https://gitee.com/seekfree/LS2K0300_Library.git
```

## WSL 侧：准备工具链和 OpenCV

```bash
cd /opt
sudo mkdir -p ls_2k0300_env

sudo cp /mnt/c/Users/你的用户名/Downloads/opencv_4_10_build.tar.xz /opt/ls_2k0300_env/
sudo cp /mnt/c/Users/你的用户名/Downloads/loongson-gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc1.6.tar.xz /opt/ls_2k0300_env/

cd /opt/ls_2k0300_env
sudo tar -xvf opencv_4_10_build.tar.xz
sudo tar -xvf loongson-gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc1.6.tar.xz
```

## WSL 侧：测试 ssh/scp

```bash
echo 125 > ~/SEEKFREE_APP
scp -O ~/SEEKFREE_APP root@192.168.x.x:/home/root/
ssh root@192.168.x.x
```

## WSL 侧：传 OpenCV 到板卡

```bash
cd /opt/ls_2k0300_env
scp -Or ./opencv_4_10_build root@192.168.x.x:/home/root/
```

## 板卡侧：添加 OpenCV 索引

```bash
cd /etc
mkdir -p ld.so.conf.d
cd ld.so.conf.d
vi opencv.conf
```

写入：

```bash
/home/root/opencv_4_10_build/lib
```

然后：

```bash
ldconfig
```

## WSL 侧：编译应用

```bash
export PATH=/opt/ls_2k0300_env/loongson-gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc1.6/bin:$PATH
cd ~/LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/project/user
./build.sh
```

## WSL 侧：如需编内核

```bash
export PATH=/opt/ls_2k0300_env/loongson-gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc1.6/bin:$PATH
cd ~/LS2K0300_Library/ls2k0300_linux_4.19
make -j$(nproc)
```

## WSL 侧：更新内核

```bash
scp -O ~/LS2K0300_Library/ls2k0300_linux_4.19/vmlinuz root@192.168.x.x:/boot
```

## 板卡侧：同步并重启

```bash
sync
reboot
```

---

你这次把 COM 口映射进 WSL 之后，教程确实可以再简化一层：**MobaXterm 基本就彻底可以不要了**，因为现在你已经同时有：

* WSL 串口：当前已验证为 `/dev/ttyACM0`
* WSL 网络工具：`ssh/scp`
* VSCode：直接 `code .` 打开 WSL 工程
