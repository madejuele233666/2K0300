#include "FUZZY_PID.h"

//需要修改，决定着拐弯时机
#define H_Min   76


MH  MHstruct;
MH  MHstructFastCar =
{
  {8.00 ,8.10 ,8.20, 8.27 , 8.35 , 8.38, 8.44} ,//舵机P值表R
  {8.00 ,8.10 ,8.20, 8.27, 8.35, 8.38, 8.44} ,//舵机P值表L
  //模糊表|-*-纵轴：反向可视距离递增-*-|-*-横轴：中值偏差变化递增-*-|
   {
    {
      {//L-IN
              { 0, 1, 2, 3, },
              { 1, 2, 3, 4, },
              { 3, 4, 5, 6, },
              { 5, 6, 6, 6, }
      }
    },
    {
      {//R-IN
              { 0, 1, 2, 3, },
              { 1, 2, 3, 4, },
              { 3, 4, 5, 6, },
              { 5, 6, 6, 6, }
      }
    },
    {
      {//L-OUT
              { 0, 1, 2, 3, },
              { 1, 2, 3, 4, },
              { 3, 4, 5, 6, },
              { 5, 6, 6, 6, }
      }
    },
    {
      {//R-OUT
              { 0, 1, 2, 3, },
              { 1, 2, 3, 4, },
              { 3, 4, 5, 6, },
              { 5, 6, 6, 6, }
      }
    }
   },// 舵机模糊表
  //电机P值表
  { 6.9, 7.2 ,7.8 , 8.2 , 8.9 , 9.5 , 10.5},
  //电机I值表
  { 10, 16.625, 21.25, 32.5, 55, 77.5, 100 },
  //{ 10, 16.625, 21.25, 32.5, 55, 77.5, 100 }{ 50, 59.375, 68.75, 87.5, 125, 162.5, 200 },
  //电机模糊表
  //模糊表|-*-纵轴：反向可视距离递增-*-|-*-横轴：脉冲偏差变化递增-*-|
  {
    {
      {//ADD-IN
        { 0, 1, 2, 3, },
        { 1, 2, 3, 4, },
        { 3, 4, 5, 6, },
        { 5, 6, 6, 6, }
      }
    },
    {
      {//SUB-IN
        { 0, 1, 2, 4, },
        { 1, 3, 5, 6, },
        { 3, 5, 6, 6, },
        { 5, 6, 6, 6, }
      }
    },
    {
      {//ADD-OUT
        { 0, 1, 2, 4, },
        { 1, 3, 5, 6, },
        { 3, 5, 6, 6, },
        { 5, 6, 6, 6, }
      }
    },
    {
      {//SUB-OUT
        { 0, 1, 2, 3, },
        { 1, 2, 3, 4, },
        { 3, 4, 5, 6, },
        { 5, 6, 6, 6, }
      }
    },
  }
};

/************************************************************************
函数名：模糊表初始化
************************************************************************/
void InitMH(void) 
{
  MHstruct = MHstructFastCar;
  MHstruct.f_SizeOfViewE = 32; //有效偏差(根据图像，距离中线偏差最大是多少)
  MHstruct.f_SizeOfViewH = 14; //有效可视距离（根据图像 就是最长的距离减去最短的距离  最长距离应该是最远看到的行数-最近看到的行数）
}

/************************************************************************
函数名：获取舵机P值
功能：通过反向可视距离和中值偏差得出不同P值
参数：i32p_P------舵机P指针
      i16_ViewH---Offline
      i16_ViewE---中值偏差
************************************************************************/
void DuoJi_GetP(float *i32p_P, signed short int i16_ViewH, signed short int i16_ViewE)

{
    MHstruct.i16_ViewH = i16_ViewH;
    float VH = f_Get_H_approximation(i16_ViewH - H_Min);
    float VE = f_Get_E_approximation(i16_ViewE, MHstruct.f_SizeOfViewE);
    float X2Y = 0;
    float X1Y = 0;
    float Y2X = 0;
    float Y1X = 0;
    int8 VH1 = (int)VH;
    if (VH1 > VH) 
    {
        VH--;
    }

    int8 VH2 = VH1 + 1;
    int8 VE1 = (int)VE;
    if (VE1 > VE) 
    {
        VE1--;
    }

    int8 VE2 = VE1 + 1;
    if (VH1 > 3) 
    {
        VH1 = 3;
    }
    if (VH2 > 3) 
    {
        VH2 = 3;
    }
    if (VE1 > 3) 
    {
        VE1 = 3;
    }
    if (VE2 > 3)
    {
        VE2 = 3;
    }
    X2Y = (MHstruct.mt_Duoji[MHstruct.ui8_IOLR].ui8_Table[VH1][VE2] -
            MHstruct.mt_Duoji[MHstruct.ui8_IOLR].ui8_Table[VH1][VE1]) *
        (VE - VE1) +
        MHstruct.mt_Duoji[MHstruct.ui8_IOLR].ui8_Table[VH1][VE1];

    X1Y = (MHstruct.mt_Duoji[MHstruct.ui8_IOLR].ui8_Table[VH2][VE2] -
            MHstruct.mt_Duoji[MHstruct.ui8_IOLR].ui8_Table[VH2][VE1]) *
        (VE - VE1) +
        MHstruct.mt_Duoji[MHstruct.ui8_IOLR].ui8_Table[VH2][VE1];

    Y2X = (MHstruct.mt_Duoji[MHstruct.ui8_IOLR].ui8_Table[VH2][VE1] -
            MHstruct.mt_Duoji[MHstruct.ui8_IOLR].ui8_Table[VH1][VE1]) *
        (VH - VH1) +
        MHstruct.mt_Duoji[MHstruct.ui8_IOLR].ui8_Table[VH1][VE1];

    Y1X = (MHstruct.mt_Duoji[MHstruct.ui8_IOLR].ui8_Table[VH2][VE2] -
            MHstruct.mt_Duoji[MHstruct.ui8_IOLR].ui8_Table[VH1][VE2]) *
        (VH - VH1) +
        MHstruct.mt_Duoji[MHstruct.ui8_IOLR].ui8_Table[VH1][VE2];
    float P_approximation = (X2Y + X1Y + Y2X + Y1X) / 4.0;
    int8 P1 = (int)P_approximation;
    if (P1 > P_approximation) 
    {
        P1--;
    }

    int8 P2 = P1 + 1;
    if (i16_ViewE < 0) 
    {
        *i32p_P = (MHstruct.f_DuoJiP_TableL[P2] - MHstruct.f_DuoJiP_TableL[P1])*(P_approximation - P1) +MHstruct.f_DuoJiP_TableL[P1];
    }
    else
    {
        *i32p_P = (MHstruct.f_DuoJiP_TableR[P2] - MHstruct.f_DuoJiP_TableR[P1])*(P_approximation - P1) + MHstruct.f_DuoJiP_TableR[P1];
    }
}

/************************************************************************
函数名：获取横轴似坐标
功能：获取get_p()函数所需变量
接口：无
调用：通过get_p()被动调用
************************************************************************/
float f_Get_H_approximation(short i16_ViewH) 
{
  float H_approximation;
  if (i16_ViewH < 0) 
  {
    i16_ViewH = 0;
  }
  if(i16_ViewH > MHstruct.f_SizeOfViewH)
    i16_ViewH = MHstruct.f_SizeOfViewH;
  H_approximation = 3 - i16_ViewH * 3 / MHstruct.f_SizeOfViewH;
  return H_approximation;
}

/************************************************************************
函数名：获取纵轴近似坐标
功能：获取get_p()函数所需变量
接口：无
调用：通过get_p()被动调用
************************************************************************/
float f_Get_E_approximation(short i16_E, float f_E_Size) 
{
  float E_approximation;
  if (i16_E < 0) 
  {
    i16_E = -i16_E;
  }
  E_approximation = i16_E * 3 / f_E_Size;
  return E_approximation;
}