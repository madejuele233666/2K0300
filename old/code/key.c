#include "key.h"
#include "PID.h"
#include "camera.h"
#include "FUZZY_PID_UCAS.h"
int yuansu_num = 0;
int F_PWM = 850;
uint8_t menue[4], menue_bisai[4];

int ruku_time=1;
int exp_light = 65;
uint8_t image_choose = 0;
float V;
float speed_k = 1.15;//220  1.15    270  1.20  0.93+0.001x
int turn_angle=0;  //编码器转过的角度，每次将其赋值给变量然后清零就行
MENU MENU_0000;
int speed_run = 70;
float see_max_test = 0.005;
void KEY_init(void)
{
    gpio_init(LEFT, GPI, GPIO_HIGH, GPI_FLOATING_IN);                      //按键初始化
    gpio_init(RIGHT, GPI, GPIO_HIGH, GPI_FLOATING_IN);                        //按键初始化
    gpio_init(UP, GPI, GPIO_HIGH, GPI_FLOATING_IN);                     //按键初始化
    gpio_init(PUSH, GPI, GPIO_HIGH, GPI_FLOATING_IN);                       //按键初始化
    gpio_init(DOWN, GPI, GPIO_HIGH, GPI_FLOATING_IN);                          //按键初始化
}
KEY key[5];
void KEY_read(void)
{
    key[0].key_sta = gpio_get_level(LEFT);
    key[1].key_sta = gpio_get_level(RIGHT);
    key[2].key_sta = gpio_get_level(UP);
    key[3].key_sta = gpio_get_level(PUSH);
    key[4].key_sta = gpio_get_level(DOWN);
    for (int i = 0; i <= 4; i++) {
        switch (key[i].judge_sta) {
        case 0: {
            if (key[i].key_sta == 0)
                key[i].judge_sta = 1;
        }
            break;
        case 1: {
            if (key[i].key_sta == 0) {
                key[i].judge_sta = 2;
                key[i].yisi_single_flag = 1;
            } else {
                key[i].judge_sta = 0;
            }
            break;
        }
        case 2: {
            if (key[i].key_sta == 0) {
                key[i].time++;
                if (key[i].time > 70) {
                    key[i].yisi_long_flag = 1;
                }
            } else if (key[i].key_sta == 1) {
                key[i].judge_sta = 0;
                key[i].time = 0;
            }
        }
        }
        if (key[i].key_sta == 1) {
            if (key[i].yisi_long_flag == 1) {
                key[i].yisi_single_flag = 0;
                key[i].yisi_long_flag = 0;
                key[i].long_flag = 1;
                key[i].single_flag = 0;
            } else if (key[i].yisi_single_flag == 1) {
                key[i].yisi_single_flag = 0;
                key[i].yisi_long_flag = 0;
                key[i].long_flag = 0;
                key[i].single_flag = 1;
            }
        }
    }
}

void menu_0000_init(void)
{
//    if(flash_check(FLASH_SECTION_INDEX, FLASH_PAGE_INDEX))                      // 判断是否有数据
//            flash_erase_page(FLASH_SECTION_INDEX, FLASH_PAGE_INDEX);                // 擦除这一页
//    flash_buffer_clear();                                                       // 清空缓冲区
//    flash_union_buffer[0].int16_type = 67;
//    flash_write_page_from_buffer(FLASH_SECTION_INDEX, FLASH_PAGE_INDEX);        // 向指定 Flash 扇区的页码写入缓冲区数据
//    flash_union_buffer[0].float_type  = 3.1415926;
    flash_read_page_to_buffer(FLASH_SECTION_INDEX, FLASH_PAGE_INDEX);

//---------------------------------------------------------------------------------------------//
        //---------此处用于参数初始化------------//
//    flash_union_buffer[0].int16_type  = 84;
//    flash_union_buffer[1].int16_type  = 1;
//    flash_union_buffer[2].float_type  = 1.10; // 环岛K
//    flash_union_buffer[3].int16_type  = 30;  //23外切 35内切 环岛路宽
//    flash_union_buffer[4].float_type  = 1.15; // 普通道路 K1.15
//    flash_union_buffer[5].int16_type  = 50; // 普通路宽  50
//    flash_union_buffer[6].int16_type  = 35; //see max
//
//
//    flash_union_buffer[7].float_type  = 5;      // outD
//    flash_union_buffer[8].float_type  = 9;      // inD
//    flash_union_buffer[9].int16_type  = 1;      // 直到加速
//    flash_union_buffer[10].int16_type  = 25;    // 入环断点时机
//    flash_union_buffer[11].int16_type  = 300;   // 环岛延时
//    flash_union_buffer[12].float_type  = 0;     // 出环系数
//    flash_union_buffer[13].int16_type  = 0;   // P_Mode
//    flash_union_buffer[14].int16_type  = 65;   // exposure_light
//
//    flash_write_page_from_buffer(FLASH_SECTION_INDEX, FLASH_PAGE_INDEX);
        //---------此处用于参数初始化------------//

    //    flash_union_buffer[1].int16_type  = 850;

    MENU_0000.all_num=15;
    MENU_0000.now_num=0;

//    MENU_0000.menu_data_int[0]=&PID_All.Speed_Target;
    MENU_0000.menu_data_int[0]=&Speed_base;
    MENU_0000.menu_data_type[0]=MEMU_INT;
    *MENU_0000.menu_data_int[0] = flash_union_buffer[0].int16_type;
    MENU_0000.step[0]=1;
    sprintf(MENU_0000.menu_string[0],"spbs");

    MENU_0000.menu_data_int[1]=&JWJC;
    MENU_0000.menu_data_type[1]=MEMU_INT;
    *MENU_0000.menu_data_int[1] = flash_union_buffer[1].int16_type;
    MENU_0000.step[1]=1;
    sprintf(MENU_0000.menu_string[1],"JWJC");

    MENU_0000.menu_data_float[2]=&circle_k;
    MENU_0000.menu_data_type[2]=MEMU_FLOAT;
    *MENU_0000.menu_data_float[2] = flash_union_buffer[2].float_type;
    MENU_0000.step[2]=0.01;
    sprintf(MENU_0000.menu_string[2],"cirk");

    MENU_0000.menu_data_int[3]=&circle_b;
    MENU_0000.menu_data_type[3]=MEMU_INT;
    *MENU_0000.menu_data_int[3] = flash_union_buffer[3].int16_type;
    MENU_0000.step[3] = 2;
    sprintf(MENU_0000.menu_string[3],"cirb");

    MENU_0000.menu_data_float[4]=&road_k;
    MENU_0000.menu_data_type[4]=MEMU_FLOAT;
    *MENU_0000.menu_data_float[4] = flash_union_buffer[4].float_type;
    MENU_0000.step[4]=0.01;
    sprintf(MENU_0000.menu_string[4],"rodk");

    MENU_0000.menu_data_int[5]=&road_b;
    MENU_0000.menu_data_type[5]=MEMU_INT;
    *MENU_0000.menu_data_int[5] = flash_union_buffer[5].int16_type;
    MENU_0000.step[5] = 2;
    sprintf(MENU_0000.menu_string[5],"rodb");

    MENU_0000.menu_data_int[6]=&see_max;
    MENU_0000.menu_data_type[6]=MEMU_INT;
    *MENU_0000.menu_data_int[6] = flash_union_buffer[6].int16_type;
    MENU_0000.step[6] = 2;
    sprintf(MENU_0000.menu_string[6],"semx");

    MENU_0000.menu_data_float[7]=&PID_TURN_CAMERA.D;
    MENU_0000.menu_data_type[7]=MEMU_FLOAT;
    *MENU_0000.menu_data_float[7] = flash_union_buffer[7].float_type;
    MENU_0000.step[7]=0.1;
    sprintf(MENU_0000.menu_string[7],"outD");

    MENU_0000.menu_data_float[8]= &PID_TURN_GYRO_CAMERA.D;
    MENU_0000.menu_data_type[8]=MEMU_FLOAT;
    *MENU_0000.menu_data_float[8] = flash_union_buffer[8].float_type;
    MENU_0000.step[8]=0.1;
    sprintf(MENU_0000.menu_string[8],"inD");

    MENU_0000.menu_data_int[9]=&Straight_permit;
    MENU_0000.menu_data_type[9]=MEMU_INT;
    *MENU_0000.menu_data_int[9] = flash_union_buffer[9].int16_type;
    MENU_0000.step[9]=1;
    sprintf(MENU_0000.menu_string[9],"zdjs");

    MENU_0000.menu_data_int[10]=&island_point;
    MENU_0000.menu_data_type[10]=MEMU_INT;
    *MENU_0000.menu_data_int[10] = flash_union_buffer[10].int16_type;
    MENU_0000.step[10] = 1;
    sprintf(MENU_0000.menu_string[10],"ldpt");

    MENU_0000.menu_data_int[11]=&island_delay;
    MENU_0000.menu_data_type[11]=MEMU_INT;
    *MENU_0000.menu_data_int[11] = flash_union_buffer[11].int16_type;
    MENU_0000.step[11] = 10;
    sprintf(MENU_0000.menu_string[11],"dely");

    MENU_0000.menu_data_float[12]=&circle_k_err;
    MENU_0000.menu_data_type[12]=MEMU_FLOAT;
    *MENU_0000.menu_data_float[12] = flash_union_buffer[12].float_type;
    MENU_0000.step[12]=0.05;
    sprintf(MENU_0000.menu_string[12],"ckEr");


    MENU_0000.menu_data_int[13]=&P_Mode;
    MENU_0000.menu_data_type[13]=MEMU_INT;
    *MENU_0000.menu_data_int[13] = flash_union_buffer[13].int16_type;
    MENU_0000.step[13] = 1;
    sprintf(MENU_0000.menu_string[13],"PMod");

    MENU_0000.menu_data_int[14]=&exp_light;
    MENU_0000.menu_data_type[14]=MEMU_INT;
    *MENU_0000.menu_data_int[14] = flash_union_buffer[14].int16_type;
    MENU_0000.step[14] = 5;
    sprintf(MENU_0000.menu_string[14],"expo");
//    MENU_0000.menu_data_float[2]=&angle_pid_behind.Kp;
//    MENU_0000.menu_data_type[2]=MEMU_FLOAT;
//    MENU_0000.step[2]=0.1;
//    sprintf(MENU_0000.menu_string[2],"hokp");
//
//    MENU_0000.menu_data_float[3]=&angle_pid_behind.Kd;
//    MENU_0000.menu_data_type[3]=MEMU_FLOAT;
//    MENU_0000.step[3]=0.1;
//    sprintf(MENU_0000.menu_string[3],"hokd");
//
//    MENU_0000.menu_data_int[4]=&speed_run;
//    MENU_0000.menu_data_type[4]=MEMU_INT;
//    MENU_0000.step[4]=100;
//    sprintf(MENU_0000.menu_string[4],"srun");
//
//    MENU_0000.menu_data_int[5]=&speed_run_jian;
//    MENU_0000.menu_data_type[5]=MEMU_INT;
//    MENU_0000.step[5]=100;
//    sprintf(MENU_0000.menu_string[5],"jian");
//
//    MENU_0000.menu_data_float[6]=&see_max_k;
//    MENU_0000.menu_data_type[6]=MEMU_FLOAT;
//    MENU_0000.step[6]=0.002;
//    sprintf(MENU_0000.menu_string[6],"seek");
//
//    MENU_0000.menu_data_int[7]=&see_max_add;
//    MENU_0000.menu_data_type[7]=MEMU_INT;
//    MENU_0000.step[7]=5;
//    sprintf(MENU_0000.menu_string[7],"seeb");
//
//    MENU_0000.menu_data_int[8]=&BLDC_PWM;
//    MENU_0000.menu_data_type[8]=MEMU_INT;
//    MENU_0000.step[8]=10;
//    sprintf(MENU_0000.menu_string[8],"BLDC");
//
//    MENU_0000.menu_data_int[9]=&BLOCK_distance;
//    MENU_0000.menu_data_type[9]=MEMU_INT;
//    MENU_0000.step[9]=200;
//    sprintf(MENU_0000.menu_string[9],"BLOC");
//
//    MENU_0000.menu_data_float[10]=&circle_k;
//    MENU_0000.menu_data_type[10]=MEMU_FLOAT;
//    MENU_0000.step[10]=0.1;
//    sprintf(MENU_0000.menu_string[10],"cirk");
////
//    MENU_0000.menu_data_int[11]=&blok;
//    MENU_0000.menu_data_type[11]=MEMU_INT;
//    MENU_0000.step[11]=2;
//    sprintf(MENU_0000.menu_string[11],"blok");
//
//    MENU_0000.menu_data_int[12]=&speed_jia;
//    MENU_0000.menu_data_type[12]=MEMU_INT;
//    MENU_0000.step[12]=100;
//    sprintf(MENU_0000.menu_string[12],"sjia");
//
//    MENU_0000.menu_data_int[13]=&speed_control_mode;
//    MENU_0000.menu_data_type[13]=MEMU_INT;
//    MENU_0000.step[13]=0;
//    sprintf(MENU_0000.menu_string[13],"mode");
}
void menu_use(void)
{
    if((gpio_get_level(SWICH1)==0)&&(gpio_get_level(SWICH2)==0))
    {

        ////////////////////////按键操作    目前是左上变小 左下变大  右上换个值/////////////////////////
        if(key[left].single_flag == 1)
          {
              if(MENU_0000.menu_data_type[MENU_0000.now_num]==MEMU_FLOAT)
              {
                  *MENU_0000.menu_data_float[MENU_0000.now_num]+=MENU_0000.step[MENU_0000.now_num];
                  flash_union_buffer[MENU_0000.now_num].float_type = *MENU_0000.menu_data_float[MENU_0000.now_num];
              }
              else
              {
                  *MENU_0000.menu_data_int[MENU_0000.now_num]+=MENU_0000.step[MENU_0000.now_num];
                  flash_union_buffer[MENU_0000.now_num].int16_type = *MENU_0000.menu_data_int[MENU_0000.now_num];
              }
              key[left].single_flag = 0;
          }
        if(key[right].single_flag == 1)
          {
//              if(MENU_0000.menu_data_type[MENU_0000.now_num]==MEMU_FLOAT)
//              *MENU_0000.menu_data_float[MENU_0000.now_num]-=MENU_0000.step[MENU_0000.now_num];
//              else
//              {
//              *MENU_0000.menu_data_int[MENU_0000.now_num]-=MENU_0000.step[MENU_0000.now_num];
//              }
              if(MENU_0000.menu_data_type[MENU_0000.now_num]==MEMU_FLOAT)
              {
                  *MENU_0000.menu_data_float[MENU_0000.now_num]-=MENU_0000.step[MENU_0000.now_num];
                  flash_union_buffer[MENU_0000.now_num].float_type = *MENU_0000.menu_data_float[MENU_0000.now_num];
              }
              else
              {
                  *MENU_0000.menu_data_int[MENU_0000.now_num]-=MENU_0000.step[MENU_0000.now_num];
                  flash_union_buffer[MENU_0000.now_num].int16_type = *MENU_0000.menu_data_int[MENU_0000.now_num];
              }
              key[right].single_flag = 0;
          }

        if(key[down].single_flag == 1)
        {
            MENU_0000.last_num= MENU_0000.now_num;
            MENU_0000.now_num++;
            MENU_0000.now_num=MENU_0000.now_num%MENU_0000.all_num;
            key[down].single_flag = 0;
            tft180_show_string(MENU_0000.last_num/8*70, (MENU_0000.last_num%8)*15, " ");
        }
        if(key[up].single_flag == 1)
        {
            MENU_0000.last_num= MENU_0000.now_num;
            MENU_0000.now_num--;
            if (MENU_0000.now_num < 0)
                MENU_0000.now_num = 0;
            MENU_0000.now_num=MENU_0000.now_num%MENU_0000.all_num;
            key[up].single_flag = 0;
            tft180_show_string(MENU_0000.last_num/8*70, (MENU_0000.last_num%8)*15, " ");
        }

        if(key[push].single_flag == 1)
        {
            if(!flash_write_page_from_buffer(FLASH_SECTION_INDEX, FLASH_PAGE_INDEX))
            {
                gpio_set_level(P21_2,1); // 蜂鸣器响 1
                system_delay_ms(100);
                gpio_set_level(P21_2,0); // 蜂鸣器关 0
            }
            key[push].single_flag = 0;
        }
        ////////////////////////显示操作/////////////////////////
        for(uint8_t i=0;i<=14;i++)
        {
             int magnitudes;
            if(i<MENU_0000.all_num)
            {
                if(i==MENU_0000.now_num)
                {
                tft180_set_color(RGB565_RED,RGB565_BLACK);
                tft180_show_string_for_double_array(10+i/8*70, (i%8)*15, MENU_0000.menu_string[i],4);
                tft180_set_color(TFT180_DEFAULT_PENCOLOR,TFT180_DEFAULT_BGCOLOR);
                }
                else
                {
                    tft180_show_string_for_double_array(10+i/8*70, (i%8)*15, MENU_0000.menu_string[i],4);
                }
                if(MENU_0000.menu_data_type[i]==MEMU_FLOAT)//float 类型显示float
                {
                    int j=0;
                    for( j=4;j>=1;j--)
                    {
                        magnitudes=fabs(*MENU_0000.menu_data_float[i])/pow(10,j);
                        if(magnitudes>0)
                            break;
                    }
                    if(j>=3)//数值比较大，小数显示少点
                tft180_show_float(35+i/8*70, (i%8)*15, *MENU_0000.menu_data_float[i], j+1, 1);
                    else if(j==2)//数值比较小，小数显示多点
                    {
                        tft180_show_float(35+i/8*70, (i%8)*15, *MENU_0000.menu_data_float[i], j+1, 2);
                    }
                    else if(j==1)//数值比较小，小数显示多点
                    {
                        tft180_show_float(35+i/8*70, (i%8)*15, *MENU_0000.menu_data_float[i], j+1, 3);
                    }
                    else if(j==0)//数值比较小，小数显示多点
                    {
                        tft180_show_float(35+i/8*70, (i%8)*15, *MENU_0000.menu_data_float[i], 1, 4);
                    }
                }
                else//int 类型显示int
                {
                    tft180_show_int(35+i/8*70, (i%8)*15, *MENU_0000.menu_data_int[i], 4);
                }
            }
            else
            {
                tft180_show_string(10+i/8*70, (i%8)*15, "Null");
            }
        }
        tft180_show_string(90, 105, "P1");
        tft180_show_string(MENU_0000.now_num/8*70, (MENU_0000.now_num%8)*15, ">");
    }
}




//  0000000+++++





//void menu_use(void)
//{
//    if((gpio_get_level(E0)==0)&&(gpio_get_level(E1)==0)&&(gpio_get_level(E2)==1))
//    {
//        //按键操作    目前是左上变小 左下变大  右上换个值
//        if(key[left].single_flag == 1)
//        {
//            if(MENU_0000.menu_data_type[MENU_0000.now_num]==MEMU_FLOAT)
//                *MENU_0000.menu_data_float[MENU_0000.now_num]+=MENU_0000.step[MENU_0000.now_num];
//            else
//                *MENU_0000.menu_data_int[MENU_0000.now_num]+=MENU_0000.step[MENU_0000.now_num];
//            key[left].single_flag = 0;
//        }
//        if(key[right].single_flag == 1)
//        {
//            if(MENU_0000.menu_data_type[MENU_0000.now_num]==MEMU_FLOAT)
//                *MENU_0000.menu_data_float[MENU_0000.now_num]-=MENU_0000.step[MENU_0000.now_num];
//            else
//                *MENU_0000.menu_data_int[MENU_0000.now_num]-=MENU_0000.step[MENU_0000.now_num];
//            key[right].single_flag = 0;
//        }
//        if(key[rightup].single_flag == 1)
//        {
//            MENU_0000.last_num= MENU_0000.now_num;
//            MENU_0000.now_num++;
//            MENU_0000.now_num=MENU_0000.now_num%MENU_0000.all_num;
//            key[rightup].single_flag = 0;
//            tft180_show_string(MENU_0000.last_num/8*80, (MENU_0000.last_num%8)*15, " ");
//        }
//        ////////////////////////显示操作/////////////////////////
//        for(uint8_t i=0;i<=14;i++)
//        {
//            int magnitudes;
//            if(i<MENU_0000.all_num)
//            {
//                if(i==MENU_0000.now_num)
//                {
//                    tft180_set_color(RGB565_RED,RGB565_BLACK);
//                    tft180_show_string_for_double_array(10+i*10, (i%8)*15, MENU_0000.menu_string[i],4);
//                    //tft180_show_string(x, y, dat);
//                    tft180_set_color(TFT180_DEFAULT_PENCOLOR,TFT180_DEFAULT_BGCOLOR);
//                }
//                else
//                {
//                    tft180_show_string_for_double_array(10+i/8*80, (i%8)*15, MENU_0000.menu_string[i],4);
//                }
//                if(MENU_0000.menu_data_type[i]==MEMU_FLOAT)//float 类型显示float
//                {
//                    int j=0;
//                    for( j=4;j>=1;j--)
//                    {
//                        magnitudes=fabs(*MENU_0000.menu_data_float[i])/pow(10,j);
//                        if(magnitudes>0)
//                            break;
//                    }
//                    if(j>=3)//数值比较大，小数显示少点
//                        tft180_show_float(45+i/8*80, (i%8)*15, *MENU_0000.menu_data_float[i], j+1, 1);
//                    else //数值比较小，小数显示多点
//                    {
//                        tft180_show_float(45+i/8*80, (i%8)*15, *MENU_0000.menu_data_float[i], j+1, 2);
//                    }
//                }
//                else//int 类型显示int
//                {
//                    tft180_show_int(45+i/8*80, (i%8)*15, *MENU_0000.menu_data_int[i], 3);
//                }
//            }
//            else
//            {
//                tft180_show_string(10+i/8*80, (i%8)*15, "Null");
//            }
//        }
//        tft180_show_string(90, 105, "P1");
//        tft180_show_string(MENU_0000.now_num*10, (MENU_0000.now_num%8)*15, ">");
//    }
//}
//void menu_011()
//{
//    if((gpio_get_level(E0)==0)&&(gpio_get_level(E1)==1)&&(gpio_get_level(E2)==1))
//    {
//
//        if(key[left].single_flag==1)
//        {
//            image_choose+=1 ;
//            key[left].single_flag=0;
//        }
//        else if(key[right].single_flag==1)
//        {
//            speed_run += 50 ;
//            key[right].single_flag=0;
//        }
//        image_choose%=2;
//        if(image_choose)
//        {
//            tft180_show_gray_image(0,0,image_fire, MT9V03X_W, MT9V03X_H, MT9V03X_W, MT9V03X_H, 0);
//        }
//        else
//        {
//            tft180_show_gray_image(0, 0, image_fire, MT9V03X_W, MT9V03X_H, MT9V03X_W, MT9V03X_H, thershold_local);
//        }
//        tft180_show_int(0,0,circle_find,2);
//        tft180_show_float(0,20,Gyr_rad.Z*57,3,2);
//        tft180_show_int(0,40,highset_line_for_control_speed,3);
//        tft180_show_int(0,60,angle_pid.Kd,3);
//        tft180_show_int(0,80,circle_permit,3);
//        tft180_show_int(0,100,mid_servo,3);
//        tft180_show_int(80,0,cross_flag.cross_state,3);
//        tft180_show_int(80,20,see_max,3);
//        tft180_show_int(80,40,L_up_corner_line,3);
//        tft180_show_int(80,60,R_up_corner_line,3);
//        tft180_show_float(80, 80, cricle_delta_angle,4,2);
//        tft180_show_int(80,100,blockFlag.seek,1);
//        if(highest_line<127&&highest_line>0)
//        {
//        for(uint8_t i=0;i<=100;i++)
//        tft180_draw_point(i, highest_line, RGB565_RED);
//        }
//        for(uint8_t i=126; i>=5; i--)
//        {//左边界画线
//            if (edgeLeft.xCoordinate[i].type == Both && edgeLeft.xCoordinate[i].inside>=0
//                    && edgeLeft.xCoordinate[i].inside<160
//                    && i>=0 && i<=128)
//            {
//                tft180_draw_point(edgeLeft.xCoordinate[i].inside, i, RGB565_RED);
//            }
//            else if (edgeLeft.xCoordinate[i].type == Single&& edgeLeft.xCoordinate[i].inside>=0
//                    && edgeLeft.xCoordinate[i].inside<160
//                    && i>=0 && i<=128)
//            {
//                tft180_draw_point(edgeLeft.xCoordinate[i].inside, i, RGB565_RED);
//            }
//            if(edgeRight.xCoordinate[i].type == Both&& edgeRight.xCoordinate[i].inside>=0
//                    && edgeRight.xCoordinate[i].inside<160
//                    && i>=0 && i<=128)
//            {
//                tft180_draw_point(edgeRight.xCoordinate[i].inside, i, RGB565_GREEN);
//            }
//            else if (edgeRight.xCoordinate[i].type == Single&& edgeRight.xCoordinate[i].inside>=0
//                    && edgeRight.xCoordinate[i].inside<160
//                    && i>=0 && i<=128)
//            {
//                tft180_draw_point(edgeRight.xCoordinate[i].inside, i, RGB565_GREEN);
//            }
//        }
//    }
//}
/********************按键保存参数******************/
uint32 flashBuffer[20];//写在FLASH的第63扇区的0页 1K差不多有32个数据可以存
//uint32 read_buff[30];
uint8 dat;
//flash功能函数
/*
 * 功能：flash数据擦除检查
 * 输入：要检查的flash扇区，以及检查的页数
 * 输出： 若擦除成功，返回1，否则返回0
 */
uint8_t flashClearCheck(uint32 sector_num, uint32 page_num){
    int i;
    for(i = 0; i < page_num; i++){
        if(flash_check(sector_num, i)){
            return 0;
        }
    }
    return 1;
}
/*
 * 功能：flash数据存储
 * 输入：要存储的数据长度，以及扇区
 * 输出：若写入成功返回1，否则返回0
 */
uint8_t flashSave(uint32 sector_num, uint32 Lenth)
{
//    for(i = 0; i < Lenth; i++){
        flash_write_page(sector_num,0, &flashBuffer[0],Lenth);
       //eeprom_page_program(sector_num, i, &flashBuffer[i]);
//    }
//    for(i = 0; i < Lenth; i++){
//        if(!flash_check(sector_num, i)){
//            return 0;
//        }
//    }
        if(!flash_check(sector_num, 0))
        {
            return 0;
        }
    return 1;
}
//-----扇区存取函数
//void normal_param_store()
//{
//    flash_erase_sector(63,0);
//
//    if(flashClearCheck(63,0))
//    { //擦除成功
//        flashBuffer[0] =  angle_pid.Kp*10;
//        flashBuffer[1] = angle_pid.Ki*10;
//        flashBuffer[2] = angle_pid.Kd*10;
//
////        flashBuffer[4] = ;
////        flashBuffer[5] = ;
////
////        flashBuffer[6] = ;
////        flashBuffer[7] = ;
////        flashBuffer[8] = ;
////        flashBuffer[9] = ;
////        flashBuffer[10] = ;
////
////        flashBuffer[11] = ;
////        flashBuffer[12] = ;
////        flashBuffer[13] = ;
////
////        flashBuffer[14] = ;
////
////        flashBuffer[15] = ;
////        flashBuffer[16] = ;
////        flashBuffer[17] = ;
////        flashBuffer[18] = ;
////        flashBuffer[19] = ;
////        flashBuffer[20] = ;
////        flashBuffer[21] = ;
////
////        flashBuffer[22] = ;
////        flashBuffer[23] = ;
////        flashBuffer[24] = ;
////        flashBuffer[25] = ;
//        //ips200_showstr(0,1,"whether save");
//        dat=flashSave(63,20);
//    }
//}

//-----扇区读取函数
//void normal_param_read(void)
//{
//    //flash_read_page(63,0,read_buff,3);
//    flash_read_page_to_buffer(63,0);
//    angle_pid.Kp=(float)flash_union_buffer[0].uint32_type/10;
//    angle_pid.Ki=(float)flash_union_buffer[1].uint32_type/10;
//    angle_pid.Kd=(float)flash_union_buffer[2].uint32_type/10;
//}
/********************按键保存参数******************/
