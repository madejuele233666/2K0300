

#ifndef PID_H_
#define PID_H_


#include "zf_common_headfile.h"
#include "motor.h"

#define uint16 uint16_t
#define int16 int16_t

#define I_COUNT_MAX 2200                             //积分累计最大值（实时修改）
#define I_COUNT_MAX_DISTANCE 4000
#define DEAD_AREA_L 80                                   //死区，减轻测速抖动带来的影响，
#define DEAD_AREA_R 0                                   //应略大于抖动范围（很难调，建议取0）
#define PID_I_ON        (TOP_SPEED)                               //积分分离，当偏差大于此值时屏蔽积分作用
#define PID_MOTOR_DUTY_MAX      9000  //7800
#define PID_setout_MAX      9000  //7800

#define SERVO_output_limit_max      (3800+70)
#define SERVO_output_limit_min      (2800-70)
#define SERVO_output_limit_middle   ((SERVO_output_limit_min + SERVO_output_limit_max)/2)
#define SERVO_TARGET                80

//******************************车速度设置******************************
#define SPEED_START             600                 //上电初始速度
#define SPEED_BASE              speed_base          //正常运行速度（三台车）
extern int speed2Car;                              //两台车速度
extern int speed3Car;                              //三台车速度
#define SPEED_MAX               (speed_base+speedMaxUpper)    //最大速度（直道用）
#define SPEED_PODAO             (450)               //坡道速度
#define SPEED_HUANDAO           (400)               //三车时环岛速度
#define SPEED_SANCHA_DOWN       (speed_base-350)    //三岔等待中间车插入速度

//******************************加速减速上限设置******************************
#define Delta_Delta_SPEED       150                                              //加速上限允许变化的量
#define Delta_SPEED_UP_NORMAL   150                                              //加速上限正常量
#define Delta_SPEED_UP_MAX      (Delta_SPEED_UP_NORMAL /*+ Delta_Delta_SPEED*/)     //加速上限最大值（第一圈车库用）
#define Delta_SPEED_UP_CURVE    50                                              //正常赛道弯道不允许加太多速度
#define Delta_SPEED_UP_MIN      0                                              //加速上限最小值(环岛用)

#define Delta_SPEED_DOWN_NORMAL   150                                                   //减速上限正常量
#define Delta_SPEED_DOWN_PODAO    50                                                   //减速上限坡道量
#define Delta_SPEED_DOWN_MAX      (Delta_SPEED_DOWN_NORMAL + Delta_SPEED_DOWN_NORMAL) //减速上限最大值（第一圈车库用）
extern int Delta_SPEED_DOWN_SECOND_CHEKU;                                             //减速上限第二圈车库量



typedef struct{
    float P;
    float I;
    float D;
    int16 Speed_Target;//陀螺仪PID对应目标角速度，摄像头对应中间
    int16 Speed;//当前速度，摄像头对应当前道路中间，陀螺仪对应当前的角速度
    int16 Speed_last;
    int16 I_count;
    int16 D_data_last;
    int16 error_last;
    int16 SPEED_MID; // 电机速度环的目标速度
}PID_paraments;
typedef struct{
    float P;
    float I;
    float D;
    float W_Target;//陀螺仪PID对应目标角速度，摄像头对应中间
    float W;//当前速度，摄像头对应当前道路中间，陀螺仪对应当前的角速度
    float W_last;
    float I_count;
    float D_data_last;
    float error_last;
}PID_GYRO;
typedef struct{
    float P;
    float I;
    float D;
    float CBH_Target;//陀螺仪PID对应目标角速度，摄像头对应中间
    float CBH;//当前速度，摄像头对应当前道路中间，陀螺仪对应当前的角速度
    float CBH_last;
    float I_count;
    float D_data_last;
    float error_last;
}PID_DC;
typedef struct{
    float P;
    float I;
    float D;
    int MID_Target;//陀螺仪PID对应目标角速度，摄像头对应中间
    int MID;//当前速度，摄像头对应当前道路中间，陀螺仪对应当前的角速度
    int MID_last;
    float I_count;
    float D_data_last;
    int error_last;
}PID_CAMERA;
typedef struct{
    float P;
    float I;
    float D;
    int Angle_Target;
    int Angle;
    int Angle_last;
    float I_count;
    float D_data_last;
    int error_last;
}PID_Angle;

//舵机结构体
typedef struct
{
    uint16 p;
    uint16 d;
    int16 error;
    int16 errorlast;
}SERVO_PID;
extern float filted_W;
extern int Speed_base,JWJC;
extern PID_paraments PID_All;
extern PID_paraments PID_R;
extern PID_paraments PID_L;
extern PID_CAMERA PID_TURN_CAMERA;
extern PID_Angle PID_TURN_Angle;
extern PID_GYRO PID_TURN_GYRO_CAMERA;
extern PID_GYRO PID_TURN_GYRO_Dianci;
extern PID_GYRO PID_TURN_GYRO_Angle;
extern int Speed_base;
//舵机结构体变量
extern SERVO_PID servo_pid;
extern uint16_t Steer_diff_wheel_coefficient;

extern int delta_speed_up;
extern int delta_speed_down;
extern int delta_speed;    //测距pid速度变化
extern uint8_t flagStraight, flagCurve;  //直道、弯道标志位
//一阶低通滤波
#define FLP(x,y,a) ((a*(x)+(10-a)*(y))/10)      //First order low pass filter, a为0~10的整数
int16_t Motor_PID(MOTOR_enum motor);
int16_t Motor_PID_average();
void my_pid(void);
extern float Turn_PID_GYRO(void);//陀螺仪PID，位置式
extern float Turn_PID_CAMERA(void);//摄像头位置式PD
extern int16_t Turn_PID_DIANCI(void);
extern float Turn_PID_Angle(void);






#endif /* PID_H_ */
