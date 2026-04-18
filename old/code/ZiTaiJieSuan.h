/*
 * ZiTaiJieSuan.h
 *
 *  Created on: 2025年1月8日
 *      Author: 27477
 */

#ifndef ZITAIJIESUAN_H
#define ZITAIJIESUAN_H

#include "zf_common_headfile.h"
#include <math.h>

#define M_PI        3.14159265358979323846
#define M_PI_2      1.57079632679489661923
#define M_PI_4      0.78539816339744830962
// 四元数
extern float q0;
extern float q1;
extern float q2;
extern float q3;

//// Madgwick滤波器参数
//#define Kp 2.0f // 比例增益
//#define Ki 0.005f // 积分增益
//#define halfT 0.005f // 采样周期的一半

// 最终引出的变量
extern volatile float FJ_Angle; // 航偏角
extern volatile float FJ_Pitch; // 俯仰角

// 零漂值
extern float gyro_bias_x;
extern float gyro_bias_y;
extern float gyro_bias_z;

// 函数声明
void calibrate_gyro(void);
void get_gyro_acc(void);
void MadgwickUpdate();
void extract_angles();

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
void IMUupdate();
#endif // ZITAIJIESUAN_H
