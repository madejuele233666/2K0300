
#include "Servo.h"
#include "ZiTaiJieSuan.h"
#include "camera.h"



//
//float Err_Sum(void)
//{
//
//    int i;
//    float err=0;
//    int weight_count=0;
//    //常规误差
//    for(i=MT9V03X_H-5;i>=MT9V03X_H-highest_line-1;i--)//常规误差计算
//    {
//        err+=(MT9V03X_W/2-1-((edgeLeft.xCoordinate[i].inside
//                + edgeRight.xCoordinate[i].inside)>>1))*Weight[i];//右移1位，等效除2
//        weight_count+=Weight[i];
//    }
//    err=err/weight_count;
//
//    return err;
//}
