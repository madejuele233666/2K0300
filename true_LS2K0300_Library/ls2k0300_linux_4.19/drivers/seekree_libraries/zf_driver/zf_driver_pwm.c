/*********************************************************************************************************************
* LS2K0300 Opensourec Library 即（LS2K0300 开源库）是一个基于官方 SDK 接口的第三方开源库
* Copyright (c) 2022 SEEKFREE 逐飞科技
*
* 本文件是LS2K0300 开源库的一部分
*
* LS2K0300 开源库 是免费软件
* 您可以根据自由软件基金会发布的 GPL（GNU General Public License，即 GNU通用公共许可证）的条款
* 即 GPL 的第3版（即 GPL3.0）或（您选择的）任何后来的版本，重新发布和/或修改它
*
* 本开源库的发布是希望它能发挥作用，但并未对其作任何的保证
* 甚至没有隐含的适销性或适合特定用途的保证
* 更多细节请参见 GPL
*
* 您应该在收到本开源库的同时收到一份 GPL 的副本
* 如果没有，请参阅<https://www.gnu.org/licenses/>
*
* 额外注明：
* 本开源库使用 GPL3.0 开源许可证协议 以上许可申明为译文版本
* 许可申明英文版在 libraries/doc 文件夹下的 GPL3_permission_statement.txt 文件中
* 许可证副本在 libraries 文件夹下 即该文件夹下的 LICENSE 文件
* 欢迎各位使用并传播本程序 但修改内容时必须保留逐飞科技的版权声明（即本声明）
*
* 文件名称          zf_common_typedef
* 公司名称          成都逐飞科技有限公司
* 适用平台          LS2K0300
* 店铺链接          https://seekfree.taobao.com/
*
* 修改记录
* 日期              作者           备注
* 2025-02-27        大W            first version
********************************************************************************************************************/

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of_gpio.h>
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <linux/irq.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/miscdevice.h>
#include <linux/of.h>
#include <linux/pwm.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include "zf_driver_pwm.h"

// PWM数据结构体，用于存储PWM相关的参数信息
// 包含频率、占空比、占空比最大值、高电平时间、周期时间、时钟频率等参数
struct pwm_control
{
    uint32 freq;       // PWM频率
    uint32 duty;       // PWM占空比
    uint32 duty_max;   // PWM占空比最大值
    uint32 duty_ns;    // PWM高电平时间（纳秒）
    uint32 period_ns;  // PWM周期时间（纳秒）
    uint32 clk_freq;   // 时钟频率
};

// encoder_dev设备结构体，用于表示该设备的相关信息和资源
struct zf_driver_pwm_struct 
{
    struct miscdevice misc;        // 杂项设备，用于简化设备注册和管理
    struct pwm_device *pwm_dev;    // PWM设备指针，指向PWM设备实例
    struct pwm_control ctl;        // PWM数据结构体，存储PWM相关参数
};


//-------------------------------------------------------------------------------------------------------------------
// 函数简介     向设备写入数据，主要用于设置PWM的占空比参数
// 参数说明     filp : 要打开的设备文件(文件描述符)，用于标识设备
// 参数说明     buf : 返回给用户空间的数据缓冲区，包含要写入的占空比数据
// 参数说明     cnt : 要读取的数据长度，这里应该是占空比数据的长度
// 参数说明     offt : 相对于文件首地址的偏移，这里未实际使用
// 返回参数     ssize_t  读取的字节数，如果为负值，表示读取失败（这里实际是写入操作，失败返回负值）
// 使用示例     假设已打开设备文件描述符为filp，用户空间数据缓冲区为buf，可调用zf_driver_pwm_write(filp, buf, sizeof(buf), NULL);
// 备注信息     函数从用户空间复制占空比数据到设备结构体，然后计算高电平时间和周期时间，
//              并配置PWM设备的参数，若过程中出现错误会输出错误信息并返回错误码。
//-------------------------------------------------------------------------------------------------------------------
static ssize_t zf_driver_pwm_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *off) 
{
    struct zf_driver_pwm_struct *dev = filp->private_data;
    int ret;

    ret = copy_from_user((uint8 *)&dev->ctl.duty, buf, cnt);
    if (ret < 0) 
    {
        // 使用 dev->misc.parent 获取设备指针
        dev_err(dev->misc.parent, "kernel write failed!\n");
        return ret;
    }

    // 计算高电平时间和周期时间
    dev->ctl.duty_ns = (u64)dev->ctl.duty * dev->ctl.period_ns / dev->ctl.duty_max ;

    // 配置 PWM 参数
    ret = pwm_config(dev->pwm_dev, dev->ctl.duty_ns, dev->ctl.period_ns);
    if (ret) 
    {
        // 使用 dev->misc.parent 获取设备指针
        dev_err(dev->misc.parent, "Failed to pwm_config device: %d\n", ret);
        return ret;
    }

    return 0;
}

static ssize_t zf_driver_pwm_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
{
    struct zf_driver_pwm_struct *dev = filp->private_data;
    int ret;

    // 将数据从内核空间复制到用户空间
    ret = copy_to_user(buf, (uint8_t *)&dev->ctl, sizeof(dev->ctl));
    if (ret) 
    {
        dev_err(dev->misc.parent, "Failed to copy data to user space: %d bytes failed\n", ret);
        return ret;
    }
    
    return 0;
}

// 设备操作函数，定义了设备的文件操作接口，这里只实现了写操作
static struct file_operations zf_driver_pwm_fops = 
{
    .owner = THIS_MODULE,
    .write = zf_driver_pwm_write,
    .read = zf_driver_pwm_read,
};

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     platform驱动的probe函数，当驱动与设备匹配后会执行，用于初始化设备相关资源
// 参数说明     pdev : platform设备，代表与驱动匹配的设备实例
// 返回参数     int  0表示成功，其他负值表示失败
// 使用示例     当驱动与设备匹配时，内核会自动调用此函数，无需手动调用
// 备注信息     函数首先申请设备结构体内存，然后设置杂项设备相关参数并注册，
//              接着获取PWM设备，读取设备树中的PWM参数并计算相关时间参数，
//              最后配置并使能PWM设备，若过程中出现错误会注销杂项设备并返回错误码。
//-------------------------------------------------------------------------------------------------------------------
static int zf_driver_pwm_probe(struct platform_device *pdev) 
{
    struct zf_driver_pwm_struct *dev;
    struct device_node *np;
    int ret;

    dev_info(&pdev->dev, "zf_driver_pwm driver and device was matched!\n");

    // 申请内存
    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
    {
        dev_err(&pdev->dev, "failed to allocate memory\n");
        return -ENOMEM;
    }

    // 设置链表节点
    np = pdev->dev.of_node;
    // 设置动态分配次设备号
    dev->misc.minor = MISC_DYNAMIC_MINOR;
    // 获取设备名称
    dev->misc.name = pdev->name;
    dev_info(&pdev->dev, "pdev->name = /dev/%s\n", pdev->name);

    // 设置文件操作结构体
    dev->misc.fops = &zf_driver_pwm_fops;
    dev->misc.parent = &pdev->dev;

    // 注册 misc 设备
    ret = misc_register(&dev->misc);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to register misc device for LED\n");
        return ret;
    }

    // 设置为私有数据
    platform_set_drvdata(pdev, dev);

    // 获取 PWM 设备
    dev->pwm_dev = devm_of_pwm_get(&pdev->dev, np, NULL);
    if (IS_ERR(dev->pwm_dev)) 
    {
        ret = PTR_ERR(dev->pwm_dev);
        dev_err(&pdev->dev, "Failed to get PWM device from device tree: %d\n", ret);
        misc_deregister(&dev->misc);
        return ret;
    }

    // 读取频率
    ret = of_property_read_s32(np, "freq", &dev->ctl.freq);
    if (ret < 0) 
    {
        dev_err(&pdev->dev, "Failed to read freq property: %d\n", ret);
        misc_deregister(&dev->misc);
        return ret;
    }
    dev_info(&pdev->dev, "setting pwm freq = %d\n", dev->ctl.freq);

    // 读取占空比
    ret = of_property_read_s32(np, "duty", &dev->ctl.duty);
    if (ret < 0) 
    {
        dev_err(&pdev->dev, "Failed to read duty property: %d\n", ret);
        misc_deregister(&dev->misc);
        return ret;
    }
    dev_info(&pdev->dev, "setting pwm duty = %d\n", dev->ctl.duty);

    // 读取PWM最大值
    ret = of_property_read_s32(np, "duty_max", &dev->ctl.duty_max);
    if (ret < 0) 
    {
        dev_err(&pdev->dev, "Failed to read duty_max property: %d\n", ret);
        misc_deregister(&dev->misc);
        return ret;
    }
    dev_info(&pdev->dev, "setting pwm duty_max = %d\n", dev->ctl.duty_max);

    // 释放节点引用
    of_node_put(np);

    dev->ctl.clk_freq = 1 * 1000 * 1000 * 1000;

    // 计算高电平时间和周期时间
    dev->ctl.period_ns = dev->ctl.clk_freq / dev->ctl.freq;
    dev->ctl.duty_ns = (u64)dev->ctl.duty * dev->ctl.period_ns / dev->ctl.duty_max ;

    // 配置 PWM 参数
    ret = pwm_config(dev->pwm_dev, dev->ctl.duty_ns, dev->ctl.period_ns);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to pwm_config device: %d\n", ret);
        misc_deregister(&dev->misc);
        return ret;
    }

    // 设置PWM极性
    ret = pwm_set_polarity(dev->pwm_dev, PWM_POLARITY_INVERSED);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to pwm_set_polarity device: %d\n", ret);
        misc_deregister(&dev->misc);
        return ret;
    }

    // 使能 PWM 输出
    ret = pwm_enable(dev->pwm_dev);
    if (ret) 
    {
        dev_err(&pdev->dev, "Failed to pwm_enable device: %d\n", ret);
        misc_deregister(&dev->misc);
        return ret;
    }

    dev_info(&pdev->dev, "PWM device initialized successfully\n");

    return 0;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     platform驱动的remove函数，在移除platform驱动时执行，用于释放设备相关资源
// 参数说明     pdev : platform设备，代表要移除驱动的设备实例
// 返回参数     int  0表示成功，其他负值表示失败
// 使用示例     当驱动被卸载时，内核会自动调用此函数，无需手动调用
// 备注信息     函数先禁用PWM设备，然后注销杂项设备，并输出设备注销的信息。
//-------------------------------------------------------------------------------------------------------------------
static int zf_driver_pwm_remove(struct platform_device *pdev) 
{
    struct zf_driver_pwm_struct *dev = platform_get_drvdata(pdev);

    // 禁用 PWM
    if (dev->pwm_dev) 
    {
        pwm_config(dev->pwm_dev, 0, 1000000);
        pwm_disable(dev->pwm_dev);
    }

    // 注销misc设备
    misc_deregister(&dev->misc);

    dev_info(&pdev->dev, "PWM device and misc device deregistered\n");

    return 0;
}

// 匹配列表，用于设备树匹配，指定驱动与具有特定兼容属性的设备进行匹配
static const struct of_device_id zf_driver_pwm_of_match[] = 
{
    { .compatible = "seekfree,pwm" },
    { /* Sentinel */ }
};

// platform驱动结构体，包含驱动的相关信息和操作函数指针
static struct platform_driver zf_driver_pwm = 
{
    .driver = {
        .name = "zf_driver_pwm",                    // 驱动名字，用于和设备匹配
        .of_match_table = zf_driver_pwm_of_match,   // 设备树匹配表
    },
    .probe = zf_driver_pwm_probe,
    .remove = zf_driver_pwm_remove,
};

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     驱动模块加载函数，用于注册platform驱动
// 参数说明     无
// 返回参数     int  0表示成功，其他负值表示失败
// 使用示例     内核在加载驱动模块时会自动调用此函数，无需手动调用
// 备注信息     函数通过调用platform_driver_register函数注册驱动，若注册失败返回错误码。
//-------------------------------------------------------------------------------------------------------------------
static int __init zf_driver_pwm_init(void) 
{
    return platform_driver_register(&zf_driver_pwm);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     驱动模块卸载函数，用于注销已注册的platform驱动
// 参数说明     无
// 返回参数     无
// 使用示例     内核在卸载驱动模块时会自动调用此函数，无需手动调用
// 备注信息     函数通过调用platform_driver_unregister函数注销驱动。
//-------------------------------------------------------------------------------------------------------------------
static void __exit zf_driver_pwm_exit(void) 
{
    platform_driver_unregister(&zf_driver_pwm);
}

module_init(zf_driver_pwm_init);
module_exit(zf_driver_pwm_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("seekfree_bigW");
