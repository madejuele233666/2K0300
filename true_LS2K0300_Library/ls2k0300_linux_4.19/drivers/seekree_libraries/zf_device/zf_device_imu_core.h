#ifndef _zf_device_imu_core_h_
#define _zf_device_imu_core_h_

#include "../zf_common/zf_common_typedef.h"
#include <linux/spinlock.h>

struct imu_dev_struct 
{
	uint8 type;							// 设备类型


	int16 (*imu_get_acc ) (struct imu_dev_struct *dev, int axis);
	int16 (*imu_get_gyro) (struct imu_dev_struct *dev, int axis);
	int16 (*imu_get_mag ) (struct imu_dev_struct *dev, int axis);

	struct spi_device *spi;				// spi设备
	struct regmap *regmap;				// 寄存器访问接口
	struct regmap_config reg_cfg;		// 寄存器访问接口
    // struct mutex lock;
	spinlock_t read_raw_lock;
};

typedef enum
{
    DEV_NO_FIND  = 0x00,
    DEV_IMU660RA = 0x10,
    DEV_IMU660RB = 0x11,
    DEV_IMU963RA = 0x12,
}imu_dev_enum;

#define IMU_TIMEOUT_COUNT                      ( 0x00FF )                



#endif