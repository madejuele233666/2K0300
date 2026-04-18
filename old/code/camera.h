
/*
 * camera.h
 *
 *  Created on: 2023年4月7日
 *      Author: 25566
 */

#ifndef CAMERA_H_
#define CAMERA_H_

extern int middle;
#include "zf_common_headfile.h"

#define MT9V03X_DVP_H   MT9V03X_H
#define MT9V03X_DVP_W   MT9V03X_W

#define START_LINE (MT9V03X_DVP_H-2)
#define END_LINE 20
#define zhidao_END 2

#define HIGHEST_line_down 5

#define img_pixel(x,y)  ((int)(*(mt9v03x_image[0]+(x)*MT9V03X_W+(y))))

#define TRAIL8ALL_ARRAYLENGTH   400

#define adding_line_slope       1//0.960        //斜率 实际值 0.68
#define adding_line_intercept   23.33//23.33   //截距   实际值 55
#define Start_Line_Wideth    125
#define L_DOWN 1
#define R_DOWN 2
#define L_UP 3
#define R_UP 4
#define L_UP_OR_R_DOWN_OK 5
#define L_DOWN_OR_R_UP_OK 6
#define Grow_Judge_Corner 0.78

//********************************八邻域轨迹用到的定义********************************
enum Trail8AllSide {SideLeft, SideRight};       //八邻域左右边界方向
typedef struct
{
    struct
    {
        int16_t row;
        int16_t column;
        int8 Growth_direction_x;
        int8 Growth_direction_y;
    }coordinate[TRAIL8ALL_ARRAYLENGTH];   //储存8邻域各点的坐标信息
    int16_t indexOfCurrent;        //始终指向(最后一个有效元素+1)的列表索引
    int16_t indexOfHigestLeft;         //最远点的坐标索引,并非为最远行的值,分左右是为了应对当最高行有很多点的情况
    int16_t indexOfHigestRight;
}TRAIL8ALL;         //八邻域寻找轨迹结构体
enum statusOfTrail8Overlap{NoOverlap, Overlap, OverlapScanStop};     //两8领域轨迹是否重叠枚举。 NoOverlao:没有重叠。 Overlap:重叠。OverlapScanStop:重叠且停止扫描

//********************************边界用到的定义********************************
enum TypeofEdgePoint {Null, Single, Both};      //某行的边界点状态，Null：该行没有边界信息； Single：该行只有内边界信息， Both：该行存在重叠现象，有内外边界
typedef struct
{
    struct
    {
    int16_t inside;                 //内边界
    int16_t insideIndexOfTrail;     //内边界的TRAIL8ALL来源索引，inside值由TRAIL8ALL.coordinate[insideIndexOfTrail]得出
    int16_t outside;                //外边界
    int16_t outsideIndexOfTrail;    //外边界的TRAIL8ALL来源索引，outside值由TRAIL8ALL.coordinate[outsideIndexOfTrail]得出
    enum TypeofEdgePoint type;      //行状态
    }xCoordinate[MT9V03X_DVP_H];    //储存x坐标的数组

    int16_t peakRow;            //最远行
    int16_t numOutside;         //外边界点数量,改变量并不是当前外边界的数量，也包括了被舍弃的外边界

}EDGE_FROM_TRAIL8ALL;   //边界结构体


enum StatusOfCircleFlagPermmitFind{NoPermit, DoPermit};                     //NoPermit为不允许发现环岛，DoPermit为允许发现环岛
enum StatusOfCircleFlagInOrOutMagnet{MagnetNull, MagnetFind};               //MagnetNull为未发现电磁信号，MagnetFind为发现电磁信号
enum StatusOfCircleFlagDirection{CircleNull,CircleLeft, CircleRight};       //CircleNull为为判断出方向，CircleLeft为左环，CircleRight为右环
enum StatusOfCircleFlagDistanceDelay{DelayNull, DelayStart, DelayOver};     //DelayNull为未开始延迟距离，DelayStart为开始延迟距离，DelayOver为延迟完成
enum StatusOfCircleFlagRepairLine{RepairNull, RepairStart, RepairOver};     //RepairNull为未开始补线，RepairStart为开始补线，RepairOver为补线完成
enum StatusOfCircleFlagFixSteer{FixNull, FixStart, FixOver};                //FixNull为未固定打角，FixStart为开始固定打角，FixOver为固定打角完成，开始正常循迹

typedef struct
{
    int lianxufaxian_distancedelay;                         //连续发现距离延迟
    //enum StatusOfCircleFlagPermmitFind      permitFind;     //允许开始发现标志位
    //uint8   inMagnet;                                       //入环电磁发现标志位，发现后开始入环
    enum StatusOfCircleFlagDirection        direction;      //环岛方向标志位
    enum StatusOfCircleFlagDistanceDelay    distanceDelay;  //延迟距离标志位
    enum StatusOfCircleFlagRepairLine       repairLine;     //入环补线标志位
    enum StatusOfCircleFlagFixSteer         fixSteer;       //出环固定打角标志位
    enum StatusOfCircleFlagInOrOutMagnet    outMagnet;      //出环电磁发现标志位
}CIRCLE_FLAG;       //环岛标志位结构体
extern int circle_find;
extern int circle_permit;
extern int island_point,island_delay;
enum StatusofroadblockFlagFind{roadblocknotfind,roadblockfind};//路障发现标志位
enum StatusofroadblockFlagDirection{blockLeft,blockRight};//路障绕行方向

typedef struct
{
    enum StatusofroadblockFlagFind seek;
    enum StatusofroadblockFlagDirection dir;
    int delay_distance;
}RoadBlock;


enum Stateofcross{cross_null,cross_find};//启动标志位
typedef struct
{
    enum Stateofcross cross_state;
}CROSS_FLAG;

enum StateofStart{run_start,start_over,run_stop};//启动标志位
typedef struct
{
    enum StateofStart start_state;
}STOP_FLAG;

enum StateofZebra{Zebra_null,Zebra_find};//启动标志位
enum StateofcheckZebra{notcheck,check};//
typedef struct
{
    enum StateofZebra zebra_state;
    enum StateofcheckZebra check_state;
    int zebra_distance;
    uint8_t zebra_time;
}Zebra_FLAG;

extern float Err;
extern float Err_last;
extern float circle_k_err;
extern int Monotonicity_L_line,Monotonicity_R_line;
extern int Emergency_threshold;

extern int Emergency_Stop;
extern int Straight_flag,Straight_permit;


extern int8 grow_dire_temp_x,grow_dire_temp_y;
extern uint8 lose_left_circlepermit;
extern uint8 lose_right_circlepermit;

extern int thershold_local;
extern uint8_t zebra_time;

extern int white_black_turncount ;
extern int see_max_add;
extern float see_max_k;
extern int see_max;
extern uint16_t lose_line_num;//左右丢边总数
extern int open_current_together_row;
extern float last_rol,now_rol;//断路用
extern int highest_line;
extern CIRCLE_FLAG circleFlag;     //环岛标志位结构体
extern RoadBlock blockFlag;
extern CROSS_FLAG cross_flag;
extern STOP_FLAG run_state;
extern Zebra_FLAG zebra_flag;
extern float cricle_angle_in,cricle_angle_out,cricle_delta_angle;//环岛
extern  float wandao_row;

extern unsigned char cricle_out_count;//环岛
extern uint8 L_edge_8_START_LINE,R_edge_8_START_LINE;

extern unsigned char L_down_corner_line,L_up_corner_line,R_down_corner_line,R_up_corner_line;//记录上下角，十字用，圆环也可以
extern unsigned char DIFF_limit,DIFF_STEP,DIFF_DOWN_LIMIT,DIFF_UP_LIMIT;
extern int highset_line_for_control_speed;
extern long int mid_servo;
extern int show_mid_line;

extern TRAIL8ALL trail8AllLeft,trail8AllRight;     //左边和右边八邻域寻找轨迹
extern EDGE_FROM_TRAIL8ALL edgeLeft,edgeRight;     //左右边界信息
extern enum statusOfTrail8Overlap flagOverlap;     //左右8邻域轨迹是否重叠标志位
extern int sum_grow_dir;
extern int BLOCK_distance;
void SCI_Send_Datas(uart_index_enum uart_num);
char least_square_method_struct_small_step(EDGE_FROM_TRAIL8ALL *egde, unsigned char point,unsigned char point_down,float *ab);
float calculate_variance(unsigned char *edge, unsigned char point);
char find_edge_straight(EDGE_FROM_TRAIL8ALL *edge,int up_line,int down_line);
void eight_neighbor(unsigned char *img,int thershold_local);
char least_square_method(unsigned char *egde, unsigned char point, float *ab);
int least_square_method_struct(EDGE_FROM_TRAIL8ALL *egde, int point,float *ab);
void connect_two_point(int egde_up,int egde_down, int point_up, int point_down, float *ab);
int GetOSTU(unsigned char *tmImage);
uint8 otsuThreshold_fast(uint8 *image);
int find_common_road(uint8_t* img);
extern int BLDC_PWM;
extern float circle_k_err;
extern int blok;
#endif /* CAMERA_H_ */






