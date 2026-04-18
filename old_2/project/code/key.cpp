#include "key.h"
#include <opencv2/opencv.hpp>

//车启动标志位
int car_start = 0;

int yuansu_num = 0;
int menue[4], menue_bisai[4];
float roadblock_chu = 90.0;
float roadblock_hui = 190.0;
float roadblock_k = 180.0;
int fache;//发车标志位 0-未发车 1-无电调发车 2-有电调发车

uint8_t image_choose;
uint8_t binary_image[Height][Width];
float V;
float speed_k = 1.15;//220  1.15    270  1.20  0.93+0.001x
KEY key[2];
void Key_read(void)
{
    key[0].key_sta = gpio_get_level(KEY_0);
    key[1].key_sta = gpio_get_level(KEY_1);
    for (int i = 0; i <= 1; i++) {
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

int16_t data_index = 0;
float expoure;
int selectedIndex = 0;

void Final_menu()
{
    int KEY=0;
    int MOD=0;
    ParamManager manager;
    manager.load();
    //添加挡墙选中索引
    std::vector<std::string> paramNames = manager.getParamNames();
    // int selectedIndex = 0;
    const float step[7] = {1,1, 0.01f,1, 0.01f, 0.01f,1};
    KEY = Key_detect();
    MOD = SWI_choose_MOD();
    /*图像查看界面*/
    if (MOD == 1)
    {
        /* 显示二值化图像 */
        if(menue_bisai[0] == 0)
        {
            memcpy(binary_image[0], rgay_image, UVC_WIDTH * UVC_HEIGHT*sizeof(binary_image[0][0]));
            for(int i = 0;i<Height;i++)
            {
                for(int j = 0;j<Width;j++)
                {
                    if(binary_image[i][j] > thershold_local)
                        binary_image[i][j] = 255;
                    else
                        binary_image[i][j] = 0;
                }
            }
            ips200_show_gray_image(0, 0, binary_image[0], Width, Height);//显示二值化图像
            draw_line();
            ips200_show_string(0,140,"state:");
            ips200_show_int(50, 140, menue_bisai[0]+1, 3);
            ips200_show_string(0,160,"exposure time:");
            expoure = cap.get(cv::CAP_PROP_EXPOSURE);
            ips200_show_float(130, 160, expoure, 3, 1);
            ips200_show_string(0,180,"thershold:");
            ips200_show_int(100, 180, thershold_local, 3);
            ips200_show_string(0,200,"farthest_line");
            ips200_show_int(130,200,farthest_line,3);
            ips200_show_string(0,220,"mid_servo");
            ips200_show_int(130,220,mid_servo,3);
            ips200_show_string(0,240,"servo_duty");
            ips200_show_float(130,240,servo_pid_output,3,1);
            ips200_show_string(0,260,"kp");
            ips200_show_float(130,260,servo_pid.Kp,5,3);


            if (KEY == 1)
            {
                expoure += 1;
                cap.set(cv::CAP_PROP_EXPOSURE, expoure);   //设置曝光时间
                key[1].single_flag = 0;
            }
            if (KEY == 2)
            {
                expoure -= 1;
                cap.set(cv::CAP_PROP_EXPOSURE, expoure);   //设置曝光时间
                key[0].single_flag = 0;
            }

        }
    }
    
    /*参数调整换行选择界面*/
    else if(MOD == 2)
    {
        
        /*展示参数*/
        /*ips上展示参数*/
        Show_on_ips_index(manager, selectedIndex,10,20);
        /*按键操作部分*/
        switch(KEY)
        {
            case 2://选择上一行
            selectedIndex = (selectedIndex - 1 + paramNames.size()) % paramNames.size();
                std::cout << "当前选中: " << paramNames[selectedIndex] 
                     << "=" << *manager.getParam(paramNames[selectedIndex]) << std::endl;
                break;
            case 1://选择下一行
            selectedIndex = (selectedIndex + 1) % paramNames.size();
                std::cout << "当前选中: " << paramNames[selectedIndex] 
                     << "=" << *manager.getParam(paramNames[selectedIndex]) << std::endl;
                break;
        }

    }
    /*参数调整界面*/
    else if(MOD == 3)
    {
        /*展示参数*/
        /*ips上展示参数*/
        Show_on_ips_index(manager, selectedIndex,10,20);
        /*按键操作部分*/
        switch(KEY)
        {
            case 2://增大索引行选择的参数
            if (auto current = manager.getParam(paramNames[selectedIndex])) 
            {
                    try 
                    {
                        // 使用更高精度的double类型处理数值
                        double oldValue = std::stod(*current);
                        double newValue = oldValue + step[selectedIndex];
                        // 四舍五入到小数点后2位
                        newValue = round(newValue * 1000) / 1000;
                        
                        std::cout << std::fixed << std::setprecision(3);
                        std::cout << paramNames[selectedIndex] << ": " 
                             << oldValue << "→" << newValue << " (加数操作成功)" << std::endl;
                        
                        // 使用stringstream确保精确的字符串转换
                        std::stringstream ss;
                        ss << std::fixed << std::setprecision(3) << newValue;
                        std::string newValueStr = ss.str();
                        
                        manager.setParam(paramNames[selectedIndex], newValueStr);
                        manager.save();
                        
                        // 验证保存后的值 - 重新加载整个文件确保一致性
                        manager.load();
                        std::string savedValue;
                        std::ifstream file("settings.txt");
                        std::string line;
                        while (getline(file, line)) 
                        {
                            if (line.empty() || line[0] == '#') continue;
                            size_t pos = line.find('=');
                            if (pos != std::string::npos) 
                            {
                                std::string key = line.substr(0, pos);
                                if (key == paramNames[selectedIndex]) 
                                {
                                    savedValue = line.substr(pos + 1);
                                    break;
                                }
                            }
                        }
                        file.close();
                        
                        // 简洁验证输出
                        if (savedValue != *manager.getParam(paramNames[selectedIndex])) 
                        {
                            std::cerr << "警告: 参数保存不一致!" << std::endl;
                        }
                        
                        // 验证四舍五入是否正确（区分增减操作）
                        double expected = (KEY == 1) ? round((oldValue + step[selectedIndex]) * 100) / 100 
                                                   : round((oldValue - step[selectedIndex]) * 100) / 100;
                        double actual = std::stod(*manager.getParam(paramNames[selectedIndex]));
                        if (abs(expected - actual) > 0.0005) 
                        {
                            std::cerr << "警告: 数值四舍五入异常! 操作类型: " 
                                 << (KEY == 1 ? "增加" : "减少")
                                 << " 期望值: " << expected 
                                 << " 实际值: " << actual << std::endl;
                        }
                    } catch (const std::exception& e) 
                    {
                        std::cerr << "参数增加错误: " << e.what() << std::endl;
                    }
                }
                break;
            case 1://减少索引行选择的数据
            if (auto current = manager.getParam(paramNames[selectedIndex])) 
            {
                    try 
                    {
                        // 使用更高精度的double类型处理数值
                        double oldValue = std::stod(*current);
                        double newValue = oldValue - step[selectedIndex];
                        // 四舍五入到小数点后2位
                        newValue = round(newValue * 1000) / 1000;
                        
                        std::cout << std::fixed << std::setprecision(3);
                        std::cout << paramNames[selectedIndex] << ": " 
                             << oldValue << "→" << newValue << " (减数操作成功)" << std::endl;
                        
                        // 使用stringstream确保精确的字符串转换
                        std::stringstream ss;
                        ss << std::fixed << std::setprecision(3) << newValue;
                        std::string newValueStr = ss.str();
                        
                        manager.setParam(paramNames[selectedIndex], newValueStr);
                        manager.save();
                        
                        // 验证保存后的值 - 重新加载整个文件确保一致性
                        manager.load();
                        std::string savedValue;
                        std::ifstream file("settings.txt");
                        std::string line;
                        while (getline(file, line)) 
                        {
                            if (line.empty() || line[0] == '#') continue;
                            size_t pos = line.find('=');
                            if (pos != std::string::npos) 
                            {
                                std::string key = line.substr(0, pos);
                                if (key == paramNames[selectedIndex]) 
                                {
                                    savedValue = line.substr(pos + 1);
                                    break;
                                }
                            }
                        }
                        file.close();
                        
                        // 简洁验证输出
                        if (savedValue != *manager.getParam(paramNames[selectedIndex])) 
                        {
                            std::cerr << "警告: 参数保存不一致!" << std::endl;
                        }
                        
                        // 验证四舍五入是否正确（区分增减操作）
                        double expected = (KEY == 1) ? round((oldValue + step[selectedIndex]) * 100) / 100 
                                                   : round((oldValue - step[selectedIndex]) * 100) / 100;
                        double actual = std::stod(*manager.getParam(paramNames[selectedIndex]));
                        if (abs(expected - actual) > 0.0005) 
                        {
                            std::cerr << "警告: 数值四舍五入异常! 操作类型: " 
                                 << (KEY == 1 ? "增加" : "减少")
                                 << " 期望值: " << expected 
                                 << " 实际值: " << actual << std::endl;
                        }
                    } catch (const std::exception& e) 
                    {
                        std::cerr << "参数减少错误: " << e.what() << std::endl;
                    }
                }
                break;
        }
    }
    /*参数*/
    else if(MOD == 4) 
    {
        manager.setParam("duoji_d", "38.45");
        manager.setParam("zhidao_P", "7.85");
        manager.setParam("circle_P", "7.45");
        manager.setParam("BLDC_PWM", "850");
        manager.setParam("car_speed", "540");
        manager.setParam("zhidao_speed", "540");
        manager.setParam("circle_speed", "540");
        manager.save();
         /*ips上展示参数*/
         Show_on_ips_index(manager, selectedIndex,10,20);
    }
    /*保底参数*/
    else if(MOD == 5) 
    {
        manager.setParam("duoji_d", "29.45");
        manager.setParam("zhidao_P", "7.70");
        manager.setParam("circle_P", "7.45");
        manager.setParam("BLDC_PWM", "850");
        manager.setParam("car_speed", "500");
        manager.setParam("zhidao_speed", "500");
        manager.setParam("circle_speed", "500");
        manager.save();
         /*ips上展示参数*/
         Show_on_ips_index(manager, selectedIndex,10,20);
    }
    /*参数赋值*/
    loadParams(manager);
}

KeyValue Key_detect()
{
    // 添加防抖延时
    const uint32_t debounceDelay = 100000; // 20ms防抖时间
    if(gpio_get_level(KEY_0) == 0)
    {
        system_delay_us(debounceDelay);
        if(gpio_get_level(KEY_0)==0)
        {
            gpio_set_level(BEEP,0);
            return KEY_0_;
        }
    }
    else if(gpio_get_level(KEY_1)==0)
    {
        system_delay_us(debounceDelay);
        if(gpio_get_level(KEY_1)==0)
        {
            gpio_set_level(BEEP,0);
            return KEY_1_;
        }
    }
    return KEY_NONE;
}

SwiValue SWI_choose_MOD()
{
    //0↑ 1↓ 2↓ 3↓           1000
    if ((gpio_get_level(SWITCH_0) == 1) && (gpio_get_level(SWITCH_1) == 0)
            && (gpio_get_level(SWITCH_2) == 0) && (gpio_get_level(SWITCH_3) == 0))
    {
        return MOD_1;
    }
    //0↑ 1↑ 2↓ 3↓           1100
    else if ((gpio_get_level(SWITCH_0) == 1) && (gpio_get_level(SWITCH_1) == 1)
            && (gpio_get_level(SWITCH_2) == 0) && (gpio_get_level(SWITCH_3) == 0))
    {
        return MOD_2;
    }
    //0↑ 1↑ 2↑ 3↓           1110
    else if ((gpio_get_level(SWITCH_0) == 1) && (gpio_get_level(SWITCH_1) == 1)
            && (gpio_get_level(SWITCH_2) == 1) && (gpio_get_level(SWITCH_3) == 0))
    {
        return MOD_3;
    }
    //0↑ 1↑ 2↑ 3↑           1010
    else if ((gpio_get_level(SWITCH_0) == 1) && (gpio_get_level(SWITCH_1) == 0)
            && (gpio_get_level(SWITCH_2) ==1) && (gpio_get_level(SWITCH_3) == 0))        
    {
        return MOD_4;
    }

    // 1001
    else if ((gpio_get_level(SWITCH_0) == 1) && (gpio_get_level(SWITCH_1) == 0)
            && (gpio_get_level(SWITCH_2) ==0) && (gpio_get_level(SWITCH_3) == 1))        
    {
        return MOD_5;
    }
    else
        return SWI_NONE;
}

int limit_num(int a,int b,int c)
{
    if((b>=a)&&(b<=c))
        return b;
    else if(b<a)
        return a;
    else if(b>c)
        return c;
    return 0;
}

void draw_line()
{
    int8_t x1_boundary1[UVC_HEIGHT], x2_boundary1[UVC_HEIGHT], x3_boundary1[UVC_HEIGHT];
    for(uint8 i = Height-1;i>0;i--)
    {
        int adding_line_width; //补出来的宽度
        x1_boundary1[i] = edgeLeft.xCoordinate[i].inside;
        x3_boundary1[i] = edgeRight.xCoordinate[i].inside;
        if ((edgeLeft.xCoordinate[i].inside < 4
            && edgeRight.xCoordinate[i].inside < 156)
            )
        { //左转弯//左边丢了右边没丢
            adding_line_width = (int) ( adding_line_slope
                    * ((float) (i)) + adding_line_intercept);
            x2_boundary1[i] = (2 * edgeRight.xCoordinate[i].inside - adding_line_width) / 2;
        }
        else if ((edgeLeft.xCoordinate[i].inside > 4
                && edgeRight.xCoordinate[i].inside > 156)
                )
        { //右转弯右边丢了左边没丢
            adding_line_width = (int) ( adding_line_slope
                    * ((float) (i)) + adding_line_intercept);
            x2_boundary1[i] = (2 * edgeLeft.xCoordinate[i].inside
                    + adding_line_width) / 2;
        }
        else
            x2_boundary1[i] = edgeLeft.xCoordinate[i].inside/2+edgeRight.xCoordinate[i].inside/2;
        ips200_draw_point(limit_num(1, x1_boundary1[i], Width-2), i, RGB565_BLUE);
        ips200_draw_point(limit_num(1, x3_boundary1[i], Width-2), i, RGB565_GREEN);
        ips200_draw_point(limit_num(1, x2_boundary1[i], Width-2), i, RGB565_RED);
    }
}

/*ips上展示数据*/
 void Show_on_ips_index(ParamManager& manager, int selectedIndex,uint8_t startY = 10, uint8_t lineHeight = 20)
 {
    // 1.获取参数名称
    std::vector<std::string> paramNames = manager.getParamNames();
    // 2.计算可以显示的行数
    uint8_t maxLines = (160 - startY) / lineHeight;
    // 3.确定显示范围（处理滚动）
    uint8_t startIdx = (selectedIndex >= maxLines) ? selectedIndex - maxLines + 1 : 0;
    uint8_t endIdx = std::min(startIdx + maxLines, static_cast<int>(paramNames.size()));
    // 4.历遍并展示参数
    for (uint8_t i = startIdx; i<endIdx;i++){
        uint8_t yPos = startY + (i - startIdx) * lineHeight;
        if(auto value = manager.getParam(paramNames[i])){
            //选中行特殊显示
            // uint16_t textColor = (i == selectedIndex) ? RGB565_BLUE : RGB565_BLACK;
            char textchoose = (i == selectedIndex) ? '>' : '|';
            std::string displayText = textchoose + paramNames[i] + ": " + *value;
            if(displayText.length() > 26){
                displayText = displayText.substr(0,23) + "...";
            }
            //在ips上展示
            // ips200_draw_point(2, yPos / 8, textColor);
            ips200_show_string(2, yPos , displayText.c_str());
        }
    }
 }

 /*读取settings.txt文件并完成参数修改*/
 bool loadParams(ParamManager& manager){
    try{
        //加载文件
        manager.load();
        //获取舵机PID参数
        auto duoji_d = manager.getParam("duoji_d");
        //获取电机差速参数
        auto zhidao_p_ = manager.getParam("zhidao_P");
        auto circle_p_ = manager.getParam("circle_P");
        auto BLDC_PWM_ = manager.getParam("BLDC_PWM");
        auto car_speed_ = manager.getParam("car_speed");
        auto zhidao_speed_ = manager.getParam("zhidao_speed");
        auto circle_speed_ = manager.getParam("circle_speed");
        //赋值转换
        PID_servo[2] = std::stof(*duoji_d);
        zhidao_P = std::stof(*zhidao_p_);
        circle_P = std::stof(*circle_p_);
        BLDC_PWM = std::stoi(*BLDC_PWM_);
        car_speed = std::stoi(*car_speed_);
        zhidao_speed = std::stoi(*zhidao_speed_);
        circle_speed = std::stoi(*circle_speed_);
        
        //日志
        // std::cout << "===参数加载成功===" << std::endl;
        // std::cout << "duoji_p = " << PID_servo[0] << "duoji_d = " << PID_servo[2] <<std::endl;
        // std::cout << "motor_speed = " << speed_run_slow << "chasu_P = " << chasu_P << std::endl;
        return true;
    }
    catch (const std::exception& e){
        std::cerr << "参数加载错误：" << e.what() <<", 使用默认参数" << std::endl;
        //设置默认值
        // PID_servo[0] = 7.355;
        // PID_servo[2] = 49.5;
        // chasu_P = 3.29;
        // speed_run_slow = 666;

        //修复文件
        manager.setParam("duoji_d", "29.45");
        manager.setParam("zhidao_P", "7.70");
        manager.setParam("circle_P", "7.45");
        manager.setParam("BLDC_PWM", "850");
        manager.setParam("car_speed", "500");
        manager.setParam("zhidao_speed", "500");
        manager.setParam("circle_speed", "500");
        manager.save();
        return false;
    }
 }