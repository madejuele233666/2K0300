#ifndef MOTOR_H_
#define MOTOR_H_

#include "zf_common_headfile.h"

#define PERCENT_MAX 1

#define Image_middle         79


/*************************AKEMAN_DIFFSPEED*************************/
#define wheel_distance        16    //轮距（左右轮之间的距离）
#define axle_distance         22    //轴距（前后轮之间的距离）
//上述宏定义可以查一下阿卡曼转角的原理
void akeman_diffspeed(int middle_speed,float angle_diff,int *speed_L,int *speed_R);

#define CAR_WIDTH 15
#define CAR_LENGHTH 20

typedef struct
{
    uint8_t mode;
    //PID 三参数
    float Kp;
    float Ki;
    float Kd;
    float Kp2;//舵机二次kp参数
    float Kd2;//舵机二次kd参数

    float max_out;  //最大输出
    float max_iout; //最大积分输出

    float set;
    float fdb;

    float out;
    float Pout;
    float Pout2;//舵机二次kp参数
    float Iout;
    float Dout;
    float Dout2;//舵机二次kd参数
    float Dbuf[3];  //微分项 0最新 1上一次 2上上次
    float error[3]; //误差项 0最新 1上一次 2上上次
} pid_type_def;

extern pid_type_def speed_pid_left;
extern pid_type_def speed_pid_right;
extern pid_type_def angle_pid;
extern pid_type_def anglespeed_pid;
extern pid_type_def servo_pid;
extern pid_type_def angle_pid_behind;
extern float PID_speed_left[5];
extern float PID_speed_right[5];
extern float PID_angle[5];
extern float PID_anglespeed[5];
extern float PID_servo[5];
extern float PID_angle_behind[5];


extern int speed_jia;
extern int speed_target;
extern int speed_run;
extern int speed_run_slow;
extern float add_k;
extern float speed_pid_output_left;
extern float speed_pid_output_right;
extern float angle_pid_output;
extern float servo_pid_output;
extern int speed_encoder_left;
extern int speed_encoder_right;
extern int speed_control_mode;
#define ANGLE_to_RAD 3.14/180

extern int wandao_distance;
extern int wandaorow;
#define LimitMax(input, max)   \
    {                          \
        if (input > max)       \
        {                      \
            input = max;       \
        }                      \
        else if (input < -max) \
        {                      \
            input = -max;      \
        }                      \
    }

enum PID_MODE
{
    PID_POSITION = 0,
    PID_DELTA
};

void PID_init(pid_type_def *pid, uint8_t mode, float PID[5], float max_out,float max_iout);
void ANGLE_PID_OUT(void);
void SPEED_PID_OUT_LEFT(void);
void SPEED_PID_OUT_RIGHT(void);
void ANGLESPEED_PID_OUT(void);
void PID_OUTPUT(void);
float PID_calc(pid_type_def *pid, float ref, float set);
void PID_clear(pid_type_def *pid);
void pid_isrhandler(void);
float Gyro_PID_position();
void SERVO_PID_OUT(void);
extern pid_type_def car_distance;
extern float PID_Distance[5] ;
extern int targetdistance;
void Steer_pid(void);
extern  float gyro_z_intergal;
extern float PID_bottom_motor[3];//35  7.5   100,0,200
extern pid_type_def bottom_motor_pid;
extern float bottom_motor_pid_output;
void bottom_PID_OUT(void);
extern int mid_servo_last;
extern int speed_run_jian;
extern void FuzzyPIDcontroller(float e_max, float e_min,
        float ec_max, float ec_min,
        float kp_max, float kp_min,
        float erro, float erro_c,
        float ki_max,float ki_min,
        float kd_max,float kd_min,float kp_bias,float ki_bias,float kd_bias,pid_type_def *pid);
extern float last_error,error,error_deta,angle_pid_output_square,servo_pid_output_gyro;
void PD_Camera(void);
/*编码器*/
#define ENCODER_1           "/dev/zf_encoder_1"//left
#define ENCODER_2           "/dev/zf_encoder_2"//right


/*电机*/
//左电机maybe
#define MOTOR1_DIR   "/dev/zf_driver_gpio_motor_1"
#define MOTOR1_PWM   "/dev/zf_device_pwm_motor_1"
//右电机maybe
#define MOTOR2_DIR   "/dev/zf_driver_gpio_motor_2"
#define MOTOR2_PWM   "/dev/zf_device_pwm_motor_2"


/*舵机*/
// 定义驱动路劲，该路劲由设备树生成
#define SERVO_MOTOR1_PWM            "/dev/zf_device_pwm_servo"

// 定义主板上舵机频率  请务必注意范围 50-300
// 如果要修改，需要直接修改设备树。
#define SERVO_MOTOR_FREQ            (servo_pwm_info.freq)                       

// 在设备树中，默认设置的10000。如果要修改，需要直接修改设备树。
#define PWM_DUTY_MAX                (servo_pwm_info.duty_max)      

extern float Kp_base;


#endif /* MOTOR_H_ */





