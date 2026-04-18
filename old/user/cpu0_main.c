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
* 文件名称          cpu0_main
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
#include "zf_common_headfile.h"
#include "All_init.h"
#include "ZiTaiJieSuan.h"
#include "PID.h"
#include "camera.h"
#include "key.h"
extern int test,weight_count;
extern float G_out;
extern float data;
extern int highest_line;
extern char BUT_flag;
extern float gyro_z_init;
extern bool Low_VCC_Flag;
char change_flag = 0;
int stopthershold = 85;
int stopthershold_flag = 0;

//extern float sequence_estimation_servo_rate,sequence_estimation_K;
//extern int weight_count;
#pragma section all "cpu0_dsram"
// 将本语句与#pragma section all restore语句之间的全局变量都放在CPU0的RAM中

// 本例程是开源库空工程 可用作移植或者测试各类内外设
// 本例程是开源库空工程 可用作移植或者测试各类内外设
// 本例程是开源库空工程 可用作移植或者测试各类内外设
int speed_L,speed_R;
extern int TEST_KEY ;
extern float rate_xcopy,rate_ycopy;
int VCC = 0;
extern int pid_output_R,pid_output_L,pid_output_Mean,output_chasu,R_output;
extern int I_data_test,error_ttt,P_data_test,D_all_left,D_all_right,vtest,usta;
extern float utest;
extern float P_test;
extern float test_sequence_estimation_servo_rate;
int adc_test = 0;
void SCI_Send_Datas2(uart_index_enum uart_num);
// **************************** 代码区域 ****************************
int core0_main(void)
{
    clock_init();                   // 获取时钟频率<务必保留>
    debug_init();                   // 初始化默认调试串口
    // 此处编写用户代码 例如外设初始化代码等
    menu_0000_init(); // 菜单参数初始化
    All_init();
    wireless_uart_init();
    // 设置逐飞助手使用DEBUG串口进行收发
    seekfree_assistant_interface_init(SEEKFREE_ASSISTANT_WIRELESS_UART);
    // 初始化逐飞c助手示波器的结构体
    seekfree_assistant_oscilloscope_struct oscilloscope_data;
    //设置为4个通道，通道数量最大为8个
    oscilloscope_data.channel_num = 8;
    uint32 tim = 0;
    // 此处编写用户代码 例如外设初始化代码等
    cpu_wait_event_ready();         // 等待所有核心初始化完毕
    while (TRUE)
    {

        oscilloscope_data.data[0] = PID_L.Speed_Target;
        oscilloscope_data.data[1] = PID_All.Speed;
//        oscilloscope_data.data[2] = Straight_flag;
        oscilloscope_data.data[3] = pid_output_Mean;//左右内环
        oscilloscope_data.data[4] = output_chasu;//前后内环
        oscilloscope_data.data[5] = utest;
//        oscilloscope_data.data[6] = D_all_right;
        oscilloscope_data.data[7] = Err;



//        oscilloscope_data.data[0] = highest_line;
//        oscilloscope_data.data[1] = P_test;
//        oscilloscope_data.data[2] = utest;
////        oscilloscope_data.data[1] = vtest;
////        oscilloscope_data.data[2] = usta;
//        oscilloscope_data.data[3] = pid_output_Mean;//左右内环
////        oscilloscope_data.data[4] = output_chasu;//前后内环
//        oscilloscope_data.data[4] = output_chasu;//前后内环
//
//        oscilloscope_data.data[5] = D_all_left;
//        oscilloscope_data.data[6] = D_all_right;
//
//        oscilloscope_data.data[7] = Err;



//        oscilloscope_data.data[0] =Err;
//        oscilloscope_data.data[1] =thershold_local;
//        oscilloscope_data.data[2] = error_ttt;
//        oscilloscope_data.data[3] =  I_data_test;//
//        oscilloscope_data.data[4] = P_data_test;
//        oscilloscope_data.data[5] = PID_L.Speed;
//        oscilloscope_data.data[6] = PID_R.Speed;
//        oscilloscope_data.data[7] = PID_All.Speed;
//        seekfree_assistant_oscilloscope_send(&oscilloscope_data);
        menu_use();

        if(circle_find> 0|| Low_VCC_Flag||utest>0)
            gpio_set_level(P21_2,1);
        else gpio_set_level(P21_2,0);
        // 此处编写需要循环执行的代码
//        adc_test = adc_mean_filter_convert (ADC2_CH10_A44, 20);
        if(mt9v03x_finish_flag == 1)
        {
            mt9v03x_finish_flag = 0;
//            thershold_local = GetOSTU (mt9v03x_image[0]);
//            system_start();
            thershold_local = otsuThreshold_fast(mt9v03x_image[0]);
            if (5 <= thershold_local && thershold_local <= Emergency_threshold|| Low_VCC_Flag) // 紧急停车
                Emergency_Stop = 1;
            if (220 <= thershold_local)
                Emergency_Stop = 1;


//            eight_neighbor(mt9v03x_image[0],thershold_local);
            eight_neighbor(mt9v03x_image[0],thershold_local);
//            tim = system_getval_ms();
            if (gpio_get_level(SWICH3)==0)
            {
                tft180_show_gray_image(0, 0, mt9v03x_image[0], MT9V03X_W, MT9V03X_H,MT9V03X_W , MT9V03X_H , 0);
                for(uint8_t i=126; i>edgeRight.peakRow; i--)
                {//左边界画线
                    if (edgeLeft.xCoordinate[i].type == Both && edgeLeft.xCoordinate[i].inside>=0
                            && edgeLeft.xCoordinate[i].inside<160
                            && i>=0 && i<=128)
                    {
                        tft180_draw_point(edgeLeft.xCoordinate[i].inside, i, RGB565_RED);
                    }
                    else if (edgeLeft.xCoordinate[i].type == Single&& edgeLeft.xCoordinate[i].inside>=0
                            && edgeLeft.xCoordinate[i].inside<160
                            && i>=0 && i<=128)
                    {
                        tft180_draw_point(edgeLeft.xCoordinate[i].inside, i, RGB565_RED);
                    }
                    if(edgeRight.xCoordinate[i].type == Both&& edgeRight.xCoordinate[i].inside>=0
                            && edgeRight.xCoordinate[i].inside<160
                            && i>=0 && i<=128)
                    {
                        //          tft180_draw_point(edgeRight.xCoordinate[i].outside/1.5, i/1.5, RGB565_GREEN);
                        tft180_draw_point(edgeRight.xCoordinate[i].inside, i, RGB565_GREEN);
                        //tft180_draw_point(edgeRight.xCoordinate[i].outside, i, RGB565_RED);
                    }
                    else if (edgeRight.xCoordinate[i].type == Single&& edgeRight.xCoordinate[i].inside>=0
                            && edgeRight.xCoordinate[i].inside<160
                            && i>=0 && i<=128)
                    {
                       tft180_draw_point(edgeRight.xCoordinate[i].inside, i, RGB565_GREEN);
                        //tft180_draw_point(edgeRight.xCoordinate[i].outside, i, RGB565_RED);
                    }
                    tft180_draw_point((edgeRight.xCoordinate[i].inside+edgeLeft.xCoordinate[i].inside)/2, i, RGB565_BLUE);

                }
                tft180_show_float(0, 0, Err, 3, 2);
                tft180_show_uint(0, 20, thershold_local, 3);
                tft180_show_uint(0, 40, highest_line, 3);
                tft180_show_uint(0, 60, L_up_corner_line, 3);
                tft180_show_uint(0, 80, R_up_corner_line, 3);
//                tft180_show_uint(0, 100, cross_flag.cross_state, 3);


                tft180_show_uint(40, 0, lose_left_circlepermit, 2);
                tft180_show_uint(40, 20, lose_right_circlepermit, 2);
                tft180_show_uint(40, 40, Straight_flag, 1);
                tft180_show_uint(40, 60, L_down_corner_line, 3);
                tft180_show_uint(40, 80, R_down_corner_line, 3);
                tft180_show_uint(40, 100, circle_find, 1);

                tft180_show_uint(80,  0, Monotonicity_L_line, 3);
                tft180_show_uint(80,  20, edgeLeft.xCoordinate[Monotonicity_L_line].inside, 3);
                tft180_show_uint(80,  60, Monotonicity_R_line, 3);
                tft180_show_uint(80,  80, edgeRight.xCoordinate[Monotonicity_R_line].inside, 3);


//                tft180_show_float(120, 0, cricle_delta_angle, 3, 1);
                tft180_show_float(120, 0, ATT_Angle.yaw, 3, 1);
                tft180_show_float(120, 20, ATT_Angle.pit, 3, 1);
                tft180_show_float(120, 40, ATT_Angle.rol, 3, 1);
                tft180_show_float(120, 60, PID_L.Speed, 2, 1);
                tft180_show_float(120, 80, PID_R.Speed, 2, 1);


            }
        }

        if (BUT_flag == 0) // 啥都不显示
        {
            if (change_flag == 0)
            {
                tft180_full(RGB565_WHITE);
                change_flag = 1;
            }
        }
        else if (BUT_flag == 1) // 启动默认显示LCD上的数据
        {
        //        tft180_show_string(0, 0, "yaw:");// 第一个参数表示的是列即最大168 第二个参数表示行 即最大120
//                tft180_show_float(30, 0, PID_TURN_GYRO_CAMERA.W_Target, 2, 3);
//                tft180_show_float(80, 105, Gyr_rad.Z*57, 2, 3);
//                tft180_show_int(0, 0, PID_All.Speed_Target, 3);
//                tft180_show_int(0, 20, PID_TURN_GYRO_CAMERA.W_Target, 3);
        //        tft180_show_string(0,20,"rol:");
        //        tft180_show_float(30, 20, ATT_Angle.rol, 2, 3);
        //        tft180_show_string(0,40,"pit:");
//                tft180_show_float(30, 40, ATT_Angle.yaw, 2, 3);
//                tft180_show_float(0, 20, PID_L.Speed_Target, 3, 2);
//            tft180_show_int(120,0,PID_L.Speed,3);
//            tft180_show_int(90,0,PID_L.Speed-PID_L.Speed_Target,2);
//            tft180_show_int(120,20,PID_R.Speed,3);
//            tft180_show_int(90,20,PID_R.Speed-PID_R.Speed_Target,2);
//            tft180_show_int(90,40,output_chasu,5);
//            tft180_show_int(90,60,pid_output_Mean,4);
//            tft180_show_uint(0, 40, circleFlag.repairLine,1);
//            tft180_show_uint(0, 60, circleFlag.fixSteer,1);
//            tft180_show_uint(0, 80, sequence_estimation_servo_rate,3);
//            tft180_show_uint(60, 20, Emergency_Stop,3);
//            tft180_show_uint(60, 40, sequence_estimation_K, 3);
//            tft180_show_uint(60, 40, thershold_local,3);
//            tft180_show_uint(60, 80, output_chasu, 3);

//            tft180_show_float(60, 100, cricle_delta_angle,3,1);
//            change_flag = 0;
        }
//        gpio_toggle_level(P33_4);

        // 此处编写需要循环执行的代码
    }
}

void SCI_Send_Datas2(uart_index_enum uart_num)
{
    int i, j;
    static unsigned short int send_data[3][4] = { { 0 }, { 0 }, { 0 } };
    short int checksum = 0;
    unsigned char xorsum = 0, high, low;

    send_data[0][0] = (unsigned short int) (PID_L.Speed);
    send_data[0][1] = (unsigned short int) (PID_R.Speed);
    send_data[0][2] = (unsigned short int)(0);
    send_data[0][3] = (unsigned short int)(0);

    send_data[1][0] = (unsigned short int) (0);
    send_data[1][1] = (unsigned short int) (0);
    send_data[1][2] = (unsigned short int) (0);
    send_data[1][3] = (unsigned short int)(0);

    send_data[2][0] = (unsigned short int)(0);
    send_data[2][1] = (unsigned short int)(0);
    send_data[2][2] = (unsigned short int)(0);
    send_data[2][3] = (unsigned short int)(0);

    uart_write_byte(uart_num, 'S');
    uart_write_byte(uart_num, 'T');
    for (i = 0; i < 3; i++)
        for (j = 0; j < 4; j++)
        {
            low = (unsigned char) (send_data[i][j] & 0x00ff);
            high = (unsigned char) (send_data[i][j] >> 8u);
            uart_write_byte(uart_num, low);
            uart_write_byte(uart_num, high);
            checksum += low;
            checksum += high;
            xorsum ^= low;
            xorsum ^= high;
        }
    uart_write_byte(uart_num, (unsigned char) (checksum & 0x00ff));
    uart_write_byte(uart_num, xorsum);
}
#pragma section all restore
// **************************** 代码区域 ****************************
