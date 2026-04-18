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

#include "zf_driver_gpio.h"

// 枚举端口电平
typedef enum {
    GPIO_LOW    = 0x00, // 定义管脚初始化电平为低      
    GPIO_HIGH   = 0x01, // 定义管脚初始化电平为高
} gpio_level_enum;

// 枚举 GPIO 引脚方向
typedef enum {
    GPIO_IN  = 0,
    GPIO_OUT = 1,
} gpio_pin_enum;

// zf_driver_gpio 设备结构体，用于表示该设备的相关信息和资源
struct zf_driver_gpio_struct {
    struct miscdevice misc;        // 杂项设备，用于简化设备注册和管理
    int32_t gpio_num;   // 引脚号
    uint8_t dir;        // 0-输入，1-输出
    uint8_t out;        // 输出值
    uint8_t in;         // 输入值
};

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     向设备写入数据，用于设置 GPIO 引脚的输出电平
// 参数说明     filp : 要打开的设备文件(文件描述符)，用于标识设备
// 参数说明     buf : 用户空间传入的数据缓冲区，包含要设置的 GPIO 电平数据
// 参数说明     cnt : 要写入的数据长度
// 参数说明     off : 相对于文件首地址的偏移，这里未实际使用
// 返回参数     ssize_t  写入的字节数，如果为负值，表示写入失败
// 使用示例     假设已打开设备文件描述符为 filp，用户空间数据缓冲区为 buf，可调用 zf_driver_gpio_write(filp, buf, sizeof(buf), NULL);
// 备注信息     函数从用户空间复制电平数据到设备结构体，根据数据设置 GPIO 引脚的电平，若过程中出现错误会输出错误信息并返回错误码。
//-------------------------------------------------------------------------------------------------------------------
static ssize_t zf_driver_gpio_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *off) {
    struct zf_driver_gpio_struct *dev = filp->private_data;
    int ret;

    // 从用户空间复制数据到内核空间
    ret = copy_from_user((uint8_t *)&dev->out, buf, cnt);
    if (ret) {
        dev_err(dev->misc.parent, "Failed to copy data from user space: %d bytes failed\n", ret);
        return -EFAULT;
    }

    // 字符转数字
    dev->out = dev->out - 0x30;

    // dev_info(dev->misc.parent, "dev->out = %d\n", dev->out);

    // 检查输入数据是否合法
    if (dev->out == GPIO_LOW || dev->out == GPIO_HIGH) 
    {
        gpio_set_value(dev->gpio_num, dev->out);
    } 
    else
    {
        dev_err(dev->misc.parent, "set gpio error, num = %d, value = %d\n", dev->gpio_num, dev->out);
    }

    return cnt;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     从设备读取数据，获取 GPIO 引脚的输入电平
// 参数说明     filp : 要打开的设备文件(文件描述符)，用于标识设备
// 参数说明     buf : 用于接收从设备读取数据的用户空间缓冲区
// 参数说明     cnt : 要读取的数据长度
// 参数说明     off : 相对于文件首地址的偏移，这里未实际使用
// 返回参数     ssize_t  读取的字节数，如果为负值，表示读取失败
// 使用示例     假设已打开设备文件描述符为 filp，用户空间数据缓冲区为 buf，可调用 zf_driver_gpio_read(filp, buf, sizeof(buf), NULL);
// 备注信息     函数读取 GPIO 引脚的当前电平，并将其复制到用户空间缓冲区，若复制失败会输出错误信息并返回错误码。
//-------------------------------------------------------------------------------------------------------------------
static ssize_t zf_driver_gpio_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
{
    struct zf_driver_gpio_struct *dev = filp->private_data;
    int ret;

    // 获取 GPIO 引脚的当前电平
    dev->in = gpio_get_value(dev->gpio_num);

    // 将数据从内核空间复制到用户空间
    ret = copy_to_user(buf, &dev->in, sizeof(dev->in));
    if (ret) 
    {
        dev_err(dev->misc.parent, "Failed to copy data to user space: %d bytes failed\n", ret);
        return -EFAULT;
    }
    
    return 0;
}

// 设备操作函数，定义了设备的文件操作接口，包含写入和读取操作
static struct file_operations zf_driver_gpio_fops = {
    .owner = THIS_MODULE,
    .write = zf_driver_gpio_write,
    .read = zf_driver_gpio_read,
};

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     platform 驱动的 probe 函数，当驱动与设备匹配后会执行，用于初始化设备相关资源
// 参数说明     pdev : platform 设备，代表与驱动匹配的设备实例
// 返回参数     int  0 表示成功，其他负值表示失败
// 使用示例     当驱动与设备匹配时，内核会自动调用此函数，无需手动调用
// 备注信息     函数首先申请设备结构体内存，然后设置杂项设备相关参数并注册，接着从设备树中获取 GPIO 信息，
//              申请 GPIO 引脚并设置其模式，最后注册 misc 设备，若过程中出现错误会释放已申请的资源并返回错误码。
//-------------------------------------------------------------------------------------------------------------------
static int zf_driver_gpio_probe(struct platform_device *pdev) {
    struct zf_driver_gpio_struct *dev;
    const char *compatible;
    int ret;

    dev_info(&pdev->dev, "zf_driver_gpio driver and device was matched!\n");

    // 申请内存
    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev) {
        dev_err(&pdev->dev, "failed to allocate memory\n");
        return -ENOMEM;
    }

    // 设置动态分配次设备号
    dev->misc.minor = MISC_DYNAMIC_MINOR;
    // 获取设备名称
    dev->misc.name = pdev->name;
    dev_info(&pdev->dev, "pdev->name = /dev/%s\n", pdev->name);

    // 设置文件操作结构体
    dev->misc.fops = &zf_driver_gpio_fops;
    dev->misc.parent = &pdev->dev;

    // 获取 compatible 属性的值并输出
    compatible = of_get_property(pdev->dev.of_node, "compatible", NULL);
    if (compatible) {
        dev_info(&pdev->dev, "Device compatible string: %s\n", compatible);

        if (strcmp(compatible, "seekfree,gpio_in") == 0) {
            dev->dir = GPIO_IN;
        } else if (strcmp(compatible, "seekfree,gpio_out") == 0) {
            dev->dir = GPIO_OUT;
        } else {
            return -ENODEV;
        }
    } else {
        dev_err(&pdev->dev, "Failed to get compatible string\n");
        return -ENODEV;
    }

    // 从设备树中获取 GPIO 信息
    dev->gpio_num = of_get_named_gpio(pdev->dev.of_node, "gpios", 0);
    if (!gpio_is_valid(dev->gpio_num)) {
        dev_err(&pdev->dev, "Failed to get GPIO from device tree\n");
        return -ENODEV;
    }

    // 申请 GPIO 引脚
    ret = gpio_request(dev->gpio_num, pdev->name);
    if (ret) {
        dev_err(&pdev->dev, "Failed to request GPIO %d\n", dev->gpio_num);
        return ret;
    }

    // 设置 GPIO 引脚 模式
    if (dev->dir == GPIO_IN) {
        gpio_direction_input(dev->gpio_num);
    } else if (dev->dir == GPIO_OUT) {
        gpio_direction_output(dev->gpio_num, 0);
    } else {
        gpio_free(dev->gpio_num);
        return -EINVAL;
    }

    // 注册 misc 设备
    ret = misc_register(&dev->misc);
    if (ret) {
        dev_err(&pdev->dev, "Failed to register misc device for GPIO\n");
        gpio_free(dev->gpio_num);
        return ret;
    }

    // 设置为私有数据
    platform_set_drvdata(pdev, dev);

    dev_info(&pdev->dev, "GPIO device initialized successfully num = %d\n", dev->gpio_num);

    return 0;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     platform 驱动的 remove 函数，在移除 platform 驱动时执行，用于释放设备相关资源
// 参数说明     pdev : platform 设备，代表要移除驱动的设备实例
// 返回参数     int  0 表示成功，其他负值表示失败
// 使用示例     当驱动被卸载时，内核会自动调用此函数，无需手动调用
// 备注信息     函数先注销 misc 设备，然后释放 GPIO 引脚，并输出相应的日志信息。
//-------------------------------------------------------------------------------------------------------------------
static int zf_driver_gpio_remove(struct platform_device *pdev) {
    struct zf_driver_gpio_struct *dev = platform_get_drvdata(pdev);

    if (dev) {
        // 注销 misc 设备
        misc_deregister(&dev->misc);
        dev_info(&pdev->dev, "Misc device deregistered\n");

        // 释放 GPIO 引脚
        gpio_free(dev->gpio_num);
        dev_info(&pdev->dev, "GPIO %d freed\n", dev->gpio_num);
    }

    return 0;
}

// 匹配列表，用于设备树匹配，指定驱动与具有特定兼容属性的设备进行匹配
static const struct of_device_id zf_driver_gpio_of_match[] = {
    { .compatible = "seekfree,gpio_in" },
    { .compatible = "seekfree,gpio_out" },
    { /* Sentinel */ }
};

// platform 驱动结构体，包含驱动的相关信息和操作函数指针
static struct platform_driver zf_driver_gpio = {
    .driver = {
        .name = "zf_driver_gpio",                    // 驱动名字，用于和设备匹配
        .of_match_table = zf_driver_gpio_of_match,   // 设备树匹配表
    },
    .probe = zf_driver_gpio_probe,
    .remove = zf_driver_gpio_remove,
};

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     驱动模块加载函数，用于注册 platform 驱动
// 参数说明     无
// 返回参数     int  0 表示成功，其他负值表示失败
// 使用示例     内核在加载驱动模块时会自动调用此函数，无需手动调用
// 备注信息     函数通过调用 platform_driver_register 函数注册驱动，若注册失败返回错误码。
//-------------------------------------------------------------------------------------------------------------------
static int __init zf_driver_gpio_init(void) {
    return platform_driver_register(&zf_driver_gpio);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     驱动模块卸载函数，用于注销已注册的 platform 驱动
// 参数说明     无
// 返回参数     无
// 使用示例     内核在卸载驱动模块时会自动调用此函数，无需手动调用
// 备注信息     函数通过调用 platform_driver_unregister 函数注销驱动。
//-------------------------------------------------------------------------------------------------------------------
static void __exit zf_driver_gpio_exit(void) {
    platform_driver_unregister(&zf_driver_gpio);
}

module_init(zf_driver_gpio_init);
module_exit(zf_driver_gpio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("seekfree_bigW");