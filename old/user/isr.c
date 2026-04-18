/*********************************************************************************************************************
* TC264 Opensourec Library 即（TC264 开源库）是一个基于官方 SDK 接口的第三方开源库
* Copyright (c) 2022 SEEKFREE 逐飞科技
*
* 本文件是 TC264 开源库的一部分
*
* TC264 开源库 是免费软件
* 您可以根据自由软件基金会发布的 GPL（GNU General Public License，即 GNU通用公共许可证）的条款
* 即 GPL 的第3版（即 GPL3.0）或（您选择的）任何后来的版本，重新发布和/或修改它
*
* 本开源库的发布是希望它能发挥作用，但并未对其作任何的保证
* 甚至没有隐含的适销性或适合特定用途的保证
* 更多细节请参见 GPL
*
* 您应该在收到本开源库的同时收到一份 GPL 的副本
* 如果没有，请参阅<https://www.gnu.org/licenses/>
*
* 额外注明：
* 本开源库使用 GPL3.0 开源许可证协议 以上许可申明为译文版本
* 许可申明英文版在 libraries/doc 文件夹下的 GPL3_permission_statement.txt 文件中
* 许可证副本在 libraries 文件夹下 即该文件夹下的 LICENSE 文件
* 欢迎各位使用并传播本程序 但修改内容时必须保留逐飞科技的版权声明（即本声明）
*
* 文件名称          isr
* 公司名称          成都逐飞科技有限公司
* 版本信息          查看 libraries/doc 文件夹内 version 文件 版本说明
* 开发环境          ADS v1.10.2
* 适用平台          TC264D
* 店铺链接          https://seekfree.taobao.com/
*
* 修改记录
* 日期              作者                备注
* 2022-09-15       pudding            first version
********************************************************************************************************************/

#include "isr_config.h"
#include "isr.h"
#include "PID.h"
#include "Motor.h"
#include "key.h"
#include "ZiTaiJieSuan.h"
#include "camera.h"
#define RadtoDeg    57.324841f              //弧度到角度 (弧度 * 180/3.1415)
extern int speed_L,speed_R,stopthershold_flag;
int pid_output_R,pid_output_L,pid_output_Mean,output_chasu;
float data;
extern float gyro_x_init;
//extern int Mid_line_last=60;
float G_out =0;
char BUT_flag = 0;
int TEST_KEY = 0;
int counter_20ms = 0,counter_10ms = 0,counter_3ms = 0,counter_5ms = 0;
extern int initover;
int bcount = 0;
 extern float utest;
// 对于TC系列默认是不支持中断嵌套的，希望支持中断嵌套需要在中断内使用 interrupt_global_enable(0); 来开启中断嵌套
// 简单点说实际上进入中断后TC系列的硬件自动调用了 interrupt_global_disable(); 来拒绝响应任何的中断，因此需要我们自己手动调用 interrupt_global_enable(0); 来开启中断的响应。
int16 pid_output;
bool change_speed_flag = 0;
// **************************** PIT中断函数 ****************************
IFX_INTERRUPT(cc60_pit_ch0_isr, 0, CCU6_0_CH0_ISR_PRIORITY)
{
    interrupt_global_enable(0);                     // 开启中断嵌套
    pit_clear_flag(CCU60_CH0);
//    counter_1ms++;  // 1ms定时器用于最内环速度环
    counter_10ms++;
    counter_10ms%=8;
    counter_3ms++; // 3ms定时器用于角度环
    counter_3ms%=3;
    counter_5ms++; // 6ms定时器用于角度环
    counter_5ms%=5;
    counter_20ms++;
    counter_20ms%=20;
    if (counter_20ms == 0&&initover ==1)
    {
        bcount++;
        if (bcount >200)
            bcount = 200;

    }

    if (counter_10ms == 0)
    {
        KEY_read();
        float W_Target_last;
        float k_waihuan = 0.9;
        PID_TURN_GYRO_CAMERA.W_Target = k_waihuan * Turn_PID_CAMERA() +(1-k_waihuan) * W_Target_last;
        W_Target_last = PID_TURN_GYRO_CAMERA.W_Target;
        if(circleFlag.lianxufaxian_distancedelay>0)
        {
            circleFlag.lianxufaxian_distancedelay = circleFlag.lianxufaxian_distancedelay-1;
        }
    }
    if (counter_5ms == 0)
    {
        speed_get_mean();
//        PID_All.Speed_Target = Speed_base-12.5*highest_line/80; // 变速
//        if (abs(Err) <= 30)
//            PID_All.Speed_Target = Speed_base;
//        else
//            PID_All.Speed_Target = Speed_base;
        if (Straight_flag == 2 && Straight_permit == 1)
        {
            if (PID_All.Speed_Target < Speed_base + 35)
                PID_All.Speed_Target += 5;
        }
        else
            {
            if (PID_All.Speed_Target > Speed_base)
                PID_All.Speed_Target -= 10 ;
            if(PID_All.Speed_Target < Speed_base)
                PID_All.Speed_Target = Speed_base;
            }
        if(bcount<=40)
            utest = 70;
        pid_output_Mean = Motor_PID_average();
    }
    if (counter_3ms == 0)
    {
        Prepare_Data();//陀螺仪
        IMUupdate(&Gyr_rad,&Acc_filt,&ATT_Angle);
        PID_TURN_GYRO_CAMERA.W=Gyr_rad.Z*RadtoDeg;// 角速度环
//       if(zebra_flag.zebra_state==Zebra_find&&zebra_flag.zebra_distance>=0)
//       {
//           zebra_flag.zebra_distance -= (int)speed_encoder/6;
//       }
//       else if(zebra_flag.zebra_state==Zebra_find&&zebra_flag.zebra_distance<0)
//       {
//           run_state.start_state=run_stop;
//       }
        if (Emergency_Stop == 1 || zebra_flag.zebra_state == Zebra_find) // 此处加入紧急停车和斑马线停车的判断，用于紧急停车和斑马线停车
        {
            if(Emergency_Stop == 1 || run_state.start_state == run_stop)  // 紧急停车
            {
                PID_L.Speed_Target = 0;
                PID_R.Speed_Target = 0;
                pid_output_R = Motor_PID(RIGHT_MOTOR); // 不再使用均值，对每个轮子单独控制
                pid_output_L = Motor_PID(LEFT_MOTOR);
                speed_set(RIGHT_MOTOR,pid_output_R);
                speed_set(LEFT_MOTOR,pid_output_L);
                gpio_set_level(P33_4, 0); // 关闭红外灯管
                // 还应加上无刷停转
                pwm_set_duty(ATOM1_CH5_P02_5,0);
                pwm_set_duty(ATOM1_CH6_P02_6,0);

            }
            else if (zebra_flag.zebra_state == Zebra_find)             // 斑马线停车
            {
                if(zebra_flag.zebra_distance>=0)
                {
                    Straight_flag == 2;
                    output_chasu=Turn_PID_GYRO(); // 输出为正，表示需要往左拐，右轮快，左轮慢
                    speed_set_chasu(pid_output_Mean,output_chasu);
                    zebra_flag.zebra_distance -= (int)PID_All.Speed/6;
                }
                if(zebra_flag.zebra_distance<0)
                    run_state.start_state=run_stop;
            }
        }
        else
        {
            output_chasu=Turn_PID_GYRO(); // 输出为正，表示需要往左拐，右轮快，左轮慢
            speed_set_chasu(pid_output_Mean,output_chasu);
        }
    }
}


IFX_INTERRUPT(cc60_pit_ch1_isr, 0, CCU6_0_CH1_ISR_PRIORITY)
{
    interrupt_global_enable(0);                     // 开启中断嵌套
    pit_clear_flag(CCU60_CH1);






}

IFX_INTERRUPT(cc61_pit_ch0_isr, 0, CCU6_1_CH0_ISR_PRIORITY)
{
    interrupt_global_enable(0);                     // 开启中断嵌套
    pit_clear_flag(CCU61_CH0);




}

IFX_INTERRUPT(cc61_pit_ch1_isr, 0, CCU6_1_CH1_ISR_PRIORITY)
{
    interrupt_global_enable(0);                     // 开启中断嵌套
    pit_clear_flag(CCU61_CH1);




}
// **************************** PIT中断函数 ****************************


// **************************** 外部中断函数 ****************************
IFX_INTERRUPT(exti_ch0_ch4_isr, 0, EXTI_CH0_CH4_INT_PRIO)
{
    interrupt_global_enable(0);                     // 开启中断嵌套
    if(exti_flag_get(ERU_CH0_REQ0_P15_4))           // 通道0中断
    {
        exti_flag_clear(ERU_CH0_REQ0_P15_4);

    }

    if(exti_flag_get(ERU_CH4_REQ13_P15_5))          // 通道4中断
    {
        exti_flag_clear(ERU_CH4_REQ13_P15_5);




    }
}

IFX_INTERRUPT(exti_ch1_ch5_isr, 0, EXTI_CH1_CH5_INT_PRIO)
{
    interrupt_global_enable(0);                     // 开启中断嵌套

    if(exti_flag_get(ERU_CH1_REQ10_P14_3))          // 通道1中断
    {
        exti_flag_clear(ERU_CH1_REQ10_P14_3);

        tof_module_exti_handler();                  // ToF 模块 INT 更新中断

    }

    if(exti_flag_get(ERU_CH5_REQ1_P15_8))           // 通道5中断
    {
        exti_flag_clear(ERU_CH5_REQ1_P15_8);


    }
}

// 由于摄像头pclk引脚默认占用了 2通道，用于触发DMA，因此这里不再定义中断函数
// IFX_INTERRUPT(exti_ch2_ch6_isr, 0, EXTI_CH2_CH6_INT_PRIO)
// {
//  interrupt_global_enable(0);                     // 开启中断嵌套
//  if(exti_flag_get(ERU_CH2_REQ7_P00_4))           // 通道2中断
//  {
//      exti_flag_clear(ERU_CH2_REQ7_P00_4);
//  }
//  if(exti_flag_get(ERU_CH6_REQ9_P20_0))           // 通道6中断
//  {
//      exti_flag_clear(ERU_CH6_REQ9_P20_0);
//  }
// }
IFX_INTERRUPT(exti_ch3_ch7_isr, 0, EXTI_CH3_CH7_INT_PRIO)
{
    interrupt_global_enable(0);                     // 开启中断嵌套
    if(exti_flag_get(ERU_CH3_REQ6_P02_0))           // 通道3中断
    {
        exti_flag_clear(ERU_CH3_REQ6_P02_0);
        camera_vsync_handler();                     // 摄像头触发采集统一回调函数
    }
    if(exti_flag_get(ERU_CH7_REQ16_P15_1))          // 通道7中断
    {
        exti_flag_clear(ERU_CH7_REQ16_P15_1);




    }
}
// **************************** 外部中断函数 ****************************


// **************************** DMA中断函数 ****************************
IFX_INTERRUPT(dma_ch5_isr, 0, DMA_INT_PRIO)
{
    interrupt_global_enable(0);                     // 开启中断嵌套
    camera_dma_handler();                           // 摄像头采集完成统一回调函数
}
// **************************** DMA中断函数 ****************************


// **************************** 串口中断函数 ****************************
// 串口0默认作为调试串口
IFX_INTERRUPT(uart0_tx_isr, 0, UART0_TX_INT_PRIO)
{
    interrupt_global_enable(0);                     // 开启中断嵌套



}
IFX_INTERRUPT(uart0_rx_isr, 0, UART0_RX_INT_PRIO)
{
    interrupt_global_enable(0);                     // 开启中断嵌套

#if DEBUG_UART_USE_INTERRUPT                        // 如果开启 debug 串口中断
        debug_interrupr_handler();                  // 调用 debug 串口接收处理函数 数据会被 debug 环形缓冲区读取
#endif                                              // 如果修改了 DEBUG_UART_INDEX 那这段代码需要放到对应的串口中断去
}


// 串口1默认连接到摄像头配置串口
IFX_INTERRUPT(uart1_tx_isr, 0, UART1_TX_INT_PRIO)
{
    interrupt_global_enable(0);                     // 开启中断嵌套




}
IFX_INTERRUPT(uart1_rx_isr, 0, UART1_RX_INT_PRIO)
{
    interrupt_global_enable(0);                     // 开启中断嵌套
    camera_uart_handler();                          // 摄像头参数配置统一回调函数
}

// 串口2默认连接到无线转串口模块
IFX_INTERRUPT(uart2_tx_isr, 0, UART2_TX_INT_PRIO)
{
    interrupt_global_enable(0);                     // 开启中断嵌套



}

IFX_INTERRUPT(uart2_rx_isr, 0, UART2_RX_INT_PRIO)
{
    interrupt_global_enable(0);                     // 开启中断嵌套
    wireless_module_uart_handler();                 // 无线模块统一回调函数



}
// 串口3默认连接到GPS定位模块
IFX_INTERRUPT(uart3_tx_isr, 0, UART3_TX_INT_PRIO)
{
    interrupt_global_enable(0);                     // 开启中断嵌套



}

IFX_INTERRUPT(uart3_rx_isr, 0, UART3_RX_INT_PRIO)
{
    interrupt_global_enable(0);                     // 开启中断嵌套
    gnss_uart_callback();                           // GNSS串口回调函数



}

// 串口通讯错误中断
IFX_INTERRUPT(uart0_er_isr, 0, UART0_ER_INT_PRIO)
{
    interrupt_global_enable(0);                     // 开启中断嵌套
    IfxAsclin_Asc_isrError(&uart0_handle);
}
IFX_INTERRUPT(uart1_er_isr, 0, UART1_ER_INT_PRIO)
{
    interrupt_global_enable(0);                     // 开启中断嵌套
    IfxAsclin_Asc_isrError(&uart1_handle);
}
IFX_INTERRUPT(uart2_er_isr, 0, UART2_ER_INT_PRIO)
{
    interrupt_global_enable(0);                     // 开启中断嵌套
    IfxAsclin_Asc_isrError(&uart2_handle);
}
IFX_INTERRUPT(uart3_er_isr, 0, UART3_ER_INT_PRIO)
{
    interrupt_global_enable(0);                     // 开启中断嵌套
    IfxAsclin_Asc_isrError(&uart3_handle);
}
