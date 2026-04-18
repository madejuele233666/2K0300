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

#include "zf_device_imu660ra.h"
#include "zf_device_config.h"

#include "zf_device_imu_core.h"

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU660RA 读寄存器
// 参数说明     dev             IMU660RA设备结构体指针，指向要操作的设备实例
// 参数说明     reg             寄存器地址，指定要读取的寄存器位置
// 返回参数     uint8           从指定寄存器读取到的数据
// 使用示例     imu660ra_read_register(dev, IMU660RA_WHO_AM_I);
// 备注信息     内部调用，用于驱动内部对设备寄存器的读取操作
//-------------------------------------------------------------------------------------------------------------------
static uint8_t imu660ra_read_register(struct imu_dev_struct *dev, uint8_t reg)
{
    uint8_t ret;
    uint8_t data[2];

    // 使用寄存器映射接口批量读取寄存器数据
    ret = regmap_bulk_read(dev->regmap, reg, data, 2);

    return data[1]; 
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU660RA 写寄存器
// 参数说明     dev             IMU660RA设备结构体指针，指向要操作的设备实例
// 参数说明     reg             寄存器地址，指定要写入的寄存器位置
// 参数说明     buff            数据缓冲区指针，包含要写入寄存器的数据
// 参数说明     len             数据长度，指定要写入的数据字节数
// 返回参数     void
// 使用示例     imu660ra_write_registers(dev, IMU660RA_PWR_MGMT_1, buff, len);
// 备注信息     内部调用，用于驱动内部对设备寄存器的批量写入操作
//-------------------------------------------------------------------------------------------------------------------
static void imu660ra_write_registers(struct imu_dev_struct *dev, uint8_t reg, uint8_t *buff, uint32_t len)
{
    // 使用寄存器映射接口批量写入寄存器数据
    regmap_bulk_write(dev->regmap,  reg, buff, len);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU660RA 写寄存器
// 参数说明     dev             IMU660RA设备结构体指针，指向要操作的设备实例
// 参数说明     reg             寄存器地址，指定要写入的寄存器位置
// 参数说明     value           数据，要写入指定寄存器的值
// 返回参数     void
// 使用示例     imu660ra_write_register(dev, IMU660RA_PWR_MGMT_1, 0x80);
// 备注信息     内部调用，用于驱动内部对设备单个寄存器的写入操作
//-------------------------------------------------------------------------------------------------------------------
static void imu660ra_write_register(struct imu_dev_struct *dev, uint8_t reg, uint8_t value)
{
    // 使用寄存器映射接口写入单个寄存器数据
    regmap_write(dev->regmap, reg, value);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU660RA 读数据
// 参数说明     dev             IMU660RA设备结构体指针，指向要操作的设备实例
// 参数说明     reg             寄存器地址，指定要读取数据的起始寄存器位置
// 参数说明     data            数据缓冲区指针，用于存储读取到的数据
// 参数说明     len             数据长度，指定要读取的数据字节数
// 返回参数     void
// 使用示例     imu660ra_read_registers(dev, IMU660RA_ACCEL_XOUT_H, dat, 6);
// 备注信息     内部调用，用于驱动内部从设备连续寄存器读取数据
//-------------------------------------------------------------------------------------------------------------------
static void imu660ra_read_registers(struct imu_dev_struct *dev, uint8_t reg, uint8_t *data, uint32_t len)
{
    // 使用寄存器映射接口批量读取连续寄存器数据
    regmap_bulk_read(dev->regmap, reg, data, len);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU660RA 自检
// 参数说明     dev             IMU660RA设备结构体指针，指向要进行自检的设备实例
// 返回参数     uint8           1-自检失败 0-自检成功
// 使用示例     imu660ra_self_check(dev);
// 备注信息     内部调用，用于在设备初始化时检查设备是否正常工作
//-------------------------------------------------------------------------------------------------------------------
uint8_t imu660ra_self_check (struct imu_dev_struct *dev)
{
    uint8_t dat = 0, return_state = 0;

    dat = imu660ra_read_register(dev, IMU660RA_CHIP_ID);

    // 读取设备ID是否等于0X24，如果不是0X24则认为没检测到设备
    if(0x24 != dat)
    {
        return_state = 1;
    }                                                        

    return return_state;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     获取 IMU660RA 加速度计数据
// 参数说明     dev             IMU660RA设备结构体指针，指向要获取数据的设备实例
// 参数说明     axis            轴编号，指定要获取哪个轴的加速度计数据
// 参数说明     val             数据指针，用于存储获取到的加速度计数据
// 返回参数     int             数据类型标识，这里返回IIO_VAL_INT表示返回整数类型数据
// 使用示例     imu660ra_get_acc(dev, IIO_MOD_X, &val);
// 备注信息     从设备读取指定轴的加速度计原始数据
//-------------------------------------------------------------------------------------------------------------------
int16 imu660ra_get_acc (struct imu_dev_struct *dev, int axis)
{
    // SPI读取第一个地址为空
    uint8_t dat[3];

    // axis_x = 1
    // axis_y = 2
    // axis_z = 3

    // 从指定寄存器地址读取加速度计数据
    imu660ra_read_registers(dev, IMU660RA_ACC_ADDRESS + (axis - IIO_MOD_X) * 2, dat, 3);

    // 将读取到的两个字节数据合并为一个16位整数
    return (int16)(((int16)dat[2] << 8 | dat[1]));
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     获取IMU660RA陀螺仪数据
// 参数说明     dev             IMU660RA设备结构体指针，指向要获取数据的设备实例
// 参数说明     axis            轴编号，指定要获取哪个轴的陀螺仪数据
// 参数说明     val             数据指针，用于存储获取到的陀螺仪数据
// 返回参数     int             数据类型标识，这里返回IIO_VAL_INT表示返回整数类型数据
// 使用示例     imu660ra_get_gyro(dev, IIO_MOD_X, &val);
// 备注信息     从设备读取指定轴的陀螺仪原始数据
//-------------------------------------------------------------------------------------------------------------------
int16 imu660ra_get_gyro (struct imu_dev_struct *dev, int axis)
{
    uint8_t dat[3];

    // axis_x = 1
    // axis_y = 2
    // axis_z = 3

    // 从指定寄存器地址读取陀螺仪数据
    imu660ra_read_registers(dev, IMU660RA_GYRO_ADDRESS + (axis - IIO_MOD_X) * 2, dat, 3);

    // 将读取到的两个字节数据合并为一个16位整数
    return (int16_t)(((uint16_t)dat[2] << 8 | dat[1]));
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     初始化 IMU660RA
// 参数说明     dev             IMU660RA设备结构体指针，指向要初始化的设备实例
// 返回参数     uint8           1-初始化失败 0-初始化成功
// 使用示例     imu660ra_init(dev);
// 备注信息     对IMU660RA设备进行初始化操作，包括自检、配置寄存器
//-------------------------------------------------------------------------------------------------------------------
uint8 imu660ra_init(struct imu_dev_struct *dev)
{
    uint8 ret = DEV_IMU660RA;

	do
    {
		if(imu660ra_self_check(dev))
        {
            // 如果程序在输出了断言信息 并且提示出错位置在这里
            // 那么就是 IMU660RA 自检出错并超时退出了
            // 检查一下接线有没有问题 如果没问题可能就是坏了
			// printk(KERN_WARNING "imu660ra self check error.\r\n");
            ret = DEV_NO_FIND;
            break;
        }

		imu660ra_write_register(dev, IMU660RA_PWR_CONF, 0x00);                       // 关闭高级省电模式
        mdelay(1);
        imu660ra_write_register(dev, IMU660RA_INIT_CTRL, 0x00);                      // 开始对模块进行初始化配置
        imu660ra_write_registers(dev, IMU660RA_INIT_DATA, (uint8 *)imu660ra_config_file, sizeof(imu660ra_config_file));   // 输出配置文件
        imu660ra_write_register(dev, IMU660RA_INIT_CTRL, 0x01);                      // 初始化配置结束
        mdelay(20);
        if(1 != imu660ra_read_register(dev, IMU660RA_INT_STA))                       // 检查是否配置完成
        {
            // 如果程序在输出了断言信息 并且提示出错位置在这里
            // 那么就是 IMU660RA 配置初始化文件出错了
            // 检查一下接线有没有问题 如果没问题可能就是坏了
            // printk(KERN_ERR "imu660ra init error.");
            ret = DEV_NO_FIND;
            break;
        }
        imu660ra_write_register(dev, IMU660RA_PWR_CTRL, 0x0E);                       // 开启性能模式  使能陀螺仪、加速度、温度传感器
        imu660ra_write_register(dev, IMU660RA_ACC_CONF, 0xA7);                       // 加速度采集配置 性能模式 正常采集 50Hz  采样频率
        imu660ra_write_register(dev, IMU660RA_GYR_CONF, 0xA9);                       // 陀螺仪采集配置 性能模式 正常采集 200Hz 采样频率

        // IMU660RA_ACC_SAMPLE 寄存器
        // 设置为 0x00 加速度计量程为 ±2  g   获取到的加速度计数据除以 16384  可以转化为带物理单位的数据 (g 代表重力加速度 物理学名词 一般情况下 g 取 9.8 m/s^2 为标准值)
        // 设置为 0x01 加速度计量程为 ±4  g   获取到的加速度计数据除以 8192   可以转化为带物理单位的数据 (g 代表重力加速度 物理学名词 一般情况下 g 取 9.8 m/s^2 为标准值)
        // 设置为 0x02 加速度计量程为 ±8  g   获取到的加速度计数据除以 4096   可以转化为带物理单位的数据 (g 代表重力加速度 物理学名词 一般情况下 g 取 9.8 m/s^2 为标准值)
        // 设置为 0x03 加速度计量程为 ±16 g   获取到的加速度计数据除以 2048   可以转化为带物理单位的数据 (g 代表重力加速度 物理学名词 一般情况下 g 取 9.8 m/s^2 为标准值)
        switch(IMU660RA_ACC_SAMPLE_DEFAULT)
        {
            default:
            {
                printk(KERN_ERR "IMU660RA_ACC_SAMPLE_DEFAULT set error.");
                ret = DEV_NO_FIND;
            }break;
            case IMU660RA_ACC_SAMPLE_SGN_2G:
            {
                imu660ra_write_register(dev, IMU660RA_ACC_RANGE, 0x00);
                //imu660ra_transition_factor[0] = 16384;
            }break;
            case IMU660RA_ACC_SAMPLE_SGN_4G:
            {
                imu660ra_write_register(dev, IMU660RA_ACC_RANGE, 0x01);
                //imu660ra_transition_factor[0] = 8192;
            }break;
            case IMU660RA_ACC_SAMPLE_SGN_8G:
            {
                imu660ra_write_register(dev, IMU660RA_ACC_RANGE, 0x02);
                //imu660ra_transition_factor[0] = 4096;
            }break;
            case IMU660RA_ACC_SAMPLE_SGN_16G:
            {
                imu660ra_write_register(dev, IMU660RA_ACC_RANGE, 0x03);
                //imu660ra_transition_factor[0] = 2048;
            }break;
        }
        
        if(ret == DEV_NO_FIND)
        {
            break;
        }

        // IMU660RA_GYR_RANGE 寄存器
        // 设置为 0x04 陀螺仪量程为 ±125  dps    获取到的陀螺仪数据除以 262.4   可以转化为带物理单位的数据 单位为 °/s
        // 设置为 0x03 陀螺仪量程为 ±250  dps    获取到的陀螺仪数据除以 131.2   可以转化为带物理单位的数据 单位为 °/s
        // 设置为 0x02 陀螺仪量程为 ±500  dps    获取到的陀螺仪数据除以 65.6    可以转化为带物理单位的数据 单位为 °/s
        // 设置为 0x01 陀螺仪量程为 ±1000 dps    获取到的陀螺仪数据除以 32.8    可以转化为带物理单位的数据 单位为 °/s
        // 设置为 0x00 陀螺仪量程为 ±2000 dps    获取到的陀螺仪数据除以 16.4    可以转化为带物理单位的数据 单位为 °/s
        switch(IMU660RA_GYRO_SAMPLE_DEFAULT)
        {
            default:
            {
                printk(KERN_ERR "IMU660RA_GYRO_SAMPLE_DEFAULT set error.");
                ret = DEV_NO_FIND;
            }break;
            case IMU660RA_GYRO_SAMPLE_SGN_125DPS:
            {
                imu660ra_write_register(dev, IMU660RA_GYR_RANGE, 0x04);
                //imu660ra_transition_factor[1] = 262.4;
            }break;
            case IMU660RA_GYRO_SAMPLE_SGN_250DPS:
            {
                imu660ra_write_register(dev, IMU660RA_GYR_RANGE, 0x03);
                //imu660ra_transition_factor[1] = 131.2;
            }break;
            case IMU660RA_GYRO_SAMPLE_SGN_500DPS:
            {
                imu660ra_write_register(dev, IMU660RA_GYR_RANGE, 0x02);
                //imu660ra_transition_factor[1] = 65.6;
            }break;
            case IMU660RA_GYRO_SAMPLE_SGN_1000DPS:
            {
                imu660ra_write_register(dev, IMU660RA_GYR_RANGE, 0x01);
                //imu660ra_transition_factor[1] = 32.8;
            }break;
            case IMU660RA_GYRO_SAMPLE_SGN_2000DPS:
            {
                imu660ra_write_register(dev, IMU660RA_GYR_RANGE, 0x00);
                //imu660ra_transition_factor[1] = 16.4;
            }break;
        }
        if(ret == DEV_NO_FIND)
        {
            break;
        }
    }while(0);
    return ret;
}
