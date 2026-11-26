/**
 * ota_pure.h — OTA 自拷贝的纯逻辑（脱离 IDF 也能编译；主机用例 esp32/tests/test_ota_pure.c）
 *
 * 与硬件绑定的部分（分区读写、esp_ota_*、任务、重启）留在 main.c；这里只放"逻辑"本身，
 * 交付固件与主机用例编译的是同一份代码。S 线 S-G e3b。
 */
#pragma once
#include <stdint.h>

/* ====== 单实例闸门（X03-F-12 = P1-15 的 ②）======
 * OTA 拷贝任务同一时刻只允许一例在跑。闸门在 app_ota_trigger() 里用"比较并交换"拿到：
 * 拿到的那一例才去建拷贝任务；其余请求直接回绝（HTTP 409）。拿到之后，所有没走到重启的
 * 出口都必须调 ota_gate_leave()，否则一次失败之后 OTA 永远被拒。 */
int  ota_gate_try_enter(void);   /* 1 = 拿到（此刻起只有这一例）；0 = 已有一例在进行 */
void ota_gate_leave(void);       /* 放开。只由拿到过的那一例调用 */
int  ota_gate_busy(void);        /* 1 = 有一例在进行 */
