#include "all_init.h"

/**
 * 函数作用：相关引脚的初始化
 * 输入参数：无
 * 输出参数：无
 */

void All_init ()
{
    /*按键、拨码、无刷电机、电机、编码器都不用写代码来初始化*/

    ips200_init("/dev/fb0");

    //电机（这个得改成两个）
    PID_init(&speed_pid_left,PID_DELTA,PID_speed_left,8000,4000);//速度环
    PID_init(&speed_pid_right,PID_DELTA,PID_speed_right,8000,4000);//速度环
    
    
    PID_init(&servo_pid,PID_POSITION,PID_servo,Sevro_Limit,0);//舵机 角速度环 单位pwm
    InitMH();


    //如果定时器1可以用，这个就不用写
    //run_state.start_state=start_over;


}
