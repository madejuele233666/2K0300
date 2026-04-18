
#include "all_init.h"
#include "FUZZY_PID_UCAS.h"
#include "key.h"
#include "pid.h"
extern int Emergency_Stop;
#define VCC_LOW    (ADC2_CH10_A44)
bool Low_VCC_Flag = 0;
int initover =0;

void All_init()
{
    PID_All.Speed_Target = Speed_base;
    gpio_init(P21_2, GPO, 0, GPO_PUSH_PULL); // 蜂鸣器
    system_delay_ms(500);
    mpu6050_init();
    system_delay_ms(500);
    MPU6050_datainit();
    InitMH();
    KEY_init(); // 五向按键初始化
    tft180_set_dir(TFT180_CROSSWISE);
    tft180_init();
    adc_init(VCC_LOW, ADC_10BIT);
    if(mt9v03x_init())
        Emergency_Stop = 1;
    if (mt9v03x_set_exposure_time (exp_light))
    {
        tft180_show_string(0, 0, "seekfree");
    }
//
    if (adc_mean_filter_convert (ADC2_CH10_A44, 100)<=400)
        Low_VCC_Flag = 1;


    // 使用T2 T6定时器   P00_7引脚为A通道    P00_8引脚为B通道
    encoder_dir_init(TIM6_ENCODER, TIM6_ENCODER_CH1_P20_3, TIM6_ENCODER_CH2_P20_0);// 左电机编码器
    encoder_dir_init(TIM2_ENCODER, TIM2_ENCODER_CH1_P33_7, TIM2_ENCODER_CH2_P33_6);// 右电机编码器
    // ATOM 0模块的通道7 使用P02_7引脚输出PWM  PWM频率50HZ  占空比百分之1000/PWM_DUTY_MAX*100
//    system_delay_ms(280);
//    pwm_init(ATOM1_CH5_P02_5, 50, 500);// 无刷PWM
//    pwm_init(ATOM1_CH6_P02_6, 50, 500);// 无刷PWM
//    system_delay_ms(200);
//    pwm_set_duty(ATOM1_CH5_P02_5,550);// 无刷PWM
//    pwm_set_duty(ATOM1_CH6_P02_6,550);// 无刷PWM
//    system_delay_ms(400);
//    pwm_set_duty(ATOM1_CH5_P02_5,600);// 无刷PWM
//    pwm_set_duty(ATOM1_CH6_P02_6,600);// 无刷PWM
//    system_delay_ms(400);
//    pwm_set_duty(ATOM1_CH5_P02_5,650);// 无刷PWM
//    pwm_set_duty(ATOM1_CH6_P02_6,650);// 无刷PWM
//    system_delay_ms(400);
//    pwm_set_duty(ATOM1_CH5_P02_5,750);// 无刷PWM
//    pwm_set_duty(ATOM1_CH6_P02_6,750);// 无刷PWM
//    system_delay_ms(400);
//    pwm_set_duty(ATOM1_CH5_P02_5,850);// 无刷PWM
//    pwm_set_duty(ATOM1_CH6_P02_6,850);// 无刷PWM
//    system_delay_ms(800);

    pwm_init(ATOM1_CH5_P02_5, 50, 500);// 无刷PWM
    pwm_init(ATOM1_CH6_P02_6, 50, 500);// 无刷PWM
    system_delay_ms(400);
    for(uint16_t cnt = 0; cnt<500; cnt++){
        pwm_set_duty(ATOM1_CH5_P02_5,500+cnt);// 无刷PWM
        pwm_set_duty(ATOM1_CH6_P02_6,500+cnt);// 无刷PWM
        system_delay_ms(6);
    }
    system_delay_ms(800);


    pwm_init(ATOM0_CH2_P21_4, 17000, 0);// 左电机PWM
    pwm_init(ATOM0_CH1_P21_3, 17000, 0);// 右电机PWM
    gpio_init(P22_2, GPO, 1, GPO_PUSH_PULL);// 左DIR
    gpio_init(P22_1, GPO, 1, GPO_PUSH_PULL);// 右DIR
    pit_init(CCU60_CH0, 1000);      // 设置周期中断5000us

    gpio_init(P33_4, GPO, 1, GPO_PUSH_PULL); // 红外灯 1表示开
    gpio_init(P21_7, GPI, 1, GPI_PULL_UP); // 按键初始化BUT2
    gpio_init(P20_6, GPI, 1, GPI_PULL_UP); // 按键初始化BUT1
//    pwm_init(ATOM1_CH0_P21_2,50,10000);
    gpio_init(P21_2, GPO, 1, GPO_PUSH_PULL); // 蜂鸣器
    system_delay_ms(200);
    initover =1;
}
