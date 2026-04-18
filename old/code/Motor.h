/*
 * Motor.h
 *
 *  Created on: 2025年3月6日
 *      Author: 27477
 */

#ifndef _MOTOR_H_
#define _MOTOR_H_

//舵机
#define SERVO TIM8_PWM_CH2_B7

//电机
extern char dir_L_R,dir_R_R;
extern int speed_L,speed_R;
#define L_PWM_PIN ATOM0_CH2_P21_4
#define R_PWM_PIN ATOM0_CH1_P21_3
#define L_PWM2_PIN ATOM0_CH3_P21_5
#define R_PWM2_PIN ATOM1_CH0_P21_2


#define TOP_SPEED 2300                //满占空比最高速度（左右通用）

typedef enum{
    LEFT_MOTOR,
    RIGHT_MOTOR,
}MOTOR_enum;

typedef enum{
    START,
    STOP,
}SPEED_STATE;

typedef struct{
    int16_t Speed_control;
    int16_t Speed_ave;
    int16_t Speed_temp;
}Speed_paraments;

extern Speed_paraments Speed_L;
extern Speed_paraments Speed_R;
extern SPEED_STATE Speed_state;

extern int16_t Speed_ave_L,Speed_ave_R;

int16_t speed_get(MOTOR_enum motor);
void speed_set(MOTOR_enum motor, int16_t output);
int16_t speed_average(MOTOR_enum motor, int16_t speed, uint8_t times);
void speed_stop(void);
void speed_control(void);
int16 speed_get_mean();
void speed_set_chasu(int16_t output_motor,int16_t output_chasu);
void G_out_PID();
void Steer_pid(void);
int16_t WHITE_average( int16_t speed, uint8_t times);










#endif /* CODE_MOTOR_H_ */
