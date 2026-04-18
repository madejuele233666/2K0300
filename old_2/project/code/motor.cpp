#include "motor.h"
#include "FUZZY_PID.h"

/***************速度环参数****************/
int speed_control_mode =0;
int speed_encoder_left = 0;
int speed_encoder_right = 0;
pid_type_def speed_pid_left;
pid_type_def speed_pid_right;
float PID_speed_left[5] = {3,5.3,2};//6.7 3.2  
float PID_speed_right[5] = {3,6.3,5};//2.5 3.5sss
int speed_target = 0;
int speed_traget_L = 0, speed_traget_R = 0, speed_middle = 0;
int speed_run = 300;//2700 -0.018 89          2500 -0.016 89
int speed_run_slow = 675;//改这个没用，要改main函数里面的
int speed_run_jian = 27;//600 600
int speed_jia = 0;
float add_k = 0;
float speed_pid_output_left=0;  //
float speed_pid_output_right=0;  //
/***************************************/
/***************舵机内环角速度环pid***************/
float PID_servo[5] = {8.235, 0, 29.45, 0, 0};// 8.005 53.5 0.103         65.5   0.0365   //这个的P项没有用了，只有D项有用
pid_type_def servo_pid;
float servo_pid_output=0;
float Island_P = 8.417;     //环岛的P
float Island_D = 37.7;      //环岛的D


/*pid初始化*/
void PID_init(pid_type_def *pid, uint8_t mode, float PID[5], float max_out,float max_iout)
{
    if (pid == NULL || PID == NULL)
    {
        return;
    }
    pid->mode = mode;
    pid->Kp = PID[0];
    pid->Ki = PID[1];
    pid->Kd = PID[2];
    pid->Kp2 = PID[3];
    pid->Kd2 = PID[4];
    pid->max_out = max_out;
    pid->max_iout = max_iout;
    pid->Dbuf[0] = pid->Dbuf[1] = pid->Dbuf[2] = 0.0f;
    pid->error[0] = pid->error[1] = pid->error[2] = pid->Pout = pid->Iout =
            pid->Dout = pid->out = 0.0f;
}
/*
*函数功能：pid计算
*输入参数：参数1->输出pid结构体指针   参数2->反馈值（实际值）   参数3->设定值（目标值）
*输出参数：pid计算出的输出
*/
float PID_calc(pid_type_def *pid, float ref, float set)
{
    if (pid == NULL)
    {
        return 0.0f;
    }
    pid->error[2] = pid->error[1];
    pid->error[1] = pid->error[0];
    pid->set = set;
    pid->fdb = ref;
    pid->error[0] = set - ref;
    if (pid->mode == PID_POSITION)
    {
        pid->Pout = pid->Kp * pid->error[0];
        pid->Iout += pid->Ki * pid->error[0];
        pid->Dbuf[2] = pid->Dbuf[1];
        pid->Dbuf[1] = pid->Dbuf[0];
        pid->Dbuf[0] = (pid->error[0] - pid->error[1]);
        pid->Dout = pid->Kd * pid->Dbuf[0];
        LimitMax(pid->Iout, pid->max_iout);
        pid->out = pid->Pout + pid->Iout + pid->Dout;
        LimitMax(pid->out, pid->max_out);
    }
    else if (pid->mode == PID_DELTA)
    {
        pid->Pout = pid->Kp * (pid->error[0] - pid->error[1]);
        pid->Iout = pid->Ki * pid->error[0];
        pid->Dbuf[2] = pid->Dbuf[1];
        pid->Dbuf[1] = pid->Dbuf[0];
        pid->Dbuf[0] = (pid->error[0] - 2.0f * pid->error[1] + pid->error[2]);
        pid->Dout = pid->Kd * pid->Dbuf[0];
        pid->out += pid->Pout + pid->Iout + pid->Dout;
        LimitMax(pid->out, pid->max_out);
    }
    return pid->out;
}

/* 
*
*舵机专属pid计算（二次Kp）
*
*
*/
float PID_calc_servo(pid_type_def *pid, float ref, float set)
{
    if (pid == NULL)
    {
        return 0.0f;
    }
    pid->error[2] = pid->error[1];
    pid->error[1] = pid->error[0];
    pid->set = set;
    pid->fdb = ref;
    pid->error[0] = set - ref;

    if (pid->mode == PID_POSITION)
    {
        pid->Pout = pid->Kp * pid->error[0];
        // 二次项
        pid->Pout2 = abs(pid->error[0]) * pid->error[0] * pid->Kp2;

        pid->Dbuf[2] = pid->Dbuf[1];
        pid->Dbuf[1] = pid->Dbuf[0];
        pid->Dbuf[0] = (pid->error[0] - pid->error[1]);
        pid->Dout = pid->Kd * pid->Dbuf[0];
        pid->Dout2 = pid->Kd2 * Gyr_rad.Z;
        pid->out = pid->Pout + pid->Pout2 + pid->Dout + pid->Dout2;
        LimitMax(pid->out, pid->max_out);
    }
    else if (pid->mode == PID_DELTA)
    {
        pid->Pout = pid->Kp * (pid->error[0] - pid->error[1]);
        pid->Iout = pid->Ki * pid->error[0];
        pid->Dbuf[2] = pid->Dbuf[1];
        pid->Dbuf[1] = pid->Dbuf[0];
        pid->Dbuf[0] = (pid->error[0] - 2.0f * pid->error[1] + pid->error[2]);
        pid->Dout = pid->Kd * pid->Dbuf[0];
        pid->out += pid->Pout + pid->Iout + pid->Dout;
        LimitMax(pid->out, pid->max_out);
    }
    return pid->out;
}

/*************************速度环********************/
/*
*函数功能：电机基础pid输出
*输入参数：无
*输出参数：无
*/
//左电机pid的基础输出       
void SPEED_PID_OUT_LEFT(void)
{
    speed_encoder_left = encoder_get_count(ENCODER_1);
    //printf("speed：%d\n",speed_encoder_left);

    speed_pid_output_left = PID_calc(&speed_pid_left, speed_encoder_left, l_speed);//pid计算


    //调电机pid需要用的
    // char buffer[50];
    // int len;
    // len = sprintf(buffer,"%.3f,%d,%d,%.5f,%5f,%.2f\n",speed_pid_left.Kp,speed_encoder_left,l_speed,speed_pid_left.Ki,speed_pid_left.Kd,speed_pid_output_left);
    // tcp_client_send_data((const uint8 *)buffer,len);
}


//右电机pid的基础输出
void SPEED_PID_OUT_RIGHT(void)
{
    speed_encoder_right = encoder_get_count(ENCODER_2);
    //printf("speed：%d\n",speed_encoder_right);

    speed_pid_output_right = PID_calc(&speed_pid_right, -speed_encoder_right, r_speed);//pid计算

    //调电机pid需要用的
    // char buffer[50];
    // int len;
    // len = sprintf(buffer,"%.3f,%d,%d,%.5f,%.2f\n",speed_pid_right.Kp,-speed_encoder_right,r_speed,speed_pid_right.Ki,speed_pid_output_right);
    // tcp_client_send_data((const uint8 *)buffer,len);
}


/**
 * 函数作用：舵机转向pid   
 * 输入参数：无
 * 输出参数：无（改变）
 */
void SERVO_PID_OUT(void)//舵机闭环控制
{
    //servo_pid.Kp = (abs((exp(-abs(mid_servo-Image_middle))-1)/(exp(-abs(mid_servo-Image_middle))+1))/2+0.5) * Kp_base;
    servo_pid_output = PID_calc_servo(&servo_pid, mid_servo, Image_middle);      //pid控制器结构体参数（包含pid参数和状态变量）
}

/**
 * 函数作用：舵机转向pid(模糊Pid)   
 * 输入参数：无
 * 输出参数：无（改变）
 */
void PD_Camera(void)
{
    float  u;
    float  P=servo_pid.Kp;//模糊p
    float  D=servo_pid.Kd;//d死的
    volatile static float error_current,error_last;
    float ek,ek1;
    error_current=Image_middle - mid_servo;
    ek=error_current;
    ek1=error_current-error_last;
    DuoJi_GetP(&P ,Height-farthest_line, Image_middle - mid_servo);//新型模糊P,P的地址，搜索截止行的位置，误差
    if(circle_find != 0)//环岛单独的pd，其他阶段都是模糊控制
    {
        P=Island_P;
        D=Island_D;
    }
    // printf("P:%.3f\n",P);
    u=P*ek+D*ek1+servo_pid.Kd2 * Gyr_rad.Z;;
    error_last=error_current;

    if(u >= servo_pid.max_out)//限幅处理
        u = servo_pid.max_out;
    else if(u <= -servo_pid.max_out)//限幅处理
        u = -servo_pid.max_out;
    servo_pid_output = u;
}

/**
 * 函数作用：特殊场景的处理             //对摄像头参数有要求，先注释掉
 * 输入参数：无
 * 输出参数：无
 */
int mid_servo_last=Width / 2;
//特殊场景修正
void Steer_pid(void)
{
    int i,mean_edge;
    static float sequence_estimation_K, sequence_estimation_servo_rate;//出环惯序估计
     //路障
    if(blockFlag.seek==roadblockfind)
    {
        if(blockFlag.dir==blockLeft)
        {
            mid_servo += blok;//路障在左，向右避让
        }
        else if(blockFlag.dir==blockRight)
        {
            mid_servo -= blok;//路障在右，向左避让
        }
    }
    //环岛
    else if(circle_find==1)
    {
        //如果环岛补线结束
        if (circleFlag.repairLine == RepairOver)
        {
            //如果还在环岛里，且转过的角度小于70度
            //左环岛左环岛左环岛
            if (circleFlag.direction == CircleLeft&&fabs(cricle_delta_angle)<70)//右环为正
            {
                //计算边缘平均值
                mean_edge = 0;
                for (i = START_LINE; i > START_LINE / 2; i = i - 2)
                {
                    mean_edge += edgeLeft.xCoordinate[i].inside;
                }
                mean_edge = mean_edge / (START_LINE / 4);
                if (mean_edge > 100)//想加惯序估计
                {
                    mid_servo = mid_servo_last;
                }
            }
            else if (circleFlag.direction == CircleRight&&fabs(cricle_delta_angle)<70)//右转servo_rate为负
            {
                mean_edge = 0;
                for (i = START_LINE; i > START_LINE / 2; i = i - 2)
                {
                    mean_edge += edgeRight.xCoordinate[i].inside;
                }
                mean_edge = mean_edge / (START_LINE / 4);
                if (mean_edge < 60)//想加惯序估计//谁大要谁
                {
                    mid_servo = mid_servo_last;
                }
            }
        }

        //出环固定打角
        if (circle_find == 1
                && circleFlag.repairLine != RepairOver)
        {
            sequence_estimation_K = 0;
            sequence_estimation_servo_rate = 0;
        }

        else if ((circleFlag.repairLine == RepairOver)&& (circleFlag.fixSteer == FixNull))
        {
            if (sequence_estimation_K < 1000)
            {
                sequence_estimation_K += 1;
                sequence_estimation_servo_rate =
                        sequence_estimation_servo_rate* (sequence_estimation_K - 1)
                                / sequence_estimation_K + ((mid_servo ) / sequence_estimation_K);
                //惯序估计
            }
        }
        
        else if (circleFlag.fixSteer == FixStart)
        {
            if (mid_servo < sequence_estimation_servo_rate
                    && circleFlag.direction == CircleRight)//右环出环时右转  右转servo_rate为负数
            {
                mid_servo = (1.4+circle_k)*sequence_estimation_servo_rate;//如果左打轮就惯序估计
                //mid_servo = 135;
            }
            else if (mid_servo > sequence_estimation_servo_rate
                    && circleFlag.direction == CircleLeft)
            {
                mid_servo = (1-circle_k)*sequence_estimation_servo_rate;
                //mid_servo = 25;
            }
            if(mid_servo>180)
            {
                mid_servo = 180;
            }
            else if(mid_servo<0)
            {
                mid_servo = -10;
            }
        }
        mid_servo_last = mid_servo;
    }
}


/**
 * 函数作用：pid的最终输出
 * 输入参数：
 * 输出参数：
 */


void PID_OUTPUT(void)
{    
    //printf("pwm_out1:%.2f     pwm_out2:%d\n",speed_pid_output_left,pwm_output_left_forward);

    //防止输出Pwm过大或者过小
    if(speed_pid_output_left>8000)
        speed_pid_output_left=8000;
    else if(speed_pid_output_left<-8000)
        speed_pid_output_left=-8000;
    if(speed_pid_output_right>8000)
        speed_pid_output_right=8000;
    else if(speed_pid_output_right<-8000)
        speed_pid_output_right=-8000;
    if(speed_pid_output_left < 0)
    {
        gpio_set_level(MOTOR2_DIR, 0);
    }
    else
    {
        gpio_set_level(MOTOR2_DIR, 1);
    }
    if(speed_pid_output_right< 0)
    {
        gpio_set_level(MOTOR1_DIR, 0);
    }
    else
    {
        gpio_set_level(MOTOR1_DIR, 1);
    }
    
/****************************************电机*************************** */
    //pwm_set_duty(MOTOR1_PWM, 1500);//右电机1950
    //pwm_set_duty(MOTOR2_PWM, 2080);//左电机1950
   
    pwm_set_duty(MOTOR2_PWM, fabs(speed_pid_output_left));//左电机1950
    pwm_set_duty(MOTOR1_PWM, fabs(speed_pid_output_right));//右电机
}
