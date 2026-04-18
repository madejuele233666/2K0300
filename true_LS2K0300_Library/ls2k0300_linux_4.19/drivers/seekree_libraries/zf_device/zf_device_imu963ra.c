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

#include "zf_device_imu963ra.h"

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU963RA 写寄存器
// 参数说明     reg             寄存器地址
// 参数说明     data            数据
// 返回参数     void
// 使用示例     imu963ra_write_acc_gyro_register(IMU963RA_SLV0_CONFIG, 0x00);
// 备注信息     内部调用
//-------------------------------------------------------------------------------------------------------------------
#define imu963ra_write_acc_gyro_register(reg, data)     (regmap_write(dev->regmap, reg, data))

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU963RA 读寄存器
// 参数说明     reg             寄存器地址
// 返回参数     uint8           数据
// 使用示例     imu963ra_read_acc_gyro_register(IMU963RA_STATUS_MASTER);
// 备注信息     内部调用
//-------------------------------------------------------------------------------------------------------------------
#define imu963ra_read_acc_gyro_register(reg)\
({\
    uint8 ret = 0; \
    uint32 data = 0; \
    ret = regmap_read(dev->regmap, reg, &data); \
    data; \
})

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU963RA 读数据 内部调用
// 参数说明     reg             寄存器地址
// 参数说明     data            数据缓冲区
// 参数说明     len             数据长度
// 返回参数     void
// 使用示例     imu963ra_read_acc_gyro_registers(IMU963RA_OUTX_L_A, dat, 6);
// 备注信息     内部调用
//-------------------------------------------------------------------------------------------------------------------
#define imu963ra_read_acc_gyro_registers(reg, data, len)     (regmap_bulk_read(dev->regmap, reg, data, len))



//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU963RA 作为 IIC 主机向磁力计写数据
// 参数说明     addr            目标地址
// 参数说明     reg             目标寄存器
// 参数说明     data            数据
// 返回参数     uint8           1-失败 0-成功
// 使用示例     imu963ra_write_mag_register(IMU963RA_MAG_ADDR, IMU963RA_MAG_CONTROL2, 0x80);
// 备注信息     内部调用
//-------------------------------------------------------------------------------------------------------------------
#define imu963ra_write_mag_register(addr, reg, data)\
({\
    uint8 return_state = 0;\
    uint16 timeout_count = 0;\
    imu963ra_write_acc_gyro_register(IMU963RA_SLV0_CONFIG, 0x00);\
    imu963ra_write_acc_gyro_register(IMU963RA_SLV0_ADD, addr << 1 | 0);\
    imu963ra_write_acc_gyro_register(IMU963RA_SLV0_SUBADD, reg);\
    imu963ra_write_acc_gyro_register(IMU963RA_DATAWRITE_SLV0, data);\
    imu963ra_write_acc_gyro_register(IMU963RA_MASTER_CONFIG, 0x4C);\
    while(0 == (0x80 & imu963ra_read_acc_gyro_register(IMU963RA_STATUS_MASTER)))\
    {\
        if(IMU_TIMEOUT_COUNT < timeout_count ++)\
        {\
            return_state = 1;\
            break;\
        }\
        mdelay(2);\
    }\
    return_state;\
})



//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU963RA 作为 IIC 主机向磁力计读数据
// 参数说明     addr            目标地址
// 参数说明     reg             目标寄存器
// 返回参数     uint8           读取的数据
// 使用示例     imu963ra_read_mag_register(IMU963RA_MAG_ADDR, IMU963RA_MAG_CHIP_ID);
// 备注信息     内部调用
//-------------------------------------------------------------------------------------------------------------------
#define imu963ra_read_mag_register(addr, reg) \
({\
    uint16 timeout_count = 0;\
    imu963ra_write_acc_gyro_register(IMU963RA_SLV0_ADD, (addr << 1) | 1); \
    imu963ra_write_acc_gyro_register(IMU963RA_SLV0_SUBADD, reg); \
    imu963ra_write_acc_gyro_register(IMU963RA_SLV0_CONFIG, 0x01); \
    imu963ra_write_acc_gyro_register(IMU963RA_MASTER_CONFIG, 0x4C); \
    while(0 == (0x01 & imu963ra_read_acc_gyro_register(IMU963RA_STATUS_MASTER))) \
    { \
        if(IMU_TIMEOUT_COUNT < timeout_count ++) \
        { \
            break; \
        } \
        mdelay(1); \
    } \
    (imu963ra_read_acc_gyro_register(IMU963RA_SENSOR_HUB_1)); \
})

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU963RA 作为 IIC 主机向磁力计自动写数据
// 参数说明     addr            目标地址
// 参数说明     reg             目标寄存器
// 返回参数     void
// 使用示例     imu963ra_connect_mag(IMU963RA_MAG_ADDR, IMU963RA_MAG_OUTX_L);
// 备注信息     内部调用
//-------------------------------------------------------------------------------------------------------------------
#define imu963ra_connect_mag(addr, reg) \
({ \
    imu963ra_write_acc_gyro_register(IMU963RA_SLV0_ADD, (addr << 1) | 1); \
    imu963ra_write_acc_gyro_register(IMU963RA_SLV0_SUBADD, reg); \
    imu963ra_write_acc_gyro_register(IMU963RA_SLV0_CONFIG, 0x06); \
    imu963ra_write_acc_gyro_register(IMU963RA_MASTER_CONFIG, 0x6C); \
})

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     获取 IMU963RA 加速度计数据
// 参数说明     dev             IMU963RA设备结构体指针，指向要获取数据的设备实例
// 参数说明     axis            轴编号，指定要获取哪个轴的加速度计数据
// 参数说明     val             数据指针，用于存储获取到的加速度计数据
// 返回参数     int             数据类型标识，这里返回IIO_VAL_INT表示返回整数类型数据
// 使用示例     imu963ra_get_acc(dev, IIO_MOD_X, &val);
// 备注信息     从设备读取指定轴的加速度计原始数据
//-------------------------------------------------------------------------------------------------------------------
int16 imu963ra_get_acc (struct imu_dev_struct *dev, int axis)
{
    uint8_t dat[2];

    // axis_x = 1
    // axis_y = 2
    // axis_z = 3

    // 从指定寄存器地址读取加速度计数据
    imu963ra_read_acc_gyro_registers(IMU963RA_OUTX_L_A + (axis - IIO_MOD_X) * 2, dat, 2);
    // 将读取到的两个字节数据合并为一个16位整数

    return (int16_t)(((uint16_t)dat[1] << 8 | dat[0]));
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     获取IMU963RA陀螺仪数据
// 参数说明     dev             IMU963RA设备结构体指针，指向要获取数据的设备实例
// 参数说明     axis            轴编号，指定要获取哪个轴的陀螺仪数据
// 参数说明     val             数据指针，用于存储获取到的陀螺仪数据
// 返回参数     int             数据类型标识，这里返回IIO_VAL_INT表示返回整数类型数据
// 使用示例     imu963ra_get_gyro(dev, IIO_MOD_X, &val);
// 备注信息     从设备读取指定轴的陀螺仪原始数据
//-------------------------------------------------------------------------------------------------------------------
int16 imu963ra_get_gyro (struct imu_dev_struct *dev, int axis)
{
    uint8_t dat[2];

    // axis_x = 1
    // axis_y = 2
    // axis_z = 3

    // 从指定寄存器地址读取陀螺仪数据
    imu963ra_read_acc_gyro_registers(IMU963RA_OUTX_L_G + (axis - IIO_MOD_X) * 2, dat, 2);

    // 将读取到的两个字节数据合并为一个16位整数
    return (int16_t)(((uint16_t)dat[1] << 8 | dat[0]));
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     获取IMU963RA磁力计
// 参数说明     dev             IMU963RA设备结构体指针，指向要获取数据的设备实例
// 参数说明     axis            轴编号，指定要获取哪个轴的磁力计数据
// 参数说明     val             数据指针，用于存储获取到的磁力计数据
// 返回参数     int             数据类型标识，这里返回IIO_VAL_INT表示返回整数类型数据
// 使用示例     imu963ra_get_gyro(dev, IIO_MOD_X, &val);
// 备注信息     从设备读取指定轴的磁力计原始数据
//-------------------------------------------------------------------------------------------------------------------
int16 imu963ra_get_mag (struct imu_dev_struct *dev, int axis)
{
    static int16 mag_dat[3] = {0};
    uint8 temp_status;
    uint8 dat[6];

    imu963ra_write_acc_gyro_register(IMU963RA_FUNC_CFG_ACCESS, 0x40);
    temp_status = imu963ra_read_acc_gyro_register(IMU963RA_STATUS_MASTER);
    if(0x01 & temp_status)
    {
        imu963ra_read_acc_gyro_registers(IMU963RA_SENSOR_HUB_1, dat, 6);
        mag_dat[0] = (int16)(((uint16)dat[1] << 8 | dat[0]));
        mag_dat[1] = (int16)(((uint16)dat[3] << 8 | dat[2]));
        mag_dat[2] = (int16)(((uint16)dat[5] << 8 | dat[4]));
    }
    imu963ra_write_acc_gyro_register(IMU963RA_FUNC_CFG_ACCESS, 0x00);


    return mag_dat[axis - IIO_MOD_X];
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU963RA 六轴自检 内部调用
// 参数说明     void
// 返回参数     uint8           1-自检失败 0-自检成功
// 使用示例     imu963ra_acc_gyro_self_check();
// 备注信息     内部调用
//-------------------------------------------------------------------------------------------------------------------
uint8 imu963ra_acc_gyro_self_check (struct imu_dev_struct *dev)
{
    uint8 return_state = 0;
    uint8 dat = 0;

    dat = imu963ra_read_acc_gyro_register(IMU963RA_WHO_AM_I);
    // 判断 ID 是否正确
    if(0x6B != dat)   
    {
        return_state = 1;
    } 

    return return_state;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     IMU963RA 磁力计自检 内部调用
// 参数说明     void
// 返回参数     uint8           1-自检失败 0-自检成功
// 使用示例     imu963ra_mag_self_check();
// 备注信息     内部调用
//-------------------------------------------------------------------------------------------------------------------
static uint8 imu963ra_mag_self_check (struct imu_dev_struct *dev)
{
    uint8 return_state = 0;
    uint8 dat = 0;
    uint16 timeout_count = 0;

    while(0xff != dat)                                                          // 判断 ID 是否正确
    {
        if(MAG_TIME_OUT < timeout_count++)
        {
            return_state = 1;
            break;
        }
        dat = imu963ra_read_mag_register(IMU963RA_MAG_ADDR, IMU963RA_MAG_CHIP_ID);
        // dev_info(&dev->spi->dev, "imu963ra_mag self dat = %d\r\n", dat);  
    }
    return return_state;
}


//-------------------------------------------------------------------------------------------------------------------
// 函数简介     初始化 IMU963RA
// 参数说明     dev             IMU963RA设备结构体指针，指向要初始化的设备实例
// 返回参数     uint8           1-初始化失败 0-初始化成功
// 使用示例     imu963ra_init(dev);
// 备注信息     对IMU963RA设备进行初始化操作，包括自检、配置寄存器
//-------------------------------------------------------------------------------------------------------------------
uint8 imu963ra_init(struct imu_dev_struct *dev)
{
    uint8 ret = DEV_IMU963RA;

	do
    {
        imu963ra_write_acc_gyro_register(IMU963RA_FUNC_CFG_ACCESS, 0x00);       // 关闭HUB寄存器访问
        imu963ra_write_acc_gyro_register(IMU963RA_CTRL3_C, 0x01);               // 复位设备
        mdelay(2);
        imu963ra_write_acc_gyro_register(IMU963RA_FUNC_CFG_ACCESS, 0x00);       // 关闭HUB寄存器访问
        if(imu963ra_acc_gyro_self_check(dev))                   
        {
            // dev_warn(&dev->spi->dev, "IMU963RA acc and gyro self check error.");                    
            ret = DEV_NO_FIND;
            break;            
        }
                            
        imu963ra_write_acc_gyro_register(IMU963RA_INT1_CTRL, 0x03);             // 开启陀螺仪 加速度数据就绪中断

        // IMU963RA_CTRL1_XL 寄存器
        // 设置为 0x30 加速度量程为 ±2  G    获取到的加速度计数据除以 16393  可以转化为带物理单位的数据 单位 g(m/s^2)
        // 设置为 0x38 加速度量程为 ±4  G    获取到的加速度计数据除以 8197   可以转化为带物理单位的数据 单位 g(m/s^2)
        // 设置为 0x3C 加速度量程为 ±8  G    获取到的加速度计数据除以 4098   可以转化为带物理单位的数据 单位 g(m/s^2)
        // 设置为 0x34 加速度量程为 ±16 G    获取到的加速度计数据除以 2049   可以转化为带物理单位的数据 单位 g(m/s^2)
        switch(IMU963RA_ACC_SAMPLE_DEFAULT)
        {
            default:
            {
                dev_warn(&dev->spi->dev, "IMU963RA_ACC_SAMPLE_DEFAULT set error.");
                ret = DEV_NO_FIND;
            }break;
            case IMU963RA_ACC_SAMPLE_SGN_2G:
            {
                imu963ra_write_acc_gyro_register(IMU963RA_CTRL1_XL, 0x30);
                //imu963ra_transition_factor[0] = 16393;
            }break;
            case IMU963RA_ACC_SAMPLE_SGN_4G:
            {
                imu963ra_write_acc_gyro_register(IMU963RA_CTRL1_XL, 0x38);
                //imu963ra_transition_factor[0] = 8197;
            }break;
            case IMU963RA_ACC_SAMPLE_SGN_8G:
            {
                imu963ra_write_acc_gyro_register(IMU963RA_CTRL1_XL, 0x3C);
                //imu963ra_transition_factor[0] = 4098;
            }break;
            case IMU963RA_ACC_SAMPLE_SGN_16G:
            {
                imu963ra_write_acc_gyro_register(IMU963RA_CTRL1_XL, 0x34);
                //imu963ra_transition_factor[0] = 2049;
            }break;
        }
        if(ret == DEV_NO_FIND)
        {
            break;
        }

        // IMU963RA_CTRL2_G 寄存器
        // 设置为 0x52 陀螺仪量程为 ±125  dps    获取到的陀螺仪数据除以 228.6   可以转化为带物理单位的数据 单位为 °/s
        // 设置为 0x50 陀螺仪量程为 ±250  dps    获取到的陀螺仪数据除以 114.3   可以转化为带物理单位的数据 单位为 °/s
        // 设置为 0x54 陀螺仪量程为 ±500  dps    获取到的陀螺仪数据除以 57.1    可以转化为带物理单位的数据 单位为 °/s
        // 设置为 0x58 陀螺仪量程为 ±1000 dps    获取到的陀螺仪数据除以 28.6    可以转化为带物理单位的数据 单位为 °/s
        // 设置为 0x5C 陀螺仪量程为 ±2000 dps    获取到的陀螺仪数据除以 14.3    可以转化为带物理单位的数据 单位为 °/s
        // 设置为 0x51 陀螺仪量程为 ±4000 dps    获取到的陀螺仪数据除以 7.1     可以转化为带物理单位的数据 单位为 °/s
        switch(IMU963RA_GYRO_SAMPLE_DEFAULT)
        {
            default:
            {
                dev_warn(&dev->spi->dev, "IMU963RA_GYRO_SAMPLE_DEFAULT set error.");
                ret = DEV_NO_FIND;
            }break;
            case IMU963RA_GYRO_SAMPLE_SGN_125DPS:
            {
                imu963ra_write_acc_gyro_register(IMU963RA_CTRL2_G, 0x52);
                //imu963ra_transition_factor[1] = 228.6;
            }break;
            case IMU963RA_GYRO_SAMPLE_SGN_250DPS:
            {
                imu963ra_write_acc_gyro_register(IMU963RA_CTRL2_G, 0x50);
                //imu963ra_transition_factor[1] = 114.3;
            }break;
            case IMU963RA_GYRO_SAMPLE_SGN_500DPS:
            {
                imu963ra_write_acc_gyro_register(IMU963RA_CTRL2_G, 0x54);
                //imu963ra_transition_factor[1] = 57.1;
            }break;
            case IMU963RA_GYRO_SAMPLE_SGN_1000DPS:
            {
                imu963ra_write_acc_gyro_register(IMU963RA_CTRL2_G, 0x58);
                //imu963ra_transition_factor[1] = 28.6;
            }break;
            case IMU963RA_GYRO_SAMPLE_SGN_2000DPS:
            {
                imu963ra_write_acc_gyro_register(IMU963RA_CTRL2_G, 0x5C);
                //imu963ra_transition_factor[1] = 14.3;
            }break;
            case IMU963RA_GYRO_SAMPLE_SGN_4000DPS:
            {
                imu963ra_write_acc_gyro_register(IMU963RA_CTRL2_G, 0x51);
                //imu963ra_transition_factor[1] = 7.1;
            }break;
        }
        if(ret == DEV_NO_FIND)
        {
            break;
        }

        imu963ra_write_acc_gyro_register(IMU963RA_CTRL3_C, 0x44);               // 使能陀螺仪数字低通滤波器
        imu963ra_write_acc_gyro_register(IMU963RA_CTRL4_C, 0x02);               // 使能数字低通滤波器
        imu963ra_write_acc_gyro_register(IMU963RA_CTRL5_C, 0x00);               // 加速度计与陀螺仪四舍五入
        imu963ra_write_acc_gyro_register(IMU963RA_CTRL6_C, 0x00);               // 开启加速度计高性能模式 陀螺仪低通滤波 133hz
        imu963ra_write_acc_gyro_register(IMU963RA_CTRL7_G, 0x00);               // 开启陀螺仪高性能模式 关闭高通滤波
        imu963ra_write_acc_gyro_register(IMU963RA_CTRL9_XL, 0x01);              // 关闭I3C接口

        imu963ra_write_acc_gyro_register(IMU963RA_FUNC_CFG_ACCESS, 0x40);       // 开启HUB寄存器访问 用于配置地磁计
        imu963ra_write_acc_gyro_register(IMU963RA_MASTER_CONFIG, 0x80);         // 复位I2C主机
        mdelay(2);                             
        imu963ra_write_acc_gyro_register(IMU963RA_MASTER_CONFIG, 0x00);         // 清除复位标志
        mdelay(2);
        
        imu963ra_write_mag_register(IMU963RA_MAG_ADDR, IMU963RA_MAG_CONTROL2, 0x80);// 复位连接的外设
        mdelay(2);
        imu963ra_write_mag_register(IMU963RA_MAG_ADDR, IMU963RA_MAG_CONTROL2, 0x00);
        mdelay(2);

        if(imu963ra_mag_self_check(dev) == 0)
        {
            ret = DEV_IMU963RA;
            // dev_info(&dev->spi->dev, "imu mode = IMU963RA.");

            // IMU963RA_MAG_ADDR 寄存器
            // 设置为 0x09 磁力计量程为 2G   获取到的磁力计数据除以 12000   可以转化为带物理单位的数据 单位 G(高斯)
            // 设置为 0x19 磁力计量程为 8G   获取到的磁力计数据除以 3000    可以转化为带物理单位的数据 单位 G(高斯)
            switch(IMU963RA_MAG_SAMPLE_DEFAULT)
            {
                default:
                {
                    dev_warn(&dev->spi->dev, "IMU963RA_MAG_SAMPLE_DEFAULT set error.");
                    ret = DEV_NO_FIND;
                }break;
                case IMU963RA_MAG_SAMPLE_2G:
                {
                    imu963ra_write_mag_register(IMU963RA_MAG_ADDR, IMU963RA_MAG_CONTROL1, 0x09);
                    //imu963ra_transition_factor[2] = 12000;
                }break;
                case IMU963RA_MAG_SAMPLE_8G:
                {
                    imu963ra_write_mag_register(IMU963RA_MAG_ADDR, IMU963RA_MAG_CONTROL1, 0x19);
                    //imu963ra_transition_factor[2] = 3000;
                }break;
            }

            imu963ra_write_mag_register(IMU963RA_MAG_ADDR, IMU963RA_MAG_FBR, 0x01);
            imu963ra_connect_mag(IMU963RA_MAG_ADDR, IMU963RA_MAG_OUTX_L);

        }
        else
        {
            ret = DEV_IMU660RB;
            // dev_info(&dev->spi->dev, "imu mode = IMU660RB.");
            
        }

        imu963ra_write_acc_gyro_register(IMU963RA_FUNC_CFG_ACCESS, 0x00);       // 关闭HUB寄存器访问

        if(ret == DEV_NO_FIND)
        {
            break;
        }

    }while(0);

    return ret;
}
