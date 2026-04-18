#ifndef _zf_device_imu660ra_h
#define _zf_device_imu660ra_h


#include "zf_common_typedef.h"
#include "zf_driver_delay.h"

extern int16 imu660ra_acc_x, imu660ra_acc_y, imu660ra_acc_z;  
extern int16 imu660ra_gyro_x, imu660ra_gyro_y, imu660ra_gyro_z;

void imu660ra_get_acc(void);
void imu660ra_get_gyro(void);

/*
 *
 *
 *
 */
//三轴浮点型
typedef struct
{
    float X;
    float Y;
    float Z;
}FLOAT_XYZ;
typedef struct
{
    float rol;
    float pit;
    float yaw;
}FLOAT_ANGLE;

extern FLOAT_ANGLE ATT_Angle;                  //z轴逆时针为负，顺时针为正
extern FLOAT_XYZ   Acc_filt,Acc_filtold,Gyr_rad;
void Prepare_Data();
void IMUupdate(FLOAT_XYZ *Gyr_rad,FLOAT_XYZ *Acc_filt,FLOAT_ANGLE *Att_Angle);

#endif
