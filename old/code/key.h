
/*
 * key.h
 *
 *  Created on: Apr 10, 2023
 *      Author: 86135
 */

#ifndef KEY_H_
#define KEY_H_
#include "zf_common_headfile.h"
#define FLASH_SECTION_INDEX   0
#define FLASH_PAGE_INDEX      0
typedef struct
{
    unsigned char judge_sta;
    bool key_sta;
    bool single_flag;
    bool long_flag;
    int16_t time;
    bool yisi_single_flag;
    bool yisi_long_flag;
}KEY;
enum MEMU_TYPE {MEMU_INT, MEMU_FLOAT};
typedef struct
{
    int all_num;
    int now_num;
    int last_num;
    float * menu_data_float[16];
    int * menu_data_int[16];
    enum MEMU_TYPE menu_data_type[16];
    char menu_string[16][4];
    float step[16];
}MENU;
void KEY_init(void);
void KEY_read(void);
extern KEY key[5];
extern int yuansu_num;
extern uint8_t menue[4],menue_bisai[4];

void menu_use(void);
void menu_0000_init(void);
void menu_1111();
void menu_110();
void menu_1110();
void menu_1011();
void menu_0011();
void menu_011();
void menu_0000();

extern uint8_t image_choose;
extern int ruku_time,exp_light;
extern float speed_k;
enum START_FLAG {Stop, Delay, Start,Over};
extern enum START_FLAG start_flag;
extern int F_PWM;
extern float circle_k,road_k;
extern int circle_b,road_b;
extern int see_max;
//enum KEYNAME {leftdown, leftup, rightdown,rightup,push};
enum KEYNAME {left, right, up,push,down};
extern enum KEYNAME keyname;
void normal_param_store();
void normal_param_read(void);
//#define     KEY0    C0
//#define     KEY1    C1
//#define     KEY2    C2
//#define     KEY3    C3
#define SWICH1          P00_8
#define SWICH2          P00_9
#define SWICH3          P00_12
#define SWICH4          PA_49
//#define     LEFTDOWN    P10_2
//#define     LEFTUP      P11_12
//#define     RIGHTDOWN   P11_11
//#define     RIGHTUP     P11_10
//#define     PUSH        P11_9
#define     LEFT        P10_2   // ×ó
#define     RIGHT       P11_12  // ÓÒ
#define     UP          P11_11  // ÉÏ
#define     PUSH        P11_10  // ÖÐ
#define     DOWN        P11_9   // ÏÂ
#endif /* KEY_H_ */
