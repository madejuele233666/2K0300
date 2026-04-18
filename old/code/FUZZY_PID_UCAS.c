#include "FUZZY_PID_UCAS.h"
#include "zf_common_headfile.h"
extern float Err;
extern int output_chasu;
#define H_Min   2
//比较暴躁的转弯

//float Fuzzy_PID_Pro[7]={0.36,0.752,1.58,1.92,2.355,2.9,3.2};

//这玩意没什么规律，p越大，转弯越好，直道会有抖动，p小转不过来，凭感觉调
//建议先用单套pd，看看车子正常的p大概在什么范围，下面的p就会有方向

// {0.22,0.452,1.28,1.82,2.355,2.5,2.6} ,//舵机P值表R
// {0.22,0.452,1.28,1.82,2.355,2.5,2.6} ,//舵机P值表L
MH  MHstruct;
int P_Mode = 0;
MH  MHstructFastCar =
{

//        {8, 8.98, 10.08, 11.31, 12.69, 14.25, 16} ,// 舵机P值表L（指数插值） ,// 舵机P值表L（指数插值）
//        {8, 8.98, 10.08, 11.31, 12.69, 14.25, 16} ,// 舵机P值表L（指数插值） ,// 舵机P值表R（指数插值）
//        {10, 11.23, 12.6, 14.11, 15.87, 17.818, 20} ,// 舵机P值表L（指数插值） ,// 舵机P值表L（指数插值）
//        {10, 11.23, 12.6, 14.11, 15.87, 17.818, 20} ,// 舵机P值表L（指数插值） ,// 舵机P值表R（指数插值）
        {10, 11.15, 12.83, 14.75, 16.96, 19.51, 22.4} ,// 舵机P值表L（指数插值） ,// 舵机P值表L（指数插值）
        {10, 11.15, 12.83, 14.75, 16.96, 19.51, 22.4} ,// 舵机P值表L（指数插值） ,// 舵机P值表R（指数插值）
//        {10.000, 11.610, 13.479, 15.659, 18.180, 21.107, 25.000},
//        {10.000, 11.610, 13.479, 15.659, 18.180, 21.107, 25.000},


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
   },
  //电机P值表
  { 2.0, 2.3 ,2.6, 3.0, 3.3, 3.8, 4.3 },
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
    float table1[7] = {8,  8.98, 10.08, 11.31, 12.69, 14.25, 16};
    float table2[7] = {10, 11.23, 12.6, 14.11, 15.87, 17.818, 20};
    float table3[7] = {10, 11.15, 12.83, 14.75, 16.96, 19.51, 22.4};
    float table4[7] = {10.000, 11.610, 13.479, 15.659, 18.180, 21.107, 25.000};
    if (P_Mode == 1)
    {
        memcpy(MHstruct.f_DuoJiP_TableL, table1, sizeof(table1));
        memcpy(MHstruct.f_DuoJiP_TableR, table1, sizeof(table1));
    }

    else if (P_Mode == 2)
    {
        memcpy(MHstruct.f_DuoJiP_TableL, table2, sizeof(table2));
        memcpy(MHstruct.f_DuoJiP_TableR, table2, sizeof(table2));
    }
    else if (P_Mode == 3)
    {
        memcpy(MHstruct.f_DuoJiP_TableL, table3, sizeof(table3));
        memcpy(MHstruct.f_DuoJiP_TableR, table3, sizeof(table3));
    }
    else if (P_Mode == 4)
    {
        memcpy(MHstruct.f_DuoJiP_TableL, table4, sizeof(table4));
        memcpy(MHstruct.f_DuoJiP_TableR, table4, sizeof(table4));
    }

//  MHstruct = MHstructFastCar;
  MHstruct.f_SizeOfViewE = 65; //有效偏差
  MHstruct.f_SizeOfViewH = 40; //有效可视距离
}

//从国科的代码中移植过来的
/************************************************************************
函数名：获取舵机P值
功能：通过反向可视距离和中值偏差得出不同P值
参数：i32p_P------舵机P指针
      i16_ViewH---Offline
      i16_ViewE---中值偏差
实例:DuoJi_GetP(&P ,MT9V03X_H-Search_Stop_Line, err);//新型模糊P,P的地址，搜索截止行的位置，误差
************************************************************************/
void DuoJi_GetP(float *i32p_P, int16 i16_ViewH, int16 i16_ViewE)
{
  MHstruct.i16_ViewH = i16_ViewH;

  float VH = f_Get_H_approximation(i16_ViewH - H_Min);
  float VE = f_Get_E_approximation(i16_ViewE, MHstruct.f_SizeOfViewE);
  float X2Y = 0;
  float X1Y = 0;
  float Y2X = 0;
  float Y1X = 0;
  int16 VH1 = (int)VH;
  if (VH1 > VH) 
  {
    VH--;
  }

  int16 VH2 = VH1 + 1;

  int16 VE1 = (int)VE;
  if (VE1 > VE) 
  {
    VE1--;
  }

  int16 VE2 = VE1 + 1;
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
//  tft180_show_int(0, 0, i16_ViewH, 4);  // 合理

//  tft180_show_float(0, 40, (float)VH, 3, 2); // 7.5
//  tft180_show_float(0, 60, (float)VE, 3, 2); // 0
//  tft180_show_float(0, 80, Err, 3, 2);      // -8
//  tft180_show_int(0, 100, i16_ViewE, 4); // 0
//  tft180_show_uint(60, 80, output_chasu, 4);
  int16 P1 = (int)P_approximation;
  if (P1 > P_approximation) 
  {
    P1--;
  }

  int16 P2 = P1 + 1;
  if (i16_ViewE < 0) 
  {
    *i32p_P = (MHstruct.f_DuoJiP_TableL[P2] - MHstruct.f_DuoJiP_TableL[P1])*(P_approximation - P1) +MHstruct.f_DuoJiP_TableL[P1];
  }
  else
  {
    *i32p_P = (MHstruct.f_DuoJiP_TableR[P2] - MHstruct.f_DuoJiP_TableR[P1])*(P_approximation - P1) + MHstruct.f_DuoJiP_TableR[P1];
  }
//  tft180_show_float(0, 20, (float)*i32p_P, 3, 2); // 5.25
}

/************************************************************************
函数名：获取横轴似坐标
功能：获取get_p()函数所需变量
接口：无
调用：通过get_p()被动调用
************************************************************************/
float f_Get_H_approximation(int i16_ViewH)
{
  float H_approximation;
  if (i16_ViewH < 0) 
  {
    i16_ViewH = 0;
  }
  H_approximation = i16_ViewH * 3 / MHstruct.f_SizeOfViewH;
  return H_approximation;
}

/************************************************************************
函数名：获取纵轴近似坐标
功能：获取get_p()函数所需变量
接口：无
调用：通过get_p()被动调用
************************************************************************/
float f_Get_E_approximation(int i16_E, float f_E_Size)
{
  float E_approximation;
  if (i16_E < 0) 
  {
    i16_E = -i16_E;
  }
  E_approximation = i16_E * 3 / f_E_Size;
  return E_approximation;
}
