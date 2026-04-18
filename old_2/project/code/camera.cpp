#include "camera.h"
#include <opencv2/imgproc/imgproc.hpp>  // for cv::cvtColor
#include <opencv2/highgui/highgui.hpp> // for cv::VideoCapture
#include <opencv2/opencv.hpp>

using namespace std;
using namespace cv;

#define FARTHEST_LIMIT 30           //31

/* 变量定义区 */
int middle = 79;//根据图像进行修改
int zhidao_flag;
int white_black_turncount = 0;//黑白跳变点
int BLDC_PWM = 850; //无刷电机最终占空比
int farthest_line;

//权重数组
uint8 mid_weight_list[Height] =
{
    42,42,32,20,15,10,8,5,3,3,             //0~9行
    3,3,2,2,1,1,1,1,1,     //10~19行
    1,1,1,1,1,1,1,1,1,1,             //20~29行
    1,1,1,1,1,1,1,1,1,1,   //30~39行
    1,1,1,1,1,1,1,1,1,1,             //40~49行
    1,1,1,1,1,1,1,1,1,1,       //50~59行
    1,1,1,1,1,1,1,1,1,1,             //60~69行
    1,1,1,1,1,1,1,1,1,1,             //70~79行
    1,1,1,1,1,1,1,1,1,1,             //80~89行
    1,1,1,1,1,1,1,1,1,1,             //90~99行
    1,1,1,1,1,1,1,1,1,1,             //100~109行
    1,1,1,1,1,1,1,1,1,1              //110~119行
};

int beginLine = 14;//最终截至行，为farthest_line + beginline


//***********************************************************************************************
//作用：大津法二值化
//参数说明：输入参数：指向图像的指针 返回值：最佳阈值
//使用示例：
//***********************************************************************************************
int GetOSTU(unsigned char *tmImage)
{
    unsigned int i, j;
    unsigned long Amount = 0;
    unsigned long PixelBack = 0;
    unsigned long PixelIntegralBack = 0;
    unsigned long PixelIntegral = 0;
    signed long PixelIntegralFore = 0;
    signed long PixelFore = 0;
    double OmegaBack, OmegaFore, MicroBack, MicroFore, SigmaB, Sigma; // 类间方差;
    unsigned char MinValue, MaxValue;
    unsigned char Threshold = 0;
    unsigned int HistoGram[256];              //
    for (j = 0; j < 256; j++)
        HistoGram[j] = 0; //初始化灰度直方图
    for (j = 0; j < Height; j += 4) {
        for (i = 0; i < Width; i += 4) {
            HistoGram[*(tmImage + j * Width + i)]++; //统计灰度级中每个像素在整幅图像中的个数
        }
    }
    for (MinValue = 0; MinValue < 256 && HistoGram[MinValue] == 0; MinValue++)
        ;        //获取最小灰度的值
    for (MaxValue = 255; MaxValue > MinValue && HistoGram[MinValue] == 0;
            MaxValue--)
        ; //获取最大灰度的值
    Amount = Width * Height / 16;
    PixelIntegral = 0;
    for (j = MinValue; j <= MaxValue; j++) {
        PixelIntegral += HistoGram[j] * j; //灰度值总数
    }
    SigmaB = -1;
    for (j = MinValue; j < MaxValue; j++) {
        PixelBack = PixelBack + HistoGram[j];   //前景像素点数
        PixelFore = Amount - PixelBack;         //背景像素点数
        OmegaBack = (float) PixelBack / Amount;  //前景像素百分比
        OmegaFore = (float) PixelFore / Amount;  //背景像素百分比
        PixelIntegralBack += HistoGram[j] * j;  //前景灰度值
        PixelIntegralFore = PixelIntegral - PixelIntegralBack;  //背景灰度值
        MicroBack = (float) PixelIntegralBack / PixelBack;       //前景灰度百分比
        MicroFore = (float) PixelIntegralFore / PixelFore;       //背景灰度百分比
        Sigma = OmegaBack * OmegaFore * (MicroBack - MicroFore)
                * (MicroBack - MicroFore);       //计算类间方差
        if (Sigma > SigmaB)                    //遍历最大的类间方差g //找出最大类间方差以及对应的阈值
                {
            SigmaB = Sigma;
            Threshold = j;
        }
    }
    return Threshold;                        //返回最佳阈值;
}


//***********************************************************************************************
//作用：发现斑马线
//参数说明：
//使用示例：
//***********************************************************************************************
uint8_t start_limit = 0;
void RoadIsZebra(uint8_t* img, int theshold)
{
    if ((circle_find == 1 /*|| cross_flag.cross_state == cross_find*/)
        && zebra_flag.check_state == notcheck)
    {
        zebra_flag.check_state = check;
        //printf("ok\n");
    }
    int i = 0, j = 0;
    uint8_t white_black_num = 0;
    //printf("ok\n");
    for (i = 40; i < 80; i++)
    {
        white_black_turncount = 0;
        for (j = 25; j <= 130; j++)
        {
            //printf("theshold:%d\n",theshold);
            if ((int)(*(img + (i)*Width + j)) >= theshold
                && (int)(*(img + (i)*Width + j - 1)) < theshold)
            {
                //printf("ok\n");
                white_black_turncount++;
                //printf("white:%d\n",white_black_turncount);
            }
            if (white_black_turncount >= 6)
            {
                white_black_num++;
                
            }
        }
        //printf("num:%d\n",white_black_num);
        if (white_black_num > 8 && zebra_flag.check_state == check)
        {
            zebra_flag.zebra_time++;
            zebra_flag.check_state = notcheck;
            if (zebra_flag.zebra_time >= 1)
            {
                printf("find_zebra\n");
                zebra_flag.zebra_state = Zebra_find;
                zebra_flag.zebra_distance = 7000;
            }
            else
            {

            }
        }
    }
}

TRAIL8ALL trail8AllLeft, trail8AllRight;     //左边和右边八邻域寻找轨迹

EDGE_FROM_TRAIL8ALL edgeLeft, edgeRight;     //左右边界信息
enum StatusOfFindAnotherHorizonEdge
{
    LeftToRight, RightToLeft
};
//***************************************************************************************************
//作用：向某一方向找同一行的边界点
//参数说明：img为摄像头数组，row为列数，colunmStart为起始行数，thresholdExposure为曝光度阈值，oriention为寻找方向
//使用示例：edge = FindAnotherHorizonEdge(img, row, column, threshold, LeftToRight);
//***************************************************************************************************
int FindAnotherHorizonEdge(uint8_t* img, int row, int colunmStart,
    int thresholdExposure, enum StatusOfFindAnotherHorizonEdge oriention)
{
    if (oriention == LeftToRight)
    {     //从左向右找
        for (int i = colunmStart; i < Width - 2; ++i)
        {    //循环找右边界点
            if ((img[row * Width + i] >= thresholdExposure
                && img[row * Width + i + 1] < thresholdExposure
                && img[row * Width + i + 2] < thresholdExposure)
                || i == Width - 3)
            {   //当前白，右两点黑 或 找到边界
                return i;
            }
        }
    }
    else
    {
        for (int i = colunmStart; i > 1; --i)
        {    //循环找左边界点
            if ((img[row * Width + i] >= thresholdExposure
                && img[row * Width + i - 1] < thresholdExposure
                && img[row * Width + i - 2] < thresholdExposure)
                || i == 2)
            {   //当前白，左两点黑 或 找到边际
                return i;
            }
        }
    }
    return -1;  //参数错误才会返回-1
}


/* 八领域所用参数 */
int thershold_local;
unsigned char DIFF_limit = 7;
float Slope_limit = 1;                //找断点斜率阈值
float Slope_limit_single = 0.5;     //当一条线斜率无穷大时的斜率阈值
unsigned char DIFF_STEP = 9; //找断点的步长，下断点的限制和上断点的限制（需要根据实际情况进行修改）
unsigned char DIFF_limit_esay = 4; //找断点的步长，下断点的限制和上断点的限制（需要根据实际情况进行修改）

uint8 L_edge_8_START_LINE, R_edge_8_START_LINE;

int8 grow_dire_temp_x,grow_dire_temp_y;

unsigned char L_down_corner_line, L_up_corner_line, R_down_corner_line,R_up_corner_line; //记录上下角，十字用，圆环也可以
//unsigned char R_down_corner_fan, L_down_corner_fan;
int16_t mid_servo = Width / 2;
int16_t mid_servo_count = Width/2;
int16_t mid_last = Width/2;

unsigned char cricle_out_count; //出环岛延时计数
CIRCLE_FLAG circleFlag;     //环岛标志位结构体
RoadBlock blockFlag;
STOP_FLAG run_state;
CROSS_FLAG cross_flag;
Zebra_FLAG zebra_flag;
uint16_t cross_width_limit, cross_width_num = 0,cross_find_start_line, cross_high_num = 0; //十字
int circleDelayDistance;    //环岛距离延迟
float cricle_angle_in, cricle_angle_out, cricle_delta_angle;
int highest_line = 0;
int high_max = 0;

uint16_t lose_line_num;    //左右边丢边总数
uint8 lose_left_circlepermit;
uint8 lose_right_circlepermit;

uint8 find_flag;//种子发现

int16_t thresholdDiff = 10;         //上断点一阶差分的阈值，入环补线用
int sum_grow_dir = 0;
int BLOCK_distance = 1000;
float circle_k = 0.5;                  //0.5       
int blok = 20;                      //可能需要修改（和物块的大小有关系）
int circle_find = 0;
int circle_permit = 0;
int circle_fast_find_right;
int circle_fast_find_left;
int maybe_circle_distance;

//***********************************************************************************************
//作用：八邻域
//参数说明：
//使用示例：
//***********************************************************************************************
void eight_neighbor(const cv::Mat& binary, EDGE_FROM_TRAIL8ALL* edgeLeft, EDGE_FROM_TRAIL8ALL* edgeRight,unsigned char *img)
{
    int i, j = 0;
    //用于差分计算的变量（？）
    int diff_L_1, diff_L_2[Height] = { 0 }, diff_R_1,diff_R_2[Height] = { 0 }, diff_L_11, diff_R_11;
    float L_down_ab[2] = { 0 }, R_down_ab[2] = { 0 };   //左右下断点来拟合直线的斜率和截距（补线）
    unsigned char L_down_corner_flag = 0, R_down_corner_flag = 0;   //查看是否找到过下断点（下断点标志位）

    //初始化左右边界起始行（？）
    L_edge_8_START_LINE = 159;    //初始化第一行，用来找边界起点
    R_edge_8_START_LINE = 0;

    //初始化拐点行（？）
    L_down_corner_line = 0;
    L_up_corner_line = 0;
    R_down_corner_line = 0;
    R_up_corner_line = 0;

    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(binary, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_NONE);

    // 初始化边界结构体
    memset(edgeLeft, 0, sizeof(EDGE_FROM_TRAIL8ALL));
    memset(edgeRight, 0, sizeof(EDGE_FROM_TRAIL8ALL));
    for (int y = 0; y < Height; y++) {
        edgeLeft->xCoordinate[y].inside = -1;
        edgeLeft->xCoordinate[y].outside = -1;
        edgeLeft->xCoordinate[y].type = Null;

        edgeRight->xCoordinate[y].inside = -1;
        edgeRight->xCoordinate[y].outside = -1;
        edgeRight->xCoordinate[y].type = Null;
    }

    // 1. 找最长白列 mainCol
    int mainCol = 0, maxWhite = 0;
    for (int x = 55; x < Width - 55; x++) {
        int count = 0;
        for (int y = Height -1 ; y >= 0; y--) {
            if (binary.at<uchar>(y, x) > 0)
                count++;
            else
                break;
        }
        if (count > maxWhite) {
            maxWhite = count;
            mainCol = x;
        }
    }
    highest_line = 120 - maxWhite + 1;

    //printf("maxwhite_line:%d\n",maxWhite);

    // 2. 遍历轮廓，计算每一行左右方向离 mainCol 最近的两个点
    for (size_t i = 0; i < contours.size(); i++) {
        const std::vector<cv::Point>& contour = contours[i];
        for (const auto& pt : contour) {
            int x = pt.x;
            int y = pt.y;
            if (x <= 0 || x > Width || y <= 0 || y > Height) continue;

            int dx = x - mainCol;

            if (dx <= 0) 
            {
                // 左边
                int16_t in = edgeLeft->xCoordinate[y].inside;
                int16_t out = edgeLeft->xCoordinate[y].outside;

                if (in == -1 || std::abs(x - mainCol) < std::abs(in - mainCol)) 
                {
                    if (in != -1 && x != in) 
                    {
                        edgeLeft->xCoordinate[y].outside = in;
                        edgeLeft->xCoordinate[y].outsideIndexOfTrail = edgeLeft->xCoordinate[y].insideIndexOfTrail;
                    }
                    edgeLeft->xCoordinate[y].inside = x;
                    edgeLeft->xCoordinate[y].insideIndexOfTrail = (int16_t)i;
                }
                else if ((out == -1 || std::abs(x - mainCol) < std::abs(out - mainCol)) && x != in) 
                {
                    edgeLeft->xCoordinate[y].outside = x;
                    edgeLeft->xCoordinate[y].outsideIndexOfTrail = (int16_t)i;
                }

            }
            else if (dx > 0) {
                // 右边
                int16_t in = edgeRight->xCoordinate[y].inside;
                int16_t out = edgeRight->xCoordinate[y].outside;

                if (in == -1 || std::abs(x - mainCol) < std::abs(in - mainCol)) 
                {
                    if (in != -1 && x != in) {
                        edgeRight->xCoordinate[y].outside = in;
                        edgeRight->xCoordinate[y].outsideIndexOfTrail = edgeRight->xCoordinate[y].insideIndexOfTrail;
                    }
                    edgeRight->xCoordinate[y].inside = x;
                    edgeRight->xCoordinate[y].insideIndexOfTrail = (int16_t)i;
                }
                else if ((out == -1 || std::abs(x - mainCol) < std::abs(out - mainCol)) && x != in) 
                {
                    edgeRight->xCoordinate[y].outside = x;
                    edgeRight->xCoordinate[y].outsideIndexOfTrail = (int16_t)i;
                }
            }
        }
    }

    // 3. 设置类型，补全单边界
    for (int y = 0; y < Height; y++) {
        // 左
        bool inL = edgeLeft->xCoordinate[y].inside != -1;
        bool outL = edgeLeft->xCoordinate[y].outside != -1;
        if (inL && outL) edgeLeft->xCoordinate[y].type = Both;
        else if (inL || outL) {
            if (!inL && outL) {
                edgeLeft->xCoordinate[y].inside = edgeLeft->xCoordinate[y].outside;
                edgeLeft->xCoordinate[y].insideIndexOfTrail = edgeLeft->xCoordinate[y].outsideIndexOfTrail;
                edgeLeft->xCoordinate[y].outside = -1;
                edgeLeft->xCoordinate[y].outsideIndexOfTrail = -1;
            }
            edgeLeft->xCoordinate[y].type = Single;
        }
        else
        {
            edgeLeft->xCoordinate[y].inside = 2;
            edgeLeft->xCoordinate[y].type = Single;
        }

        // 右
        bool inR = edgeRight->xCoordinate[y].inside != -1;
        bool outR = edgeRight->xCoordinate[y].outside != -1;
        if (inR && outR) edgeRight->xCoordinate[y].type = Both;
        else if (inR || outR) {
            if (!inR && outR) {
                edgeRight->xCoordinate[y].inside = edgeRight->xCoordinate[y].outside;
                edgeRight->xCoordinate[y].insideIndexOfTrail = edgeRight->xCoordinate[y].outsideIndexOfTrail;
                edgeRight->xCoordinate[y].outside = -1;
                edgeRight->xCoordinate[y].outsideIndexOfTrail = -1;
            }
            edgeRight->xCoordinate[y].type = Single;
        }
        else
        {
            edgeRight->xCoordinate[y].inside = 158;
            edgeRight->xCoordinate[y].type = Single;
        }
    }

    // 4. 设置 peakRow
    edgeLeft->peakRow = -1;
    edgeRight->peakRow = -1;
    for (int y = Height - 1; y >= 0; y--) {
        if (edgeLeft->xCoordinate[y].type != Null && edgeLeft->peakRow == -1)
            edgeLeft->peakRow = y;
        if (edgeRight->xCoordinate[y].type != Null && edgeRight->peakRow == -1)
            edgeRight->peakRow = y;
    }
    /*! @brief
     *
     * 记录丢边数量
     *
     * 记录丢边数量
     *
     * 记录丢边数量
     *
     * 记录丢边数量

     */
    lose_line_num = 0;
    lose_left_circlepermit=0;
    lose_right_circlepermit=0;

    for (i = START_LINE; i > highest_line; --i)
    {    //从开始到最后一行

        //需要根据实际情况进行修改
        if (edgeRight->xCoordinate[i].inside >= 157
                || edgeLeft->xCoordinate[i].inside <= 3)
        {
            lose_line_num++;
        }
        if (i>=20 && i<=START_LINE-30 && edgeLeft->xCoordinate[i].inside < 4)
        {
            lose_left_circlepermit++;
        }
        if (i>=20 && i<=START_LINE-30 && edgeRight->xCoordinate[i].inside > 156)
        {
            lose_right_circlepermit++;
        }
    }


    /*! @brief
     *
     * 找断点
     *
     * 找断点
     *
     * 找断点
     *
     * 找断点
     */

    /*! @brief
     * 找左边的上下角（上下断点）
     *
     * 找左边的上下角（上下断点）
     *
     * 找左边的上下角（上下断点）
     */
    //int16_t thresholdDiff = 10;//上断点一阶差分的阈值，入环补线用

    //不在圆环的固定打角模式下
    if (circleFlag.fixSteer != FixStart)
    {//整个if包括找左边上下拐点
        for (i = START_LINE-2*DIFF_STEP; i > highest_line + DIFF_STEP ;--i)//从下往上找
        {//整个for包括找左边上下拐点
            diff_L_1 = edgeLeft->xCoordinate[i].inside - edgeLeft->xCoordinate[i + DIFF_STEP].inside;//上减下    为正     相差比较小
            diff_L_11 = edgeLeft->xCoordinate[i - DIFF_STEP].inside - edgeLeft->xCoordinate[i].inside;//上减下  为负    相差比较大
            diff_L_2[i] = diff_L_11 - diff_L_1;//很负 减 微正
            //*************下断点*************
            //*************下断点*************
            if(diff_L_2[i] <= -1 * DIFF_limit + 8 && diff_L_11 < 0 && diff_L_1>0 && L_down_corner_flag != 255)
            { //边界数组的二阶差分够大做初次判断，找到一次就不用继续找了
              //两点求斜率
                if (i < START_LINE - 1 - DIFF_STEP && i > DIFF_STEP + 2)
                { //防止越界和误判（上述if判断可以修改）
                    float kb11[2],kb1[2],kb2[2];   //存一元一次方程的k和b的数组
                    connect_two_point(edgeLeft->xCoordinate[i].inside,
                            edgeLeft->xCoordinate[i+DIFF_STEP].inside,
                            i,
                            i+DIFF_STEP,
                            kb11); //两点求斜率(近处点)
                    connect_two_point(
                            edgeLeft->xCoordinate[i-DIFF_STEP].inside,
                            edgeLeft->xCoordinate[i].inside,
                            i-DIFF_STEP,
                            i,
                            kb1); //两点求斜率(远处点)
                    connect_two_point(edgeLeft->xCoordinate[i+DIFF_STEP].inside,
                        edgeLeft->xCoordinate[i+DIFF_STEP+4].inside,
                        i+DIFF_STEP,
                        i+DIFF_STEP + 4,
                        kb2); //(两个近处点，防止s弯)
                    //两点求斜率
                    //这个条件可以修改
                    //原本条件是fabs(kb1[0]) > 1
                    //  if(i == 68)
                    //     {
                    //         printf("point1:%d,point2:%d,point3:%d,k11:%.1f,k1:%.1f,flag:%d,kb2:%.2f\n",edgeLeft->xCoordinate[i].inside,edgeLeft->xCoordinate[i-DIFF_STEP].inside,edgeLeft->xCoordinate[i+DIFF_STEP].inside,kb11[0],kb1[0],kb11[0] *kb1[0] >=0 && kb11[0] *kb1[0] < 1,kb2[0]);
                    //     }
                    if(kb11[0] *kb1[0] <= 0.1 && kb11[0] *kb1[0] > -1.1 && i >= 30 
                        && edgeLeft->xCoordinate[i+DIFF_STEP].inside != -1 && edgeLeft->xCoordinate[i-DIFF_STEP].inside != -1 && edgeLeft->xCoordinate[i].inside != -1)
                    { //后一项条件是为了考虑一条直线为竖直的情况
                        L_down_corner_line = i;
                        L_down_corner_flag = 255;  //这个标志位=255意味着找到了下断点
                    }
                }
            }
            //**************上断点***************
            if (diff_L_2[i] <= -1 * DIFF_limit_esay && diff_L_11 >= 0)//微正 减 很正
            {
                // if(i == 25)
                // {
                //     printf("ok\n");
                // }
                if(L_up_corner_line == 0
                        /*&& Grow_Diretion_Judge_Corner(&trail8AllLeft,&edgeLeft,i,L_UP,9)==L_UP_OR_R_DOWN_OK
                        &&Grow_Diretion_Judge_K(&trail8AllLeft,&edgeLeft,i,L_UP,9)*/)
                {
                    if (i < START_LINE - 1 - 2*DIFF_STEP && i > DIFF_STEP + 8)
                    {   //防止越界和误判
                        float kb11[2],kb1[2],kb2[2];   //存一元一次方程的k和b的数组
                        connect_two_point(edgeLeft->xCoordinate[i].inside,
                                edgeLeft->xCoordinate[i+DIFF_STEP].inside,
                                i,
                                i+DIFF_STEP,
                                kb11); //两点求斜率
                        connect_two_point(
                                edgeLeft->xCoordinate[i-DIFF_STEP].inside,
                                edgeLeft->xCoordinate[i].inside,
                                i-DIFF_STEP,
                                i,
                                kb1); //两点求斜率
                        connect_two_point(edgeLeft->xCoordinate[i-DIFF_STEP].inside,
                            edgeLeft->xCoordinate[i-DIFF_STEP-6].inside,
                            i-DIFF_STEP,
                            i-DIFF_STEP - 6,
                            kb2); 
                        // if(i == 30)
                        // {
                        //     printf("k11:%.1f,k1:%.1f,flag:%d,kb2:%.2f\n",kb11[0],kb1[0],kb11[0] *kb1[0] >=0 && kb11[0] *kb1[0] < 1,kb2[0]);
                        // }
                        if(kb11[0] *kb1[0] >= 0 && kb11[0] *kb1[0] <= 5 && abs(kb1[0]) > 0.7 /*&& kb2[0] < -0.5*/
                    && edgeLeft->xCoordinate[i+DIFF_STEP].inside != -1 && edgeLeft->xCoordinate[i-DIFF_STEP].inside != -1 && edgeLeft->xCoordinate[i].inside != -1)
                        {  //后一项条件是为了考虑一条直线为竖直的情况s
                            L_up_corner_line = i;
                            //printf("L_up:%d\n",L_up_corner_line);
                            if (L_down_corner_flag != 255)
                            {
                                L_down_corner_line = START_LINE;

                            }


                            /*
                                                                                        * 如果满足下列所有条件：
                             * 1、上下断点行号都不为0（即找到断点）
                             * 2、上下断点列号差值大于70
                             * 3、下断点列号大于5，表示下断点不在图像边缘
                             * 4、不处于环岛
                                                                                         * 那么认为上断点寻找有误，重新寻找
                             * */
                            if ((L_down_corner_line != 0 && L_up_corner_line != 0
                                    && edgeLeft->xCoordinate[L_up_corner_line].inside-edgeLeft->xCoordinate[L_down_corner_line].inside > 80
                                    && edgeLeft->xCoordinate[L_down_corner_line].inside > 5
                                    && circle_find+circleFlag.direction+circleFlag.repairLine+circleFlag.fixSteer+circleFlag.outMagnet==0)||edgeLeft->xCoordinate[i].inside >118)
                            {//距离太远上断点就不太对了
                                L_up_corner_line = 0;
                                //printf("wrong\n");
                            }
                            break;//找到上断点后，不管有没有下断点都不用再找了
                        }
                    }
                    // else
                    //     printf("error\n");
                }
            }
        }
    }
    /*! @brief
     *
     * 找右边的角（断点）
     *
     * 找右边的角（断点）
     *
     * 找右边的角（断点）
     *
     * 找右边的角（断点）
     */
    if (circleFlag.fixSteer != FixStart)
    {
        //完全入环的时候，因为要准备出环，所以有(circleFlag.fixSteer != FixStart)防止出现上断点乱补线
        for (i = START_LINE -2*DIFF_STEP; i > highest_line + DIFF_STEP; --i)
        {
            diff_R_1 = edgeRight->xCoordinate[i].inside - edgeRight->xCoordinate[i + DIFF_STEP].inside;//小减大   上减下  拐点处小于零 接近零
            diff_R_11 = edgeRight->xCoordinate[i - DIFF_STEP].inside - edgeRight->xCoordinate[i].inside;//小减大 上减下  拐点处大于零 相差较大
            diff_R_2[i] = diff_R_11 - diff_R_1;
            //**********下断点***************
            if (diff_R_2[i] >= DIFF_limit -8 && diff_R_11 > 0&& R_down_corner_flag != 255 )
            {
                if (i < START_LINE - 1 - DIFF_STEP && i > DIFF_STEP + 2)
                { //防止越界和误判
                    float kb11[2],kb1[2],kb2[2];   //存一元一次方程的k和b的数组,kb11为远处，kb1为近处
                    connect_two_point(edgeRight->xCoordinate[i].inside,
                        edgeRight->xCoordinate[i+DIFF_STEP].inside,
                            i,
                            i+DIFF_STEP,
                            kb11); //两点求斜率
                    connect_two_point(edgeRight->xCoordinate[i-DIFF_STEP].inside,
                        edgeRight->xCoordinate[i].inside,
                            i-DIFF_STEP,
                            i,
                            kb1); //两点求斜率
                    connect_two_point(edgeRight->xCoordinate[i+DIFF_STEP].inside,
                        edgeRight->xCoordinate[i+DIFF_STEP+4].inside,
                        i+DIFF_STEP,
                        i+DIFF_STEP+4,
                        kb2); 
                    if(kb11[0] *kb1[0] < 0.1 && kb11[0] *kb1[0] > -1.1 && i >= 30 
                        && edgeRight->xCoordinate[i+DIFF_STEP].inside != -1 && edgeRight->xCoordinate[i-DIFF_STEP].inside != -1 && edgeRight->xCoordinate[i].inside != -1)
                    { //后一项条件是为了考虑一条直线为竖直的情况
                        R_down_corner_line = i;
                        R_down_corner_flag = 255;
                    }
                }
            }
            //**********上断点***********
            if (diff_R_2[i] >= DIFF_limit_esay && diff_R_1 < 0 )   //微负  减  很负
            {
                if(R_up_corner_line == 0
                  /*&& Grow_Diretion_Judge_Corner(&trail8AllRight,&edgeRight,i,R_UP,9)==L_DOWN_OR_R_UP_OK
                  && Grow_Diretion_Judge_K(&trail8AllRight,&edgeRight,i,R_UP,9)*/)
                {
                    if (i < START_LINE - 1 - 2*DIFF_STEP && i > DIFF_STEP + 9)
                    {   //防止越界和误判
                        float kb11[2],kb1[2],kb2[2];   //存一元一次方程的k和b的数组,kb11为远处，kb1为近处
                        connect_two_point(edgeRight->xCoordinate[i].inside,
                            edgeRight->xCoordinate[i+DIFF_STEP].inside,
                                i,
                                i+DIFF_STEP,
                                kb11); //两点求斜率
                        connect_two_point(edgeRight->xCoordinate[i-DIFF_STEP].inside,
                            edgeRight->xCoordinate[i].inside,
                                i-DIFF_STEP,
                                i,
                                kb1); //两点求斜率
                        connect_two_point(edgeRight->xCoordinate[i-DIFF_STEP].inside,
                            edgeRight->xCoordinate[i-DIFF_STEP-6].inside,
                            i-DIFF_STEP,
                            i-DIFF_STEP-6,
                            kb2); 
                        // if(i == 28)
                        // {
                        //     printf("k11:%.1f,k1:%.1f,flag:%d,kb2:%.2f\n",kb11[0],kb1[0],kb11[0] *kb1[0] >=0 && kb11[0] *kb1[0] < 1,kb2[0]);
                        // }
                        if(kb11[0] *kb1[0] >0 && kb11[0] *kb1[0] < 1 && abs(kb1[0]) >= 0.65 /*&& kb2[0] > 0.5*/ &&
                        edgeRight->xCoordinate[i+DIFF_STEP].inside != -1 && edgeRight->xCoordinate[i-DIFF_STEP].inside != -1 && edgeRight->xCoordinate[i].inside != -1)
                        {  //后一项条件是为了考虑一条直线为竖直的情况
                            //printf("upx:%d,downx:%d,upy:%d,downy:%d,k:%.3f\n",edgeRight->xCoordinate[i-DIFF_STEP].inside,edgeRight->xCoordinate[i].inside,i-DIFF_STEP,i,kb1[0]);
                            R_up_corner_line = i;
                            if (R_down_corner_flag != 255)
                            {
                                R_down_corner_line = START_LINE;
                            }
                            if ((R_up_corner_line != 0 && R_down_corner_line != 0
                                    && edgeRight->xCoordinate[R_down_corner_line].inside-edgeRight->xCoordinate[R_up_corner_line].inside > 70
                                    && edgeRight->xCoordinate[R_down_corner_line].inside < 155
                                    && circle_find+circleFlag.direction+circleFlag.repairLine+circleFlag.fixSteer+circleFlag.outMagnet==0)||edgeRight->xCoordinate[i].inside < 38)
                            {//距离太远上断点就不太对了
                                R_up_corner_line = 0;
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    /*入十字发现
     *
     *入十字发现
     *
     *入十字发现
     */

    //如果已经检测到十字路口，并且左右边界都在图像两侧（即已经进入十字了）    那么保持十字路口状态
    if(cross_flag.cross_state==cross_find&&(edgeLeft->xCoordinate[START_LINE-40].inside<5||edgeRight->xCoordinate[START_LINE-40].inside>155))//使十字状态保持
    {
        cross_flag.cross_state=cross_find;
    }


    else//未检测到十字路口，重置十字路口状态
    {
        cross_flag.cross_state=cross_null;
        cross_width_num = 0;
        cross_high_num = 0;
    }

    //开始判断十字
    /*
                * 满足下列所有情况，才开始检测十字路口
     * 1、未检测到十字
     * 2、不在环岛
     * 3、左右边界下拐点行号大于30
     * 4、左右边界下拐点行号小于等于START_LINE
     * 5、左右边界下拐点行号差值小于100
     * */
    if (cross_flag.cross_state==cross_null
            && circle_find + circleFlag.direction+
               circleFlag.repairLine + circleFlag.fixSteer+ circleFlag.outMagnet == 0
            && L_down_corner_line > 30
            && R_down_corner_line > 30
            && L_down_corner_line <= START_LINE
            && R_down_corner_line <= START_LINE
            && fabs(L_down_corner_line - R_down_corner_line)< 60)
    {

        //计算赛道宽度阈值
        cross_width_limit = edgeRight->xCoordinate[R_down_corner_line].inside
                - edgeLeft->xCoordinate[L_down_corner_line].inside + 50; //计算对比用的宽度
        if (cross_width_limit > 140)
        {        //限幅
            cross_width_limit = 140;
        }
        else if(cross_width_limit < 110)
        {
            cross_width_limit = 110;
        }


        //确定检测起始行
        if(R_down_corner_line >= L_down_corner_line)
        {        //那个断点更高，就从哪个开始找
            cross_find_start_line = L_down_corner_line - 1;
        }
        else if(R_down_corner_line < L_down_corner_line)
        {
            cross_find_start_line = R_down_corner_line - 1;
        }

        //检测赛道宽度
        for (i = cross_find_start_line; i > highest_line; --i)
        {       //看看宽度够不够
            if (edgeRight->xCoordinate[i].inside - edgeLeft->xCoordinate[i].inside > cross_width_limit)
            {
                cross_width_num++;
                if(cross_width_num > 7)
                {
                    break;
                }
            }
        }


        //看纵向的贯通性
        if(cross_width_num > 7)
        { //看看纵向够不够高  从左边下拐点到右边下拐点有足够的纵贯通
            //printf("ok\n");
            for (i = edgeLeft->xCoordinate[L_down_corner_line].inside;
                    i < edgeRight->xCoordinate[R_down_corner_line].inside; ++i)
            {
                for (j = cross_find_start_line; j > highest_line; --j)
                {
                    if (img_pixel(i,j) < thershold_local || j == highest_line + 1)
                    { //找到了黑点，或者找到highest_line都还是白色的
                        if (j < 30)
                        {
                            cross_high_num++;
                            break;
                        }
                    }
                }

                //如果够宽，并且纵向贯通，那么就是十字
                if (cross_high_num > 7)
                {        //数量够了，发现十字
                    //printf("ok\n");
                    cross_flag.cross_state=cross_find;
                    break;
                }
            }
        }
    }
    /*路障发现
     *
     *路障发现
     *
     *路障发现
     */
    //左边路障
    float average = 0;
    int sum_diff = 0;
    /*
     *如果满足一下情况，开始检测左侧路障
     * 1、左边界上拐点在40~80行
     * 2、未找到右边界上拐点
     * 3、不在环岛
     * 4、未检测到十字路口
     * */
    if(L_up_corner_line>=40&&L_up_corner_line<=80
            &&L_down_corner_flag!=255&&R_up_corner_line==0&&circle_find == 0
            && cross_flag.cross_state==cross_null)
    {
        for (int i = 0; i < 10; i++)
        {

            //这个可以到时候看一下路障是什么样子
            //从左上角点（即路障处）到靠近车一侧数10个行，这10行的左右外边界都不能在边线上（因为就是正常赛道）
            //如果上述条件成立，则开始判断路障
            if(edgeLeft->xCoordinate[L_up_corner_line+2*i].inside < 10
                    ||edgeRight->xCoordinate[L_up_corner_line+2*i].inside > 150) //要留30 的余量
            {
                break;
            }


            else if(i == 9)
            {
                //计算左边界斜率，存储在L_down_ab中
                connect_two_point(edgeLeft->xCoordinate[L_up_corner_line+10].inside,
                                  edgeLeft->xCoordinate[START_LINE].inside,
                                  L_up_corner_line+10, START_LINE, L_down_ab);

                //计算左边界平均偏差（原先点-斜线）
                for (int t = L_up_corner_line; t > L_up_corner_line - 10; t--)
                {
                    sum_diff += edgeLeft->xCoordinate[t].inside - (((float) t - L_down_ab[1]) / L_down_ab[0]);
                }
                average = (float)sum_diff / 10;
                // printf("a:%.3f\n",average);
                //判断左侧路障
                /*
                 * 1、平均偏差在10到25之间
                 * 2、从左上角点起始行到搜线起始行，右边界是一条直线
                 * 如果满足上述两个条件，那么就是路障
                 * */
                if(average>18 && average<30 && find_edge_straight(edgeRight,L_up_corner_line,START_LINE)==1)
                {
                    blockFlag.seek = roadblockfind;
                    blockFlag.dir = blockLeft;
                    blockFlag.delay_distance = BLOCK_distance;
                    printf("left_block\n");
                }
            }
        }
    }
    //右边路障（与左侧路障判断条件类似）
    else if(R_up_corner_line>=40&&R_up_corner_line<=80
            &&R_down_corner_flag!=255&&L_up_corner_line==0&&circle_find == 0
            && cross_flag.cross_state==cross_null)
    {
        for (int i = 0; i < 10; i++)
        {
            if(edgeLeft->xCoordinate[R_up_corner_line+2*i].inside < 10
                    ||edgeRight->xCoordinate[R_up_corner_line+2*i].inside > 150) //要留30 的余量
            {
                break;
            }
            else if(i == 9)
            {
                connect_two_point(edgeRight->xCoordinate[R_up_corner_line+10].inside,
                    edgeRight->xCoordinate[START_LINE].inside,
                                  R_up_corner_line+10, START_LINE, R_down_ab);
                for (int t = R_up_corner_line; t > R_up_corner_line - 10; t--)
                {
                    sum_diff += edgeRight->xCoordinate[t].inside - (((float) t - R_down_ab[1]) / R_down_ab[0]);
                }
                average = (float)sum_diff / 10;
                if(average<-18 && average>-30 && find_edge_straight(edgeLeft,R_up_corner_line,START_LINE)==1)
                {
                    blockFlag.seek = roadblockfind;
                    blockFlag.dir = blockRight;
                    blockFlag.delay_distance = BLOCK_distance;
                    printf("right_block\n");
                }
            }
        }
     }
    /*
     * 斑马线
     *
     * 斑马线
     *
     * 斑马线
    */
    RoadIsZebra(rgay_image, thershold_local);

     /*陀螺仪角度记录*/
    // char buffer[50];
    // int len;
    // len = sprintf(buffer,"%.3f\n",cricle_delta_angle);
    // tcp_client_send_data((const uint8 *)buffer,len);
    /*
     * 以下是环岛
     *
     * 以下是环岛
     *
     * 以下是环岛
    */







    // circle_permit = 0;   //初始化不允许发现（防止误判）

    // //如果环岛相关的标志位都未激活，才会进入环岛检测
    // if(circle_find+circleFlag.direction+circleFlag.distanceDelay+circleFlag.repairLine+
    //        circleFlag.fixSteer+circleFlag.outMagnet==0)
    // {
    //     for (int i = 0; i < 5; i++)
    //     {
    //         //避免识别成十字
    //         if((edgeLeft->xCoordinate[START_LINE-30-2*i].inside < 5 && edgeRight->xCoordinate[START_LINE-30-2*i].inside > 155)
    //                 ||(lose_left_circlepermit>15&&lose_right_circlepermit>15))//如果该行全白
    //             break;//感觉是为了跟十字区分开
    //         if(i == 4)
    //             circle_permit = 1;   //允许开始发现
    //     }
    // }
    // // printf("%d,%d\n",lose_left_circlepermit,lose_right_circlepermit);

    /*
    为了速度太快判不进去，新条件
    为了速度太快判不进去，新条件
    为了速度太快判不进去，新条件
    */
   if(R_down_corner_flag == 255 && R_up_corner_line != 0 )
   {   
        circle_fast_find_right = 1;
        maybe_circle_distance = 7000;
        //printf("maybe_circle\n");
   }

   if(L_down_corner_flag == 255 && L_up_corner_line != 0 )
   {   
        circle_fast_find_left = 1;
        maybe_circle_distance = 7000;
        //printf("maybe_circle\n");
   }


    /*
     * 1、环岛检测连续发现距离延迟小于0（怕连续两次发现）
     * 2、当前未进入环岛
     * 3、当前不是路障
     * 4、不是十字路口
     * */
    //printf("cross_flag.cross_state:%d\n",cross_flag.cross_state);
    if(/*circle_permit==1 && */circleFlag.lianxufaxian_distancedelay <= 0
            && circle_find != 1 && blockFlag.seek==roadblocknotfind
            && cross_flag.cross_state==cross_null && gpio_get_level(SWITCH_3) ==0)
    {

        //右环岛右环岛右环岛右环岛右环岛右环岛右环岛
        /*
         * 检测到右环岛条件
         * 1、右上拐点在30~100行范围内
         * 2、左边界从20~120行是直线
         * 3、左边界无断点（即左边界连续）
         * 4、左侧边界正常，右侧边界丢失多
         * */
        //printf("corner:%d\n",R_up_corner_line);
        //printf("result:%d\n",find_edge_straight2(&edgeRight,R_up_corner_line-15,R_up_corner_line));
        //printf("R_up:%d\n",R_up_corner_line);
        //printf("R_up:%d,straight:%d,flag = %d\n",R_up_corner_line,find_edge_straight(&edgeLeft,30,67),
        // (edgeRight->xCoordinate[R_up_corner_line+5].inside >= 155 || edgeRight->xCoordinate[R_up_corner_line+8].inside >= 155 || edgeRight->xCoordinate[R_up_corner_line+3].inside >= 155));
        if(/*(circle_fast_find_right == 1 && R_down_corner_flag != 255 && find_edge_straight(edgeLeft,35,75)==1 && R_up_corner_line>=15 && ((find_edge_straight2(edgeRight,R_up_corner_line-14,R_up_corner_line) == 1 && R_up_corner_line <= 30) || (find_edge_straight3(edgeRight,R_up_corner_line-20,R_up_corner_line) == 1 && R_up_corner_line > 30))) 
        || */(R_up_corner_line>=15&&R_up_corner_line<=40&&find_edge_straight(edgeLeft,35,75)==1
                && L_down_corner_line==0 && L_up_corner_line==0 && lose_left_circlepermit <= 3 && lose_right_circlepermit >= 10
            /*&& R_down_corner_flag != 255*/ && ((find_edge_straight2(edgeRight,R_up_corner_line-14,R_up_corner_line) == 1 && R_up_corner_line <= 30) || (find_edge_straight3(edgeRight,R_up_corner_line-20,R_up_corner_line) == 1 && R_up_corner_line > 30))))
        {
            /*
             * 环岛标志位置1
             * 环岛为右环岛
             * 重置角度（可能为了打角用）
             * 下面这个好像也是角度
             * 进圆环时的角度也重置
             * 连续发现距离延迟设置为20000（好像是为了修正环岛走的路径，让圆更加圆）
             * */
            circle_find = 1;
            circleFlag.direction = CircleRight;
            cricle_delta_angle = 0;
            ATT_Angle.yaw = 0;
            cricle_angle_in = ATT_Angle.yaw;
            circleFlag.lianxufaxian_distancedelay = 100000;
            printf("right0\n");
            gpio_set_level(BEEP,1);
            circle_fast_find_right = 0;
            maybe_circle_distance = 0;
            printf("R_up:%d,lose_left:%d,lose_right:%d\n",R_up_corner_line,lose_left_circlepermit,lose_right_circlepermit);

        }


        // printf("L_up%d,find_edge_straight%d,R_down_corncer%d,R_up_coner%d,lose_rigit%d,lose_left%d\n",L_up_corner_line,find_edge_straight(&edgeRight,30,80),R_down_corner_line
        // ,R_up_corner_line,lose_right_circlepermit,lose_left_circlepermit);
        //左环岛左环岛左环岛左环岛左环岛左环岛左环岛左环岛
        if(/*(circle_fast_find_left == 1 && L_down_corner_flag != 255 && find_edge_straight(edgeLeft,35,75)==1 && L_up_corner_line >=15 && ((find_edge_straight2(edgeLeft,L_up_corner_line-14,L_up_corner_line) == 1 && L_up_corner_line <= 30) || (find_edge_straight3(edgeLeft,L_up_corner_line-20,L_up_corner_line) == 1 && L_up_corner_line > 30))) 
        || */(L_up_corner_line>=15&&L_up_corner_line<=40&&find_edge_straight(edgeRight,35,75)==1 
                && R_down_corner_line==0 && R_up_corner_line==0 && lose_right_circlepermit <= 3 && lose_left_circlepermit >= 10
                /*&& L_down_corner_flag!= 255*/ && ((find_edge_straight2(edgeLeft,L_up_corner_line-14,L_up_corner_line) == 1 && L_up_corner_line <= 30) || (find_edge_straight3(edgeLeft,L_up_corner_line-20,L_up_corner_line) == 1 && L_up_corner_line > 30))))

        {
            circle_find = 1;
            circleFlag.direction = CircleLeft;
            cricle_delta_angle = 0;
            ATT_Angle.yaw = 0;
            cricle_angle_in = ATT_Angle.yaw;
            circleFlag.lianxufaxian_distancedelay = 100000;
            printf("left0\n");
            gpio_set_level(BEEP,1);
            circle_fast_find_left = 0;
            maybe_circle_distance = 0;
            printf("L_up:%d,lose_left:%d,lose_right:%d\n",L_up_corner_line,lose_left_circlepermit,lose_right_circlepermit);
        }
    }


    //环岛内转角
    //这个判断是判断判断车还在环岛状态
    /*
     *
     * 用来计算当前位置和起始位置的角度差然后用作判断在环岛中的哪个位置的依据
     *
     * */
    if (circle_find + circleFlag.direction + circleFlag.repairLine
            + circleFlag.fixSteer != 0)
    {
        cricle_angle_out = ATT_Angle.yaw;
        cricle_delta_angle = (cricle_angle_out - cricle_angle_in);//右环为正
    }


    /*入圆环距离延时
     *
     * 入圆环距离延时
     *
     * 入圆环距离延时
     *
     * 入圆环距离延时
     */
    /*
     *
     * 好像没有用，这个原本是用来优化环岛的路线的，但是用处不是很大
     *
     *
     * */
    if (circle_find == 1
            && circleFlag.distanceDelay == DelayNull)
    {
        circleDelayDistance = 0;//距离延迟赋值      3000对应450速度
        circleFlag.distanceDelay = DelayOver;
    }



    /*入圆环补线
     *
     * 入圆环补线
     *
     * 入圆环补线
     *
     * 入圆环补线
     */
    if (circleFlag.distanceDelay == DelayOver
        && circleFlag.repairLine != RepairOver)
    {
        int rowCircleUpCorner = 0;//环岛上拐点行
        static int rowCircleUpCornerMax = 0;    //上一帧环岛上断点行
        int thresholdCircleUpCorner = 10;       //环岛上断点一阶差分阈值

        //左环岛补线
        if (circleFlag.direction == CircleLeft)
        {
            //**************************找内环最高行  丢边行**************************
            int rowCircleInerHigest = START_LINE;      //内环丢边行
            if (FindAnotherHorizonEdge(img, rowCircleInerHigest,edgeLeft->xCoordinate[rowCircleInerHigest].inside,
                    thershold_local,RightToLeft) <= 3)
            {
                rowCircleInerHigest = START_LINE;
            }
            else
            {         //存在内环，找内环丢边
                int index;
                int bianjie;
                for (index = START_LINE-1; index > zhidao_END; --index)
                {
                    bianjie=FindAnotherHorizonEdge(img, index,edgeRight->xCoordinate[index].inside,
                            thershold_local,RightToLeft);
                    if (bianjie<= 3)
                    {
                        rowCircleInerHigest = index;
                        break;
                    }
                }
            }
            //*******************从最下面开始向上，以右边界为起点找左边***********************
            //***********************每一次都从最上面开始找不准**************************
            if (circleFlag.repairLine != RepairOver)
            {
                int edge, edgeLast;
                edgeLast = edgeLeft->xCoordinate[rowCircleInerHigest].inside;
                int index;
                for (index = rowCircleInerHigest-1; index > zhidao_END; --index)
                { //从上往下遍历

                    edge = edgeLeft->xCoordinate[index].inside;
                    if (edge-edgeLast> thresholdCircleUpCorner)
                    { //满足一阶差分足够大
                        if (index - 1 < rowCircleUpCornerMax - 5)
                            continue;
                        rowCircleUpCorner =  index - 2;
                        break;
                    }
                    edgeLast = edge;
                }
                if (rowCircleUpCorner < rowCircleInerHigest //未开始补线时，拐点行必须高，否则可能是车屁股
                        && (circleFlag.repairLine == RepairNull
                                || circleFlag.repairLine == RepairStart))
                {
                    L_up_corner_line = rowCircleUpCorner;       //左上断点赋值
                }
                else
                {
                    L_up_corner_line = 0;
                }
            }
            if (L_up_corner_line != 0)
            {//发现断点后开始入环补线
                int downLine;
                downLine = 1.9 * L_up_corner_line + 25;//1.7   18
                if (downLine > START_LINE)
                {
                    downLine = START_LINE;
                }
                connect_two_point(edgeLeft->xCoordinate[L_up_corner_line].inside,
                    edgeRight->xCoordinate[downLine].inside,
                                        L_up_corner_line, downLine, R_down_ab);
                for (i = downLine; i > L_up_corner_line - 1; i--)
                {
                    edgeRight->xCoordinate[i].inside = ((float) i - R_down_ab[1]) / R_down_ab[0];
                }
                circleFlag.repairLine = RepairStart;
            }
            else if (circleFlag.repairLine == RepairStart && cricle_delta_angle < -50)
            {        //没有断点并且角度够20°，全进环岛
                circleFlag.repairLine = RepairOver;
                //gpio_set_level(BEEP,0);
                //printf("over\n");
            }
        }
        //右环
        if (circleFlag.direction == CircleRight)
        {
            //**************************找内环丢边行**************************
            int rowCircleInerHigest = START_LINE;      //内环丢边行
            if (FindAnotherHorizonEdge(img, rowCircleInerHigest,edgeLeft->xCoordinate[rowCircleInerHigest].inside,
                    thershold_local,LeftToRight) >= Width - 3)
            { //起始点便是边上
                rowCircleInerHigest = START_LINE;
            }
            else
            {         //存在内环，找内环丢边
                int index;
                int bianjie;
                for (index = START_LINE-1; index > zhidao_END; --index)
                {
                    bianjie=FindAnotherHorizonEdge(img, index,edgeLeft->xCoordinate[index].inside,
                            thershold_local,LeftToRight);
                    if (bianjie>= Width - 3)
                    {

                        rowCircleInerHigest = index;
                        break;
                    }
                }
            }
            //**************************从最下面开始向上，以左边界为起点找右边**************************
            if (circleFlag.repairLine != RepairOver)
            {
                int edge, edgeLast;
                edgeLast = edgeRight->xCoordinate[rowCircleInerHigest-1].inside;
                int index;
                for (index = rowCircleInerHigest-1; index > zhidao_END; --index)
                { //从上往下遍历

                    edge = edgeRight->xCoordinate[index].inside;
                    if (edgeLast - edge > thresholdCircleUpCorner)
                    { //满足一阶差分足够大
                        if (index - 1 < rowCircleUpCornerMax - 5)
                            continue;
                        rowCircleUpCorner =  index - 2;
                        break;
                    }
                    edgeLast = edge;
                }
                if (rowCircleUpCorner < rowCircleInerHigest //未开始补线时，拐点行必须高，否则可能是车屁股
                        && (circleFlag.repairLine == RepairNull
                                || circleFlag.repairLine == RepairStart))
                {
                    R_up_corner_line = rowCircleUpCorner;       //左上断点赋值
                }
                else
                {
                    R_up_corner_line = 0;
                }
            }
            if (R_up_corner_line != 0)
            {        //发现断点后开始入环补线
                int downLine;
                downLine = 1.9 * R_up_corner_line + 25;
                if (downLine > START_LINE)
                {
                    downLine = START_LINE;
                }
                connect_two_point(edgeRight->xCoordinate[R_up_corner_line].inside,
                        edgeLeft->xCoordinate[downLine].inside,
                        R_up_corner_line, downLine, L_down_ab);
                for (i = downLine; i > R_up_corner_line - 1; i--)
                {
                    edgeLeft->xCoordinate[i].inside = ((float) i - L_down_ab[1]) / L_down_ab[0];
                }
                circleFlag.repairLine = RepairStart;
            }
            else if (circleFlag.repairLine == RepairStart&& cricle_delta_angle > 50)
            {        //没有断点了，完全进环岛
                circleFlag.repairLine = RepairOver;
                //gpio_set_level(BEEP,0);
                //printf("over\n");
            }
        }
        if (circleFlag.repairLine != RepairOver)
        {
            if (rowCircleUpCorner > rowCircleUpCornerMax)
                rowCircleUpCornerMax = rowCircleUpCorner;  //记录最大的值
        }
        else
        {
            rowCircleUpCornerMax = 0;      //复位
        }
    }

    /*发现出圆环，开始固定打角
     *
     *发现出圆环，开始固定打角
     *
     * 发现出圆环，开始固定打角
     *
     * 发现出圆环，开始固定打角
     */
    if (circleFlag.repairLine == RepairOver && circleFlag.fixSteer == FixNull)
    {
        if ((circleFlag.direction == CircleRight))
        {        //完全靠转够角度出环
            //如果转过角度大于200并且.......或者直接转够210度，执行固定打角，出环
            if (cricle_delta_angle > 246.5 ||(cricle_delta_angle > 245.9
                    && (L_up_corner_line > (Height * 1 / 3 - 10)
                            || (L_down_corner_line > (Height * 1 / 3 - 10)
                                    && L_down_corner_flag == 255)))) //转够200°且上或下拐点在屏幕下半部分
            {     //转够270°必须固定打角
                printf("right fix start,angle:%.3f\n",cricle_delta_angle);
                circleFlag.fixSteer = FixStart;     //正在出环
            }
        }
        else if ((circleFlag.direction == CircleLeft))
        {
            if ((cricle_delta_angle < -244.5
                    && (R_up_corner_line > (Height * 1 / 3 - 5)
                            || (R_down_corner_line > (Height * 1 / 3 - 5)
                                    && R_down_corner_flag == 255))) //转够-200°且上或下拐点在屏幕下半部分
            || cricle_delta_angle < -246.5)
            {     //转够-270°必须固定打角
                printf("left fix start,angle:%.3f\n",cricle_delta_angle);
                circleFlag.fixSteer = FixStart;     //正在出环
            }
        }
    }
    /*解除出环固定打角
     *
     *解除出环固定打角
     *
     *解除出环固定打角
     *
     *解除出环固定打角
     */
    //如果固定打角了，并且角度大于290°或者小于-290°，那么说明已经出环岛了，可以取消固定打角了，把所有关于圆环的标志位都清零
    if (circleFlag.fixSteer == FixStart)
    {
        if (circleFlag.direction == CircleRight)
        {
            if (cricle_delta_angle > 338.2)
            {      //强制解除
                circleFlag.fixSteer = FixOver;  //固定打角解除
                circle_find = 0;   //环岛标志位全部复位
                circleFlag.direction = CircleNull;
                circleFlag.distanceDelay = DelayNull;
                circleFlag.repairLine = RepairNull;
                circleFlag.fixSteer = FixNull;
                circleFlag.outMagnet = MagnetNull;
                printf("right fix over:%.3f\n",cricle_delta_angle);
                gpio_set_level(BEEP,0);
            }
        }
        else if (circleFlag.direction == CircleLeft)
        {
            if (cricle_delta_angle < -338.8)
            {
                circleFlag.fixSteer = FixOver;  //固定打角解除
                circle_find = 0;   //环岛标志位全部复位
                circleFlag.direction = CircleNull;
                circleFlag.distanceDelay = DelayNull;
                circleFlag.repairLine = RepairNull;
                circleFlag.fixSteer = FixNull;
                circleFlag.outMagnet = MagnetNull;
                printf("left fix over:.%3f\n",cricle_delta_angle);
                gpio_set_level(BEEP,0);
            }
        }
    }

    //表示已经出环岛了，进一步确保环岛相关的标志位被复位
    if (circleFlag.fixSteer == FixOver && R_up_corner_line != 0 && circleFlag.direction==CircleRight)
    {
        circle_find = 0;   //环岛标志位全部复位
        circleFlag.direction = CircleNull;
        circleFlag.distanceDelay = DelayNull;
        circleFlag.repairLine = RepairNull;
        circleFlag.fixSteer = FixNull;
        circleFlag.outMagnet = MagnetNull;
    }
    else if(circleFlag.fixSteer == FixOver && L_up_corner_line != 0 && circleFlag.direction==CircleLeft)
    {
        circle_find = 0;   //环岛标志位全部复位
        circleFlag.direction = CircleNull;
        circleFlag.distanceDelay = DelayNull;
        circleFlag.repairLine = RepairNull;
        circleFlag.fixSteer = FixNull;
        circleFlag.outMagnet = MagnetNull;
    }




    
    /*以上是环岛
     *
     *以上是环岛
     *
     *以上是环岛
     *
     *以上是环岛
     */



    /*
     *非圆环只有下断点 补线
     *
     *非圆环只有下断点 补线
     *
     *非圆环只有下断点 补线
     */
    unsigned char calculate_slope_flag;    //用来判断求斜率是否成功，成功后才开始补线

    //如果未检测到环岛，执行下列操作（这个应该是十字，只检测到下断点的时候的补线代码？）
    if(circle_find != 1)
    {
        if (L_down_corner_line != 0 && edgeLeft->xCoordinate[L_down_corner_line].inside > 5
                && L_up_corner_line==0)
        {
            calculate_slope_flag = least_square_method_struct(edgeLeft, L_down_corner_line, L_down_ab);
            if (calculate_slope_flag == 1)
            {
                for (i = L_down_corner_line; i > zhidao_END; i--)
                {
                    edgeLeft->xCoordinate[i].inside = ((float) i - L_down_ab[1]) / L_down_ab[0];

                    if (edgeLeft->xCoordinate[i].inside >= 158
                            || edgeLeft->xCoordinate[i].inside > edgeRight->xCoordinate[i].inside)
                    {
                        if (highest_line < i)    //补线到哪前瞻就到哪
                        {
                            highest_line = i;
                        }
                        break;
                    }
                }
            }
        }
        if (R_down_corner_line != 0&& edgeRight->xCoordinate[R_down_corner_line].inside < 155
                && R_up_corner_line == 0)
        {
            calculate_slope_flag = least_square_method_struct(edgeRight, R_down_corner_line, R_down_ab);
            if (calculate_slope_flag == 1)
            {
                for (i = R_down_corner_line; i > zhidao_END; i--)
                {
                    edgeRight->xCoordinate[i].inside = ((float) i - R_down_ab[1]) / R_down_ab[0];
                    if (edgeRight->xCoordinate[i].inside <=2
                            || edgeRight->xCoordinate[i].inside <= edgeLeft->xCoordinate[i].inside)
                    {
                        if (highest_line < i)
                        {
                            highest_line = i;
                        }
                        break;
                    }
                }
            }
        }
    }

    if((zhidao_flag == 0 && circle_find != 1 && L_down_corner_line == 0 && R_down_corner_line == 0 &&
    R_up_corner_line == 0 && L_up_corner_line == 0 && find_edge_straight(edgeLeft,20,80) == 1 
&& find_edge_straight(edgeRight,20,80) == 1 )|| (zhidao_flag == 1 && find_edge_straight(edgeLeft,30,80) == 1 
&& find_edge_straight(edgeRight,30,80) == 1))
    {
        zhidao_flag = 1;
    }
    else
    {
        zhidao_flag = 0;
    }
    /*两点连线
      *
      * 两点连线
      *
      * 两点连线
      *
      * 两点连线
      *
      * 两点连线
      *
    */
   
    if(circle_find != 1)
    {
         if (L_down_corner_line != 0 && L_up_corner_line != 0 /*&& edgeLeft->xCoordinate[L_up_corner_line].inside <= 70*/)
         {
             connect_two_point(edgeLeft->xCoordinate[L_up_corner_line].inside,
                edgeLeft->xCoordinate[L_down_corner_line].inside,
                                     L_up_corner_line, L_down_corner_line, L_down_ab);
             for (i = L_down_corner_line; i > L_up_corner_line - 1; i--)
             {
                edgeLeft->xCoordinate[i].inside = ((float) i - L_down_ab[1])/ L_down_ab[0];
                 if (edgeLeft->xCoordinate[i].inside> edgeRight->xCoordinate[i].inside)
                 {
                     break;
                 }
             }
         }
         /************************两点连线*****************************/
         /************************两点连线*****************************/
         /************************两点连线*****************************/
         if (R_down_corner_line != 0 && R_up_corner_line != 0 /*&& edgeLeft->xCoordinate[R_up_corner_line].inside >= 50*/)
         {
                 connect_two_point(edgeRight->xCoordinate[R_up_corner_line].inside,
                    edgeRight->xCoordinate[R_down_corner_line].inside,
                        R_up_corner_line, R_down_corner_line, R_down_ab);
             for (i = R_down_corner_line; i > R_up_corner_line - 1; i--)
             {
                edgeRight->xCoordinate[i].inside = ((float) i - R_down_ab[1]) / R_down_ab[0];
                 if (edgeRight->xCoordinate[i].inside < edgeLeft->xCoordinate[i].inside)
                 {
                     break;
                 }
             }
         }
    }



    /*找中点
     *
     * 找中点
     *
     * 找中点
     * 找中点
     *
     * 找中点

     */


    //根据道路的状态和前瞻的行号计算车辆的中线（mid_servo)
    if (highest_line < 120)
    {        //防止太低 数组越界

        //如果找到环岛，并且环岛已经开始补线了
        if ((circle_find == 1
            && circleFlag.repairLine == RepairStart))
        {      //环岛//入环时候的找中点
            if (highest_line < FARTHEST_LIMIT)     
            {
                highest_line = FARTHEST_LIMIT;
            }


            if ((highest_line + HIGHEST_line_down >= L_up_corner_line
                    && circleFlag.direction == CircleLeft)
                    || (highest_line + HIGHEST_line_down >= R_up_corner_line
                            && circleFlag.direction == CircleRight))
            {
                //圆环补线进入时会出现上拐点  highest_line + HIGHEST_line_down在上拐点下方时（即上拐点太高）
                if (L_up_corner_line!=0&&highest_line + HIGHEST_line_down - L_up_corner_line < 5
                        && circleFlag.direction == CircleLeft)
                { //highest_line + HIGHEST_line_down在上拐点下方五个像素之内(距离环口稍远处，但已开始补线)
                    mid_servo = (2 + edgeRight->xCoordinate[highest_line + HIGHEST_line_down + 5].inside) / 2 - 20;//-10
                }
                else if (R_up_corner_line!=0&&highest_line + HIGHEST_line_down - R_up_corner_line
                        < 5 && circleFlag.direction == CircleRight)
                { //highest_line + HIGHEST_line_down在上拐点下方五个像素之内(距离环口稍远处，但已开始补线)
                    mid_servo = (edgeLeft->xCoordinate[highest_line + HIGHEST_line_down].inside + Width - 2)/ 2 + 20;//+10
                }
                else if(R_up_corner_line!=0&& circleFlag.direction == CircleRight)//highest_line + HIGHEST_line_down在上拐点下方五个像素之外
                {
                    mid_servo = (edgeLeft->xCoordinate[highest_line
                            + HIGHEST_line_down].inside+ edgeRight->xCoordinate[highest_line+ HIGHEST_line_down].inside) / 2 + 20;
                }
                else if(L_up_corner_line!=0&& circleFlag.direction == CircleLeft)//highest_line + HIGHEST_line_down在上拐点下方五个像素之外
                {
                    mid_servo = (edgeLeft->xCoordinate[highest_line
                            + HIGHEST_line_down].inside+ edgeRight->xCoordinate[highest_line+ HIGHEST_line_down].inside) / 2 - 20;
                }
                // else if((R_up_corner_line!=0&& circleFlag.direction == CircleRight)
                //         ||(L_up_corner_line!=0&& circleFlag.direction == CircleLeft))//highest_line + HIGHEST_line_down在上拐点下方五个像素之外
                // {
                //     mid_servo = (edgeLeft->xCoordinate[highest_line
                //             + HIGHEST_line_down].inside+ edgeRight->xCoordinate[highest_line+ HIGHEST_line_down].inside) / 2;
                // }
            }
        }


        else
        {
            if(highest_line <= FARTHEST_LIMIT)
                farthest_line = FARTHEST_LIMIT;
            else 
                farthest_line = highest_line;
            int weight_index = 0;
        
            int sum = 0, num = 0;
            for (int index = farthest_line; index < farthest_line + beginLine; ++index)
            {
                int adding_line_width; //补出来的宽度
                //如果左右两边都丢线，则舍弃这一行
                if(edgeLeft->xCoordinate[index].inside <= 3 && edgeRight->xCoordinate[index].inside >= 157 && i >= 95)
                {
                    weight_index ++;
                    continue;
                }
                else if ((edgeLeft->xCoordinate[index].inside < 4
                    && edgeRight->xCoordinate[index].inside < 156)
                    )
                { //左转弯//左边丢了右边没丢
                    adding_line_width = (int)( adding_line_slope
                            * ((float) (index)) + adding_line_intercept);
                    mid_servo_count = (2 * edgeRight->xCoordinate[index].inside - adding_line_width) / 2;
                }
                else if ((edgeLeft->xCoordinate[index].inside > 4
                        && edgeRight->xCoordinate[index].inside > 156)
                        )
                { //右转弯右边丢了左边没丢
                    adding_line_width = (int)( adding_line_slope
                            * ((float) (index)) + adding_line_intercept);
                    mid_servo_count = (2 * edgeLeft->xCoordinate[index].inside
                            + adding_line_width) / 2;
                }
                else if(edgeLeft->xCoordinate[index].inside != -1 
                    && edgeRight->xCoordinate[index].inside != -1)
                {
                    mid_servo_count = (edgeLeft->xCoordinate[index].inside
                        + edgeRight->xCoordinate[index].inside) / 2;
                }
                sum += mid_servo_count * mid_weight_list[weight_index];
                num += mid_weight_list[weight_index];
                weight_index ++;
            }
            if(num == 0)
            {
                //for (int index = highest_line + 3; index > highest_line; --index)
            }
            else
            {
                mid_servo_count = sum / num;
                mid_servo = mid_servo_count * 0.9 + mid_last * 0.1;
                mid_last = mid_servo_count;
            }
    }
}
}

/************************************************************************************************
 * 最小二乘法拟合直线,适用于刘铖康的结构体
 * 参数：边界， 断点， 存放a、b的数组指针y=ax+b
 * 返回值 0断点过于靠边  1成功拟合
 *ht 2022.4.14
 ***************************************************************************************************/
 #define COUNT_NUM 10
 #define COUNT_STEP 2
 int least_square_method_struct(EDGE_FROM_TRAIL8ALL* egde, int point, float* ab)
{
    int i, step = COUNT_STEP;
    int sumx = 0, sumy = 0, sumxy = 0, sumxx = 0, div_num;
    step = (START_LINE - point) / 10;
    if (step == 0)
    { //判断一下下边是否还有十行
        return 0;
    }
    //判断一下拟合直线用的最后一个点是不是已经到边界了，到边界的话会不准
    if (egde->xCoordinate[point + COUNT_NUM * step].inside < 3
        || egde->xCoordinate[point + COUNT_NUM * step].inside > 156)
    {
        //循环找一下不到边界的最长的步长
        for (i = step - 1; i >= 0; --i)
        {
            if (i == 0)
            {
                return 0;
            }
            else
            {
                if (egde->xCoordinate[point + COUNT_NUM * i].inside > 3
                    && egde->xCoordinate[point + COUNT_NUM * i].inside < 156)
                {
                    step = i;
                }
            }
        }
    }
    for (i = 0; i < COUNT_NUM; ++i)
    {
        sumx += egde->xCoordinate[point + i * step].inside;
        sumy += point + i * step;
        sumxx += egde->xCoordinate[point + i * step].inside
            * egde->xCoordinate[point + i * step].inside;
        sumxy += egde->xCoordinate[point + i * step].inside
            * (point + i * step);
    }
    div_num = (COUNT_NUM * sumxx - sumx * sumx);
    if (div_num != 0)
    {
        *ab = ((float)(COUNT_NUM * sumxy - sumx * sumy)) / div_num;
    }
    else
    {
        *ab = 10000;
    }
    *(ab + 1) = ((float)(sumy - (*(ab)) * sumx)) / COUNT_NUM;
    return 1;
}

/************************************************************************************************
 * 最小二乘法拟合直线,适用于刘铖康的结构体
 * 参数：边界， 断点， 存放a、b的数组指针y=ax+b
 * 返回值 0断点过于靠边  1成功拟合
 *速度决策拟线，小步长
 ***************************************************************************************************/
#define COUNT_NUM_2 5
char least_square_method_struct_small_step(EDGE_FROM_TRAIL8ALL* egde, unsigned char point,
    unsigned char point_down, float* ab) {
    unsigned char i, step = COUNT_STEP;
    int sumx = 0, sumy = 0, sumxy = 0, sumxx = 0, div_num;

    step = (point_down - point) / 5;
    if (step == 0) { //判断一下下边是否还有十行
        return 0;
    }

    for (i = 0; i < COUNT_NUM_2; ++i) {
        sumx += egde->xCoordinate[point + i * step].inside;
        sumy += point + i * step;
        sumxx += egde->xCoordinate[point + i * step].inside
            * egde->xCoordinate[point + i * step].inside;
        sumxy += egde->xCoordinate[point + i * step].inside
            * (point + i * step);
    }

    div_num = (COUNT_NUM_2 * sumxx - sumx * sumx);
    if (div_num != 0) {
        *ab = ((float)(COUNT_NUM_2 * sumxy - sumx * sumy)) / div_num;
    }
    else {
        *ab = 10000;
    }

    *(ab + 1) = ((float)(sumy - (*(ab)) * sumx)) / COUNT_NUM_2;
    return 1;
}


/************************************************************************************************
 * 连接两点的斜率拟合
 * 参数：边界，两个 断点， 存放a、b的数组指针, edge为横坐标，point为纵坐标
 * 无返回值
 *ht 2022.
 ***************************************************************************************************/
void connect_two_point(int egde_up, int egde_down, int point_up, int point_down, float* ab)
{
    int sumx = 0, sumy = 0, sumxy = 0, sumxx = 0, div_num;
    sumx = egde_up + egde_down;
    sumy = point_up + point_down;
    sumxx = egde_up * egde_up + egde_down * egde_down;
    sumxy = egde_up * point_up + egde_down * point_down;

    div_num = 2 * sumxx - sumx * sumx;
    if (div_num != 0)
    {
        *ab = ((float)(2 * sumxy - sumx * sumy)) / div_num;
    }
    else
    {
        *ab = 10000;
    }
    *(ab + 1) = ((float)(sumy - (*(ab)) * sumx)) / 2;
}

/*
 * 比较一条边界是否是直线(40行)
 * 方法：一段边界的上下点连线，比较二者的方差
 * 返回值： 0   方差太大，不是直线 或者 线段太短，
 *        1    方差小，认为是直线
 *         if (find_edge_straight(edgeRight, L_up_corner_line, L_down_corner_line)) {

 }
 */
#define STRAIGHT_ERROR_LIMIT 4 //方差比较的阈值
char find_edge_straight(EDGE_FROM_TRAIL8ALL* edge, int up_line, int down_line)
{
    float ab[2];
    int i, compare_point, compare_num = 30; //compare_num是比较的个数，越多越准
    float sum = 0;

    //这句话原本有
    if (down_line - up_line < 10)
    {
        return 0;
    }
    connect_two_point(edge->xCoordinate[up_line].inside,
        edge->xCoordinate[down_line].inside, up_line, down_line, ab); //获得斜率

    if(ab[0] == 0)
        return 0;

    for (i = up_line; i < down_line; i +=
        ((down_line - up_line) / compare_num))
    { //求方差
        compare_point = ((float)i - ab[1]) / ab[0];
        sum += pow((compare_point - edge->xCoordinate[i].inside), 2);
    }
    if (sum / compare_num < STRAIGHT_ERROR_LIMIT)
    { //比较阈值
        return 1; //直线
    }
    else
    {
        return 0;
    }
}

/*
 * 比较一条边界是否是直线(12行)
 * 方法：一段边界的上下点连线，比较二者的方差
 * 返回值： 0   方差太大，不是直线 或者 线段太短，
 *        1    方差小，认为是直线
 *         if (find_edge_straight(edgeRight, L_up_corner_line, L_down_corner_line)) {

 }
 */
char find_edge_straight2(EDGE_FROM_TRAIL8ALL* edge, int up_line, int down_line)
{
    float ab[2];
    int i, compare_point, compare_num = 13; //compare_num是比较的个数，越多越准
    float sum = 0;

    //这句话原本有
    if (down_line - up_line < 5)
    {
        return 0;
    }
    connect_two_point(edge->xCoordinate[up_line].inside,
        edge->xCoordinate[down_line].inside, up_line, down_line, ab); //获得斜率

    if(ab[0] == 0)
        return 0;

    for (i = up_line; i < down_line; i +=
        ((down_line - up_line) / compare_num))
    { //求方差
        compare_point = ((float)i - ab[1]) / ab[0];
        sum += pow((compare_point - edge->xCoordinate[i].inside), 2);
    }
    if (sum / compare_num < STRAIGHT_ERROR_LIMIT-0.5)
    { //比较阈值
        return 1; //直线
    }
    else
    {
        return 0;
    }
    
}

/*
 * 比较一条边界是否是直线(20行)
 * 方法：一段边界的上下点连线，比较二者的方差
 * 返回值： 0   方差太大，不是直线 或者 线段太短，
 *        1    方差小，认为是直线
 *         if (find_edge_straight(edgeRight, L_up_corner_line, L_down_corner_line)) {

 }
 */
char find_edge_straight3(EDGE_FROM_TRAIL8ALL* edge, int up_line, int down_line)
{
    float ab[2];
    int i, compare_point, compare_num = 14; //compare_num是比较的个数，越多越准
    float sum = 0;

    //这句话原本有
    if (down_line - up_line < 5)
    {
        return 0;
    }
    connect_two_point(edge->xCoordinate[up_line].inside,
        edge->xCoordinate[down_line].inside, up_line, down_line, ab); //获得斜率

    if(ab[0] == 0)
        return 0;

    for (i = up_line; i < down_line; i +=
        ((down_line - up_line) / compare_num))
    { //求方差
        compare_point = ((float)i - ab[1]) / ab[0];
        sum += pow((compare_point - edge->xCoordinate[i].inside), 2);
    }
    if (sum / compare_num < STRAIGHT_ERROR_LIMIT)
    { //比较阈值
        return 1; //直线
    }
    else
    {
        return 0;
    }
}