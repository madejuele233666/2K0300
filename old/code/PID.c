/*
 * PID.c
 *
 *  Created on: 2025年3月6日
 *      Author: 27477
 */
#include "zf_common_headfile.h"
#include "pid.h"
#include "math.h"
#include "camera.h"
#include "ZiTaiJieSuan.h"
#include "FUZZY_PID_UCAS.h"
int Mid_Line = 0;
int JWJC = 1;
extern int circle_find;
#define Chasu_MAX 9000

unsigned char DianciFlag=0;
//摄像头PID
PID_CAMERA PID_TURN_CAMERA={
    22,//P 12
    0,//I为零
    5,//D 5 最优 P 11.3 D 0
    MT9V03X_DVP_W/2,//摄像头目标位置
    MT9V03X_DVP_W/2,//摄像头当前中间行位置
    MT9V03X_DVP_W/2,//上一次摄像头中间行位置
    0,
    0,
    0
};
PID_GYRO PID_TURN_GYRO_CAMERA={
    20,//P 37
    0.0,//I
    9,//D 7.5-》 72
    0.0,//目标角速度
    0.0,//当前角速度
    0.0,
    0.0,
    0.0,
    0.0
};

//陀螺仪角度PID
PID_Angle PID_TURN_Angle={
    7.0,//P
    0.0,//I
    5.0,//D
    0,
    0,
    0,
    0,
    0,
    0
};
PID_GYRO PID_TURN_GYRO_Angle={
    25.0,//P
    0.0,//I
    20.0,//D
    0.0,//目标角速度
    0.0,//当前角速度
    0.0,
    0.0,
    0.0,
    0.0
};

int Speed_base = 77;
//均值电机PID
PID_paraments PID_All={
    240, // 200
    10, // 6.3
    20, // 20
    67,// 速度
    0,
    0,
    0,
    0,
    0,
    67
    // 作为左右速度均值，放入速度环
};
//电机PID
PID_paraments PID_L={
    110, // 4
    0.8, // 3
    20, // 4
    80,//
    0,
    0,
    0,
    0,
    0,
    70
};

PID_paraments PID_R={
    110,//7  4
    0.8,//4  5
    20,//5  5
    80,//
    0,
    0,
    0,
    0,
    0,
    70
};



int delta_speed_up = Delta_SPEED_UP_NORMAL;
int delta_speed_down = Delta_SPEED_DOWN_NORMAL;
float turn_angle_target=0;
float P_test = 0;
int P_data_test = 0,I_data_test = 0,D_data_test = 0,D_all_left=0,D_all_right = 0,vtest = 0,usta=0;
float utest = 0;
//-------------------------------------------------------------------------------------------------------------------
//  @brief      转向PID
//  @param
//  @return     float
//  Sample usage:  Turn_PID_GYRO() ;
//-------------------------------------------------------------------------------------------------------------------
float filted_W = 0;
float Turn_PID_GYRO(void)//陀螺仪PID，位置式
{
    float Gyro_output=0.0;
    float error=0;
    float P_data=0,I_data=0,D_data=0;
    float k = 0.7;// 滤波权重
    int16_t COUNT_MAX=10000;
    filted_W = k*PID_TURN_GYRO_CAMERA.W+(1-k)*PID_TURN_GYRO_CAMERA.W_last;
    // W_Target由摄像头误差经过角度环得到，W为当前角速度，
    error=PID_TURN_GYRO_CAMERA.W_Target-filted_W;
    if(error<=DEAD_AREA_L && error>=-DEAD_AREA_L)                                           //死区
        error=0;
    COUNT_MAX=1200;
    P_data=PID_TURN_GYRO_CAMERA.P*error;
    PID_TURN_GYRO_CAMERA.I_count+=error;
    if(PID_TURN_GYRO_CAMERA.I_count>COUNT_MAX)       PID_TURN_GYRO_CAMERA.I_count=COUNT_MAX;      //限幅
    else if(PID_TURN_GYRO_CAMERA.I_count<-COUNT_MAX)       PID_TURN_GYRO_CAMERA.I_count=-COUNT_MAX;
    I_data=PID_TURN_GYRO_CAMERA.I_count*PID_TURN_GYRO_CAMERA.I;
    D_data=PID_TURN_GYRO_CAMERA.D*(error-PID_TURN_GYRO_CAMERA.error_last);

    PID_TURN_GYRO_CAMERA.D_data_last=D_data;
    PID_TURN_GYRO_CAMERA.error_last=error;                 //更新误差
    Gyro_output=(P_data+I_data+D_data);
    PID_TURN_GYRO_CAMERA.W_last = filted_W;
    if (Gyro_output>=Chasu_MAX) Gyro_output = Chasu_MAX;
    else if (Gyro_output<=-Chasu_MAX)  Gyro_output = -Chasu_MAX;         //5900
//    Gyro_output=(P_data+I_data+D_data)/1000.0;
//    // 限幅
//    if(Gyro_output>0.9)
//        Gyro_output=0.9;
//    else if(Gyro_output<-0.9)
//        Gyro_output=-0.9;
    return Gyro_output;
}
int ucont;
float Turn_PID_CAMERA(void)//摄像头位置式PD
{
    float Camera_output;
    int error=0;
    float P_data=0,D_data=0;
//    error=PID_TURN_CAMERA.MID_Target-PID_TURN_CAMERA.MID;
    error=Err; // 修改了误差计算逻辑，不在用上面的计算方式
//    error = 0;
//    if (abs(error-PID_TURN_CAMERA.error_last)>=25&&error*PID_TURN_CAMERA.error_last<0)
//    {
//        error = PID_TURN_CAMERA.error_last;
//    }
//    if(error<=-DEAD_AREA_L && error>=DEAD_AREA_L)                                           //死区
//        error = PID_TURN_CAMERA.error_last;
    float P = 0;

//    if(utest>0)
//    {
//        if(error>=0)
//            DuoJi_GetP(&P ,highest_line+5, (int)(error+(utest/2)));
//        else if(error<0)
//            DuoJi_GetP(&P ,highest_line+5, (int)(error - (utest/2)));
//
//    }
//    else
        DuoJi_GetP(&P ,highest_line, (int)error);


    if (circle_find> 0)
        P = PID_TURN_CAMERA.P;
    P_test = P;
    P_data=P*error;
//    P_data_test = P_data;
    D_data=PID_TURN_CAMERA.D*(error-PID_TURN_CAMERA.error_last);
    D_data_test = error-PID_TURN_CAMERA.error_last;


//u,omg,识别
    if(usta==1)
    {
        D_all_left -=1;
        D_all_right-=1;
        ucont = 0;
    }
    if(error>=8)
        D_all_left += 1;
    if(D_all_left<0)
        D_all_left = 0;
    if(D_all_left>120)
        D_all_left = 120;

    if(error<=-8)
        D_all_right += 1;
    if(D_all_right<0)
        D_all_right = 0;
    if(D_all_right>120)
        D_all_right = 120;



    if(
            (D_all_left>15&&D_all_right>15&&circle_find != 1)||
            (D_all_right>50&&circle_find != 1)||
            (D_all_left>50&&circle_find != 1)

       )
        utest += 3;
    else
        utest -= 0.5;
    if(utest>=70)utest=70;
    if(Straight_flag==1)utest-=2;
    if(Straight_flag==2||circle_find >0){
        utest = 0;
        D_all_left =0;
        D_all_right=0;
        ucont = 0;
    }
    if(cross_flag.cross_state==cross_find)
        utest -= 1;



    if(utest<0)utest=0;

//以上，u，omg，识别

    Camera_output=(P_data+D_data);

    PID_TURN_CAMERA.D_data_last=D_data;
    PID_TURN_CAMERA.error_last=error;                 //更新误差
    return Camera_output;
}

float Turn_PID_Angle(void)//陀螺仪角度位置式PD
{
    float Angle_output;
    int error=0;
    float P_data=0,D_data=0;
    error=PID_TURN_Angle.Angle-PID_TURN_Angle.Angle_Target;

    if(error<=DEAD_AREA_L && error>=-DEAD_AREA_L)                                           //死区
         error=0;
    P_data=PID_TURN_Angle.P*error;
    D_data=PID_TURN_Angle.D*(error-PID_TURN_Angle.error_last);
    Angle_output=(P_data+D_data);

    PID_TURN_Angle.D_data_last=D_data;
    PID_TURN_Angle.error_last=error;                 //更新误差
    return Angle_output;
}

//-------------------------------------------------------------------------------------------------------------------
//  @brief      电机PID（左右通用）
//  @param      motor           选择电机
//  @return     int16
//  Sample usage:               Motor_PID(RIGHT_MOTOR);
//-------------------------------------------------------------------------------------------------------------------
int16_t Motor_PID(MOTOR_enum motor)                                                                             //位置式PID程序设计
{

  int16_t Motor_output;                               //PID调速输出的占空比
  int error=0;
  int P_data=0,I_data=0,D_data=0;

  if(motor==LEFT_MOTOR)                         //左机
  {
        error=PID_L.Speed_Target-PID_L.Speed; // 左轮是期望速度-实际测速

//        if(error<=DEAD_AREA_L && error>=-DEAD_AREA_L)                                           //死区
//            error=0;

        P_data=PID_L.P*error;

        PID_L.I_count+=error;
        if(PID_L.I_count>I_COUNT_MAX)       PID_L.I_count=I_COUNT_MAX;      //限幅
        else if(PID_L.I_count<-I_COUNT_MAX)       PID_L.I_count=-I_COUNT_MAX;
        I_data=PID_L.I_count*PID_L.I;
        if(error>PID_I_ON || error<-PID_I_ON)                                                           //积分分离
            I_data=0;
        D_data=PID_L.D*(error-PID_L.error_last);
        PID_L.D_data_last=(int16)D_data;

        PID_L.error_last=(int16)error;                 //更新误差

  }
  else if(motor==RIGHT_MOTOR)               //右机
  {
        error=PID_R.Speed_Target-PID_R.Speed; // 右轮是实际测速 - 期望速度

//        if(error<=DEAD_AREA_R && error>=-DEAD_AREA_R)                                           //死区
//            error=0;

        P_data=PID_R.P*error;

        PID_R.I_count+=error;
        if(PID_R.I_count>I_COUNT_MAX)       PID_R.I_count=I_COUNT_MAX;    //限幅
        else if(PID_R.I_count<-I_COUNT_MAX)       PID_R.I_count=-I_COUNT_MAX;
        I_data=PID_R.I_count*PID_R.I;
        if(error>PID_I_ON || error<-PID_I_ON)                                                           //积分分离
            I_data=0;

        D_data=PID_R.D*(error-PID_R.error_last);
        PID_R.D_data_last=(int16)D_data;
        PID_R.error_last=(int16)error;                 //更新误差

  }
    else return 0;

    Motor_output=(P_data+I_data+D_data);

    if(Motor_output>=PID_MOTOR_DUTY_MAX)        Motor_output=PID_MOTOR_DUTY_MAX;  //限幅
    else if(Motor_output<=-PID_MOTOR_DUTY_MAX)  Motor_output=-PID_MOTOR_DUTY_MAX;
  return Motor_output;
}

//-------------------------------------------------------------------------------------------------------------------
//  @brief      左右电机PID共用输出
//  @param      motor           选择电机
//  @return     int16
//  Sample usage:               Motor_PID(RIGHT_MOTOR);
//
//-------------------------------------------------------------------------------------------------------------------
int error_ttt = 0;
int16_t Motor_PID_average()                                                                             //位置式PID程序设计
{

    int16_t Motor_output;                               //PID调速输出的占空比
    int error=0;
    int P_data=0,I_data=0,D_data=0;

    error=PID_All.Speed_Target-PID_All.Speed;

//    if(abs(Err)>=40)
//        error = error * 0.5;
//    else
//        error = error * (1-abs(Err)/80);
//    error_ttt = error;
    P_data=PID_All.P*error;

    PID_All.I_count+=error;

    if(PID_All.I_count>I_COUNT_MAX)       PID_All.I_count=I_COUNT_MAX;    //限幅
    else if(PID_All.I_count<-I_COUNT_MAX)       PID_All.I_count=-I_COUNT_MAX;

    if(circle_find !=0)
    {
         int Ic_MAX = 600;
         if(PID_All.I_count>Ic_MAX)       PID_All.I_count=Ic_MAX;
    }
    if(utest>0 && JWJC == 1)
    {
        int Ic_MAX = 900 - 10 * utest;
        if(PID_All.I_count>Ic_MAX)       PID_All.I_count=Ic_MAX;    //限幅
    }
    vtest = PID_All.I_count;
    I_data=PID_All.I_count*PID_All.I;
    if(I_data>9000)       I_data=9000;    //限幅
    else if(I_data<-9000)       I_data=-9000;

    if(P_data>9000)       P_data=9000;    //限幅
    else if(P_data<-9000)       P_data=-9000;
//    P_data=PID_All.P*error;
//    PID_All.I_count+=error*PID_All.I;
//    if(PID_All.I_count>I_COUNT_MAX)       PID_All.I_count=I_COUNT_MAX;      //限幅
//    else if(PID_All.I_count<-I_COUNT_MAX)       PID_All.I_count=-I_COUNT_MAX;
//    if(PID_All.I_count>6000)       PID_All.I_count=6000;      //限幅
//    else if(PID_All.I_count<-6000)       PID_All.I_count=-6000;
//    if(error>PID_I_ON || error<-PID_I_ON)                                                           //积分分离
//        I_data=0;
//    I_data=PID_All.I_count;
    D_data=PID_All.D*(error-PID_All.error_last);
    PID_All.D_data_last=(int16)D_data;

    PID_All.error_last=(int16)error;                 //更新误差

    Motor_output=(P_data+I_data+D_data);
    P_data_test = P_data;
    I_data_test = I_data;
//u,omg识别
//    int DUTY_MAX = 9000 - 35 * utest;
//    if(Motor_output>=DUTY_MAX)        Motor_output=DUTY_MAX;  //限幅
//    else if(Motor_output<=-DUTY_MAX)  Motor_output=-DUTY_MAX;
    if(Motor_output>=PID_MOTOR_DUTY_MAX)        Motor_output=PID_MOTOR_DUTY_MAX;  //限幅
    else if(Motor_output<=-PID_MOTOR_DUTY_MAX)  Motor_output=-PID_MOTOR_DUTY_MAX;
    return Motor_output;
}

#define CAR_WIDTH 15
#define CAR_LENGHTH 20
#define TURN_DIR_1 1

int delta_speed = 0;    //测距pid速度变化
int speedMaxUpper = 100;//150;        //直道加速的值
//int speed2Car = SPEED_START;                              //两台车速度(不在这更改)
//int speed3Car = 450;                                        //三台车速度
int Delta_SPEED_DOWN_SECOND_CHEKU = 50;                     //减速上限第二圈车库量
int countnum=0;
int midsum=0;
int Mid_Estimate=0;
int Mid_line_last=84;

void my_pid(void)
{
    int16 pid_output;
//    int i,mean_edge;
//    int delta_speed;
    static float G_out = 0;
//    static float  sequence_estimation_K,sequence_estimation_G_out;



    // 此处有疑问？？？
    PID_L.Speed = speed_get(LEFT_MOTOR);
    PID_R.Speed = speed_get(RIGHT_MOTOR);
    PID_R.Speed_last=PID_R.Speed;
    PID_L.Speed_last=PID_L.Speed;

    Mid_line_last=Mid_Line;
    // 此处是正常循迹。。。。
    // 此处有更新其中的部分参数来自ZiTaiJieSuan.c/.h文件相关的
    PID_TURN_Angle.Angle=ATT_Angle.yaw;//更新当前角度

    //    PID_TURN_GYRO_Angle.W_Target=Turn_PID_Angle();
        PID_TURN_GYRO_Angle.W_Target=0; // 角度环输出的目标角度，后面传入角速度环 Turn_PID_GYRO();

//    PID_TURN_GYRO_Angle.W=gyro_x;//更新当前角速度值
    PID_TURN_GYRO_Angle.W=Gyr_rad.Z;//更新当前角速度值

    G_out=Turn_PID_GYRO();

    PID_L.Speed_Target=(int16_t)((1-G_out)*PID_L.SPEED_MID);
    PID_R.Speed_Target=(int16_t)((1+G_out)*PID_R.SPEED_MID);

    pid_output = Motor_PID(RIGHT_MOTOR);
    speed_set(RIGHT_MOTOR,pid_output);

    pid_output = Motor_PID(LEFT_MOTOR);
    speed_set(LEFT_MOTOR,pid_output);

}
