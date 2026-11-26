/* test_link_ack_comm.c — 主控（ESP32）那一份 comm.c 的 X01-F-03 用例（S 线 e1）。
 *
 * 用例本体只有一份（project_tcs/tests/test_link_ack.c）；这里定义 LINK_ACK_COMM_ONLY
 * 跳过 link_send 那几节（link_send.c 是屏端模块），把主控的 comm.c 编进来跑同一组
 * comm 层断言——两端 ACK 结果解析必须一致。
 *
 * 由 esp32/tests/run_m207_tests.sh 的 linkack 一行编译运行。
 */
#define LINK_ACK_COMM_ONLY 1
#include "../../project_tcs/tests/test_link_ack.c"
