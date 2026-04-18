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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>


#include "zf_driver_encoder.h"

// PWM寄存器
struct pwm_reg_struct
{
	u32	irq;
	u32	ctrl_reg;
	u32	low_buffer_reg;
	u32	full_buffer_reg;
	u64	clock_frequency;
};

// encoder_dev设备结构体
struct encoder_dev_struct
{
	struct miscdevice misc;		// 杂项设备
	int dir_pin;				// 方向引脚
	void __iomem *mmio_base;	// 映射地址
	struct pwm_reg_struct reg;	// 寄存器
};


 /*
 * @description : 从设备读取数据
 * @param - filp : 要打开的设备文件(文件描述符)
 * @param - buf : 返回给用户空间的数据缓冲区
 * @param - cnt : 要读取的数据长度
 * @param - offt : 相对于文件首地址的偏移
 * @return : 读取的字节数，如果为负值，表示读取失败
 */
static ssize_t encoder_dir_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
 {
	struct encoder_dev_struct *dev = filp->private_data;

 	// unsigned char data[4] = {0};
 	int err = 0;
	int count = 0;
	int dir = 0;
	int32_t value = 0;
	
	// 读取编码器数据
	dev->reg.full_buffer_reg = (int)readl(dev->mmio_base + FULL_BUFFER);
	dev->reg.low_buffer_reg = (int)readl(dev->mmio_base + LOW_BUFFER);

	// 读取方向
	dir = gpio_get_value(dev->dir_pin);
	if(dir < 0)
	{
		printk("encoder_dir_read dir error\r\n");
		return -1;
	}

	if(dev->reg.low_buffer_reg == 0)
	{
		value = 0;
	}
	else
	{
		count = dev->reg.full_buffer_reg * ((dir << 1) - 1);
		value = 100000000 / count;
		// value = dev->reg.clock_frequency / count;
		value = value >> 6;
	}

	// 上传数据
 	err = copy_to_user(buf, (uint8_t *)&value, sizeof(value));
	if(err < 0)
	{
		printk("err copy_to_user\r\n");
		return -1;
	}

	// 可以通过这个寄存器来判断是否有数据。
	// 如果没有数据这里设置0 读取出来就是0
	dev->reg.low_buffer_reg = 0;
	writel(dev->reg.low_buffer_reg,  dev->mmio_base + LOW_BUFFER);

 	return 0;
 }
// 设备操作函数 
static struct file_operations encoder_dir_fops = 
{
	.owner = THIS_MODULE,
	// .open = encoder_dir_open,
 	.read = encoder_dir_read,
};

/*
 * @description		: platform驱动的probe函数，当驱动与
 * 					  设备匹配以后此函数就会执行
 * @param - dev 	: platform设备
 * @return 			: 0，成功;其他负值,失败
 */
static int encoder_dir_probe(struct platform_device *pdev)
{	
	struct encoder_dev_struct *dev; 
	struct device_node *np = pdev->dev.of_node;
	struct resource *mem;
	int ret;
	u32 clk;


	printk("encoder driver and device was matched!\r\n");

	// 申请内存
	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if(!dev){
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	// 设置动态分配次设备号
	dev->misc.minor = MISC_DYNAMIC_MINOR;
	// 获取设备名称
	if (!(of_property_read_string(np, "dev-name", &dev->misc.name)))
	{
		dev_info(&pdev->dev, "find dev-name %s\n", dev->misc.name);
		dev_info(&pdev->dev, "kasprintf = %s\n", kasprintf(GFP_KERNEL, "encoder_%d", pdev->id));
	}
	else
	{
		dev_err(&pdev->dev, "of_property_read_u32 frequency\n");
		return -EINVAL;
	}
	// 设置文件操作结构体
   	dev->misc.fops = &encoder_dir_fops;
    dev->misc.parent = &pdev->dev;

    // 注册 misc 设备
    ret = misc_register(&dev->misc);
    if (ret) {
        kfree(dev->misc.name);
        dev_err(&pdev->dev, "Failed to register misc device for LED\n");
        return ret;
    }

	// 设置为私有数据
    platform_set_drvdata(pdev, dev);

	// 获取DIR引脚
	dev->dir_pin = of_get_named_gpio(np, "encoder-dir-gpio", 0);
	if (dev->dir_pin < 0) {
		printk("can't get encoder-gpio\r\n");
		return -EINVAL;
	}

	// 申请DIR引脚
	if(gpio_request(dev->dir_pin, "encoder_dir_gpio") < 0)
	{
		dev_err(&pdev->dev, "gpio_request\n");
		return -EINVAL;
	}

	// 设置引脚输入
	if(gpio_direction_input(dev->dir_pin)< 0) 
	{
		dev_err(&pdev->dev, "gpio_direction_input\n");
		return -EINVAL;
		
	}
	printk("encoder dir pin = %d\r\n", dev->dir_pin);

	// 获取寄存器地址
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if(!mem){
		dev_err(&pdev->dev, "no mem resource?\n");
		return -ENODEV;
	}
	// 映射寄存器
	dev->mmio_base = devm_ioremap_resource(&pdev->dev, mem);
	if(!dev->mmio_base){
		dev_err(&pdev->dev, "mmio_base is null\n");
		return -ENOMEM;
	}
	// 初始化编码器
	printk("mmio = 0x%p\r\n", (u32 *)dev->mmio_base);

	dev->reg.ctrl_reg = CTRL_EN | CTRL_CAPTE;
	dev->reg.low_buffer_reg = 0;
	dev->reg.full_buffer_reg = 0;

	writel(dev->reg.full_buffer_reg, dev->mmio_base + LOW_BUFFER);
	writel(dev->reg.low_buffer_reg, dev->mmio_base + FULL_BUFFER);
	writel(dev->reg.ctrl_reg, dev->mmio_base + CTRL);

	// 获取频率
	if (!(of_property_read_u32(np, "clock-frequency", &clk)))
	{
		dev->reg.clock_frequency = clk;
	}
	else
	{
		dev_err(&pdev->dev, "of_property_read_u32 frequency\n");
		return -EINVAL;
	}
	dev_info(&pdev->dev, "dev->clock_frequency=%llu", dev->reg.clock_frequency);


	return 0;
}

/*
 * @description		: platform驱动的remove函数，移除platform驱动的时候此函数会执行
 * @param - dev 	: platform设备
 * @return 			: 0，成功;其他负值,失败
 */
static int encoder_dir_remove(struct platform_device *pdev)
{
	struct encoder_dev_struct *dev = platform_get_drvdata(pdev);

	// 卸载驱动的时候设置DIR引脚电平
	gpio_set_value(dev->dir_pin, 1); 	
	// 释放IO
	gpio_free(dev->dir_pin);
	// 注销misc设备
    misc_deregister(&dev->misc);

	return 0;
}

/* 匹配列表 */
static const struct of_device_id encoder_dir_of_match[] = {
	{ .compatible = "seekfree,encoder" },
	{ /* Sentinel */ }
};

/* platform驱动结构体 */
static struct platform_driver encoder_dir_driver = {
	.driver		= {
		.name	= "zf_driver_encoder",			/* 驱动名字，用于和设备匹配 */
		.of_match_table	= encoder_dir_of_match, /* 设备树匹配表 		 */
	},
	.probe		= encoder_dir_probe,
	.remove		= encoder_dir_remove,
};
		
/*
 * @description	: 驱动模块加载函数
 * @param 		: 无
 * @return 		: 无
 */
static int __init encoder_dir_driver_init(void)
{
	return platform_driver_register(&encoder_dir_driver);
}

/*
 * @description	: 驱动模块卸载函数
 * @param 		: 无
 * @return 		: 无
 */
static void __exit encoder_dir_driver_exit(void)
{
	platform_driver_unregister(&encoder_dir_driver);
}

module_init(encoder_dir_driver_init);
module_exit(encoder_dir_driver_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("seekfree_bigW");



