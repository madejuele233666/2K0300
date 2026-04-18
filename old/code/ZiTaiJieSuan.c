/*
 * ZiTaiJieSuan.c
 *
 *  Created on: 2025年1月8日
 *      Author: 27477
 */

#include "zf_common_headfile.h"
#include "ZiTaiJieSuan.h"
#include <math.h>
volatile float FJ_Angle = 0;//最后引出的变量
volatile float FJ_Pitch = 0;//最后引出的变量
// 四元数
//float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f;


float gx, gy, gz;
float ax, ay, az;
// 零漂值
float gyro_bias_x = 0.0f, gyro_bias_y = 0.0f, gyro_bias_z = 0.0f;

// 校准陀螺仪
void calibrate_gyro()
{
    int16 sum_gyro_x = 0, sum_gyro_y = 0, sum_gyro_z = 0;
    for (int i = 0; i < 100; i++) {
        mpu6050_get_gyro();
        sum_gyro_x += mpu6050_gyro_x;
        sum_gyro_y += mpu6050_gyro_y;
        sum_gyro_z += mpu6050_gyro_z;
        system_delay_ms(10); // 延时以获取多个样本
    }
    gyro_bias_x = sum_gyro_x / 100;
    gyro_bias_y = sum_gyro_y / 100;
    gyro_bias_z = sum_gyro_z / 100;
}

//void get_gyro_acc()
//{
//    // 获取陀螺仪数据并去零漂
//    mpu6050_get_gyro();
//    gx = mpu6050_gyro_transition(mpu6050_gyro_x - gyro_bias_x);
//    gy = mpu6050_gyro_transition(mpu6050_gyro_y - gyro_bias_y);
//    gz = mpu6050_gyro_transition(mpu6050_gyro_z - gyro_bias_z);
//
//    // 获取加速度计数据
//    mpu6050_get_acc();
//    ax = mpu6050_acc_transition(mpu6050_acc_x);
//    ay = mpu6050_acc_transition(mpu6050_acc_y);
//    az = mpu6050_acc_transition(mpu6050_acc_z);
//}
//
//
//// Madgwick滤波器更新函数
//void MadgwickUpdate() {
//    float norm;
//    float vx, vy, vz;
//    float ex, ey, ez;
//
//    // 归一化加速度计数据
//    norm = sqrt(ax * ax + ay * ay + az * az);
//    ax /= norm;
//    ay /= norm;
//    az /= norm;
//
//    // 计算四元数的导数
//    float qDot1 = -q1 * gx - q2 * gy - q3 * gz;
//    float qDot2 = q0 * gx + q2 * gz - q3 * gy;
//    float qDot3 = q0 * gy - q1 * gz + q3 * gx;
//    float qDot4 = q0 * gz + q1 * gy - q2 * gx;
//
//    // 计算误差
//    vx = 2 * (q1 * q3 - q0 * q2) - ax;
//    vy = 2 * (q0 * q1 + q2 * q3) - ay;
//    vz = 2 * (0.5f - q1 * q1 - q2 * q2) - az;
//
//    // 计算四元数的更新
//    qDot1 += Kp * vx;
//    qDot2 += Kp * vy;
//    qDot3 += Kp * vz;
//
//    // 更新四元数
//    q0 += qDot1 * halfT;
//    q1 += qDot2 * halfT;
//    q2 += qDot3 * halfT;
//    q3 += qDot4 * halfT;
//
//    // 归一化四元数
//    norm = sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
//    q0 /= norm;
//    q1 /= norm;
//    q2 /= norm;
//    q3 /= norm;
//}

// 从四元数中提取航偏角和俯仰角
void extract_angles() {
    float yaw0 = atan2(2 * (q0 * q3 + q1 * q2), 1 - 2 * (q2 * q2 + q3 * q3));
    float pitch0 = asin(2 * (q1 * q3 - q0 * q2));
    FJ_Angle = yaw0* 180.0 / M_PI;
    FJ_Pitch = pitch0 * 180.0 / M_PI;
}

/*******************************************************************************
* 函  数 ：float FindPos(float*a,int low,int high)
* 功  能 ：确定一个元素位序
* 参  数 ：a  数组首地址
*          low数组最小下标
*          high数组最大下标
* 返回值 ：返回元素的位序low
* 备  注 : 无
*******************************************************************************/
float FindPos(float*a,int low,int high)
{
    float val = a[low];                      //选定一个要确定值val确定位置
    while(low<high)
    {
        while(low<high && a[high]>=val)
        {
             high--;                       //如果右边的数大于VAL下标往前移
        }
             a[low] = a[high];             //当右边的值小于VAL则复值给A[low]

        while(low<high && a[low]<=val)
        {
             low++;                        //如果左边的数小于VAL下标往后移
        }
             a[high] = a[low];             //当左边的值大于VAL则复值给右边a[high]
    }
    a[low] = val;
    return low;
}

/*******************************************************************************
* 函  数 ：void QuiteSort(float* a,int low,int high)
* 功  能 ：快速排序
* 参  数 ：a  数组首地址
*          low数组最小下标
*          high数组最大下标
* 返回值 ：无
* 备  注 : 无
*******************************************************************************/
 void QuiteSort(float* a,int low,int high)
 {
     int pos;
     if(low<high)
     {
         pos = FindPos(a,low,high); //排序一个位置
         QuiteSort(a,low,pos-1);    //递归调用
         QuiteSort(a,pos+1,high);
     }
 }

/*******************************************************
*函数：void MPU6050_datainit(void)
*功能：获得陀螺仪初始零偏
************************************************************/
float gyro_x_init,gyro_y_init,gyro_z_init;
void MPU6050_datainit(void)
{
    int i,num=0;
    int gx=0,gy=0,gz=0;
    for(i = 350;i>0;i--)
    {
        mpu6050_get_gyro();

        if(mpu6050_gyro_z != 0 && mpu6050_gyro_y != 0 && mpu6050_gyro_x != 0)
        {
            num++;
            gx+=mpu6050_gyro_x;
            gy+=mpu6050_gyro_y;
            gz+=mpu6050_gyro_z;
        }
        system_delay_ms(6);

        if(gz>30000000 || gz<-30000000)
            break;
    }
    gyro_x_init = (float)gx/num;
    gyro_y_init = (float)gy/num;
    gyro_z_init = (float)gz/num;
}

/********************************************************************************
* 函  数 ：void  SortAver_FilterXYZ(INT16_XYZ *acc,FLOAT_XYZ *Acc_filt,uint8_t n)
* 功  能 ：去最值平均值滤波三组数据
* 参  数 ：*acc 要滤波数据地址
*          *Acc_filt 滤波后数据地址
* 返回值 ：无
* 备  注 : 无
********************************************************************************/
#define N 20      //滤波缓存数组大小

void  SortAver_FilterXYZ(short *acc_x,short *acc_y,short *acc_z,FLOAT_XYZ *Acc_filt,uint8_t n)
{
    static float bufx[N],bufy[N],bufz[N];
    static uint8_t cnt =0,flag = 1;
    float temp1=0,temp2=0,temp3=0;
    uint8_t i;
    bufx[cnt] = *acc_x;
    bufy[cnt] = *acc_y;
    bufz[cnt] = *acc_z;
    cnt++;      //这个的位置必须在赋值语句后，否则bufx[0]不会被赋值
    if(cnt<n && flag)
        return;   //数组填不满不计算
    else
        flag = 0;

    QuiteSort(bufx,0,n-1);
    QuiteSort(bufy,0,n-1);
    QuiteSort(bufz,0,n-1);
    for(i=1;i<n-1;i++)
     {
        temp1 += bufx[i];
        temp2 += bufy[i];
        temp3 += bufz[i];
     }

     if(cnt>=n) cnt = 0;
     Acc_filt->X  = temp1/(n-2);
     Acc_filt->Y  = temp2/(n-2);
     Acc_filt->Z  = temp3/(n-2);
}
/*********************************************************************************************************
* 函  数：void Prepare_Data(void)
* 功　能：对陀螺仪去零偏后的数据滤波及赋予物理意义，为姿态解算做准备
* 参  数：无
* 返回值：无
**********************************************************************************************************/


FLOAT_XYZ   Acc_filt,Acc_filtold,Gyr_rad;
#define G           9.80665f                // m/s^2
#define Acc_Gain    0.0001220f        //加速度变成G (初始化加速度满量程-+4g LSBa = 2*4/65535.0)
#define Gyro_Gr     0.0010641f        //角速度变成弧度(3.1415/180 * LSBg)
#define Kp_New      0.9f              //互补滤波当前数据的权重
#define Kp_Old      0.1f              //互补滤波历史数据的权重

void Prepare_Data(void)
{
    static uint8_t IIR_mode = 1;


    mpu6050_get_gyro();
    mpu6050_get_acc();

    SortAver_FilterXYZ(&mpu6050_acc_x,&mpu6050_acc_y,&mpu6050_acc_z,&Acc_filt,12);//对加速度原始数据进行去极值滑动窗口滤波

    //加速度AD值 转换成 米/平方秒
    Acc_filt.X = (float)Acc_filt.X * Acc_Gain * G;
    Acc_filt.Y = (float)Acc_filt.Y * Acc_Gain * G;
    Acc_filt.Z = (float)Acc_filt.Z * Acc_Gain * G;

    //陀螺仪AD值 转换成 弧度/秒
    Gyr_rad.X = (float) (mpu6050_gyro_x - gyro_x_init) * Gyro_Gr;
    Gyr_rad.Y = (float) (mpu6050_gyro_y - gyro_y_init) * Gyro_Gr;
    Gyr_rad.Z = (float) (mpu6050_gyro_z - gyro_z_init) * Gyro_Gr;


    if(IIR_mode)
    {
        Acc_filt.X = Acc_filt.X * Kp_New + Acc_filtold.X * Kp_Old;
        Acc_filt.Y = Acc_filt.Y * Kp_New + Acc_filtold.Y * Kp_Old;
        Acc_filt.Z = Acc_filt.Z * Kp_New + Acc_filtold.Z * Kp_Old;


        Acc_filtold.X =  Acc_filt.X;
        Acc_filtold.Y =  Acc_filt.Y;
        Acc_filtold.Z =  Acc_filt.Z;
    }

}

/****************************************************************************************************
* 函  数：static float invSqrt(float x)
* 功　能: 快速计算 1/Sqrt(x)
* 参  数：要计算的值
* 返回值：计算的结果
* 备  注：比普通Sqrt()函数要快四倍See: http://en.wikipedia.org/wiki/Fast_inverse_square_root
*****************************************************************************************************/
static float invSqrt(float x)
{
    float halfx = 0.5f * x;
    float y = x;
    long i = *(long*)&y;
    i = 0x5f375a86 - (i>>1);
    y = *(float*)&i;
    y = y * (1.5f - (halfx * y * y));
    return y;
}
/*********************************************************************************************************
* 函  数：void IMUupdate(FLOAT_XYZ *Gyr_rad,FLOAT_XYZ *Acc_filt,FLOAT_ANGLE *Att_Angle)
* 功　能：获取姿态角
* 参  数：Gyr_rad  指向角速度的指针（注意单位必须是弧度）
*         Acc_filt  指向加速度的指针
*         Att_Angle 指向姿态角的指针
* 返回值：无
* 备  注：求解四元数和欧拉角都在此函数中完成
**********************************************************************************************************/
//kp=ki=0 就是完全相信陀螺仪
float q0 = 1, q1 = 0, q2 = 0, q3 = 0;     // quaternion elements representing the estimated orientation
float exInt = 0, eyInt = 0, ezInt = 0;    // scaled integral error

#define Kp 1.5f                         // proportional gain governs rate of convergence to accelerometer/magnetometer
                                         //比例增益控制加速度计，磁力计的收敛速率
#define Ki 0.005f                        // integral gain governs rate of convergence of gyroscope biases
                                         //积分增益控制陀螺偏差的收敛速度
#define halfT 0.005f                     // half the sample period 采样周期的一半
#define RadtoDeg    57.324841f              //弧度到角度 (弧度 * 180/3.1415)

FLOAT_ANGLE ATT_Angle;                  //z轴逆时针为负，顺时针为正

void IMUupdate(FLOAT_XYZ *Gyr_rad,FLOAT_XYZ *Acc_filt,FLOAT_ANGLE *Att_Angle)
{

    float ax = Acc_filt->X,ay = Acc_filt->Y,az = Acc_filt->Z;
    float gx = Gyr_rad->X,gy = Gyr_rad->Y,gz = Gyr_rad->Z;
    float vx, vy, vz;
    float ex, ey, ez;
    float norm;

    float q0q0 = q0*q0;
    float q0q1 = q0*q1;
    float q0q2 = q0*q2;
    //float q0q3 = q0*q3;
    float q1q1 = q1*q1;
    //float q1q2 = q1*q2;
    float q1q3 = q1*q3;
    float q2q2 = q2*q2;
    float q2q3 = q2*q3;
    float q3q3 = q3*q3;

    if(ax*ay*az==0)
    return;

    //加速度计测量的重力向量(机体坐标系)
    norm = invSqrt(ax*ax + ay*ay + az*az);
    ax = ax * norm;
    ay = ay * norm;
    az = az * norm;
    //  printf("ax=%0.2f ay=%0.2f az=%0.2f\r\n",ax,ay,az);

    //陀螺仪积分估计重力向量(机体坐标系)
    vx = 2*(q1q3 - q0q2);
    vy = 2*(q0q1 + q2q3);
    vz = q0q0 - q1q1 - q2q2 + q3q3 ;
    // printf("vx=%0.2f vy=%0.2f vz=%0.2f\r\n",vx,vy,vz);

    //测量的重力向量与估算的重力向量差积求出向量间的误差
    ex = (ay*vz - az*vy); //+ (my*wz - mz*wy);
    ey = (az*vx - ax*vz); //+ (mz*wx - mx*wz);
    ez = (ax*vy - ay*vx); //+ (mx*wy - my*wx);

    //用上面求出误差进行积分
    exInt = exInt + ex * Ki;
    eyInt = eyInt + ey * Ki;
    ezInt = ezInt + ez * Ki;

    //将误差PI后补偿到陀螺仪
    gx = gx + Kp*ex + exInt;
    gy = gy + Kp*ey + eyInt;
    gz = gz + Kp*ez + ezInt;//这里的gz由于没有观测者进行矫正会产生漂移，表现出来的就是积分自增或自减

    //四元素的微分方程
    q0 = q0 + (-q1*gx - q2*gy - q3*gz)*halfT;
    q1 = q1 + (q0*gx + q2*gz - q3*gy)*halfT;
    q2 = q2 + (q0*gy - q1*gz + q3*gx)*halfT;
    q3 = q3 + (q0*gz + q1*gy - q2*gx)*halfT;

    //单位化四元数
    norm = invSqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3);
    q0 = q0 * norm;
    q1 = q1 * norm;
    q2 = q2 * norm;
    q3 = q3 * norm;

    //四元数转换成欧拉角(Z->Y->X)
    Att_Angle->yaw += Gyr_rad->Z *RadtoDeg*0.003f;
    Att_Angle->rol = -asin(2.f * (q1q3 - q0q2))* 57.3f;                            // roll(负号要注意)
    Att_Angle->pit = -atan2(2.f * q2q3 + 2.f * q0q1, q0q0 - q1q1 - q2q2 + q3q3)* 57.3f ; // pitch
    //失控保护 (调试时可注释掉)
//  Safety_Check();
}
