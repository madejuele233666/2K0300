#include <linux/spi/spi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/unaligned/be_byteshift.h>

#include "zf_device_imu_core.h"
#include "zf_device_imu660ra.h"
#include "zf_device_imu963ra.h"

//-------------------------------------------------------------------------------------------------------------------
// 枚举简介     IMU扫描元素枚举
// 成员说明     INV_IMU_CHAN_ACCL_X    加速度计X轴扫描元素
// 成员说明     INV_IMU_CHAN_ACCL_Y    加速度计Y轴扫描元素
// 成员说明     INV_IMU_CHAN_ACCL_Z    加速度计Z轴扫描元素
// 成员说明     INV_IMU_CHAN_GYRO_X    陀螺仪X轴扫描元素
// 成员说明     INV_IMU_CHAN_GYRO_Y    陀螺仪Y轴扫描元素
// 成员说明     INV_IMU_CHAN_GYRO_Z    陀螺仪Z轴扫描元素
// 使用示例     enum inv_icm20608_scan my_scan_element = INV_IMU_CHAN_ACCL_X;
// 备注信息     用于标识IMU设备不同轴的扫描元素
//-------------------------------------------------------------------------------------------------------------------
enum inv_imu_chan
{
    INV_IMU_CHAN_ACC_X,
    INV_IMU_CHAN_ACC_Y,
    INV_IMU_CHAN_ACC_Z,
    INV_IMU_CHAN_GYRO_X,
    INV_IMU_CHAN_GYRO_Y,
    INV_IMU_CHAN_GYRO_Z,
    INV_IMU_CHAN_MAG_X,
    INV_IMU_CHAN_MAG_Y,
    INV_IMU_CHAN_MAG_Z,
};
//-------------------------------------------------------------------------------------------------------------------
// 宏简介     IMU通道配置宏
// 参数说明     _type       通道类型，如IIO_ACCEL（加速度计）、IIO_ANGL_VEL（陀螺仪）等
// 参数说明     _channel2   通道修饰符，如IIO_MOD_X、IIO_MOD_Y、IIO_MOD_Z等
// 参数说明     _index      扫描索引，用于确定通道在扫描序列中的位置
// 使用示例     IMU_CHAN(IIO_ACCEL, IIO_MOD_X, INV_IMU_CHAN_ACCL_X);
// 备注信息     用于快速配置IMU设备的通道信息
//-------------------------------------------------------------------------------------------------------------------
#define IMU_CHAN(_type, _channel2, _index)                    \
    {                                                             \
        .type = _type,                                        \
        .modified = 1,                                        \
        .channel2 = _channel2,                                \
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	      \
        .scan_index = _index,                                 \
        .scan_type = {                                        \
                .sign = 's',                          \
                .realbits = 16,                       \
                .storagebits = 16,                    \
                .shift = 0,                           \
                .endianness = IIO_BE,                 \
                 },                                       \
    }

// 定义IMU的通道配置数组
static const struct iio_chan_spec imu_3axis_channels[] = 
{
    // 加速度计X轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_X, INV_IMU_CHAN_ACC_X),
    // 加速度计Y轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_Y, INV_IMU_CHAN_ACC_Y),
    // 加速度计Z轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_Z, INV_IMU_CHAN_ACC_Z),
};

// 定义IMU的通道配置数组
static const struct iio_chan_spec imu_6axis_channels[] = 
{
    // 加速度计X轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_X, INV_IMU_CHAN_ACC_X),
    // 加速度计Y轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_Y, INV_IMU_CHAN_ACC_Y),
    // 加速度计Z轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_Z, INV_IMU_CHAN_ACC_Z),

    // 陀螺仪X轴通道配置
    IMU_CHAN(IIO_ANGL_VEL, IIO_MOD_X, INV_IMU_CHAN_GYRO_X),
    // 陀螺仪Y轴通道配置
    IMU_CHAN(IIO_ANGL_VEL, IIO_MOD_Y, INV_IMU_CHAN_GYRO_Y),
    // 陀螺仪Z轴通道配置
    IMU_CHAN(IIO_ANGL_VEL, IIO_MOD_Z, INV_IMU_CHAN_GYRO_Z),
};

// 定义IMU的通道配置数组
static const struct iio_chan_spec imu_9axis_channels[] = 
{
    // 加速度计X轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_X, INV_IMU_CHAN_ACC_X),
    // 加速度计Y轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_Y, INV_IMU_CHAN_ACC_Y),
    // 加速度计Z轴通道配置
    IMU_CHAN(IIO_ACCEL, IIO_MOD_Z, INV_IMU_CHAN_ACC_Z),

    // 陀螺仪X轴通道配置
    IMU_CHAN(IIO_ANGL_VEL, IIO_MOD_X, INV_IMU_CHAN_GYRO_X),
    // 陀螺仪Y轴通道配置
    IMU_CHAN(IIO_ANGL_VEL, IIO_MOD_Y, INV_IMU_CHAN_GYRO_Y),
    // 陀螺仪Z轴通道配置
    IMU_CHAN(IIO_ANGL_VEL, IIO_MOD_Z, INV_IMU_CHAN_GYRO_Z),

    // 磁力计X轴通道配置
    IMU_CHAN(IIO_MAGN, IIO_MOD_X, INV_IMU_CHAN_MAG_X),
    // 磁力计Y轴通道配置
    IMU_CHAN(IIO_MAGN, IIO_MOD_Y, INV_IMU_CHAN_MAG_Y),
    // 磁力计Z轴通道配置
    IMU_CHAN(IIO_MAGN, IIO_MOD_Z, INV_IMU_CHAN_MAG_Z),

};



//-------------------------------------------------------------------------------------------------------------------
// 函数简介     获取 IMU 加速度计数据
// 参数说明     dev             IMU设备结构体指针，指向要获取数据的设备实例
// 参数说明     axis            轴编号，指定要获取哪个轴的加速度计数据
// 参数说明     val             数据指针，用于存储获取到的加速度计数据
// 返回参数     int             数据类型标识，这里返回IIO_VAL_INT表示返回整数类型数据
// 使用示例     imu_get_acc(dev, IIO_MOD_X, &val);
// 备注信息     从设备读取指定轴的加速度计原始数据
//-------------------------------------------------------------------------------------------------------------------
static int imu_get_acc_raw (struct imu_dev_struct *dev, int axis, int *val)
{
    if(dev->imu_get_acc != NULL)
    {
        *val = dev->imu_get_acc(dev, axis);
    }
    else
    {
        dev_err(&dev->spi->dev, "dev->imu_get_acc = NULL\n");
    }

    return IIO_VAL_INT;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     获取IMU陀螺仪数据
// 参数说明     dev             IMU设备结构体指针，指向要获取数据的设备实例
// 参数说明     axis            轴编号，指定要获取哪个轴的陀螺仪数据
// 参数说明     val             数据指针，用于存储获取到的陀螺仪数据
// 返回参数     int             数据类型标识，这里返回IIO_VAL_INT表示返回整数类型数据
// 使用示例     imu_get_gyro(dev, IIO_MOD_X, &val);
// 备注信息     从设备读取指定轴的陀螺仪原始数据
//-------------------------------------------------------------------------------------------------------------------
static int imu_get_gyro_raw (struct imu_dev_struct *dev, int axis, int *val)
{
    if(dev->imu_get_gyro != NULL)
    {
        *val = dev->imu_get_gyro(dev, axis);
    }
    else
    {
        dev_err(&dev->spi->dev, "dev->imu_get_gyro = NULL\n");
    }

    return IIO_VAL_INT;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     获取IMU磁力计数据
// 参数说明     dev             IMU设备结构体指针，指向要获取数据的设备实例
// 参数说明     axis            轴编号，指定要获取哪个轴的陀螺仪数据
// 参数说明     val             数据指针，用于存储获取到的陀螺仪数据
// 返回参数     int             数据类型标识，这里返回IIO_VAL_INT表示返回整数类型数据
// 使用示例     imu_magn_gyro(dev, IIO_MOD_X, &val);
// 备注信息     从设备读取指定轴的陀螺仪原始数据
//-------------------------------------------------------------------------------------------------------------------
static int imu_get_mag_raw (struct imu_dev_struct *dev, int axis, int *val)
{
    if(dev->imu_get_mag != NULL)
    {
        *val = dev->imu_get_mag(dev, axis);
    }
    else
    {
        dev_err(&dev->spi->dev, "dev->imu_get_mag = NULL\n");
    }

    return IIO_VAL_INT;
}


static int imu_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan, int *val, int *val2, long mask)
{
	struct imu_dev_struct *dev = iio_priv(indio_dev);
	int ret = 0;
	unsigned long flags;

	switch (mask) 
    {
        case IIO_CHAN_INFO_RAW:
		// 由于规则通道组只有一个通道，所以这里需要用锁来限定一段时间只能读取一个通道的值。
            spin_lock_irqsave(&dev->read_raw_lock, flags);
            switch (chan->type) 
            {
                case IIO_ACCEL:		// 加速度计原始数据
                    ret = imu_get_acc_raw(dev, chan->channel2, val);
                    break;
                case IIO_ANGL_VEL:	// 陀螺仪原始数据
                    ret = imu_get_gyro_raw(dev, chan->channel2, val);
                    break;
                case IIO_MAGN:	    // 磁力计原始数据
                    ret = imu_get_mag_raw(dev, chan->channel2, val);
                    break;
                default:
                    ret = -EINVAL;
                    break;
            }
            spin_unlock_irqrestore(&dev->read_raw_lock, flags);

            break;
        default:
            ret = -EINVAL;
            break;
	}

    return ret;
}	

// iio_info结构体变量
static const struct iio_info imu_info = {
	.read_raw		= imu_read_raw,
	// .write_raw		= imu_write_raw,
	// .write_raw_get_fmt = &imu_write_raw_get_fmt,	/* 用户空间写数据格式 */
};

// 初始化 regmap_config 设置
static void setup_regmap_config(struct imu_dev_struct *dev)
{
    // 寄存器长度为 8 位
    dev->reg_cfg.reg_bits = 8;
    // 值长度为 8 位
    dev->reg_cfg.val_bits = 8;
    // 读掩码设置为 0X80，imu 使用 SPI 接口读的时候寄存器最高位应该为 1
    dev->reg_cfg.read_flag_mask = 0x80;
}

// 自动判断是 imu660ra、imu660rb、imu963ra
static int detect_imu_device(struct imu_dev_struct *dev, uint32 timeout_count)
{
    uint32 count = 0;

    int ret = DEV_NO_FIND;
    do {
        ret = imu660ra_init(dev);
        if (ret != DEV_NO_FIND)
        {
            dev->type = ret;
            break;
        }

        ret = imu963ra_init(dev);
        if (ret != DEV_NO_FIND) 
        {
            dev->type = ret;
            break;
        }

        if (timeout_count < (count)++) 
        {
            break;
        }
        mdelay(1);
    } while (ret == DEV_NO_FIND);

    return ret;
}

// 设置设备的读取函数
static void setup_imu_read_functions(struct imu_dev_struct *dev)
{
    switch (dev->type) {
    case DEV_IMU660RA:
        dev_info(&dev->spi->dev, "DEV_IMU660RA init success\n");
        dev->imu_get_acc = imu660ra_get_acc;
        dev->imu_get_gyro = imu660ra_get_gyro;
        break;
    case DEV_IMU660RB:
        dev_info(&dev->spi->dev, "DEV_IMU660RB init success\n");
        dev->imu_get_acc = imu963ra_get_acc;
        dev->imu_get_gyro = imu963ra_get_gyro;
        break;
    case DEV_IMU963RA:
        dev_info(&dev->spi->dev, "DEV_IMU963RA init success\n");
        dev->imu_get_acc = imu963ra_get_acc;
        dev->imu_get_gyro = imu963ra_get_gyro;
        dev->imu_get_mag = imu963ra_get_mag;
        break;
    default:
        break;
    }
}

// 设置 iio_dev 的其他成员变量
static void setup_iio_dev(struct iio_dev *indio_dev, struct imu_dev_struct *dev)
{
    // 设置 iio_dev 的父设备为当前 spi 设备
    indio_dev->dev.parent = &dev->spi->dev;
    // 关联 iio_dev 的操作函数信息
    indio_dev->info = &imu_info;
    // 设置 iio_dev 的名称
    switch (dev->type) {
    case DEV_IMU660RA:
        indio_dev->name = "IMU660RA";
        break;
    case DEV_IMU660RB:
        indio_dev->name = "IMU660RB";
        break;
    case DEV_IMU963RA:
        indio_dev->name = "IMU963RA";
        break;
    default:
        break;
    }

    // 设置工作模式为直接模式，提供 sysfs 接口
    indio_dev->modes = INDIO_DIRECT_MODE;

    if (dev->type == DEV_IMU660RA || dev->type == DEV_IMU660RB) {
        // 关联 iio_dev 的通道配置
        indio_dev->channels = imu_6axis_channels;
        // 设置 iio_dev 的通道数量
        indio_dev->num_channels = ARRAY_SIZE(imu_6axis_channels);
    } else if (dev->type == DEV_IMU963RA) {
        // 关联 iio_dev 的通道配置
        indio_dev->channels = imu_9axis_channels;
        // 设置 iio_dev 的通道数量
        indio_dev->num_channels = ARRAY_SIZE(imu_9axis_channels);
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     驱动初始化
// 参数说明     spi  SPI设备结构体指针
// 返回参数     int  0 - 初始化成功; 负数 - 初始化失败
// 使用示例     由Linux自动调用
// 备注信息     该函数用于初始化IMU设备驱动，包括分配资源、注册字符设备等操作
//-------------------------------------------------------------------------------------------------------------------
static int imu_probe(struct spi_device *spi)
{
    int ret;
    struct imu_dev_struct *dev;
    struct iio_dev *indio_dev;


    // 打印调试信息，表明进入驱动初始化流程
    printk("imu_probe\r\n");

    // 1、申请 iio_dev 内存
    indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*dev));
    if (!indio_dev) {
        dev_err(&spi->dev, "Failed to allocate iio_dev memory \n");
        return -ENOMEM;
    }

    // 2、获取 imu_dev 结构体地址
    dev = iio_priv(indio_dev);
    dev->spi = spi;
    // 将 indio_dev 设置为 spi->dev 的 driver_data，方便后续通过 spi 设备获取 iio_dev
    spi_set_drvdata(spi, indio_dev);
    // 初始化自旋锁，用于保护对设备的并发访问
    spin_lock_init(&dev->read_raw_lock);

    // 初始化 regmap_config 设置
    setup_regmap_config(dev);

    // 初始化 spi 接口的 regmap
    dev->regmap = regmap_init_spi(spi, &dev->reg_cfg);
    if (IS_ERR(dev->regmap))
    {
        dev_err(&spi->dev, "Failed to initialize regmap for \n");
        ret = PTR_ERR(dev->regmap);
        return ret;
    }

    // 保存 spi 设备指针
    dev->spi = spi;

    // 初始化 spi_device，设置模式为 0
    spi->mode = SPI_MODE_0;
    // 应用 SPI 设备的设置
    ret = spi_setup(spi);
    if (ret < 0) 
    {
        dev_err(&spi->dev, "Failed to setup SPI device for\n");
        regmap_exit(dev->regmap);
        return ret;
    }

    // 自动判断是 imu660ra、imu660rb、imu963ra
    ret = detect_imu_device(dev, IMU_TIMEOUT_COUNT);
    if (ret == DEV_NO_FIND) {
        
        dev_err(&spi->dev, "error : no find seekfree imu device.\n");
        dev_err(&spi->dev, "error : no find seekfree imu device.\n");
        dev_err(&spi->dev, "error : no find seekfree imu device.\n");

        regmap_exit(dev->regmap);
        return -EPERM;
    }

    // 设置设备的读取函数ccc
    setup_imu_read_functions(dev);

    // 3、设置 iio_dev 的其他成员变量
    setup_iio_dev(indio_dev, dev);

    // 4、注册 iio_dev
    ret = iio_device_register(indio_dev);
    if (ret < 0) {
        dev_err(&spi->dev, "Failed to register iio device \n");
        regmap_exit(dev->regmap);
        dev->regmap = NULL;
        return -EPERM;
    }

    // 初始化成功，返回 0
    return 0;

}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     驱动移除
// 参数说明     spi  SPI设备结构体指针，用于获取设备驱动数据
// 返回参数     int  通常返回0表示移除成功
// 使用示例     由Linux内核自动调用，当设备移除时触发
// 备注信息     该函数负责释放驱动初始化时分配的各种资源，包括字符设备、设备号、设备类、设备实例和寄存器映射
//-------------------------------------------------------------------------------------------------------------------
static int imu_remove(struct spi_device *spi)
{
    // 从SPI设备中获取驱动数据
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct imu_dev_struct *dev = iio_priv(indio_dev);
	
	/* 1、删除regmap */ 
    if (dev->regmap != NULL) 
    {
        regmap_exit(dev->regmap);
        dev->regmap = NULL; 
    }

    if (indio_dev != NULL) 
    {
        /* 2、注销IIO */
        iio_device_unregister(indio_dev);
        indio_dev = NULL;
    }

    // 打印移除成功信息
    dev_err(&spi->dev, "imu_remove: Successfully removed driver for device \n");

    return 0;
}

// 设备树匹配表
static const struct of_device_id imu_of_match[] = {
	{ .compatible = "seekfree,imu" },
	{ /* Sentinel */ }
};

// IMU驱动结构体
static struct spi_driver imu_driver = {
	.probe = imu_probe,
	.remove = imu_remove,
	.driver = {
			.owner = THIS_MODULE,
		   	.name = "imu",
		   	.of_match_table = imu_of_match,
		   },
};

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     驱动入口函数
// 参数说明     void
// 返回参数     int          
// 使用示例     由linux自动调用
// 备注信息     
//-------------------------------------------------------------------------------------------------------------------
static int __init imu_driver_init(void)
{
	return spi_register_driver(&imu_driver);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     驱动出口函数
// 参数说明     void
// 返回参数     int          
// 使用示例     由linux自动调用
// 备注信息     
//-------------------------------------------------------------------------------------------------------------------
static void __exit imu_driver_exit(void)
{
	spi_unregister_driver(&imu_driver);
}

module_init(imu_driver_init);
module_exit(imu_driver_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("seekfree_bigW");
