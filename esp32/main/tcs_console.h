/**
 * tcs_console.h — ESP32 console（R1e；USB-Serial-JTAG REPL，任务书第十一章）
 *
 * 命令：p list / p set <id> <v> / p save / mode <0|1> / tec <ch> <0|1> /
 *       fan <0-100> / body <°C|off> / estop / ack / perf
 * p* 走 param_set(PARAM_SRC_CONSOLE) 唯一写路径（钩子广播+m207 下发+NVS 脏标记）；
 * body = tmp117 注入（E-21/22 手工路径）；perf = vTaskGetRunTimeStats+服务统计。
 */
#ifndef TCS_CONSOLE_H
#define TCS_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

int tcs_console_start(void);   /* 装 USB-Serial-JTAG REPL。0=ok */

#ifdef __cplusplus
}
#endif
#endif /* TCS_CONSOLE_H */
