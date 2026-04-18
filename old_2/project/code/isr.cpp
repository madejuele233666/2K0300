#include "isr.h"

#define wheel_distance        16
#define axle_distance         22
int black_time;//黑线检测计时器（用于判断是否出赛道了）
int temp_pwm = 500;//PWM临时变量，用于无刷电机缓启动(原本是500)
float ATT_angle_jiaodu_last;
int Speed_dif;
int l_speed = 0;  //这个要改为0
int r_speed = 0;  //这个要改为0
float servo_angle;
float chasu_P;   //4.1
float zhidao_P = 7.70;
float circle_P = 7.45;
int zhidao_speed = 500;
int circle_speed = 500;
extern int fache;

uint8 TIM3_divide = 0;//用于10ms任务分频
/* 电机调PID参数 *///5ms速度环
void TIM4_IRQHANDLER(void)
{
    if(gpio_get_level(SWITCH_0) == 1 && gpio_get_level(SWITCH_1) == 0 && gpio_get_level(KEY_0) == 0)
    {
        l_speed += 5 ;
        r_speed += 5 ;
    }
    else if(gpio_get_level(SWITCH_0) == 1 && gpio_get_level(SWITCH_1) == 0 && gpio_get_level(KEY_1) == 0)
    {
        l_speed -= 5 ;
        r_speed -= 5 ;
    }
    else if(gpio_get_level(SWITCH_0) == 0 && gpio_get_level(SWITCH_1) == 0 && gpio_get_level(KEY_0) == 0)
    {
        speed_pid_left.Kp += 0.01;
        speed_pid_right.Kp +=0.01;
    }

    else if(gpio_get_level(SWITCH_0) == 0 && gpio_get_level(SWITCH_1) == 0 && gpio_get_level(KEY_1) == 0)
    {
        speed_pid_left.Kp -=0.01;
        speed_pid_right.Kp -=0.01;
    }

    else if(gpio_get_level(SWITCH_1) == 1 && gpio_get_level(KEY_0) == 0)
    {
        speed_pid_left.Ki += 0.01;
        speed_pid_right.Ki += 0.01;
    }

    else if(gpio_get_level(SWITCH_1) == 1 && gpio_get_level(KEY_1) == 0)
    {
        speed_pid_left.Ki -=0.01;
        speed_pid_right.Ki -=0.01;
    }
    SPEED_PID_OUT_LEFT();//速度环
    SPEED_PID_OUT_RIGHT();
    PID_OUTPUT();
}


enum START_FLAG start_flag;
int accl_time=0;//加速时间，启动后2s逐渐加速
bool accl_flag=0;//是否完成加速
#define accl_wait_time 300
int TIM1_divide10ms;
// struct timespec ts1;
// struct timespec ts2;


//5ms定时器函数
void TIM1_IRQHANDLER(void)
{
    /******************************帧数计算**************************************/
    // ts2.tv_sec = ts1.tv_sec;
    // ts2.tv_nsec = ts1.tv_nsec;
    // clock_gettime(CLOCK_REALTIME,&ts1);
    // printf("%lld\n",(long long)(ts1.tv_sec-ts2.tv_sec) * 1000000LL+(long long)(ts1.tv_nsec-ts2.tv_nsec)/1000LL);

    //printf("speed_target:%d\n",speed_target);
    //printf("acc_time:%d\n",accl_time);
    //printf("slow:%d,speed_target:%d,acc_flag:%d\n",speed_run_slow,speed_target,accl_flag);
    if(accl_flag==1 && circle_find != 1 && zhidao_flag == 0)
    {
        speed_target = speed_run_slow;//7.85
        if(speed_target <= 0)
            speed_target = 0;
        chasu_P = zhidao_P;
    }
    else if(accl_flag == 1 && circle_find == 1)//7.58
    {
        speed_target = circle_speed;
        if(speed_target <= 0)
            speed_target = 0;
        chasu_P = circle_P;
    }
    else if(accl_flag==1 && zhidao_flag == 1)
    {
        speed_target = zhidao_speed;//+10
        if(speed_target <= 0)
            speed_target = 0;
        chasu_P = zhidao_P;
        //printf("ok\n");
    }

    //差速计算
    float theta_rad = (servo_angle) * M_PI / 180.0;
    Speed_dif = ((wheel_distance*tan(theta_rad)) / (2*axle_distance))*speed_target*chasu_P;

    if(Speed_dif > 0)
    {
        l_speed = speed_target - Speed_dif;
        r_speed = speed_target;
    }
    else if(Speed_dif < 0)
    {
        r_speed = speed_target + Speed_dif;
        l_speed = speed_target;
    }
    else
    {
        l_speed = speed_target ;
        r_speed = speed_target ;
    }

    if(accl_flag == 0)
    {
        l_speed = 0;
        r_speed = 0;
    }

    //电机控制
    SPEED_PID_OUT_LEFT();//速度环
    SPEED_PID_OUT_RIGHT();

    // speed_encoder_left = encoder_get_count(ENCODER_1);
    //speed_encoder_right = encoder_get_count(ENCODER_2);
    // printf("l_speed:%d\n",speed_encoder_left);
    //printf("r_speed:%d\n",speed_encoder_right);

    PID_OUTPUT();      //输出

    //启动状态各模式
    //车已经启动完成，开始加速（这个speed_target）的用处其实没看懂
    if(run_state.start_state==start_over)
    {
        if(accl_time<accl_wait_time&&accl_flag==0)//启动后2s加满
        {
            accl_time++;

            //这个后面要取消注释
            // speed_target=speed_run*(float)accl_time/accl_wait_time;
        }
        if(accl_time>=accl_wait_time&&accl_flag==0)
        {
            //printf("ok\n");
            accl_flag=1;
        }
    }

    //停车（电机停转，无刷电机停转）
    else if(run_state.start_state==run_stop)
    {
        accl_flag = 0;
        speed_run_slow = 0;
        speed_target = 0;
        //printf("target_speed:%d\n",speed_target);
        //l_speed = r_speed = 0;
        // pwm_set_duty(MOTOR1_PWM, 0);
        // pwm_set_duty(MOTOR2_PWM, 0);
        pwm_set_duty(BRUSHLESS_1,0);//无刷
        pwm_set_duty(BRUSHLESS_2,0);
    }
    //开始启动——启动无刷电机
    else if(run_state.start_state==run_start)
    {
        if(temp_pwm<=BLDC_PWM)
        {
            temp_pwm++;
        }
    }   
    else if(run_state.start_state==run_brushless_start)
    {
        if(temp_pwm<=BLDC_PWM)
        {
            temp_pwm++;
            //printf("temp:%d\n",temp_pwm);
        }

        pwm_set_duty(BRUSHLESS_1, temp_pwm);
        pwm_set_duty(BRUSHLESS_2, temp_pwm);
    }  
}


//10ms定时器
void TIM2_IRQHANDLER(void)
{
    /* 出界保护 */
    // uint8 temp = 0;
    if(highest_line > 118 &&black_time<100)
    {
        black_time++;
    }
    else if(highest_line < 118 && black_time > 0&&run_state.start_state!=run_stop)
    {
        black_time--;
    }
    if(black_time>30)
    {
        // run_state.start_state=run_stop;
        printf("outside\n");
        // temp = 1;
    }
 
    if( fache == 1 && run_state.start_state!=start_over
            &&zebra_flag.zebra_state==Zebra_null && run_state.start_state!=run_stop)
    {
         if(temp_pwm>=BLDC_PWM)
         {
             run_state.start_state=start_over;
         }
         else if(temp_pwm<=BLDC_PWM)
         {
             run_state.start_state=run_start;
         }
    }

    else if( fache == 2 && run_state.start_state!=start_over
        &&zebra_flag.zebra_state==Zebra_null && run_state.start_state!=run_stop)
    {
        if(temp_pwm>=BLDC_PWM)
        {
            run_state.start_state=start_over;
        }
        else if(temp_pwm<=BLDC_PWM)
        {
            run_state.start_state=run_brushless_start;
        }
    }


    Prepare_Data();//陀螺仪
    IMUupdate(&Gyr_rad,&Acc_filt,&ATT_Angle);

    /*******************下面为特殊元素处理（看是否到达特定元素）**********************/
    //环岛
    if(circleFlag.lianxufaxian_distancedelay > 0)
    {
        circleFlag.lianxufaxian_distancedelay -= (speed_encoder_left-speed_encoder_right)/2;
        //circleFlag.lianxufaxian_distancedelay -= ((float)speed_encoder_left);
        //printf("distance:%.3f\n", circleFlag.lianxufaxian_distancedelay);
    }


    //路障
    if(blockFlag.seek==roadblockfind && blockFlag.delay_distance > 0)
    {
        blockFlag.delay_distance -= (int)(speed_encoder_left-speed_encoder_right)/2;
        printf("distance:%d\n",blockFlag.delay_distance);
        //blockFlag.delay_distance -= (int)(speed_encoder_left)/2/3;
        if(blockFlag.delay_distance <= 0)
        {
            blockFlag.seek=roadblocknotfind;
            blockFlag.delay_distance = 0;
        }
    }


    //斑马线
    if(zebra_flag.zebra_state==Zebra_find&&zebra_flag.zebra_distance>=0)
    {
        zebra_flag.zebra_distance -= (int)(speed_encoder_left-speed_encoder_right)/2;
        //zebra_flag.zebra_distance -= (int)(speed_encoder_left)/2;
    }
    else if(zebra_flag.zebra_state==Zebra_find&&zebra_flag.zebra_distance<0)
    {
        run_state.start_state=run_stop;//到达斑马线，停车
    }

    //环岛预识别
    if((circle_fast_find_right == 1 || circle_fast_find_left == 1) && maybe_circle_distance > 0)
    {
        maybe_circle_distance -= (int)(speed_encoder_left-speed_encoder_right)/2;
        if(maybe_circle_distance <= 0)
        {
            circle_fast_find_right = 0;
            circle_fast_find_left = 0;
            maybe_circle_distance = 0;
        }
    }

}

void TIM3_IRQHANDLER()
{
    //差速计算
    float theta_rad = (servo_angle) * M_PI / 180.0;
    Speed_dif = ((wheel_distance*tan(theta_rad)) / (2*axle_distance))*speed_target*chasu_P;


    if(Speed_dif > 0)
    {
        l_speed = speed_target - Speed_dif;
        r_speed = speed_target;
    }
    else if(Speed_dif <0)
    {
        r_speed = speed_target + Speed_dif;
        l_speed = speed_target;
    }
    else
    {
        l_speed = speed_target;
        r_speed = speed_target;
    }
    //     l_speed = speed_target ;
    // r_speed = speed_target ;

    //电机控制
    SPEED_PID_OUT_LEFT();//速度环
    SPEED_PID_OUT_RIGHT();
    // speed_encoder_left = encoder_get_count(ENCODER_1);
    // speed_encoder_right = encoder_get_count(ENCODER_2);
    // printf("l_speed:%d\n",speed_encoder_left);
    // printf("r_speed:%d\n",speed_encoder_right);
    PID_OUTPUT();      //输出

}