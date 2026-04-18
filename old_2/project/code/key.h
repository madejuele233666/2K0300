
/*
 * key.h
 *
 *  Created on: Apr 10, 2023
 *      Author: 86135
 */

 #ifndef KEY_H_
 #define KEY_H_
 #include "zf_common_headfile.h"
 #include "save.h"
 #include <string>
 #include <iostream> 
 #include <exception>
 #include <iomanip>
 #include <vector>
 #include <algorithm>
 typedef struct
 {
     unsigned char judge_sta;   //当前状态机状态
     bool key_sta;              //当前按键电平状态
     bool single_flag;          //短按标志位
     bool long_flag;            //长按标志位
     int16_t time;              //按下持续时间计数
     bool yisi_single_flag;
     bool yisi_long_flag;
 }KEY;
 void Key_read(void);
 extern KEY key[2];
 extern int yuansu_num;
 extern int menue[4],menue_bisai[4];
 extern float roadblock_chu ;
 extern float roadblock_hui ;
 extern float roadblock_hui ;
 extern float roadblock_k ;
 void draw_line();
 int limit_num(int a,int b,int c);
 void Show_on_ips_index(ParamManager& manager, int selectedIndex,uint8_t startY,uint8_t lineHeight);
bool loadParams(ParamManager& manager);
 extern uint8_t image_choose;
 extern int ruku_time;
 extern float speed_k;
 extern float chasu_P;
 extern int speed_run_slow;
 extern float PID_servo[5];
 extern int fache;
 extern float zhidao_P;
 extern float circle_P;
 extern int BLDC_PWM;
 extern int car_speed;
 extern int zhidao_speed;
 extern int circle_speed;

 extern int car_start;
 void Final_menu();
 enum KeyValue 
 {
     KEY_NONE = 0,
     KEY_0_ = 1,
     KEY_1_ = 2
 };
 KeyValue Key_detect();
 enum SwiValue
 {
     SWI_NONE = 0,
     MOD_1 = 1,//图像显示
     MOD_2 = 2,//上下移动索引行，KEY_0下移，KEY_1上移
     MOD_3 = 3,//修改选定数值，KEY_0减小，KEY_1增大
     MOD_4 = 4,//保底方案？
     MOD_5 = 5,//保底发车
     MOD_6 = 6//电调发车
 };
 SwiValue SWI_choose_MOD();
 #endif /* KEY_H_ */
  