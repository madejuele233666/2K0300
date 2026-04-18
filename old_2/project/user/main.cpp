#include "zf_common_headfile.h"
#include <opencv2/opencv.hpp>
#include <pthread.h>



//逐飞助手所需变量
#define SERVER_IP "192.168.173.93"
#define PORT 8888
//逐飞助手图传变量
int8_t x1_boundary[UVC_HEIGHT], x2_boundary[UVC_HEIGHT], x3_boundary[UVC_HEIGHT];
uint8_t image_copy[UVC_HEIGHT][UVC_WIDTH];
TimerFdWrapper timer1(5,TIM1_IRQHANDLER);
TimerFdWrapper timer2(10,TIM2_IRQHANDLER);
// TimerFdWrapper timer3(10,TIM3_IRQHANDLER);          //改一下优先级，3要大于2
// TimerFdWrapper timer4(5,TIM4_IRQHANDLER);
struct timespec ts1;
struct timespec ts2;
extern cv::Mat gray;      // 缩放缓存（避免重复内存分配）
cv::Mat binary;
cv::Mat displayImage;
cv::VideoWriter writer;
cv::Size frameSize(Width,Height);
extern float servo_angle;
int car_speed = 500;            //这个是实际速度


void sigint_handler(int signum) 
{
    std::cout<<"收到Ctrl+C，程序即将退出"<<std::endl;
    run_state.start_state = run_stop;
    timer1.stop();
    timer2.stop();
    pwm_set_duty(MOTOR1_PWM, 0);   
    pwm_set_duty(MOTOR2_PWM, 0);
    pwm_set_duty(SERVO_MOTOR1_PWM,4674);   
    pwm_set_duty(BRUSHLESS_1,0);   
    pwm_set_duty(BRUSHLESS_2,0);    
    gpio_set_level(BEEP,0);
    exit(0);
}

void cleanup()
{
    std::cout<<"程序异常退出，执行清理操作"<<std::endl;
    // 关闭电机
    run_state.start_state = run_stop;
    timer1.stop();
    timer2.stop();
    pwm_set_duty(MOTOR1_PWM, 0);   
    pwm_set_duty(MOTOR2_PWM, 0);
    pwm_set_duty(BRUSHLESS_1,0);   
    pwm_set_duty(BRUSHLESS_2,0); 
    pwm_set_duty(SERVO_MOTOR1_PWM,4674); 
}

int main(int, char**) 
{
    /************************TCP服务*******************************/
    // if(tcp_client_init(SERVER_IP, PORT) == 0)
    // {
    //     printf("tcp_client ok\r\n");
    // }
    // else
    // {
    //     printf("tcp_client error\r\n");
    //     return -1;
    // }


    /************************逐飞助手初始化************************************/
    // // 逐飞助手初始化 设置回调函数
    // seekfree_assistant_interface_init(tcp_client_send_data, tcp_client_read_data);
    // // 逐飞助手配置显示图像信息
    // seekfree_assistant_camera_information_config(SEEKFREE_ASSISTANT_MT9V03X, image_copy[0], UVC_WIDTH, UVC_HEIGHT);
    // seekfree_assistant_camera_boundary_config(X_BOUNDARY, UVC_HEIGHT, x1_boundary, x2_boundary, x3_boundary, NULL, NULL ,NULL);



    // 注册清理函数
    atexit(cleanup);

    // 注册SIGINT信号的处理函数
    signal(SIGINT, sigint_handler);

    /********************************摄像头初始化********************************/
    if(uvc_camera_init(CAMERA) < 0)
    {
        std::cout<<"cam_err"<<std::endl;
        return -1; 
    }
    system_delay_ms(1);


    /********************************舵机中值调试********************************/
    //pwm_set_duty(SERVO_MOTOR1_PWM, 4674);        //调整舵机中值        4674

    /********************************电调调试********************************/
    // pwm_set_duty(BRUSHLESS_1,790);
    // pwm_set_duty(BRUSHLESS_2,790);

    /*******************************其他初始化***********************************/
    All_init();

    gpio_set_level(BEEP,1);
    system_delay_ms(300);
    gpio_set_level(BEEP,0);


    /*******************************开启定时器**********************************/
    timer1.start();
    timer2.start();    
    // //timer3.start();
    //timer4.start();

    while(1)
    {

        //clock_gettime(CLOCK_REALTIME,&ts1);


        /*********************阻塞式等待，图像刷新****************************/
        if(wait_image_refresh() < 0)
        {
            // 摄像头未采集到图像，这里需要关闭电机，关闭电调等。
            //printf("no image\n");
            run_state.start_state = run_stop;
            timer1.stop();      //速度pid控制   5ms
            timer2.stop();      //读取陀螺仪+出界保护   
            pwm_set_duty(MOTOR1_PWM, 0);   
            pwm_set_duty(MOTOR2_PWM, 0); 
            // timer3.stop();
            //timer4.stop();
            exit(0);
        }


        /*************************大津法***********************************/
        thershold_local = threshold(gray, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
   
        
        /****************************八邻域*******************************/
        eight_neighbor(binary, &edgeLeft, &edgeRight,rgay_image);

        /***************************舵机控制******************************/
        Steer_pid();
        // SERVO_PID_OUT();
        PD_Camera();
        if(servo_pid_output>Sevro_Limit)
            servo_pid_output=Sevro_Limit;
        else if(servo_pid_output<-Sevro_Limit)
            servo_pid_output=-Sevro_Limit;

        pwm_set_duty(SERVO_MOTOR1_PWM, 4674+servo_pid_output);
        servo_angle = servo_pid_output * 0.03;

        
        /**************************录制函数*****************************/
        // writer.open("output.avi",cv::VideoWriter::fourcc('M', 'J', 'P','G'),50,frameSize,true);
        
        // if(!writer.isOpened())
        // {
        //     std::cerr<<"无法创建视频文件："<<std::endl;
        //     return -1;
        // }
        // cvtColor(gray, displayImage, cv::COLOR_GRAY2BGR);
        
        // for(int row = 0;row<Height;row++)
        // {
        //     cv::circle(displayImage, cv::Point(edgeLeft.xCoordinate[row].inside, row), 1, cv::Scalar(0, 0, 255), -1);  // 红色点
        //     cv::circle(displayImage, cv::Point(edgeRight.xCoordinate[row].inside, row), 1, cv::Scalar(0, 255, 255), -1);  // 黄色点

        // }
        // writer.write(displayImage);

        

        /****************************屏幕菜单**********************************/
        if(gpio_get_level(SWITCH_0) == 1)
            {
                Final_menu();
            }

        /***************************按键发车**********************************/
        else if ((gpio_get_level(SWITCH_0) == 0) && (gpio_get_level(SWITCH_1) == 0)
        && (gpio_get_level(SWITCH_2) ==1))    
        {
            if(gpio_get_level(KEY_0) == 0)
            {
                ips200_clear();
                circleFlag.fixSteer = FixOver;  //固定打角解除
                circle_find = 0;   //环岛标志位全部复位
                circleFlag.direction = CircleNull;
                circleFlag.distanceDelay = DelayNull;
                circleFlag.repairLine = RepairNull;
                circleFlag.fixSteer = FixNull;
                circleFlag.outMagnet = MagnetNull;
                zebra_flag.check_state = notcheck;
                cross_flag.cross_state = cross_null;
                zebra_flag.zebra_time = 0;
                circleFlag.lianxufaxian_distancedelay = 0;
                blockFlag.delay_distance = 0;
                black_time = 0;
                zebra_flag.zebra_distance = 0;
                zebra_flag.zebra_state = Zebra_null;
                blockFlag.seek = roadblocknotfind;
                blockFlag.delay_distance = 0;
                temp_pwm = 500;
                accl_flag=0;
                accl_time=0;
                zhidao_flag = 0;
                speed_target = 0;
                gpio_set_level(BEEP,1);
                system_delay_ms(300);
                gpio_set_level(BEEP,0);
                speed_run_slow = car_speed;
                run_state.start_state = run_brushless_start;
                //0010 电调发车
                fache = 2;
            }
        }
        /********************************调试***********************************************/

        /**********************检查编码器************************/
        // speed_encoder_left = encoder_get_count(ENCODER_1);
        // speed_encoder_right = encoder_get_count(ENCODER_2);

        /**********************检查电机************************/
        // gpio_set_level(MOTOR1_DIR, 1);
        // pwm_set_duty(MOTOR1_PWM, 1000);//左电机
        // pwm_set_duty(MOTOR2_PWM,1000);

        /**********************检查拨玛和按键************************/
        //printf("Key0:%d,Key1:%d,Switch0:%d,Switch1:%d,Switch2:%d,Switch3:%d\n",gpio_get_level(KEY_0),gpio_get_level(KEY_1),gpio_get_level(SWITCH_0),gpio_get_level(SWITCH_1),gpio_get_level(SWITCH_2),gpio_get_level(SWITCH_3));

        /**********************计算误差是否有问题************************/
       //printf("mid_servo:%d,servo_output:%.3f\n",mid_servo,servo_pid_output);

       /**********************断点找寻************************/
        //printf("左下%d,左上%d,右下%d,右上%d\n",L_down_corner_line,L_up_corner_line,R_down_corner_line,R_up_corner_line);

        /**********************tcp看图像************************/
        //memcpy(image_copy[0], rgay_image, UVC_WIDTH * UVC_HEIGHT*sizeof(image_copy[0][0]));
        // memcpy(image_copy[0], binary.data , UVC_WIDTH * UVC_HEIGHT*sizeof(image_copy[0][0]));
        // for(int i = 0 ;i< Height;i++)
        // {
        //     int adding_line_width; //补出来的宽度
        //     x1_boundary[i] = edgeLeft.xCoordinate[i].inside;
        //     x3_boundary[i] = edgeRight.xCoordinate[i].inside;
        //     if ((edgeLeft.xCoordinate[i].inside < 4
        //         && edgeRight.xCoordinate[i].inside < 156)
        //         )
        //     { //左转弯//左边丢了右边没丢
        //         adding_line_width = (int) ( adding_line_slope
        //                 * ((float) (i)) + adding_line_intercept);
        //         x2_boundary[i] = (2 * edgeRight.xCoordinate[i].inside - adding_line_width) / 2;
        //     }
        //     else if ((edgeLeft.xCoordinate[i].inside > 4
        //             && edgeRight.xCoordinate[i].inside > 156)
        //             )
        //     { //右转弯右边丢了左边没丢
        //         adding_line_width = (int) ( adding_line_slope
        //                 * ((float) (i)) + adding_line_intercept);
        //         x2_boundary[i] = (2 * edgeLeft.xCoordinate[i].inside
        //                 + adding_line_width) / 2;
        //     }
        //     else
        //         x2_boundary[i] = edgeLeft.xCoordinate[i].inside/2+edgeRight.xCoordinate[i].inside/2;
        // }
        // seekfree_assistant_camera_send();
        // printf("highest_line:%d\n",farthest_line);

         /**********************检查陀螺仪************************/
        //printf("angle:%.3f\n",cricle_delta_angle);

        /**********************检查是否发现斑马线************************/
        //printf("%d\n",zebra_flag.zebra_state);

        /**********************检查主程序是否有问题************************/
        // printf("ok\n");

        /**********************环岛连续发现距离调整************************/
        //printf("distance:%.3f\n",circleFlag.lianxufaxian_distancedelay);

        // clock_gettime(CLOCK_REALTIME,&ts2);
        // long delta_ms = (ts2.tv_sec - ts1.tv_sec) * 1000 +
        //         (ts2.tv_nsec - ts1.tv_nsec) / 1000000;
        // printf("time:%ld\n",delta_ms);
    }
}
