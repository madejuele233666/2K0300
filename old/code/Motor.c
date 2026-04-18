#include "zf_common_headfile.h"
#include "Motor.h"
#include "PID.h"
#include "ZiTaiJieSuan.h"
#include "Camera.h"
Speed_paraments Speed_L={
    SPEED_START,
    0,
    0,
};
Speed_paraments Speed_R={
    SPEED_START,
    0,
    0,
};
SPEED_STATE Speed_state=START;
int mid_servo_last = 80;
int R_output;
extern float utest;
float test_sequence_estimation_servo_rate = 0;
void Steer_pid(void)
{
//     int i,mean_edge;
     static float sequence_estimation_K, sequence_estimation_servo_rate;//出环惯序估计
     //环岛
     test_sequence_estimation_servo_rate = sequence_estimation_servo_rate;
     if(circle_find==1)
     {
//         if (circleFlag.repairLine == RepairOver)
//         {
//              if (circleFlag.direction == CircleLeft&&fabs(cricle_delta_angle)<70)//右环为正
//              {
//                  mean_edge = 0;
//                  for (i = START_LINE; i > START_LINE / 2; i = i - 2)
//                  {
//                      mean_edge += edgeLeft.xCoordinate[i].inside;
//                  }
//                  mean_edge = mean_edge / (START_LINE / 4);
//                  if (mean_edge > 100)//想加惯序估计
//                  {
//                      mid_servo = mid_servo_last;
//                  }
//              }
//              else if (circleFlag.direction == CircleRight&&fabs(cricle_delta_angle)<70)//右转servo_rate为负
//              {
//                  mean_edge = 0;
//                  for (i = START_LINE; i > START_LINE / 2; i = i - 2)
//                  {
//                      mean_edge += edgeRight.xCoordinate[i].inside;
//                  }
//                  mean_edge = mean_edge / (START_LINE / 4);
//                  if (mean_edge < 60)//想加惯序估计//谁大要谁
//                  {
//                      mid_servo = mid_servo_last;
//                  }
//              }
//          }
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
                                  / sequence_estimation_K + ((Err) / sequence_estimation_K);
                  //惯序估计
              }
          }
          else if (circleFlag.fixSteer == FixStart)
          {
              if (
//                      Err > sequence_estimation_servo_rate
//                      &&
                      circleFlag.direction == CircleRight)//右环出环时右转  右转servo_rate为负数
              {
                  Err = (1+circle_k_err)*sequence_estimation_servo_rate;//如果左打轮就惯序估计
              }
              else if (
//                      Err < sequence_estimation_servo_rate
//                      &&
                      circleFlag.direction == CircleLeft)
              {
                  Err = (1+circle_k_err)*sequence_estimation_servo_rate;
              }
              if(Err>60)
              {
                  Err = 60;
              }
              else if(Err<-60)
              {
                  Err = -60;
              }
          }
//          tft180_show_uint(0, 80, sequence_estimation_servo_rate,3);
//          mid_servo_last = Err;
     }
}
//-------------------------------------------------------------------------------------------------------------------
//  @brief      获取两轮编码器速度均值（结合定时中断使用）
//  @param                 选择电机
//  @return     int16_t
//  Sample usage:               speed_get_mean();
//-------------------------------------------------------------------------------------------------------------------
int16 speed_get_mean()
{
    int16 temp = 0;
    PID_R.Speed=-encoder_get_count(TIM2_ENCODER);//采集R
    encoder_clear_count (TIM2_ENCODER);
    PID_L.Speed=encoder_get_count(TIM6_ENCODER);//采集L
    encoder_clear_count (TIM6_ENCODER);

    temp = (PID_L.Speed+PID_R.Speed)/2;


//    temp = PID_R.Speed;
    PID_All.Speed= temp; // 左右电机测速取均值
    static int speed_counter  = 0;
    if (PID_R.Speed <= -40 || PID_L.Speed <= -40)
//        Emergency_Stop = 1;
        speed_counter++;
    else
        {
            speed_counter --;
            if (speed_counter < 0)
                speed_counter = 0;
        }
    if (speed_counter>=25)
        {
        gpio_set_level(P21_2,1);
        Emergency_Stop = 1;
        }
    return temp;
}

//-------------------------------------------------------------------------------------------------------------------
//  @brief      设置速度（结合定时中断使用）
//  @param      output_motor               速度环PID输出的占空比
//  @param      output_chasu               左右轮期望差速的占空比
//  @return     void
//  Sample usage:               speed_set_chasu(output_motor,output_chasu);
//-------------------------------------------------------------------------------------------------------------------
void speed_set_chasu(int16_t output_motor,int16_t output_chasu)
{
    int16_t output_strong=0;
    int L_chasu_PWM,R_chasu_PWM;//健壮！

    output_strong = output_motor;
    float k_gain = 0;
    int DUTY_MAX = PID_MOTOR_DUTY_MAX - 20 * utest;

    if(output_chasu>0)
    {
//        if(output_strong>DUTY_MAX)
//            output_strong = DUTY_MAX;
        R_chasu_PWM =output_strong + (1-k_gain) * output_chasu;

        L_chasu_PWM =output_strong - (1+k_gain) * output_chasu;
    }
    else
    {
//        if(output_strong>DUTY_MAX)
//            output_strong = DUTY_MAX;
        L_chasu_PWM =output_strong - (1-k_gain) * output_chasu;

        R_chasu_PWM =output_strong + (1+k_gain) * output_chasu;
    }

//    L_chasu_PWM =output_strong - output_chasu;
//    R_chasu_PWM =output_strong + output_chasu;
//    if (output_strong>= 0)
//        L_chasu_PWM =output_strong - output_chasu;
//    else R_chasu_PWM =output_strong + output_chasu;

    if(L_chasu_PWM>=PID_setout_MAX)        L_chasu_PWM=PID_setout_MAX-1;        //限幅
    else if(L_chasu_PWM<=-PID_setout_MAX)  L_chasu_PWM=-PID_setout_MAX+1;

    if(R_chasu_PWM>=PID_setout_MAX)        R_chasu_PWM=PID_setout_MAX-1;        //限幅
    else if(R_chasu_PWM<=-PID_setout_MAX)  R_chasu_PWM=-PID_setout_MAX+1;

    if(L_chasu_PWM >= 0) // 左轮正转
    {
        gpio_set_level(P22_2,1); // 0 表示左轮正转
        pwm_set_duty(L_PWM_PIN, L_chasu_PWM);
    }
    else // 此时反转 限制反转
    {
        gpio_set_level(P22_2,0);// 1 表示左轮反转
//        if (L_chasu_PWM<=-2000)
//            L_chasu_PWM = -2000;
        pwm_set_duty(L_PWM_PIN,  -L_chasu_PWM);
    }
    if(R_chasu_PWM >= 0)
    {
        gpio_set_level(P22_1,1); // 1 表示右轮正转
        pwm_set_duty(R_PWM_PIN, R_chasu_PWM);
    }
    else // 右轮正转进入此语句 限制反转
    {
        gpio_set_level(P22_1,0); // 0 表示右轮反转
//        if(R_chasu_PWM<=-2000)
//            R_chasu_PWM = -2000;
        pwm_set_duty(R_PWM_PIN,  -R_chasu_PWM);
    }
}

//-------------------------------------------------------------------------------------------------------------------
//  @brief      获取速度（结合定时中断使用）
//  @param      motor           选择电机
//  @return     int16_t
//  Sample usage:               speed_get(RIGHT_MOTOR);
//-------------------------------------------------------------------------------------------------------------------
int16_t speed_get(MOTOR_enum motor)
{
  int16_t speed_temp=0;

  if(motor==RIGHT_MOTOR)
  {
      speed_temp=-encoder_get_count(TIM2_ENCODER);//采集R
      encoder_clear_count (TIM2_ENCODER);
  }
  else if(motor==LEFT_MOTOR)
  {
      speed_temp=encoder_get_count(TIM6_ENCODER);//采集L
      encoder_clear_count (TIM6_ENCODER);
  }
  else
      return 0;

  return speed_temp;

}

//-------------------------------------------------------------------------------------------------------------------
//  @brief      设置占空比
//  @param      output          占空比大小
//  @param      motor           选择电机
//  @return     void
//  Sample usage:               speed_set(RIGHT_MOTOR,5000);
//-------------------------------------------------------------------------------------------------------------------
void speed_set(MOTOR_enum motor, int16_t output)
{
    int16_t output_strong=0;                                //健壮！

    output_strong=output;

    if(output_strong>=PID_MOTOR_DUTY_MAX)        output_strong=PID_MOTOR_DUTY_MAX-1;        //限幅
  else if(output_strong<=-PID_MOTOR_DUTY_MAX)  output_strong=-PID_MOTOR_DUTY_MAX+1;
    if(motor==RIGHT_MOTOR)
    {
        if(output_strong >= 0)
        {
            gpio_set_level(P22_1,1); // 1 表示右轮正转
            pwm_set_duty(R_PWM_PIN, output_strong);

        }
        else // 右轮正转进入此语句
        {
            gpio_set_level(P22_1,0); // 0 表示右轮反转
            pwm_set_duty(R_PWM_PIN,  -output_strong);

        }
    }
    else if(motor==LEFT_MOTOR)
    {
        if(output_strong >= 0)// 左轮正转进入此语句
        {
            gpio_set_level(P22_2,1); // 0 表示左轮正转
            pwm_set_duty(L_PWM_PIN, output_strong);
//            pwm_set_duty(L_PWM2_PIN,  0);

        }
        else
        {
            gpio_set_level(P22_2,0);// 1 表示左轮反转
            pwm_set_duty(L_PWM_PIN,  -output_strong);
        }
    }
}


//-------------------------------------------------------------------------------------------------------------------
//  @brief      停车
//  @return     void
//  Sample usage:               speed_stop();
//-------------------------------------------------------------------------------------------------------------------

void speed_stop(void)
{
    if(PID_L.Speed_Target != 0)
    {
        Speed_L.Speed_temp=PID_L.Speed_Target;
        PID_L.Speed_Target=0;
    }
    if(PID_R.Speed_Target != 0)
    {
        Speed_R.Speed_temp=PID_R.Speed_Target;
        PID_R.Speed_Target=0;
    }
}


extern int pid_output_R,pid_output_L;
extern float G_out;
void G_out_PID()
{
    PID_L.Speed = speed_get(LEFT_MOTOR);
    PID_R.Speed = speed_get(RIGHT_MOTOR);
    PID_R.Speed_last=PID_R.Speed;
    PID_L.Speed_last=PID_L.Speed;

    PID_TURN_GYRO_CAMERA.W_Target=Turn_PID_CAMERA(); // 角度环
    PID_TURN_GYRO_CAMERA.W=Gyr_rad.Z*57.3248;// 角速度环
    G_out=Turn_PID_GYRO();

    PID_L.Speed_Target=(int16_t)((1-G_out)*PID_L.SPEED_MID);
    PID_R.Speed_Target=(int16_t)((1+G_out)*PID_R.SPEED_MID);

    pid_output_R = Motor_PID(RIGHT_MOTOR);
    speed_set(RIGHT_MOTOR,pid_output_R);

    pid_output_L = Motor_PID(LEFT_MOTOR);
    speed_set(LEFT_MOTOR,pid_output_L);
}
