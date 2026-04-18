#include "camera.h"
#include "ZiTaiJieSuan.h"
#include "Motor.h"
float rate_xcopy = 0,rate_ycopy= 0;
int Emergency_threshold = 40;
int Emergency_Stop = 0;
int see_max = 35;
int middle = 80;
int see_max_add=89;
int Straight_flag = 0;
int Straight_permit  = 0;
float see_max_k=-0.018;//3200 -0.018 89
int white_black_turncount = 0;
int BLDC_PWM = 890;
int island_delay = 500;
int island_point = 25;
float circle_k = 1.10,road_k = 1.15;
int circle_b = 35,road_b = 55;
extern int test;
extern int usta;
extern int bcount;

int Monotonicity_L_line = 0,Monotonicity_R_line = 0;
//const char Weight[128] = {
//        // 00 ——15 行权重
//    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 10
//    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 20
//    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
//    1, 1, 1, 1, 1, 2, 4, 6, 8, 10,
//    12, 14, 15, 16, 17, 18,17,16,15,14,
//    12, 10, 8,  6,  4,  2, 1, 1, 1, 1,
//    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
//    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
//    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
//    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
//    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
//    1, 1, 1, 1, 1, 1, 1, 1,// 112 ——127 行权重
//}; // 各行权重根据实际情况调整
const char Weight[128] = {
        // 00 ——15 行权重
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 10
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 20
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 30
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 40
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 50
//    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
//    1,2,3,5,8,8,5,3,2,1,
    1, 3, 5, 8, 12, 12, 8 ,5, 3,1, // 56->19.6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,// 70
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,// 112 ——127 行权重
}; // 各行权重根据实际情况调整
//const char D_Weight[30] = {
//        1, 2, 4, 6, 8, 10, 12, 14, 16, 18,
//        17,16,15,14,13,12,11,10,9,8,
//        7,6,5,4,3,2,1,1,1,1
//};
const char D_Weight[30] = {
        1, 1, 2, 2, 3, 3, 4, 4, 5, 5,
        6, 6, 7, 7, 8, 8, 7, 7, 6, 6,
        5, 5, 4, 4, 3, 3, 2, 2, 1, 1
};
//const char D_Weight[20] = {
//        1,1,2,4,6,8,10,12,14,16,
//        16,14,12,10,8,6,4,2,1,1
//};
int min_weight_line = 26;
int max_weight_line = 56;
//int min_weight_line = 26;
//int max_weight_line = 46;
//***********************************************************************************************
//作用：发现斑马线
//参数说明：
//使用示例：
//***********************************************************************************************
uint8_t start_limit = 0;
void RoadIsZebra(uint8_t *img, int theshold)
{
    if((circle_find==1||cross_flag.cross_state==cross_find)
            &&zebra_flag.check_state==notcheck||bcount>1*50)
    {
        zebra_flag.check_state=check;
    }

    int i = 0, j = 0;
    white_black_turncount = 0;
    uint8_t white_black_num=0;
    for (i = 80; i < 120; i++)
    {
        white_black_turncount = 0;
        for ( j=edgeLeft.xCoordinate[i].inside; j <=edgeRight.xCoordinate[i].inside; j++)
        {

            if ((int) (*(img + (i) * MT9V03X_DVP_W + j)) >= theshold
                    && (int) (*(img + (i) * MT9V03X_DVP_W + j - 1)) < theshold)
            {
                white_black_turncount++;
            }
            if (white_black_turncount >= 7)
            {
                white_black_num++;
            }
        }
        if(white_black_num>5&&zebra_flag.check_state==check)
        {
            zebra_flag.zebra_time++;
            zebra_flag.check_state=notcheck;
            if(zebra_flag.zebra_time>=1)
            {
                zebra_flag.zebra_state = Zebra_find;
                zebra_flag.zebra_distance = 2400;
            }
            else
            {

            }
        }
    }
}
/*-------------------------------------------------------------------------------------------------------------------
  @brief     单调性突变检测
  @param     起始点，终止行
  @return    点所在的行数，找不到返回0
  Sample     Find_Right_Up_Point(int start,int end);
  @note      前5后5它最大（最小），那他就是角点
-------------------------------------------------------------------------------------------------------------------*/
int Monotonicity_Change_Left(int start,int end)//单调性改变，返回值是单调性改变点所在的行数
{
    int i;
    int monotonicity_change_line=0;
    if(lose_left_circlepermit>=0.9*MT9V03X_H)//大部分都丢线，没有单调性判断的意义
       return monotonicity_change_line;
    if(end>=MT9V03X_H-1-5)//数组越界保护，在判断第i个点时
        end=MT9V03X_H-1-5; //要访问它前后5个点，数组两头的点要不能作为起点终点
    if(start<=5)
        start=5;
    if(start>=end)//递减计算，入口反了，直接返回0
      return monotonicity_change_line;
    for(i=end;i>=start;i--)//会读取前5后5数据，所以前面对输入范围有要求
    {
        if(edgeLeft.xCoordinate[i].inside==edgeLeft.xCoordinate[i+5].inside
        &&edgeLeft.xCoordinate[i].inside==edgeLeft.xCoordinate[i-5].inside
        &&edgeLeft.xCoordinate[i].inside==edgeLeft.xCoordinate[i+4].inside
        &&edgeLeft.xCoordinate[i].inside==edgeLeft.xCoordinate[i-4].inside
        &&edgeLeft.xCoordinate[i].inside==edgeLeft.xCoordinate[i+3].inside
        &&edgeLeft.xCoordinate[i].inside==edgeLeft.xCoordinate[i-3].inside
        &&edgeLeft.xCoordinate[i].inside==edgeLeft.xCoordinate[i+2].inside
        &&edgeLeft.xCoordinate[i].inside==edgeLeft.xCoordinate[i-2].inside
        &&edgeLeft.xCoordinate[i].inside==edgeLeft.xCoordinate[i+1].inside
        &&edgeLeft.xCoordinate[i].inside==edgeLeft.xCoordinate[i-1].inside)
        {//一堆数据一样，显然不能作为单调转折点
            continue;
        }
        else if(edgeLeft.xCoordinate[i].inside>=edgeLeft.xCoordinate[i+5].inside
        &&edgeLeft.xCoordinate[i].inside>=edgeLeft.xCoordinate[i-5].inside
        &&edgeLeft.xCoordinate[i].inside>=edgeLeft.xCoordinate[i+4].inside
        &&edgeLeft.xCoordinate[i].inside>=edgeLeft.xCoordinate[i-4].inside
        &&edgeLeft.xCoordinate[i].inside>=edgeLeft.xCoordinate[i+3].inside
        &&edgeLeft.xCoordinate[i].inside>=edgeLeft.xCoordinate[i-3].inside
        &&edgeLeft.xCoordinate[i].inside>=edgeLeft.xCoordinate[i+2].inside
        &&edgeLeft.xCoordinate[i].inside>=edgeLeft.xCoordinate[i-2].inside
        &&edgeLeft.xCoordinate[i].inside>=edgeLeft.xCoordinate[i+1].inside
        &&edgeLeft.xCoordinate[i].inside>=edgeLeft.xCoordinate[i-1].inside)
        {//就很暴力，这个数据是在前5，后5中最大的（可以取等），那就是单调突变点
            monotonicity_change_line=i;
            break;
        }
    }
    return monotonicity_change_line;
}

/*-------------------------------------------------------------------------------------------------------------------
  @brief     单调性突变检测
  @param     起始点，终止行
  @return    点所在的行数，找不到返回0
  Sample     Find_Right_Up_Point(int start,int end);
  @note      前5后5它最大（最小），那他就是角点
-------------------------------------------------------------------------------------------------------------------*/
int Monotonicity_Change_Right(int start,int end)//单调性改变，返回值是单调性改变点所在的行数
{
    int i;
    int monotonicity_change_line=0;

    if(lose_right_circlepermit>=0.9*MT9V03X_H)//大部分都丢线，没有单调性判断的意义
        return monotonicity_change_line;
    if(end>=MT9V03X_H-1-5)//数组越界保护
        end=MT9V03X_H-1-5;
     if(start<=5)
         start=5;
    if(start>=end)
        return monotonicity_change_line;
    for(i=end;i>=start;i--)//会读取前5后5数据，所以前面对输入范围有要求
        {
            if(edgeRight.xCoordinate[i].inside==edgeRight.xCoordinate[i+5].inside
            &&edgeRight.xCoordinate[i].inside==edgeRight.xCoordinate[i-5].inside
            &&edgeRight.xCoordinate[i].inside==edgeRight.xCoordinate[i+4].inside
            &&edgeRight.xCoordinate[i].inside==edgeRight.xCoordinate[i-4].inside
            &&edgeRight.xCoordinate[i].inside==edgeRight.xCoordinate[i+3].inside
            &&edgeRight.xCoordinate[i].inside==edgeRight.xCoordinate[i-3].inside
            &&edgeRight.xCoordinate[i].inside==edgeRight.xCoordinate[i+2].inside
            &&edgeRight.xCoordinate[i].inside==edgeRight.xCoordinate[i-2].inside
            &&edgeRight.xCoordinate[i].inside==edgeRight.xCoordinate[i+1].inside
            &&edgeRight.xCoordinate[i].inside==edgeRight.xCoordinate[i-1].inside)
            {//一堆数据一样，显然不能作为单调转折点
                continue;
            }
            else if(edgeRight.xCoordinate[i].inside<=edgeRight.xCoordinate[i+5].inside
            &&edgeRight.xCoordinate[i].inside<=edgeRight.xCoordinate[i-5].inside
            &&edgeRight.xCoordinate[i].inside<=edgeRight.xCoordinate[i+4].inside
            &&edgeRight.xCoordinate[i].inside<=edgeRight.xCoordinate[i-4].inside
            &&edgeRight.xCoordinate[i].inside<=edgeRight.xCoordinate[i+3].inside
            &&edgeRight.xCoordinate[i].inside<=edgeRight.xCoordinate[i-3].inside
            &&edgeRight.xCoordinate[i].inside<=edgeRight.xCoordinate[i+2].inside
            &&edgeRight.xCoordinate[i].inside<=edgeRight.xCoordinate[i-2].inside
            &&edgeRight.xCoordinate[i].inside<=edgeRight.xCoordinate[i+1].inside
            &&edgeRight.xCoordinate[i].inside<=edgeRight.xCoordinate[i-1].inside)
            {//就很暴力，这个数据是在前5，后5中最大的（可以取等），那就是单调突变点
                monotonicity_change_line=i;
                break;
            }
        }
        return monotonicity_change_line;
}
float Err; // 加权后的Err，Err>0 表示车头往右偏，Err<0 表示车头往左偏
float Err_last = 0; // 加权后的Err，Err>0 表示车头往右偏，Err<0 表示车头往左偏
/*begin  大津法比赛   begin*/
//快速大津法二值化 pixelSum = width * height/4;
//-------------------------------------------------------------------------------------------------------------------
//  @brief      快速大津
//  @return     uint8
//  @since      v1.1
//  Sample usage:   OTSU_Threshold = otsuThreshold(mt9v03x_image_dvp[0]);//大津法阈值
//-------------------------------------------------------------------------------------------------------------------
uint8 otsuThreshold_fast(uint8 *image)   //注意计算阈值的一定要是原图像
{
#define GrayScale 256
    int Pixel_Max=0;
    int Pixel_Min=255;
    uint16 width = MT9V03X_W;   //宽100
    uint16 height = MT9V03X_H;  //高80
    int pixelCount[GrayScale];  //各像素GrayScale的个数pixelCount 一维数组
    float pixelPro[GrayScale];  //各像素GrayScale所占百分比pixelPro 一维数组
    int i, j, pixelSum = width * height/4;  //pixelSum是获取总的图像像素个数的1/4，相应下面轮询时高和宽都是以2为单位自增
    uint8 threshold = 0;
//    uint8 last_threshold = 0;
    uint8* data = image;  //指向像素数据的指针

    //清零
    for (i = 0; i < GrayScale; i++)
    {
        pixelCount[i] = 0;
        pixelPro[i] = 0;
    }

    uint32 gray_sum=0;  //每次执行到这会将gray_sum清零
    //统计灰度级中每个像素在整幅图像中的个数
    for (i = 0; i < height; i+=2)   //高
    {
        for (j = 0; j < width; j+=2)    //宽
        {
            pixelCount[(int)data[i * width + j]]++;  //将当前的点的像素值作为计数数组的下标
            gray_sum+=(int)data[i * width + j];       //灰度值总和
            if(data[i * width + j]>Pixel_Max)   Pixel_Max=data[i * width + j];
            if(data[i * width + j]<Pixel_Min)   Pixel_Min=data[i * width + j];
        }
    }

    //计算每个像素值的点在整幅图像中的比例
    for (i = Pixel_Min; i < Pixel_Max; i++)
    {
        pixelPro[i] = (float)pixelCount[i] / pixelSum;
    }

    //遍历灰度级[0,255]
    float w0, w1, u0tmp, u1tmp, u0, u1, u, deltaTmp, deltaMax = 0;

    w0 = w1 = u0tmp = u1tmp = u0 = u1 = u = deltaTmp = 0;
    for (j = Pixel_Min; j < Pixel_Max; j++)
    {

        w0 += pixelPro[j];  //背景部分每个灰度值的像素点所占比例之和   即背景部分的比例
        u0tmp += j * pixelPro[j];  //背景部分 每个灰度值的点的比例 *灰度值

        w1=1-w0;
        u1tmp=gray_sum/pixelSum-u0tmp;

        u0 = u0tmp / w0;              //背景平均灰度
        u1 = u1tmp / w1;              //前景平均灰度
        u = u0tmp + u1tmp;            //全局平均灰度
        deltaTmp = (float)(w0 *w1* (u0 - u1)* (u0 - u1)) ;
        if (deltaTmp > deltaMax)
        {
            deltaMax = deltaTmp;
            threshold = (uint8)j;
        }
        if (deltaTmp < deltaMax)
        {
            break;
        }

    }

    return threshold;
}

//***********************************************************************************************
//作用：大津法
//参数说明：
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
    for (j = 0; j < MT9V03X_DVP_H; j += 4) {
        for (i = 0; i < MT9V03X_DVP_W; i += 4) {
            HistoGram[*(tmImage + j * MT9V03X_DVP_W + i)]++; //统计灰度级中每个像素在整幅图像中的个数
        }
    }
    for (MinValue = 0; MinValue < 256 && HistoGram[MinValue] == 0; MinValue++)
        ;        //获取最小灰度的值
    for (MaxValue = 255; MaxValue > MinValue && HistoGram[MinValue] == 0;
            MaxValue--)
        ; //获取最大灰度的值
    Amount = MT9V03X_DVP_W * MT9V03X_DVP_H / 16;
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
//作用：滤波
//参数说明：
//使用示例：
//***********************************************************************************************
void Bin_Image_Filter(uint8_t *Bin_Image,char thershold) // 未使用！！！
{
    for (int nr = 0; nr < MT9V03X_H; nr++) {
        for (int nc = 0; nc < MT9V03X_W; nc = nc + 1) {
            if ((Bin_Image[nr * MT9V03X_W + nc] <= thershold)
                    && (Bin_Image[(nr - 1) * MT9V03X_W + nc]
                            + Bin_Image[(nr + 1) * MT9V03X_W + nc]
                            + Bin_Image[nr * MT9V03X_W + nc + 1]
                            + Bin_Image[nr * MT9V03X_W + nc - 1] > 2*thershold)) {
                Bin_Image[nr * MT9V03X_W + nc] = 255;
            } else if ((Bin_Image[nr * MT9V03X_W + nc] > thershold)
                    && (Bin_Image[(nr - 1) * MT9V03X_W + nc]
                            + Bin_Image[(nr + 1) * MT9V03X_W + nc]
                            + Bin_Image[nr * MT9V03X_W + nc + 1]
                            + Bin_Image[nr * MT9V03X_W + nc - 1] < 2 * thershold)) {
                Bin_Image[nr * MT9V03X_W + nc] = 0;
            }
        }
    }
}
//*********************************************************************************************************************
//作用：计算拐点处相邻索引点的八邻域生长方向是否符合
//参数说明：
//使用示例：
//*********************************************************************************************************************
uint8 Grow_Diretion_Judge_Corner(TRAIL8ALL *trail,EDGE_FROM_TRAIL8ALL *edge,int corner_line,uint8 corner_type,int8 step)
{
    int x_sum1=0,y_sum2=0;
    int m=0;
    float rate_x=0,rate_y=0;
    if(corner_type==L_DOWN)
    {
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail - step;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                y_sum2+=trail->coordinate[m].Growth_direction_y;
            }
        }

        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail + step; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                x_sum1+=trail->coordinate[m].Growth_direction_x;
            }
        }

    }
    else if(corner_type==R_DOWN)
    {
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail - step;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                y_sum2+=trail->coordinate[m].Growth_direction_y;
            }
        }

        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail + step; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                x_sum1+=trail->coordinate[m].Growth_direction_x;
            }
        }

    }
    else if(corner_type==L_UP)
    {
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail - step;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                x_sum1+=trail->coordinate[m].Growth_direction_x;
            }
        }

        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail + step; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                y_sum2+=trail->coordinate[m].Growth_direction_y;
            }
        }

    }
    else if(corner_type==R_UP)
    {
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail - step;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                x_sum1+=trail->coordinate[m].Growth_direction_x;
            }
        }

        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail + step; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                y_sum2+=trail->coordinate[m].Growth_direction_y;
            }
        }

    }

    rate_x = (float)x_sum1/step;
    rate_y = (float)y_sum2/step;
    rate_xcopy = rate_x;
    rate_ycopy = rate_y;
//    tft180_show_float(60, 20, rate_x, 1, 2);
//    tft180_show_float(60, 30, rate_y, 1, 2);
    if(rate_x<-Grow_Judge_Corner&&rate_y>Grow_Judge_Corner)//0.7
    {
        return L_DOWN_OR_R_UP_OK;
    }
    else if(rate_x>Grow_Judge_Corner&&rate_y>Grow_Judge_Corner)
    {
        return L_UP_OR_R_DOWN_OK;
    }
    else
        return 0;
}

uint8 Grow_Diretion_Judge_K(TRAIL8ALL *trail,EDGE_FROM_TRAIL8ALL *edge,int corner_line,uint8 corner_type,int8 step)
{
    int x_sum1=0,x_sum2=0,y_sum1=0,y_sum2=0;
    int m=0;
    if(corner_type==L_DOWN)
    {
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail - step;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                y_sum2-=trail->coordinate[m].Growth_direction_y;
            }
        }
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail - step;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                x_sum2-=trail->coordinate[m].Growth_direction_x;
            }
        }
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail + step; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                x_sum1+=trail->coordinate[m].Growth_direction_x;
            }
        }
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail + step; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                y_sum1+=trail->coordinate[m].Growth_direction_y;
            }
        }
    }
    else if(corner_type==R_DOWN)
    {
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail - step;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                y_sum2-=trail->coordinate[m].Growth_direction_y;
            }
        }
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail - step;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                x_sum2-=trail->coordinate[m].Growth_direction_x;
            }
        }
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail + step; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                x_sum1+=trail->coordinate[m].Growth_direction_x;
            }
        }
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail + step; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                y_sum1+=trail->coordinate[m].Growth_direction_y;
            }
        }
    }
    else if(corner_type==L_UP)
    {
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail - step;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                x_sum1-=trail->coordinate[m].Growth_direction_x;
            }
        }
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail - step;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                y_sum1-=trail->coordinate[m].Growth_direction_y;
            }
        }
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail + step; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                y_sum2+=trail->coordinate[m].Growth_direction_y;
            }
        }
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail + step; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                x_sum2+=trail->coordinate[m].Growth_direction_x;
            }
        }
    }
    else if(corner_type==R_UP)
    {
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail - step;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                x_sum1-=trail->coordinate[m].Growth_direction_x;
            }
        }
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail - step;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                y_sum1-=trail->coordinate[m].Growth_direction_y;
            }
        }
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail + step; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                y_sum2+=trail->coordinate[m].Growth_direction_y;
            }
        }
        for( m = edge->xCoordinate[corner_line].insideIndexOfTrail;
                m < edge->xCoordinate[corner_line].insideIndexOfTrail + step; m++)
        {
            if(m > 0 && m < (trail->indexOfCurrent-2))
            {
                x_sum2+=trail->coordinate[m].Growth_direction_x;
            }
        }
    }
    float dot = 0,length=0;
    float angle = 0;
    dot = x_sum1*x_sum2 + y_sum1 * y_sum2;
    length = sqrtf(x_sum1*x_sum1+y_sum1*y_sum1)*sqrtf(x_sum2*x_sum2+y_sum2*y_sum2);
    angle = acos(dot/length);
//    tft180_show_float(60, 40, angle, 1, 2);
    if(angle<2.8)//1.9
        return 1;
    else
        return 0;
}

/************************************************************************************************
 * 连接两点的斜率拟合
 * 参数：边界，两个 断点， 存放a、b的数组指针, edge为横坐标，point为纵坐标
 * 无返回值
 *ht 2022.
 ***************************************************************************************************/
void connect_two_point(int egde_up, int egde_down, int point_up, int point_down,float *ab)
{
    int sumx = 0, sumy = 0, sumxy = 0, sumxx = 0, div_num;
    sumx = egde_up + egde_down;
    sumy = point_up + point_down;
    sumxx = egde_up * egde_up + egde_down * egde_down;
    sumxy = egde_up * point_up + egde_down * point_down;

    div_num = 2 * sumxx - sumx * sumx;
    if (div_num != 0)
    {
        *ab = ((float) (2 * sumxy - sumx * sumy)) / div_num;
    }
    else
    {
        *ab = 10000;
    }
    *(ab + 1) = ((float) (sumy - (*(ab)) * sumx)) / 2;
}

/*
 * 比较一条边界是否是直线
 * 方法：一段边界的上下点连线，比较二者的方差
 * 返回值： 0   方差太大，不是直线 或者 线段太短，
 *        1    方差小，认为是直线
 *         if (find_edge_straight(edgeRight, L_up_corner_line, L_down_corner_line)) {

 }
 */
#define STRAIGHT_ERROR_LIMIT 5 //方差比较的阈值

char find_edge_straight2(EDGE_FROM_TRAIL8ALL *edge, int up_line, int down_line)
{
    float ab[2];
    int i, compare_point, compare_num = 20; //compare_num是比较的个数，越多越准  //30
    float sum = 0;

    if (down_line - up_line < 10)
    {
        return 0;
    }
    connect_two_point(edge->xCoordinate[up_line].inside,
            edge->xCoordinate[down_line].inside, up_line, down_line, ab); //获得斜率

    for (i = up_line; i < down_line; i += 1)
    { //求方差
        compare_point = ((float) i - ab[1]) / ab[0];
        sum += pow((compare_point - edge->xCoordinate[i].inside), 2);
    }
    if (sum / compare_num < 3)
    { //比较阈值
        return 1; //直线
    }
    else
    {
        return 0;
    }
}

char find_edge_straight(EDGE_FROM_TRAIL8ALL *edge, int up_line, int down_line)
{
    float ab[2];
    int i, compare_point, compare_num = 30; //compare_num是比较的个数，越多越准  //30
    float sum = 0;

    if (down_line - up_line < 10)
    {
        return 0;
    }
    connect_two_point(edge->xCoordinate[up_line].inside,
            edge->xCoordinate[down_line].inside, up_line, down_line, ab); //获得斜率

    for (i = up_line; i < down_line; i +=
            ((down_line - up_line) / compare_num))
    { //求方差
        compare_point = ((float) i - ab[1]) / ab[0];
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
TRAIL8ALL trail8AllLeft, trail8AllRight;

//*********************************************************************************************************************
//作用：向左/右八邻域初始化
//参数说明：trail为需要初始化的八邻域轨迹
//使用示例：Trail8AllInit(&trail8AllLeft);
//*********************************************************************************************************************
void Trail8AllInit(TRAIL8ALL *trail)
{
    trail->indexOfCurrent = 0;      //当前行索引置零
    trail->indexOfHigestLeft = 0;       //最高行索引置零,该变量并非为最远行的值
    trail->indexOfHigestRight = 0;       //最高行索引置零,该变量并非为最远行的值
}

//*********************************************************************************************************************
//作用：向左/右八邻域寻找轨迹加入新点
//参数说明：row为加入新点的行号，column为新点列号，side为边界类型。返回最新空点的索引号
//使用示例：trail8AllBoundaryStore(1,2,sideLeft);
//*********************************************************************************************************************
int16_t Trail8AllBoundaryStore(int16_t row, int16_t column,enum Trail8AllSide side)
{
    if (side == SideLeft)
    {
        if (trail8AllLeft.indexOfCurrent == TRAIL8ALL_ARRAYLENGTH - 3) //超出数组范围！！！考虑是否要加大数组长度
            return -1;

        trail8AllLeft.coordinate[trail8AllLeft.indexOfCurrent].row = row; //储存新来点坐标
        trail8AllLeft.coordinate[trail8AllLeft.indexOfCurrent].column = column;

        if (row
                < trail8AllLeft.coordinate[trail8AllLeft.indexOfHigestLeft].row)
        { //当前行比最远行更远，用当前索引替换最远索引
            trail8AllLeft.indexOfHigestLeft = trail8AllLeft.indexOfCurrent;
            trail8AllLeft.indexOfHigestRight = trail8AllLeft.indexOfCurrent;
        }
        else if (row
                == trail8AllLeft.coordinate[trail8AllLeft.indexOfHigestLeft].row)
        { //当前行和最远行相同，修改最高行左右边的索引
            if (column
                    < trail8AllLeft.coordinate[trail8AllLeft.indexOfHigestLeft].column)
            { //比左边更左
                trail8AllLeft.indexOfHigestLeft = trail8AllLeft.indexOfCurrent;
            }
            else if (column
                    > trail8AllLeft.coordinate[trail8AllLeft.indexOfHigestRight].column)
            { //比右边更右
                trail8AllLeft.indexOfHigestRight = trail8AllLeft.indexOfCurrent;
            }
        }

        trail8AllLeft.coordinate[trail8AllLeft.indexOfCurrent].Growth_direction_x = grow_dire_temp_x;//记录生长方向
        trail8AllLeft.coordinate[trail8AllLeft.indexOfCurrent].Growth_direction_y = grow_dire_temp_y;

        trail8AllLeft.indexOfCurrent++;

        return trail8AllLeft.indexOfCurrent;
    }
    else
    {
        if (trail8AllRight.indexOfCurrent == TRAIL8ALL_ARRAYLENGTH - 3) //超出数组范围！！！考虑是否要加大数组长度
            return -1;

        trail8AllRight.coordinate[trail8AllRight.indexOfCurrent].row = row; //储存行列号
        trail8AllRight.coordinate[trail8AllRight.indexOfCurrent].column = column;

        if (row
                < trail8AllRight.coordinate[trail8AllRight.indexOfHigestLeft].row)
        { //当前行比最远行更远，用当前索引替换最远索引
            trail8AllRight.indexOfHigestLeft = trail8AllRight.indexOfCurrent;
            trail8AllRight.indexOfHigestRight = trail8AllRight.indexOfCurrent;
        }
        else if (row
                == trail8AllRight.coordinate[trail8AllRight.indexOfHigestLeft].row)
        { //当前行和最远行相同，修改最高行左右边的索引
            if (column
                    < trail8AllRight.coordinate[trail8AllRight.indexOfHigestLeft].column)
            { //比左边更左
                trail8AllRight.indexOfHigestLeft = trail8AllRight.indexOfCurrent;
            }
            else if (column
                    > trail8AllLeft.coordinate[trail8AllRight.indexOfHigestRight].column)
            { //比右边更右
                trail8AllRight.indexOfHigestRight = trail8AllRight.indexOfCurrent;
            }
        }

        trail8AllRight.coordinate[trail8AllRight.indexOfCurrent].Growth_direction_x = grow_dire_temp_x;//记录生长方向
        trail8AllRight.coordinate[trail8AllRight.indexOfCurrent].Growth_direction_y = grow_dire_temp_y;

        trail8AllRight.indexOfCurrent++;

        return trail8AllRight.indexOfCurrent;
    }
}

enum statusOfTrail8Overlap flagOverlap;
//*********************************************************************************************************************
//作用：判断左右8邻域轨迹是否重叠
//参数说明：edgeScanning为正在扫描的8邻域轨迹，edgeFinish为已经完成扫描的8邻域轨迹， flag为重叠标志位
//使用示例：checkTrail8Overlap(&trail8AllLeft,&trail8AllRight,&flagOverlap);
//*********************************************************************************************************************
enum statusOfTrail8Overlap CheckTrail8Overlap(TRAIL8ALL *trailScanning,TRAIL8ALL *trailFinish, enum statusOfTrail8Overlap *flag)
{
    uint16_t thresholdDistance = 20;     //距离阈值   20
    int16_t rowScanning, columnScanning;  //左边8领域轨迹最远点
    int16_t rowFinish, columnFinishLeft, columnFinishRight;  //右边8领域轨迹最远点
    int distanceLeft, distanceRight;    //与左边最高点距离，与右边最高点距离
    static int distanceMin = -1;        //两点距离最小值(-1表示未赋值过)

    if (trailScanning->indexOfCurrent == 2)//找完种子后存入的第一个点
    {//首次进入距离初始化一次
        distanceMin = -1;
    }
    rowScanning = trailScanning->coordinate[trailScanning->indexOfCurrent - 1].row; //提取正在扫描过的这个点的信息
    columnScanning = trailScanning->coordinate[trailScanning->indexOfCurrent - 1].column;

    rowFinish = trailFinish->coordinate[trailFinish->indexOfHigestLeft].row; //左边是新的    //提取完成轨迹点的信息
    columnFinishLeft = trailFinish->coordinate[trailFinish->indexOfHigestLeft].column; //正常来说最远左右点在扫右边界时已经存过了
    columnFinishRight = trailFinish->coordinate[trailFinish->indexOfHigestRight].column;

    distanceLeft = fabs(rowScanning - rowFinish) + fabs(columnScanning - columnFinishLeft);
    distanceRight = fabs(rowScanning - rowFinish) + fabs(columnScanning - columnFinishRight);

    //重叠判断，若满足距离条件，继续扫描直道找到距离最小的点才认为重叠（停止扫描）
    if (distanceLeft < thresholdDistance)
    {        //与右边界最远行左边的点进行距离判断
        if (distanceMin > distanceLeft || distanceMin == -1)//间距逐渐变小逐渐接近
        {       //不是距离最小值点点
            distanceMin = distanceLeft;
            *flag = Overlap;//即将重叠
        }
        else      // 与右边的最远行点之间的距离先变小后变大
        {      //是距离最小值的点
            *flag = OverlapScanStop;//确定重叠了
        }
    }
    else if (distanceRight < thresholdDistance)
    { //与右边点距离判断
        if (distanceMin > distanceRight || distanceMin == -1)
        { //不是距离最小值点点
            distanceMin = distanceRight;
            *flag = Overlap;
        }
        else      // 与右边的最远行点之间的距离先变小后变大
        {      //是距离最小值的点
            *flag = OverlapScanStop;
        }
    }
    else
    {
        if (distanceMin == -1)
            *flag = NoOverlap;
        else
        {//有且只有一个点满足距离判断条件，无法进入上面的条件，认为重叠//几乎不用考虑
            *flag = OverlapScanStop;
        }
    }
    return *flag;
}

EDGE_FROM_TRAIL8ALL edgeLeft, edgeRight;     //左右边界信息
//****************************************************************************************************
//作用：根据八邻域法得出的轨迹解算出边界,以内边界为主要循迹对象，外边界为辅助
//参数说明：edge为左/右边界的指针，trail为八邻域的左/右轨迹，side边界类型,pattern为边界解算方式，0表示按正常方式解算，1表示每行只能解算一次。
//使用示例：calculateEdgeFromTrail8All(&edgeRight,&trail8AllRight,SideRight,flagOverlap,0);
//*****************************************************************************************************
void CalculateEdgeFromTrail8All(EDGE_FROM_TRAIL8ALL *edge, TRAIL8ALL *trail,
        enum Trail8AllSide side, enum statusOfTrail8Overlap flag, int pattern)
{
    //*******************初始化***************************
    edge->peakRow = MT9V03X_DVP_H - 1;    //峰值行号设为最近处
    edge->numOutside = 0;               //外边界数置零
    for (uint16_t i = 0; i < MT9V03X_DVP_H; i++)    //行状态初始化
        edge->xCoordinate[i].type = Null;

    //*******************边界解算***************************
    uint16_t thresholdDistance = 5;     //距离阈值
    uint16_t thresholdRowOutside = 10;  //外边界数量计数阈值

    if (side == SideLeft)
    {
        //*****************以下是左边界解算**********************
        uint16_t row, column;    //当前从trail遍历到的行和列
        for (uint16_t i = 0; i < trail->indexOfCurrent; i++)
        {
            row = trail->coordinate[i].row;
            column = trail->coordinate[i].column;

            if (row < edge->peakRow)       //最远行更新
                edge->peakRow = row;

            if (edge->xCoordinate[row].type == Null)
            { //如果该行是Null状态,直接给内边界，type升级
                edge->xCoordinate[row].inside = column;
                edge->xCoordinate[row].insideIndexOfTrail = i;  //记录父级
                edge->xCoordinate[row].type = Single;           //状态升级
            }
            else if (edge->xCoordinate[row].type == Single && pattern == 0)
            { //如果该行是Single状态,column与内边界比较，靠内的给内边界，靠外的给外边界，type升级
                if (fabs(column - edge->xCoordinate[row].inside) > thresholdDistance)
                {       //当两点距离大与一个数时才保留，用于滤除八邻域噪声
                    if (column <= edge->xCoordinate[row].inside)
                    {  //找到了更靠左一点的左边界
                        edge->xCoordinate[row].outside = column;
                        edge->xCoordinate[row].outsideIndexOfTrail = i; //记录父级
                    }
                    else
                    {  //找到了更靠右一点的左边界
                        edge->xCoordinate[row].outside = edge->xCoordinate[row].inside;
                        edge->xCoordinate[row].outsideIndexOfTrail = edge->xCoordinate[row].insideIndexOfTrail;
                        edge->xCoordinate[row].inside = column;
                        edge->xCoordinate[row].insideIndexOfTrail = i;//记录它在父级八邻域轨迹中为第几个点
                    }
                    if (row > thresholdRowOutside)
                        edge->numOutside++;                 //外边界数加一
                    edge->xCoordinate[row].type = Both;     //type升级
                }
            }
            else if (pattern == 0)
            { //如果该行是Both状态,column与两边界相比，比内边界更加靠内就给内边界，比外边界更靠外就给外边界
                if (column < edge->xCoordinate[row].outside)
                {  //比外边界更外
                    edge->xCoordinate[row].outside = column;
                    edge->xCoordinate[row].outsideIndexOfTrail = i;
                }
                else if (column > edge->xCoordinate[row].inside)
                { //比内边界更内
                    edge->xCoordinate[row].inside = column;
                    edge->xCoordinate[row].insideIndexOfTrail = i;//记录它在父级八邻域轨迹中为第几个点
                }
                if (row > thresholdRowOutside)
                    edge->numOutside++;     //外边界数加一
            }
        }
    }
    else
    {
        //*****************以下是右边界解算**********************
        uint16_t row, column;    //当前从trail遍历到的行和列
        for (uint16_t i = 0; i < trail->indexOfCurrent; i++)
        {
            if (flag != NoOverlap && i == trail->indexOfHigestLeft) //如果8邻域轨迹发送重叠，那么当遍历到最高点时停止
                break;
            row = trail->coordinate[i].row;
            column = trail->coordinate[i].column;

            if (row < edge->peakRow)       //最远行更新
                edge->peakRow = row;

            if (edge->xCoordinate[row].type == Null)
            { //如果该行是Null状态,直接给内边界，type升级
                edge->xCoordinate[row].inside = column;
                edge->xCoordinate[row].insideIndexOfTrail = i;  //记录父级
                edge->xCoordinate[row].type = Single;
            }
            else if (edge->xCoordinate[row].type == Single && pattern == 0)
            { //如果该行是Single状态,column与内边界比较，靠内的给内边界，靠外的给外边界，type升级
                if (fabs(column - edge->xCoordinate[row].inside) > thresholdDistance)
                {       //当两点距离大与一个数时才保留，用于滤除八邻域噪声
                    if (column >= edge->xCoordinate[row].inside)
                    {  //比内边界更外
                        edge->xCoordinate[row].outside = column;
                        edge->xCoordinate[row].outsideIndexOfTrail = i;
                    }
                    else
                    {                                         //比内边界更内
                        edge->xCoordinate[row].outside = edge->xCoordinate[row].inside;
                        edge->xCoordinate[row].outsideIndexOfTrail = edge->xCoordinate[row].insideIndexOfTrail;

                        edge->xCoordinate[row].inside = column;
                        edge->xCoordinate[row].insideIndexOfTrail = i;
                    }
                    if (row > thresholdRowOutside)
                        edge->numOutside++;     //外边界数加一
                    edge->xCoordinate[row].type = Both;
                }
            }
            else if (pattern == 0)
            { //如果该行是Both状态,column与两边界相比，比内边界更加靠内就给内边界，比外边界更靠外就给外边界
                if (column > edge->xCoordinate[row].outside)
                {  //比外边界更外
                    edge->xCoordinate[row].outside = column;
                    edge->xCoordinate[row].outsideIndexOfTrail = i;
                }
                else if (column < edge->xCoordinate[row].inside)
                { //比内边界更内
                    edge->xCoordinate[row].inside = column;
                    edge->xCoordinate[row].insideIndexOfTrail = i;
                }
                if (row > thresholdRowOutside)
                    edge->numOutside++;     //外边界数加一
            }
        }
    }
}

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
        for (int i = colunmStart; i < MT9V03X_DVP_W - 2; ++i)
        {    //循环找右边界点
            if ((img[row * MT9V03X_DVP_W + i] >= thresholdExposure
                    && img[row * MT9V03X_DVP_W + i + 1] < thresholdExposure
                    && img[row * MT9V03X_DVP_W + i + 2] < thresholdExposure)
                    || i == MT9V03X_DVP_W - 3)
            {   //当前白，右两点黑 或 找到边界
                return i;
            }
        }
    }
    else
    {
        for (int i = colunmStart; i > 1; --i)
        {    //循环找左边界点
            if ((img[row * MT9V03X_DVP_W + i] >= thresholdExposure
                    && img[row * MT9V03X_DVP_W + i - 1] < thresholdExposure
                    && img[row * MT9V03X_DVP_W + i - 2] < thresholdExposure)
                    || i == 2)
            {   //当前白，左两点黑 或 找到边际
                return i;
            }
        }
    }
    return -1;  //参数错误才会返回-1
}

/*
 * 八邻域
 * by ht
 *图像在LCD显示左上是0，0；意味着摄像头拍的最底下是MT9V03X_DVP_H，最左是0
 *107
 *286
 *345
 */
int thershold_local;
unsigned char DIFF_limit = 15;  //10
float Slope_limit = 1;                //找断点斜率阈值
float Slope_limit_single = 0.5;     //当一条线斜率无穷大时的斜率阈值
unsigned char DIFF_STEP = 10; //找断点的步长，下断点的限制和上断点的限制 5

uint8 L_edge_8_START_LINE, R_edge_8_START_LINE;
int8 grow_dire_temp_x,grow_dire_temp_y;

unsigned char L_down_corner_line, L_up_corner_line, R_down_corner_line,R_up_corner_line; //记录上下角，十字用，圆环也可以
//unsigned char R_down_corner_fan, L_down_corner_fan;
long int mid_servo = MT9V03X_DVP_W / 2;
int weight_count;
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
int highset_line_for_control_speed;
int high_max = 0;

uint16_t lose_line_num;    //左右边丢边总数
uint8 lose_left_circlepermit;
uint8 lose_right_circlepermit;

uint8 find_flag;//种子发现
float wandao_row;

int16_t thresholdDiff = 10;         //上断点一阶差分的阈值，入环补线用
int sum_grow_dir = 0;
int BLOCK_distance = 2000;
float circle_k_err = 0;
int blok = 40;
int circle_find = 0;
int circle_permit = 0;

void eight_neighbor(unsigned char *img, int thershold_local)
{
    find_flag = 0;    //种子发现标志位
    int i, j, k, current_num, current_row, current_col, edge_num = 0;
    int point1, point2;


    int diff_L_1, diff_L_2[MT9V03X_DVP_H] = { 0 }, diff_R_1,diff_R_2[MT9V03X_DVP_H] = { 0 }, diff_L_11, diff_R_11;
    float L_down_ab[2] = { 0 }, R_down_ab[2] = { 0 };
    unsigned char L_down_corner_flag = 0, R_down_corner_flag = 0;   //查看是否找到过下断点
    //先找第一行的左右种子

    L_edge_8_START_LINE = 159;    //初始化第一行，用来找边界起点
    R_edge_8_START_LINE = 0;

    L_down_corner_line = 0;
    L_up_corner_line = 0;
    R_down_corner_line = 0;
    R_up_corner_line = 0;

    Trail8AllInit(&trail8AllLeft);      //八邻域轨迹初始化
    Trail8AllInit(&trail8AllRight);     //八邻域轨迹初始化
    //非第一圈车库正常扫描
    if (((int) (*(img + (START_LINE) * MT9V03X_DVP_W + MT9V03X_DVP_W / 2)))
            < thershold_local
            && ((((int) (*(img + (START_LINE) * MT9V03X_DVP_W - 1
                    + MT9V03X_DVP_W / 2))) < thershold_local
                    && ((int) (*(img + (START_LINE) * MT9V03X_DVP_W - 2
                            + MT9V03X_DVP_W / 2))) < thershold_local)
                    || (((int) (*(img + (START_LINE) * MT9V03X_DVP_W + 1
                            + MT9V03X_DVP_W / 2))) < thershold_local
                            && ((int) (*(img + (START_LINE) * MT9V03X_DVP_W + 2
                                    + MT9V03X_DVP_W / 2))) < thershold_local)))
    { //最后一行中间附近几个点为黑
        /****如果上来边界就找不到（因为黑色）就换一种找的办法****/
        for (i = 1; i < MT9V03X_DVP_W / 2 - 2; ++i) {  //范围：最后一行左半边  从中间向左循环
            if (((int) (*(img + (START_LINE) * MT9V03X_DVP_W - i
                    + MT9V03X_DVP_W / 2))) >= thershold_local
                    && ((int) (*(img + (START_LINE) * MT9V03X_DVP_W - i - 1
                            + MT9V03X_DVP_W / 2))) >= thershold_local) { //在最后一行从中间向左找到连续两个白点
                R_edge_8_START_LINE = MT9V03X_DVP_W / 2 - i;
                Trail8AllBoundaryStore(START_LINE, MT9V03X_DVP_W / 2 - i,
                        SideRight);    //起始右边界

                if (R_edge_8_START_LINE < 4)
                {    //找到边上了整个八邻域结束
                    return;
                }
                for (j = i + 1; j < MT9V03X_DVP_W / 2 - 1; ++j)
                { //找到了右边界再找左边界
                    if (((int) (*(img + (START_LINE) * MT9V03X_DVP_W - j
                            + MT9V03X_DVP_W / 2))) < thershold_local
                            && ((int) (*(img + (START_LINE) * MT9V03X_DVP_W - j
                                    - 1 + MT9V03X_DVP_W / 2)))
                                    < thershold_local) { //最后一行在右边界起始点处  继续向左  直到找到两个黑点则为左边界起始点
                        L_edge_8_START_LINE = -1 * j + 1 + MT9V03X_DVP_W / 2;
                        Trail8AllBoundaryStore(START_LINE,
                                -1 * j + 1 + MT9V03X_DVP_W / 2, SideLeft); //起始左边界
                        find_flag = 1;
                        break;
                    }
                    if (j == MT9V03X_DVP_W / 2 - 2) { //如果左边界找到左边际上了   则2为左边界
                        L_edge_8_START_LINE = -1 * j + MT9V03X_DVP_W / 2;
                        Trail8AllBoundaryStore(START_LINE,
                                -1 * j + MT9V03X_DVP_W / 2, SideLeft); //起始左边界
                        find_flag = 1;
                    }
                }
                break;
            }    //上述if语句应对左急转弯
            else if (((int) (*(img + (START_LINE) * MT9V03X_DVP_W + i
                    + MT9V03X_DVP_W / 2))) >= thershold_local
                    && ((int) (*(img + (START_LINE) * MT9V03X_DVP_W + i + 1
                            + MT9V03X_DVP_W / 2))) >= thershold_local) { //应对右急转弯找种子   在最后一行从中间向右循环
                L_edge_8_START_LINE = i + MT9V03X_DVP_W / 2;
                //发现连续两个白点 鉴定为左边界
                Trail8AllBoundaryStore(START_LINE, i + MT9V03X_DVP_W / 2,
                        SideLeft);    //起始左边界
                if (L_edge_8_START_LINE > MT9V03X_DVP_W - 5) {
                    return;
                }
                for (j = i + 1; j < MT9V03X_DVP_W / 2 - 1; ++j) { //左边界基础上继续向右寻找右边界
                    if (((int) (*(img + (START_LINE) * MT9V03X_DVP_W + j
                            + MT9V03X_DVP_W / 2))) < thershold_local
                            && ((int) (*(img + (START_LINE) * MT9V03X_DVP_W + j
                                    + 1 + MT9V03X_DVP_W / 2)))
                                    < thershold_local) {
                        R_edge_8_START_LINE = j - 1 + MT9V03X_DVP_W / 2;
                        Trail8AllBoundaryStore(START_LINE,
                                j - 1 + MT9V03X_DVP_W / 2, SideRight); //起始右边界
                        find_flag = 1;
                        break;
                    }
                    if (j == MT9V03X_DVP_W / 2 - 2) {    //找到右边际上了 直接158为右边界
                        R_edge_8_START_LINE = j + MT9V03X_DVP_W / 2;
                        Trail8AllBoundaryStore(START_LINE,
                                j + MT9V03X_DVP_W / 2, SideRight);   //起始右边界
                        find_flag = 1;
                    }
                }
                break;
            }
        }

    }    //上述为如果最后一行上来全黑
    else
    {
        find_flag = 1;
        for (i = 0; i < MT9V03X_DVP_W / 2 - 1; i++)    //右边的第2行的种子
                {    //范围：最后一行 右半边循环
            point1 = (int) (*(img + (START_LINE) * MT9V03X_DVP_W + i
                    + MT9V03X_DVP_W / 2));
            point2 = (int) (*(img + (START_LINE) * MT9V03X_DVP_W + i + 1
                    + MT9V03X_DVP_W / 2));

            if (point2 < thershold_local && point1 < thershold_local) { //最后一行中间靠右某个点的右边两个点连续黑则存为黑边界
                R_edge_8_START_LINE = i - 1 + MT9V03X_DVP_W / 2;
                Trail8AllBoundaryStore(START_LINE, i - 1 + MT9V03X_DVP_W / 2,
                        SideRight); //起始右边界
                break;
            }
            if (i == MT9V03X_DVP_W / 2 - 2) { //找到了右边际上直接存为158
                R_edge_8_START_LINE = i + MT9V03X_DVP_W / 2;
                Trail8AllBoundaryStore(START_LINE, i + MT9V03X_DVP_W / 2,
                        SideRight); //起始右边界
                break;
            }
        }
        for (i = 0; i < MT9V03X_DVP_W / 2 - 1; i++) //左边的第2行的种子
                { //从中间向左找左种子
            point1 = (int) (*(img + (START_LINE) * MT9V03X_DVP_W - i
                    + MT9V03X_DVP_W / 2));
            point2 = (int) (*(img + (START_LINE) * MT9V03X_DVP_W - i - 1
                    + MT9V03X_DVP_W / 2));
            if (point2 < thershold_local && point1 < thershold_local) {
                L_edge_8_START_LINE = -i + 1 + MT9V03X_DVP_W / 2;
                Trail8AllBoundaryStore(START_LINE, -i + 1 + MT9V03X_DVP_W / 2,
                        SideLeft); //起始左边界
                break;
            }
            if (i == MT9V03X_DVP_W / 2 - 2) {
                L_edge_8_START_LINE = -i + MT9V03X_DVP_W / 2;
                Trail8AllBoundaryStore(START_LINE, -i + MT9V03X_DVP_W / 2,
                        SideLeft); //起始左边界
                break;
            }
        }
    }//上述为正常赛道从中间向外找种子

    // 处理弯入十字
    if((L_edge_8_START_LINE+R_edge_8_START_LINE)/2 > 80)
    {
        for(uint8_t i=0; i < 80; i++)
        {  //令第2行的靠左边的50个点全部变黑，为了解决十字拐错弯
            img[2*MT9V03X_DVP_W + i] = 0;
        }
    }
    else if((L_edge_8_START_LINE+R_edge_8_START_LINE)/2 < 80)
    {
        for(uint8_t i=0; i < 80; i++)
        {  //令第2行的靠右边的50个点全部变黑，为了解决十字拐错弯
            img[3*MT9V03X_DVP_W - i - 1] = 0;
        }
    }
    else if((L_edge_8_START_LINE+R_edge_8_START_LINE)/2 == 80 || (L_edge_8_START_LINE < 3 && R_edge_8_START_LINE > 157))
    {
        for(uint8_t i=0; i < 50; i++)
        {  //令第2行的靠左边的50个点全部变黑，为了解决十字拐错弯
            img[2*MT9V03X_DVP_W + i] = 0;
        }
        for(uint8_t i=0; i < 50; i++)
        {  //令第2行的靠右边的50个点全部变黑，为了解决十字拐错弯
            img[3*MT9V03X_DVP_W - i - 1] = 0;
        }
    }

    /*! @brief
     *
     * 开始八邻域找边界
     *
     * 开始八邻域找边界
     *
     * 开始八邻域找边界
     *
     * 开始八邻域找边界
     *
     * 开始八邻域找边界
     */
    //开始八邻域找边界

    current_row = START_LINE;
    current_col = R_edge_8_START_LINE;
    current_num = 3;
    high_max = current_row - 1;
    edge_num = 0;
    while(1) //先找右
    {
        grow_dire_temp_x = 0;
        grow_dire_temp_y = 0;
        for(k=0;k<8;k++)
        {
            current_num--; //切换位置顺时针
            if (current_num<0)
            {
                current_num=7;
            }
            if(current_num==0)
            {

                point1 = (int)(*(img+(current_row-1)*MT9V03X_DVP_W+(current_col)));
                point2 = (int)(*(img+(current_row-1)*MT9V03X_DVP_W+(current_col+1)));
                if(point1 >= thershold_local && point2 < thershold_local)
                {
                    i = current_col;
                    j = current_row-1;
                    grow_dire_temp_x = 0;
                    grow_dire_temp_y = 1;
                    break;
                }
                if(point1 >= thershold_local && point2 >= thershold_local && current_col+1 > MT9V03X_DVP_W-2 )
                {
                    i = MT9V03X_DVP_W-2;
                    j = current_row-1;
                    grow_dire_temp_x = 0;
                    grow_dire_temp_y = 1;
                    break;
                }
            }
            if(current_num==1)
            {
                point1 = (int)(*(img+(current_row-1)*MT9V03X_DVP_W+(current_col-1)));
                point2 = (int)(*(img+(current_row-1)*MT9V03X_DVP_W+(current_col)));
                if(point1 >= thershold_local && point2 < thershold_local)
                {
                    if (current_col - 1 < 2)
                    {
                        continue;
                    }
                    i = current_col-1;
                    j = current_row-1;
                    grow_dire_temp_x = -1;
                    grow_dire_temp_y = 1;
                    break;
                }
            }
            if(current_num==2)
            {
                point1 = (int)(*(img+(current_row)*MT9V03X_DVP_W+(current_col-1)));
                point2 = (int)(*(img+(current_row-1)*MT9V03X_DVP_W+(current_col-1)));
                if(point1 >= thershold_local && point2 < thershold_local)
                {
                    if (current_col - 1 < 2)
                    {
                        continue;
                    }
                    i = current_col-1;
                    j = current_row;
                    grow_dire_temp_x = -1;
                    grow_dire_temp_y = 0;
                    break;
                }
            }
            if(current_num==3)
            {
                point1 = (int)(*(img+(current_row+1)*MT9V03X_DVP_W+(current_col-1)));
                point2 = (int)(*(img+(current_row)*MT9V03X_DVP_W+(current_col-1)));
                if(point1 >= thershold_local && point2 < thershold_local)
                {
                    if (current_col - 1 < 2)
                    {
                        continue;
                    }
                    i = current_col-1;
                    j = current_row+1;
                    grow_dire_temp_x = -1;
                    grow_dire_temp_y = -1;
                    break;
                }
            }
            if(current_num==4)
            {

                point1 = (int)(*(img+(current_row+1)*MT9V03X_DVP_W+(current_col)));
                point2 = (int)(*(img+(current_row+1)*MT9V03X_DVP_W+(current_col-1)));
                if(point1 >= thershold_local && point2 < thershold_local)
                {
                    i = current_col;
                    j = current_row+1;
                    grow_dire_temp_x = 0;
                    grow_dire_temp_y = -1;
                    break;
                }
                if(point1 >= thershold_local && point2 >= thershold_local && current_col-1 < 2)
                {
                    i = 2;
                    j = current_row+1;
                    grow_dire_temp_x = 0;
                    grow_dire_temp_y = -1;
                    break;
                }
            }
            if(current_num==5)
            {
                point1 = (int)(*(img+(current_row+1)*MT9V03X_DVP_W+(current_col+1)));
                point2 = (int)(*(img+(current_row+1)*MT9V03X_DVP_W+(current_col)));
                if(point1 >= thershold_local && point2 < thershold_local)
                {
                    if (current_col + 1 > MT9V03X_DVP_W - 2)
                    {
                        continue;
                    }
                    i = current_col+1;
                    j = current_row+1;
                    grow_dire_temp_x = 1;
                    grow_dire_temp_y = -1;
                    break;
                }
            }
            if(current_num==6)
            {
                point1 = (int)(*(img+(current_row)*MT9V03X_DVP_W+(current_col+1)));
                point2 = (int)(*(img+(current_row+1)*MT9V03X_DVP_W+(current_col+1)));
                if(point1 >= thershold_local && point2 < thershold_local)
                {
                    if (current_col + 1 > MT9V03X_DVP_W - 2)
                    {
                        continue;
                    }
                    i = current_col+1;
                    j = current_row;
                    grow_dire_temp_x = 1;
                    grow_dire_temp_y = 0;
                    break;
                }
            }
            if(current_num==7)
            {
                point1 = (int)(*(img+(current_row-1)*MT9V03X_DVP_W+(current_col+1)));
                point2 = (int)(*(img+(current_row)*MT9V03X_DVP_W+(current_col+1)));
                if(point1 >= thershold_local && point2 < thershold_local)
                {
                    if (current_col + 1 > MT9V03X_DVP_W - 2)
                    {
                        continue;
                    }
                    i = current_col+1;
                    j = current_row-1;
                    grow_dire_temp_x = 1;
                    grow_dire_temp_y = 1;
                    break;
                }
            }
            if(k==7)    //环绕一圈都没找到便舍弃图像
                return;
        }
        Trail8AllBoundaryStore(j, i, SideRight);    //右边界储存
        if(high_max == j )
        {
            high_max = j-1;//如果在上边，就赋值
        }
        current_num += 3;
        if (current_num==8)
        {
            current_num =0;
        }
        else if (current_num == 9)
        {
            current_num = 1;
        }
        else if (current_num == 10)
        {
            current_num = 2;
        }
        current_col = i;
        current_row = j;    //更新
        edge_num++;
        if (current_col <3
                || edge_num > TRAIL8ALL_ARRAYLENGTH - 2
                || current_row <= zhidao_END
                || (current_row >= START_LINE && edge_num > 70))
        {    //快扫到边上了，停吧
            highest_line = high_max + 1;//舍弃扫到的最后一个
            break;
        }

    }
    /*! @brief
     *
     * 开始八邻域找边界
     *
     * 开始八邻域找边界
     *
     * 开始八邻域找边界
     *
     * 开始八邻域找边界
     *
     * 开始八邻域找边界
     */
    //开始八邻域找边界//左侧
    current_row = START_LINE;
    current_col = L_edge_8_START_LINE;
    current_num = 5;
    high_max = current_row - 1;
    edge_num = 0;
    while(1)
    {
        grow_dire_temp_x = 0;
        grow_dire_temp_y = 0;
        for(k=0;k<8;k++)
        {
            current_num++;    //切换位置逆时针
            if (current_num>7)
            {
                current_num=0;
            }
            if(current_num==0)
            {
                point1 = (int)(*(img+(current_row-1)*MT9V03X_DVP_W+(current_col)));
                point2 = (int)(*(img+(current_row-1)*MT9V03X_DVP_W+(current_col-1)));
                if(point1 >= thershold_local && point2 < thershold_local)
                {
                    i = current_col;
                    j = current_row-1;
                    grow_dire_temp_x = 0;
                    grow_dire_temp_y = 1;
                    break;
                }
                if(point1 >= thershold_local && point2 >= thershold_local && current_col-1 < 2 )
                {
                    i = 2;
                    j = current_row-1;
                    grow_dire_temp_x = 0;
                    grow_dire_temp_y = 1;
                    break;
                }
            }
            if(current_num==1)
            {
                point1 = (int)(*(img+(current_row-1)*MT9V03X_DVP_W+(current_col-1)));
                point2 = (int)(*(img+(current_row)*MT9V03X_DVP_W+(current_col-1)));
                if(point1 >= thershold_local && point2 < thershold_local)
                {
                    if (current_col - 1 < 2)
                    {
                        continue;
                    }
                    i = current_col-1;
                    j = current_row-1;
                    grow_dire_temp_x = -1;
                    grow_dire_temp_y = 1;
                    break;
                }
            }
            if(current_num==2)
            {
                point1 = (int)(*(img+(current_row)*MT9V03X_DVP_W+(current_col-1)));
                point2 = (int)(*(img+(current_row+1)*MT9V03X_DVP_W+(current_col-1)));
                if(point1 >= thershold_local && point2 < thershold_local)
                {
                    if (current_col - 1 < 2)
                    {
                        continue;
                    }
                    i = current_col-1;
                    j = current_row;
                    grow_dire_temp_x = -1;
                    grow_dire_temp_y = 0;
                    break;
                }
            }
            if(current_num==3)
            {
                point1 = (int)(*(img+(current_row+1)*MT9V03X_DVP_W+(current_col-1)));
                point2 = (int)(*(img+(current_row+1)*MT9V03X_DVP_W+(current_col)));
                if(point1 >= thershold_local && point2 < thershold_local)
                {
                    if (current_col - 1 < 2)
                    {
                        continue;
                    }
                    i = current_col-1;
                    j = current_row+1;
                    grow_dire_temp_x = -1;
                    grow_dire_temp_y = -1;
                    break;
                }
            }
            if(current_num==4)
            {

                point1 = (int)(*(img+(current_row+1)*MT9V03X_DVP_W+(current_col)));
                point2 = (int)(*(img+(current_row+1)*MT9V03X_DVP_W+(current_col+1)));
                if(point1 >= thershold_local && point2 < thershold_local)
                {
                    i = current_col;
                    j = current_row+1;
                    grow_dire_temp_x = 0;
                    grow_dire_temp_y = -1;
                    break;
                }
                if(point1 >= thershold_local && point2 >= thershold_local && current_col+1 > MT9V03X_DVP_W-2)
                {
                    i = MT9V03X_DVP_W-2;
                    j = current_row+1;
                    grow_dire_temp_x = 0;
                    grow_dire_temp_y = -1;
                    break;
                }
            }
            if(current_num==5)
            {
                point1 = (int)(*(img+(current_row+1)*MT9V03X_DVP_W+(current_col+1)));
                point2 = (int)(*(img+(current_row)*MT9V03X_DVP_W+(current_col+1)));
                if(point1 >= thershold_local && point2 < thershold_local)
                {
                    if (current_col + 1 > MT9V03X_DVP_W - 2)
                    {
                        continue;
                    }
                    i = current_col+1;
                    j = current_row+1;
                    grow_dire_temp_x = 1;
                    grow_dire_temp_y = -1;
                    break;
                }
            }
            if(current_num==6)
            {
                point1 = (int)(*(img+(current_row)*MT9V03X_DVP_W+(current_col+1)));
                point2 = (int)(*(img+(current_row-1)*MT9V03X_DVP_W+(current_col+1)));
                if(point1 >= thershold_local && point2 < thershold_local)
                {
                    if (current_col + 1 > MT9V03X_DVP_W - 2)
                    {
                        continue;
                    }
                    i = current_col+1;
                    j = current_row;
                    grow_dire_temp_x = 1;
                    grow_dire_temp_y = 0;
                    break;
                }
            }
            if(current_num==7)    //zhaobianjie
            {
                point1 = (int)(*(img+(current_row-1)*MT9V03X_DVP_W+(current_col+1)));
                point2 = (int)(*(img+(current_row-1)*MT9V03X_DVP_W+(current_col)));
                if(point1 >= thershold_local && point2 < thershold_local)
                {
                    if (current_col + 1 > MT9V03X_DVP_W - 2)
                    {
                        continue;
                    }
                    i = current_col+1;
                    j = current_row-1;
                    grow_dire_temp_x = 1;
                    grow_dire_temp_y = 1;
                    break;
                }
            }
            if(k==7)    //环绕一圈都没找到便舍弃图像
                return;
        }
        Trail8AllBoundaryStore(j, i, SideLeft);     //左边界储存
        if( CheckTrail8Overlap(&trail8AllLeft,&trail8AllRight,&flagOverlap) == OverlapScanStop )
        {
            if (highest_line < high_max + 1)
            {
                highest_line = high_max + 1;//前瞻总是取左右边界中距离短的
            }
            break;//判断是否重叠,重叠时退出循环
        }
        if(high_max == j )//如果在上边，就赋值
        {
            high_max = j-1;
        }
        current_num -= 3;
        if (current_num==-1)
        {
            current_num =7;
        }
        else if (current_num==-2)
        {
            current_num=6;
        }
        else if (current_num == -3)
        {
            current_num=5;
        }
        current_col = i;
        current_row = j;     //更新
        edge_num++;

        if (current_col>MT9V03X_DVP_W -4
                || (edge_num > TRAIL8ALL_ARRAYLENGTH - 2 )
                || current_row == zhidao_END
                || (current_row >= START_LINE && edge_num > 70))
        {     //快扫到边上了，停吧
            if (highest_line < high_max + 1)
            {
                highest_line = high_max + 1;
            }
            break;
        }
    }
    CalculateEdgeFromTrail8All(&edgeLeft, &trail8AllLeft, SideLeft,flagOverlap, 0);   //从八邻域轨迹解算边界
    CalculateEdgeFromTrail8All(&edgeRight, &trail8AllRight, SideRight,flagOverlap, 0);  //从八邻域轨迹解算边界

    // 此处的sum_grow_dir表示左右边界整体往什么方向偏移，如果处于知道，则为0.
    sum_grow_dir = 0;
    for(int m=START_LINE;m > highest_line;m-=2)
    {
        if(trail8AllLeft.coordinate[edgeLeft.xCoordinate[m].insideIndexOfTrail].column > 3 )
        {
            sum_grow_dir += trail8AllLeft.coordinate[edgeLeft.xCoordinate[m].insideIndexOfTrail].Growth_direction_x;
        }
        else if(trail8AllLeft.coordinate[edgeLeft.xCoordinate[m].insideIndexOfTrail].column <= 3 )
        {
            sum_grow_dir -= 1;
        }
        if(trail8AllRight.coordinate[edgeRight.xCoordinate[m].insideIndexOfTrail].column < 157 )
        {
            sum_grow_dir += trail8AllRight.coordinate[edgeRight.xCoordinate[m].insideIndexOfTrail].Growth_direction_x;
        }
        else if(trail8AllRight.coordinate[edgeRight.xCoordinate[m].insideIndexOfTrail].column >= 157 )
        {
            sum_grow_dir += 1;
        }
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
#define permit_limit 10
    for (i = START_LINE; i > highest_line; --i)
    {    //从开始到最后一行

        if (edgeRight.xCoordinate[i].inside > 155
                || edgeLeft.xCoordinate[i].inside < 5)
        {
            lose_line_num++;
        }
        if (i>=30&&i<=START_LINE&&edgeLeft.xCoordinate[i].inside < 5)
        {
            lose_left_circlepermit++;
        }
        if (i>=30&&i<=START_LINE&&edgeRight.xCoordinate[i].inside > 155)
        {
            lose_right_circlepermit++;
        }


    }
//    /*! @brief
//     *
//     * 直道检测
//     *
//     * 直道检测
//     *
//     * 直道检测
//     *
//     * 直道检测
//     */
//    if (lose_left_circlepermit < 8 && lose_right_circlepermit < 8)
//    {
//        if (find_edge_straight(&edgeRight,15,55)==1
//                    && find_edge_straight(&edgeLeft,15,55) == 1
//                    )
//            {
//                if (find_edge_straight(&edgeRight,35,75)==1
//                        && find_edge_straight(&edgeLeft,35,75) == 1)
//                {
//                    Straight_flag = 1;
//                }
//                else Straight_flag = 0;
//            }
//        else Straight_flag = 0;
//    }
//
//    else Straight_flag = 0;

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
    if (circleFlag.fixSteer != FixStart)
    {//整个if包括找左边上下拐点
        for (i = START_LINE-1-DIFF_STEP; i > highest_line + DIFF_STEP+5;--i)
        {//整个for包括找左边上下拐点
            diff_L_1 = edgeLeft.xCoordinate[i].inside - edgeLeft.xCoordinate[i + DIFF_STEP].inside;//上减下    为正     相差比较小
            diff_L_11 = edgeLeft.xCoordinate[i - DIFF_STEP].inside - edgeLeft.xCoordinate[i].inside;//上减下  为负    相差比较大
            diff_L_2[i] = diff_L_11 - diff_L_1;//很负 减 微正
            //*************下断点*************
            //*************下断点*************
            if(diff_L_2[i] <= -1 * DIFF_limit && diff_L_11 < 0 && L_down_corner_flag != 255)// 此处主要找左下拐点
            { //边界数组的二阶差分够大做初次判断，找到一次就不用继续找了
              //两点求斜率
                if(Grow_Diretion_Judge_Corner(&trail8AllLeft,&edgeLeft,i,L_DOWN,DIFF_STEP)==L_DOWN_OR_R_UP_OK
                        &&Grow_Diretion_Judge_K(&trail8AllLeft,&edgeLeft,i,L_DOWN,DIFF_STEP))
                {
                    int16_t indexTrail = edgeLeft.xCoordinate[i].insideIndexOfTrail; //取出八邻域轨迹索引
                    if (indexTrail < trail8AllLeft.indexOfCurrent - 1 - DIFF_STEP && indexTrail > DIFF_STEP + 1)
                    { //防止越界和误判
                        float kb11[2], kb1[2];
                        //存一元一次方程的k和b的数组,kb11为远处，kb1为近处
                        connect_two_point(trail8AllLeft.coordinate[indexTrail].column,
                                trail8AllLeft.coordinate[indexTrail + DIFF_STEP].column,
                                trail8AllLeft.coordinate[indexTrail].row,
                                trail8AllLeft.coordinate[indexTrail + DIFF_STEP].row,
                                kb11);//与上边的点连线   从上至下自左向右 为正    竖直为10000    从上至下自右向左为为负  水平为0
                        //两点求斜率
                        connect_two_point(trail8AllLeft.coordinate[indexTrail - DIFF_STEP].column,
                                trail8AllLeft.coordinate[indexTrail].column,
                                trail8AllLeft.coordinate[indexTrail - DIFF_STEP].row,
                                trail8AllLeft.coordinate[indexTrail].row, kb1);//与下边的点连线
                        //两点求斜率
                        if(fabs(kb11[0])< 1 && fabs(kb1[0]) > 0.85 &&!(kb11[0]>0&&kb1[0]>0))
                        { //后一项条件是为了考虑一条直线为竖直的情况
                            L_down_corner_line = i;
                            L_down_corner_flag = 255;  //这个标志位=255意味着找到了下断点
                        }
                    }
                }
            }
            //**************上断点***************
            if (diff_L_2[i] <= -1 * DIFF_limit && diff_L_1 > 0)//微正 减 很正
            {
//                tft180_show_int(60, 80, diff_L_2[i],3);
//                tft180_show_int(60, 90, diff_L_1,3);
                int GDJC = Grow_Diretion_Judge_Corner(&trail8AllLeft,&edgeLeft,i,L_UP,20);
//                int GDJK = Grow_Diretion_Judge_K(&trail8AllLeft,&edgeLeft,i,L_UP,DIFF_STEP);
                int GDJK = Grow_Diretion_Judge_K(&trail8AllLeft,&edgeLeft,i,L_UP,20);
//                tft180_show_int(60, 100, GDJC,1);
//                tft180_show_int(60, 110, GDJK,1);
                if(L_up_corner_line == 0
                        && GDJC==L_UP_OR_R_DOWN_OK
                        &&GDJK)
                {
                    int16_t indexTrail =  edgeLeft.xCoordinate[i].insideIndexOfTrail;   //取出八邻域轨迹索引
                    if (indexTrail < trail8AllLeft.indexOfCurrent - 1 - DIFF_STEP && indexTrail > 1 + DIFF_STEP)
                    {   //防止越界和误判
                        float kb11[2],kb1[2];   //存一元一次方程的k和b的数组,kb11为远处，kb1为近处
                        connect_two_point(trail8AllLeft.coordinate[indexTrail].column,
                                trail8AllLeft.coordinate[indexTrail+DIFF_STEP].column,
                                trail8AllLeft.coordinate[indexTrail].row,
                                trail8AllLeft.coordinate[indexTrail+DIFF_STEP].row,
                                kb11); //两点求斜率
                        connect_two_point(trail8AllLeft.coordinate[indexTrail-DIFF_STEP].column,
                                trail8AllLeft.coordinate[indexTrail].column,
                                trail8AllLeft.coordinate[indexTrail-DIFF_STEP].row,
                                trail8AllLeft.coordinate[indexTrail].row,
                                kb1); //两点求斜率
//                        tft180_show_float(60, 0, kb11[0], 1, 4);
//                        tft180_show_float(60, 10, kb1[0], 1, 4);
                        int temp_str = find_edge_straight2(&edgeLeft,i-20,i);
                        if(fabs(kb11[0]) > 1 && fabs(kb1[0]) < 1
                                &&temp_str==1
                                &&!(kb11[0]>0&&kb1[0]>0))// 此处kb11[0]>0&&kb1[0]>0表示不是右边线的拐点
                        {  //后一项条件是为了考虑一条直线为竖直的情况
                            L_up_corner_line = i;
                            if (L_down_corner_flag != 255)
                            {
                                L_down_corner_line = START_LINE;
                            }
                            if (L_down_corner_line != 0 && L_up_corner_line != 0
                                    && edgeLeft.xCoordinate[L_up_corner_line].inside-edgeLeft.xCoordinate[L_down_corner_line].inside > 70
                                    && edgeLeft.xCoordinate[L_down_corner_line].inside > 5
                                    && circle_find+circleFlag.direction+circleFlag.repairLine+circleFlag.fixSteer+circleFlag.outMagnet==0)
                            {//距离太远上断点就不太对了
                                L_up_corner_line = 0;
                            }
                            break;//找到上断点后，不管有没有下断点都不用再找了
                        }
                    }
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
        for (i = START_LINE -1-DIFF_STEP; i > highest_line + DIFF_STEP+5; --i)
        {
            diff_R_1 = edgeRight.xCoordinate[i].inside - edgeRight.xCoordinate[i + DIFF_STEP].inside;//小减大   上减下  拐点处小于零 接近零
            diff_R_11 = edgeRight.xCoordinate[i - DIFF_STEP].inside - edgeRight.xCoordinate[i].inside;//小减大 上减下  拐点处大于零 相差较大
            diff_R_2[i] = diff_R_11 - diff_R_1;
            //**********下断点***************
            if (diff_R_2[i] >= DIFF_limit && diff_R_11 > 0&& R_down_corner_flag != 255 )
            {
                if(Grow_Diretion_Judge_Corner(&trail8AllRight,&edgeRight,i,R_DOWN,DIFF_STEP)==L_UP_OR_R_DOWN_OK
                        &&Grow_Diretion_Judge_K(&trail8AllRight,&edgeRight,i,R_DOWN,DIFF_STEP))
                {
                    int16_t indexTrail = edgeRight.xCoordinate[i].insideIndexOfTrail; //取出八邻域轨迹索引
                    if (indexTrail< trail8AllRight.indexOfCurrent - 1 - DIFF_STEP && indexTrail > DIFF_STEP + 1)
                    { //防止越界和误判
                        float kb11[2], kb1[2];   //存一元一次方程的k和b的数组,kb11为远处，kb1为近处
                        connect_two_point(trail8AllRight.coordinate[indexTrail].column,
                                trail8AllRight.coordinate[indexTrail + DIFF_STEP].column,
                                trail8AllRight.coordinate[indexTrail].row,
                                trail8AllRight.coordinate[indexTrail + DIFF_STEP].row,kb11); //两点求斜率
                        connect_two_point(trail8AllRight.coordinate[indexTrail - DIFF_STEP].column,
                                trail8AllRight.coordinate[indexTrail].column,
                                trail8AllRight.coordinate[indexTrail - DIFF_STEP].row,
                                trail8AllRight.coordinate[indexTrail].row, kb1); //两点求斜率
                        if(fabs(kb11[0]) < 1 && fabs(kb1[0]) > 0.85 && !(kb11[0]<0&&kb1[0]<0))
                        { //后一项条件是为了考虑一条直线为竖直的情况
                            R_down_corner_line = i;
                            R_down_corner_flag = 255;
                        }
                    }
                }
            }
            //**********上断点***********
            if (diff_R_2[i] >= DIFF_limit && diff_R_1 < 0 )   //微负  减  很负
            {
                int DIFF_STEP_in = 15;
                if(R_up_corner_line == 0
                 && Grow_Diretion_Judge_Corner(&trail8AllRight,&edgeRight,i,R_UP,20)==L_DOWN_OR_R_UP_OK
                    && Grow_Diretion_Judge_K(&trail8AllRight,&edgeRight,i,R_UP,20))
                {
                    int16_t indexTrail =  edgeRight.xCoordinate[i].insideIndexOfTrail;   //取出八邻域轨迹索引
                    if (indexTrail < trail8AllRight.indexOfCurrent - 1 - DIFF_STEP
                            &&indexTrail > 1 + DIFF_STEP)
                    {   //防止越界和误判
                        float kb11[2],kb1[2];   //存一元一次方程的k和b的数组,kb11为远处，kb1为近处
                        connect_two_point(trail8AllRight.coordinate[indexTrail].column,
                                trail8AllRight.coordinate[indexTrail+DIFF_STEP_in].column,
                                trail8AllRight.coordinate[indexTrail].row,
                                trail8AllRight.coordinate[indexTrail+DIFF_STEP_in].row,
                                kb11); //两点求斜率
                        connect_two_point(trail8AllRight.coordinate[indexTrail-DIFF_STEP_in].column,
                                trail8AllRight.coordinate[indexTrail].column,
                                trail8AllRight.coordinate[indexTrail-DIFF_STEP_in].row,
                                trail8AllRight.coordinate[indexTrail].row,
                                kb1); //两点求斜率
                        if(fabs(kb11[0]) > 1 && fabs(kb1[0]) < 1
                                && find_edge_straight2(&edgeRight,i-20,i)==1
                                &&!(kb11[0]<0&&kb1[0]<0))
                        {  //后一项条件是为了考虑一条直线为竖直的情况
                            R_up_corner_line = (char)i;
                            if (R_down_corner_flag != 255)
                            {
                                R_down_corner_line = START_LINE;
                            }
                            if (R_up_corner_line != 0 && R_down_corner_line != 0
                                    && edgeRight.xCoordinate[R_down_corner_line].inside-edgeRight.xCoordinate[R_up_corner_line].inside > 70
                                    && edgeRight.xCoordinate[R_down_corner_line].inside < 155
                                    && circle_find+circleFlag.direction+circleFlag.repairLine+circleFlag.fixSteer+circleFlag.outMagnet==0)
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

    /*
     *前瞻随速度更改
     *
     *前瞻随速度更改
     *
     *前瞻随速度更改
     */






    /*入十字发现
     *
     *入十字发现
     *
     *入十字发现
     */
    if(cross_flag.cross_state==cross_find&&(edgeLeft.xCoordinate[START_LINE].inside<5||edgeRight.xCoordinate[START_LINE].inside>155))//使十字状态保持
    {
        cross_flag.cross_state=cross_find;
    }
    else//源代码中的
    {
        cross_flag.cross_state=cross_null;
        cross_width_num = 0;
        cross_high_num = 0;
    }
    if (cross_flag.cross_state==cross_null
            && circle_find + circleFlag.direction+
               circleFlag.repairLine + circleFlag.fixSteer+ circleFlag.outMagnet == 0
            && L_down_corner_line > 30
            && R_down_corner_line > 30
            && L_down_corner_line <= START_LINE
            && R_down_corner_line <= START_LINE
            && fabs(L_down_corner_line - R_down_corner_line)< 100)
    {
        cross_width_limit = edgeRight.xCoordinate[R_down_corner_line].inside
                - edgeLeft.xCoordinate[L_down_corner_line].inside + 50; //计算对比用的宽度
        if (cross_width_limit > 150)
        {        //限幅
            cross_width_limit = 150;
        }
        else if(cross_width_limit < 100)
        {
            cross_width_limit = 100;
        }
        if(R_down_corner_line >= L_down_corner_line)
        {        //那个断点更高，就从哪个开始找
            cross_find_start_line = L_down_corner_line - 1;
        }
        else if(R_down_corner_line < L_down_corner_line)
        {
            cross_find_start_line = R_down_corner_line - 1;
        }
        for (i = cross_find_start_line; i > highest_line; --i)
        {       //看看宽度够不够
            if (edgeRight.xCoordinate[i].inside - edgeLeft.xCoordinate[i].inside > cross_width_limit)
            {
                cross_width_num++;
                if(cross_width_num > 7)
                {
                    break;
                }
            }
        }
        if(cross_width_num > 7)
        { //看看纵向够不够高  从左边下拐点到右边下拐点有足够的纵贯通
            for (i = edgeLeft.xCoordinate[L_down_corner_line].inside;
                    i < edgeRight.xCoordinate[R_down_corner_line].inside; ++i)
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
                if (cross_high_num > 7)
                {        //数量够了，发现十字
                    cross_flag.cross_state=cross_find;
                    break;
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
    RoadIsZebra(mt9v03x_image[0],thershold_local);


    /*
    * 圆环单调改变点检测
    *
    * 圆环单调改变点检测
    *
    * 圆环单调改变点检测
   */



    Monotonicity_L_line = 0;
    Monotonicity_R_line = 0;
    if( L_down_corner_line <= 40 && L_up_corner_line == 0
            &&find_edge_straight(&edgeRight,20,100) == 1
            && lose_right_circlepermit <= 10 && lose_left_circlepermit > 20 && lose_left_circlepermit < 85) // 左圆环
    {
        Monotonicity_L_line =  Monotonicity_Change_Left (20,100);
        int Monotonicity_L_row = edgeLeft.xCoordinate[Monotonicity_L_line].inside;
        if (Monotonicity_L_row  <= 80 && Monotonicity_L_row  >= 30)
        {
            float ab[2] ={0};
            least_square_method_struct_small_step(&edgeRight,40,80,ab);
            ab[0] = -ab[0];
            ab[1] = Monotonicity_L_line - Monotonicity_L_row * ab[0];
//            Monotonicity_L_row = ((float) i - L_down_ab[1]) / L_down_ab[0];
            for (i = 126; i > highest_line; i--)
            {
                int template = 0;
                template = ((float) i - ab[1]) / ab[0];
                if (template>=158)
                    template = 158;
                else if (template<=2)
                    template = 2;
                edgeLeft.xCoordinate[i].inside = template;
            }
        }

    }

    else if (R_down_corner_line <= 40 && R_up_corner_line == 0
            && find_edge_straight(&edgeLeft,20,100) == 1
            && lose_left_circlepermit <= 10 && lose_right_circlepermit > 15 && lose_right_circlepermit < 85)  // 右圆环
    {
        Monotonicity_R_line =  Monotonicity_Change_Right (20,100);
        int Monotonicity_R_row = edgeRight.xCoordinate[Monotonicity_R_line].inside;
        if (Monotonicity_R_row  >= 80 && Monotonicity_R_row  <= 130)
        {
            float ab[2] ={0};
            least_square_method_struct_small_step(&edgeLeft,40,80,ab);
            ab[0] = -ab[0];
            ab[1] = Monotonicity_R_line - Monotonicity_R_row * ab[0];
//            Monotonicity_L_row = ((float) i - L_down_ab[1]) / L_down_ab[0];
            for (i = 126; i > highest_line; i--)
            {
                int template = 0;
                template = ((float) i - ab[1]) / ab[0];
                if (template>=158)
                    template = 158;
                else if (template<=2)
                    template = 2;
                edgeRight.xCoordinate[i].inside = template;
            }
        }
    }
    /*
     * 以下是环岛
     *
     * 以下是环岛
     *
     * 以下是环岛
    */
//    circle_permit = 1;   //初始化不允许发现
//    if(circle_find+circleFlag.direction+circleFlag.distanceDelay+circleFlag.repairLine+
//           circleFlag.fixSteer+circleFlag.outMagnet==0)
//    {
//        for (int i = 0; i < 5; i++)
//        {
//            if((edgeLeft.xCoordinate[START_LINE-30-2*i].inside < 5 && edgeRight.xCoordinate[START_LINE-30-2*i].inside > 155)
//                    ||(lose_left_circlepermit>15&&lose_right_circlepermit>15))//如果该行全白
//                break;//感觉是为了跟十字区分开
//            if(i == 4)
//                circle_permit = 1;   //允许开始发现
//        }
//    }
    if(/*circle_permit==1 && */circleFlag.lianxufaxian_distancedelay <= 0
            && circle_find != 1 && blockFlag.seek==roadblocknotfind
            && cross_flag.cross_state==cross_null)
    {
        //右环岛右环岛右环岛右环岛右环岛右环岛右环岛
        if(R_up_corner_line >= island_point &&R_up_corner_line<=100&&find_edge_straight(&edgeLeft,40,80)==1
                &&L_down_corner_line==0&&L_up_corner_line==0
                /*&&edgeRight.xCoordinate[R_up_corner_line+13].inside > 150*/
                &&lose_left_circlepermit <= 25&& lose_right_circlepermit > 20)
        {
            circle_find = 1;
            circleFlag.direction = CircleRight;
            cricle_delta_angle = 0;
            ATT_Angle.yaw = 0; // 往右转yaw减小 往左转yaw增大
            cricle_angle_in = ATT_Angle.yaw;
            circleFlag.lianxufaxian_distancedelay = island_delay;//20000
        }
        //左环岛左环岛左环岛左环岛左环岛左环岛左环岛左环岛
        if(L_up_corner_line >= island_point && L_up_corner_line<=100&&find_edge_straight(&edgeRight,40,80)==1
                &&R_down_corner_line==0&&R_up_corner_line==0
                /*&&edgeLeft.xCoordinate[L_up_corner_line+13].inside < 10*/
                &&lose_right_circlepermit <= 25 && lose_left_circlepermit > 20)
        {
            circle_find = 1;
            circleFlag.direction = CircleLeft;
            cricle_delta_angle = 0;
            ATT_Angle.yaw = 0;
            cricle_angle_in = ATT_Angle.yaw;
            circleFlag.lianxufaxian_distancedelay = island_delay;
        }
    }
    //环岛内转角
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
    if (circle_find == 1
            && circleFlag.distanceDelay == DelayNull)
    {
        circleDelayDistance = 0;//距离延迟赋值      3000对应450速度
        circleFlag.distanceDelay = DelayOver; // 如果想要用距离延时，可以启动Start
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
        //左环
        if (circleFlag.direction == CircleLeft)
        {
            //**************************找内环最高行  丢边行**************************
            int rowCircleInerHigest = START_LINE;      //内环丢边行
            if (FindAnotherHorizonEdge(img, rowCircleInerHigest,edgeLeft.xCoordinate[rowCircleInerHigest].inside,
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
                    bianjie=FindAnotherHorizonEdge(img, index,edgeRight.xCoordinate[index].inside,
                            thershold_local,RightToLeft);
                    if (bianjie<= 3)
                    {
                        rowCircleInerHigest = index;
                        test = rowCircleInerHigest;
                        break;
                    }
                }
            }
            //*******************从最下面开始向上，以右边界为起点找左边***********************
            //***********************每一次都从最上面开始找不准**************************
            if (circleFlag.repairLine != RepairOver)
            {
                int edge, edgeLast;
                edgeLast = edgeLeft.xCoordinate[rowCircleInerHigest].inside;
                int index;
                for (index = rowCircleInerHigest-1; index > zhidao_END; --index)
                { //从上往下遍历

                    edge = edgeLeft.xCoordinate[index].inside;
                    if (edge-edgeLast > thresholdCircleUpCorner)
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
//            if (L_up_corner_line != 0)
//            {//发现断点后开始入环补线
//                int downLine;
//                downLine = 3.3 * L_up_corner_line + 25;//1.7   18
//                if (downLine > START_LINE)
//                {
//                    downLine = START_LINE;
//                }
//                if (L_up_corner_line<=50) // 刚入环时，用右上断点直接拉下
//                 {
//                     connect_two_point(edgeLeft.xCoordinate[L_up_corner_line].inside,
//                             edgeRight.xCoordinate[downLine].inside,
//                             L_up_corner_line, downLine, R_down_ab);
//                     highest_line = L_up_corner_line;
//                     for (i = downLine; i > L_up_corner_line - 1; i--)
//                     {
//                         edgeRight.xCoordinate[i].inside = ((float) i - R_down_ab[1]) / R_down_ab[0];
//                     }
//                 }
//                 else if (L_up_corner_line>50) // 入环一段距离后，用直线拟合圆环的外边界，使入环更平稳
//                 {
//                     int LeftAll_Index = edgeLeft.xCoordinate[L_up_corner_line].insideIndexOfTrail; // 断点（属于内边界）在未解算的边界中的索引
//                     int LeftAll_Index_before = LeftAll_Index -80; // 未解算的边界的前面80个
//                     while (1)
//                     {
// //                        trail8AllLeft.coordinate[index].row
//                         if (trail8AllLeft.coordinate[LeftAll_Index_before].column >= 20)
//                         {
//                             highest_line = trail8AllLeft.coordinate[LeftAll_Index_before].row;
//                             break;
//                         }
//                         LeftAll_Index_before += 10;
//                     }
//                     connect_two_point(edgeRight.xCoordinate[downLine].inside,
//                             trail8AllLeft.coordinate[LeftAll_Index_before].column,
//                             downLine,trail8AllLeft.coordinate[LeftAll_Index_before].row,
//                             R_down_ab);
//                     for (i = 126; i > trail8AllLeft.coordinate[LeftAll_Index_before].row - 1; i--)
//                     {
//                         int template = 0;
//                         template = ((float) i - R_down_ab[1]) / R_down_ab[0];
//                         if (template>=158)
//                             template = 158;
//                         else if (template<=2)
//                             template = 2;
//                         edgeRight.xCoordinate[i].inside = template;
//                     }
//
//                 }
            if (L_up_corner_line != 0)
            {//发现断点后开始入环补线
                int downLine;
                downLine = 3.3 * L_up_corner_line + 25;//1.7   18
                if (downLine > START_LINE)
                {
                    downLine = START_LINE;
                }

                     connect_two_point(edgeLeft.xCoordinate[L_up_corner_line].inside,
                             edgeRight.xCoordinate[downLine].inside,
                             L_up_corner_line, downLine, R_down_ab);
                     highest_line = L_up_corner_line;
                     for (i = downLine; i > L_up_corner_line - 1; i--)
                     {
                         edgeRight.xCoordinate[i].inside = ((float) i - R_down_ab[1]) / R_down_ab[0];
                     }

                     int LeftAll_Index = edgeLeft.xCoordinate[L_up_corner_line].insideIndexOfTrail; // 断点（属于内边界）在未解算的边界中的索引
                     int LeftAll_Index_before = LeftAll_Index -80; // 未解算的边界的前面80个

                     while (1)
                     {
                         LeftAll_Index -=1 ;
                         if (trail8AllLeft.coordinate[LeftAll_Index].column >= 20)
                         {
//                             tft180_show_uint(120, 80, trail8AllLeft.coordinate[LeftAll_Index].row, 3);
//                             tft180_show_uint(120, 100, trail8AllLeft.coordinate[LeftAll_Index].column, 3);
                            if (trail8AllLeft.coordinate[LeftAll_Index].row < highest_line)
                            {
                                highest_line = trail8AllLeft.coordinate[LeftAll_Index].row;
                                edgeRight.xCoordinate[trail8AllLeft.coordinate[LeftAll_Index].row].inside = trail8AllLeft.coordinate[LeftAll_Index].column;
                            }
                            else if (trail8AllLeft.coordinate[LeftAll_Index].row > highest_line
                            && abs(trail8AllLeft.coordinate[LeftAll_Index].column - edgeLeft.xCoordinate[L_up_corner_line].inside)>=10)
                                break;
                         }
                         else break;
                     }
                circleFlag.repairLine = RepairStart;
            }

            else if (circleFlag.repairLine == RepairStart && cricle_delta_angle > +50) // 左转为正 右转为负 参数可调
            {        //没有断点并且角度够20°，全进环岛
                circleFlag.repairLine = RepairOver;
            }
        }
        //右环
        if (circleFlag.direction == CircleRight)
        {
            //**************************找内环丢边行**************************
            int rowCircleInerHigest = START_LINE;      //内环丢边行
            if (FindAnotherHorizonEdge(img, rowCircleInerHigest,edgeLeft.xCoordinate[rowCircleInerHigest].inside,
                    thershold_local,LeftToRight) >= MT9V03X_DVP_W - 3)
            { //起始点便是边上
                rowCircleInerHigest = START_LINE;
            }
            else
            {         //存在内环，找内环丢边
                int index;
                int bianjie;
                for (index = START_LINE-1; index > zhidao_END; --index)
                {
                    bianjie=FindAnotherHorizonEdge(img, index,edgeLeft.xCoordinate[index].inside,
                            thershold_local,LeftToRight);
                    if (bianjie>= MT9V03X_DVP_W - 3)
                    {

                        rowCircleInerHigest = index;
                        test = rowCircleInerHigest;
                        break;
                    }
                }
            }
            //**************************从最下面开始向上，以左边界为起点找右边**************************
            if (circleFlag.repairLine != RepairOver)
            {
                int edge, edgeLast;
                edgeLast = edgeRight.xCoordinate[rowCircleInerHigest-1].inside;
                int index;
                for (index = rowCircleInerHigest-1; index > zhidao_END; --index)
                { //从上往下遍历

                    edge = edgeRight.xCoordinate[index].inside;
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
                downLine = 3.3 * R_up_corner_line + 25;
                if (downLine > START_LINE)
                {
                    downLine = START_LINE;
                }
                connect_two_point(edgeRight.xCoordinate[R_up_corner_line].inside,
                        edgeLeft.xCoordinate[downLine].inside,
                        R_up_corner_line, downLine, L_down_ab);
                highest_line = R_up_corner_line;
                for (i = downLine; i > R_up_corner_line - 1; i--)
                {
                    edgeLeft.xCoordinate[i].inside = ((float) i - L_down_ab[1]) / L_down_ab[0];
                }
                int RightAll_Index = edgeRight.xCoordinate[R_up_corner_line].insideIndexOfTrail; // 断点（属于内边界）在未解算的边界中的索引
//                int RightAll_Index_before = RightAll_Index -80; // 未解算的边界的前面80个
                while (1)
                {
                    RightAll_Index -=1 ;
                 if (trail8AllRight.coordinate[RightAll_Index].column <= 140)
                 {
//                             tft180_show_uint(120, 80, trail8AllLeft.coordinate[LeftAll_Index].row, 3);
//                             tft180_show_uint(120, 100, trail8AllLeft.coordinate[LeftAll_Index].column, 3);
                    if (trail8AllRight.coordinate[RightAll_Index].row < highest_line)
                    {
                        highest_line = trail8AllRight.coordinate[RightAll_Index].row;
                        edgeLeft.xCoordinate[trail8AllRight.coordinate[RightAll_Index].row].inside = trail8AllRight.coordinate[RightAll_Index].column;
                    }
                    else if (trail8AllRight.coordinate[RightAll_Index].row > highest_line
                    && abs(trail8AllRight.coordinate[RightAll_Index].column - edgeRight.xCoordinate[R_up_corner_line].inside)>=10)
                        break;
                 }
                 else break;
                }
                circleFlag.repairLine = RepairStart;
            }
            else if (circleFlag.repairLine == RepairStart && cricle_delta_angle < -50)
            {        //没有断点了，完全进环岛
                circleFlag.repairLine = RepairOver;
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
            if ((cricle_delta_angle < -200        //-200
                    && (L_up_corner_line > MT9V03X_DVP_H * 1 / 3
                            || (L_down_corner_line > MT9V03X_DVP_H * 1 / 3
                                    && L_down_corner_flag == 255))) //转够200°且上或下拐点在屏幕下半部分
            || cricle_delta_angle < -180)          //-220/-240
            {     //转够270°必须固定打角
                circleFlag.fixSteer = FixStart;     //正在出环
            }
        }
        else if ((circleFlag.direction == CircleLeft))
        {
            if ((cricle_delta_angle > 200       //200
                    && (R_up_corner_line > MT9V03X_DVP_H * 1 / 3
                            || (R_down_corner_line > MT9V03X_DVP_H * 1 / 3
                                    && R_down_corner_flag == 255))) //转够-200°且上或下拐点在屏幕下半部分
            || cricle_delta_angle > 180)        // 220/240
            {     //转够-270°必须固定打角
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

    if (circleFlag.fixSteer == FixStart)
    {
        if (circleFlag.direction == CircleRight)
        {
            if (cricle_delta_angle < -345) // 300
            {      //强制解除
                circleFlag.fixSteer = FixOver;  //固定打角解除
                circle_find = 0;   //环岛标志位全部复位
                circleFlag.direction = CircleNull;
                circleFlag.distanceDelay = DelayNull;
                circleFlag.repairLine = RepairNull;
                circleFlag.fixSteer = FixNull;
                circleFlag.outMagnet = MagnetNull;

            }
        }
        else if (circleFlag.direction == CircleLeft)
        {
            if (cricle_delta_angle > +345) // 330
            {
                circleFlag.fixSteer = FixOver;  //固定打角解除
                circle_find = 0;   //环岛标志位全部复位
                circleFlag.direction = CircleNull;
                circleFlag.distanceDelay = DelayNull;
                circleFlag.repairLine = RepairNull;
                circleFlag.fixSteer = FixNull;
                circleFlag.outMagnet = MagnetNull;
            }
        }
    }
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
        if(circle_find != 1)
        {
            if (L_down_corner_line != 0 && edgeLeft.xCoordinate[L_down_corner_line].inside > 5
                    && L_up_corner_line==0)
            {
                calculate_slope_flag = least_square_method_struct(&edgeLeft, L_down_corner_line, L_down_ab);
                if (calculate_slope_flag == 1)
                {
                    for (i = L_down_corner_line; i > zhidao_END; i--)
                    {
                        edgeLeft.xCoordinate[i].inside = ((float) i - L_down_ab[1]) / L_down_ab[0];

                        if (edgeLeft.xCoordinate[i].inside >= 158
                                || edgeLeft.xCoordinate[i].inside > edgeRight.xCoordinate[i].inside)
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
            if (R_down_corner_line != 0&& edgeRight.xCoordinate[R_down_corner_line].inside < 155
                    && R_up_corner_line == 0)
            {
                calculate_slope_flag = least_square_method_struct(&edgeRight, R_down_corner_line, R_down_ab);
                if (calculate_slope_flag == 1)
                {
                    for (i = R_down_corner_line; i > zhidao_END; i--)
                    {
                        edgeRight.xCoordinate[i].inside = ((float) i - R_down_ab[1]) / R_down_ab[0];
                        if (edgeRight.xCoordinate[i].inside <=2
                                || edgeRight.xCoordinate[i].inside <= edgeLeft.xCoordinate[i].inside)
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
             if (L_down_corner_line != 0 && L_up_corner_line != 0)
             {
                 connect_two_point(edgeLeft.xCoordinate[L_up_corner_line].inside,
                                         edgeLeft.xCoordinate[L_down_corner_line].inside,
                                         L_up_corner_line, L_down_corner_line, L_down_ab);
                 for (i = L_down_corner_line; i > L_up_corner_line - 1; i--)
                 {
                     edgeLeft.xCoordinate[i].inside = ((float) i - L_down_ab[1])/ L_down_ab[0];
                     if (edgeLeft.xCoordinate[i].inside> edgeRight.xCoordinate[i].inside)
                     {
                         break;
                     }
                 }
             }
             /************************两点连线*****************************/
             /************************两点连线*****************************/
             /************************两点连线*****************************/
             if (R_down_corner_line != 0 && R_up_corner_line != 0)
             {
                     connect_two_point(edgeRight.xCoordinate[R_up_corner_line].inside,
                            edgeRight.xCoordinate[R_down_corner_line].inside,
                            R_up_corner_line, R_down_corner_line, R_down_ab);
                 for (i = R_down_corner_line; i > R_up_corner_line - 1; i--)
                 {
                     edgeRight.xCoordinate[i].inside = ((float) i - R_down_ab[1]) / R_down_ab[0];
                     if (edgeRight.xCoordinate[i].inside < edgeLeft.xCoordinate[i].inside)
                     {
                         break;
                     }
                 }
             }
        }

    /*! @brief
     *
     * 直道检测
     *
     * 直道检测
     *
     * 直道检测
     *
     * 直道检测
     */
        if (
                    (
                          (lose_left_circlepermit < 8 && ((lose_right_circlepermit < 50 && R_up_corner_line == 0) || circleFlag.lianxufaxian_distancedelay>0))
                        ||(lose_right_circlepermit < 8&& ((lose_left_circlepermit < 50 && L_up_corner_line == 0) || circleFlag.lianxufaxian_distancedelay>0))
                        ||(cross_flag.cross_state==cross_find)
                        ||(lose_right_circlepermit < 8 && lose_left_circlepermit < 8)
                    )
                        &&(Monotonicity_L_line == 0 && Monotonicity_R_line == 0)
               )

    {
        if (find_edge_straight(&edgeRight,15,55)==1
                    && find_edge_straight(&edgeLeft,15,55) == 1
                    )
            {
                if (find_edge_straight(&edgeRight,35,75)==1
                        && find_edge_straight(&edgeLeft,35,75) == 1)
                {
                    Straight_flag = 1;
                }
                else Straight_flag = 0;
            }
        else Straight_flag = 0;
    }

    else Straight_flag = 0;


    /// u --
    if (find_edge_straight(&edgeRight,60,100)==1
        && find_edge_straight(&edgeLeft,60,100) == 1
        &&lose_left_circlepermit < 20
        &&lose_right_circlepermit < 20
                        )
        usta = 1;
    else
        usta = 0;




    /*找中点
    *
    * 找中点
    *
    * 找中点
    * 找中点
    *
    * 找中点
    */
    highset_line_for_control_speed=highest_line;
    weight_count=0; // 用于计算权重
    mid_servo = 0;
    Err_last = Err;
    if (highest_line < 120)
    {        //防止太低 数组越界
        // 下面需要修改midservo的计算 即Err的计算逻辑
        if ((circle_find == 1
                && circleFlag.repairLine == RepairStart || circleFlag.repairLine == RepairOver))
        {      //环岛//入环时候的找中点
            if (highest_line < see_max)            //see跟速度有关 有上下限
            {
                highest_line = see_max;
            }

            if (circleFlag.direction == CircleLeft)//highest_line + HIGHEST_line_down在上拐点上方时(大概已经入环10°)
            {
                int max_scan = 0;
                if (L_up_corner_line>50) // 当拐点靠下时，右上拐点往上八十（虚）trail8AllRight.coordinate[RightAll_Index_before].row
                    max_scan = highest_line;
//                else // 当拐点考上时，用右上断点行作为最高行
//                {
//                    max_scan = L_up_corner_line;
//                    highest_line = L_up_corner_line;
//                }
                for (int index = MT9V03X_H-2; index > highest_line; index--)
                {
//                    int adding_line_width_circle = (int) (adding_line_slope
//                            * ((float) (index)) + adding_line_intercept);
//                    int adding_line_width_circle = ( 1.15 *  (index) + 45);
//                    int adding_line_width_circle = ( 1.10 *  (index) + 35);
                    int adding_line_width_circle = ( circle_k *  (index) + circle_b);
                    mid_servo += Weight[index]*(MT9V03X_W/2
                            -(edgeRight.xCoordinate[index].inside - adding_line_width_circle/2));
                    weight_count+=Weight[index];
                }
                // 入环时按照左边线计算误差
                if (weight_count != 0)
                    {Err = mid_servo/weight_count;
                    if(Err<0)
                        Err = Err_last;
                    }

            }
            else if (circleFlag.direction == CircleRight)
            {
                int max_scan = 0;
                if (R_up_corner_line>50) // 当拐点靠下时，右上拐点往上八十（虚）trail8AllRight.coordinate[RightAll_Index_before].row
                    max_scan = highest_line;
//                else // 当拐点考上时，用右上断点行作为最高行
//                {
//                    max_scan = R_up_corner_line;
//                    highest_line = R_up_corner_line;
//                }
                for (int index = MT9V03X_H-2; index > highest_line; index--)
                {
//                    int adding_line_width_circle = (int) (adding_line_slope
//                            * ((float) (index)) + adding_line_intercept);
//                    int adding_line_width_circle = ( 1.15 *  (index) + 45);
//                    int adding_line_width_circle = ( 1.10 *  (index) + 35);
                    int adding_line_width_circle = ( circle_k *  (index) + circle_b);
                    mid_servo += Weight[index]*(MT9V03X_W/2
                            -(edgeLeft.xCoordinate[index].inside+ adding_line_width_circle/2));
                    weight_count+=Weight[index];
                }
                // 入环时按照左边线计算误差
                if (weight_count != 0)
                    {Err = mid_servo/weight_count;
                    if(Err>0)
                        Err = Err_last;
                    }

            }
        }
        else
        {
            //无元素普通道路
            if (highest_line < START_LINE - 10)
            {
//                if (highest_line < see_max)
//                {
//                    highest_line = see_max;   //根据速度调整
//                }
                if (highest_line < see_max)
                {
                    highest_line = see_max;   //根据速度调整
                }
//                int sum = 0, num = 0;
//                int beginLine;
//                if ( START_LINE - highest_line > 10)   //最底部与前瞻行相差10以上
//                    beginLine = highest_line + 10;   //beginLine等于前瞻下移10行
//                else
//                {//最底部与前瞻相差不到20
//                    beginLine = highest_line + 1;//beginLine等于前瞻下移1行
//                }
//                for (int index = MT9V03X_H-20; index > highest_line; index--)
                int counter_mid = 0;

                for (int index = highest_line+1; index < MT9V03X_H-15; index++)
                {
                     int weight = Weight[index];
                    if (index >= min_weight_line && index < max_weight_line)
                    {
//                        weight = Weight[index]*D_Weight[index-min_weight_line];
                        weight = Weight[index];
                    }
                    counter_mid++;
                    if (counter_mid>=62)
                    {
                        break;
                    }
                    int adding_line_width = 0; //补出来的宽度
                    if ((edgeLeft.xCoordinate[index].inside < 4
                            && edgeRight.xCoordinate[index].inside < 156)
                            ||(lose_left_circlepermit > 20 && L_down_corner_line==0
                                    && find_edge_straight(&edgeRight,20,120) && Monotonicity_L_line == 0))
                    { //左转弯//左边丢了右边没丢
//                        adding_line_width = ( 1.15 *  (index) + 55);
                        adding_line_width = ( road_k *  (index) + road_b);
                        mid_servo += weight*(MT9V03X_W/2-(edgeRight.xCoordinate[index].inside - adding_line_width/2));
                        weight_count+=weight;
                    }
                    else if ((edgeLeft.xCoordinate[index].inside > 4
                            && edgeRight.xCoordinate[index].inside > 156)
                            ||(lose_right_circlepermit>20&&R_down_corner_line==0
                                    && find_edge_straight(&edgeLeft,20,120) && Monotonicity_R_line == 0))
                    { //右转弯右边丢了左边没丢
//                        adding_line_width = ( 1.15 *  (index) + 55);
                        adding_line_width = ( road_k *  (index) + road_b);

                        mid_servo += weight*(MT9V03X_W/2 -(edgeLeft.xCoordinate[index].inside+ adding_line_width/2));
                        weight_count+=weight;
                    }
                    else
                    { //左右都没丢
                        mid_servo += weight*(MT9V03X_W/2 - (edgeLeft.xCoordinate[index].inside+edgeRight.xCoordinate[index].inside)/2);
                        weight_count+=weight;
                    }
                }
            }
            // 非圆环的正常道路
            if (weight_count != 0)
                Err = (int)mid_servo/weight_count; // Err>0 表示车头往右偏，Err<0 表示车头往左偏
            //Err = MT9V03X_W/2 - (edgeLeft.xCoordinate[70].inside+edgeRight.xCoordinate[70].inside)/2;
            if (Straight_flag == 1 && abs(Err) < 8) // 直道检测2
            {
                Straight_flag = 2;
            }
            else Straight_flag = 0;

            if (Straight_flag == 2 && circle_find == 0 && lose_left_circlepermit <=5 && lose_right_circlepermit <=5)
            {
                circleFlag.lianxufaxian_distancedelay = 0;
            }
        }
        Steer_pid();
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
int least_square_method_struct(EDGE_FROM_TRAIL8ALL *egde, int point,float *ab)
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
                        && egde->xCoordinate[point + COUNT_NUM * i].inside< 156)
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
        *ab = ((float) (COUNT_NUM * sumxy - sumx * sumy)) / div_num;
    }
    else
    {
        *ab = 10000;
    }
    *(ab + 1) = ((float) (sumy - (*(ab)) * sumx)) / COUNT_NUM;
    return 1;
}
/************************************************************************************************
 * 最小二乘法拟合直线,适用于刘铖康的结构体
 * 参数：边界， 断点， 存放a、b的数组指针y=ax+b
 * 返回值 0断点过于靠边  1成功拟合
 *速度决策拟线，小步长
 ***************************************************************************************************/
#define COUNT_NUM_2 5
char least_square_method_struct_small_step(EDGE_FROM_TRAIL8ALL *egde, unsigned char point,
        unsigned char point_down,        float *ab) {
    unsigned char i, step = COUNT_STEP;
    int sumx = 0, sumy = 0, sumxy = 0, sumxx = 0, div_num;

    step = (point_down - point) / 5;
    if (step == 0) { //判断一下下边是否还有十行
        return 0;
    }
//
//    //判断一下拟合直线用的最后一个点是不是已经到边界了，到边界的话会不准
//    if (egde->xCoordinate[point + COUNT_NUM_2 * step].inside < 3
//            || egde->xCoordinate[point + COUNT_NUM_2 * step].inside > 156) {
//        //循环找一下不到边界的最长的步长
//        for (i = step - 1; i >= 0; --i) {
//            if (i == 0) {
//                return 0;
//            } else {
//                if (egde->xCoordinate[point + COUNT_NUM_2 * i].inside > 3
//                        && egde->xCoordinate[point + COUNT_NUM_2 * i].inside
//                                < 156) {
//                    step = i;
//                }
//            }
//        }
//    }

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
        *ab = ((float) (COUNT_NUM_2 * sumxy - sumx * sumy)) / div_num;
    } else {
        *ab = 10000;
    }

    *(ab + 1) = ((float) (sumy - (*(ab)) * sumx)) / COUNT_NUM_2;
    return 1;
}

