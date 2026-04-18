
#include "all_init.h"
#include "ZiTaiJieSuan.h"

void All_init_core1 ()
{
    system_delay_ms(500);
    pit_init(CCU60_CH1, 10000);// 初始化core1的定时器 10ms
    // 初始化陀螺仪

//    wireless_uart_init();
}
