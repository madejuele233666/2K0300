#ifndef _zf_device_encoder_h_
#define _zf_device_encoder_h_


#include "../zf_common/zf_common_typedef.h"



#define LOW_BUFFER  0x004
#define FULL_BUFFER 0x008
#define CTRL		0x00c

/* CTRL counter each bit */
#define CTRL_EN		BIT(0)		// 计数器使能
#define CTRL_OE		BIT(3)		// 脉冲输出使能（低有效）
#define CTRL_SINGLE	BIT(4)		// 单脉冲控制位
#define CTRL_INTE	BIT(5)		// 中断使能
#define CTRL_INT	BIT(6)		// 中断状态
#define CTRL_RST	BIT(7)		// 计数器重置
#define CTRL_CAPTE	BIT(8)		// 测量脉冲使能
#define CTRL_INVERT	BIT(9)		// 输出翻转使能
#define CTRL_DZONE	BIT(10)		// 防死区使能



#endif