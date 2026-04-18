#ifndef ALL_INIT_H_
#define ALL_INIT_H_
#define         RESULT_ROW              60//结果图行列
#define         RESULT_COL              80
#define         USED_ROW                128  //用于透视图的行列
#define         USED_COL                160
#include "zf_common_headfile.h"
#include "FUZZY_PID.h"


#define BEEP        "/dev/zf_driver_gpio_beep"
#define KEY_0       "/dev/zf_driver_gpio_key_0"     //BUT1  按下为0
#define KEY_1       "/dev/zf_driver_gpio_key_1"     //BUT2  按下为0（靠近拨玛）
#define SWITCH_0    "/dev/zf_driver_gpio_switch_0"  //拨玛1 拨上去为1(最上面的)
#define SWITCH_1    "/dev/zf_driver_gpio_switch_1"
#define SWITCH_2    "/dev/zf_driver_gpio_switch_2"  
#define SWITCH_3    "/dev/zf_driver_gpio_switch_3"  //g了
#define BRUSHLESS_1           "/dev/zf_device_pwm_esc_1"      //无刷1 (板子内)
#define BRUSHLESS_2           "/dev/zf_device_pwm_esc_2"      //无刷2（板子外）左边
// struct pwm_info brushless_1_info;                             //无刷1的信息
// struct pwm_info burshless_2_info;                             //无刷2的信息


#define CAMERA "/dev/video0"

extern int turn_angle;
//extern int adc_stop_L,adc_stop_R;
void All_init (void);
//extern uint8_t PerImg_ip[RESULT_ROW][RESULT_COL][2];
#define Sevro_Limit 350

#endif /* ALL_INIT_H_ */