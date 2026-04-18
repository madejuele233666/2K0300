#ifndef __ISR_H
#define __ISR_H

#include "zf_common_headfile.h"

//有刷电机//
#define MOTOR1_DIR   "/dev/zf_driver_gpio_motor_1"
#define MOTOR1_PWM   "/dev/zf_device_pwm_motor_1"
#define MOTOR2_DIR   "/dev/zf_driver_gpio_motor_2"
#define MOTOR2_PWM   "/dev/zf_device_pwm_motor_2"

#define SERVO_MOTOR1_PWM            "/dev/zf_device_pwm_servo"//舵机

//extern int sum_dir_add_result;
void TIM1_IRQHANDLER(void);
void TIM2_IRQHANDLER(void);
void TIM3_IRQHANDLER(void);
void TIM4_IRQHANDLER(void);

enum START_FLAG {Stop, Delay, Start,Over};//这个是在key里面写的
extern int sum_dir_add_result;
extern int l_speed,r_speed;
extern int temp_pwm;
extern int accl_time;//加速时间，启动后2s逐渐加速
extern bool accl_flag;//是否完成加速
extern int black_time;

#endif


