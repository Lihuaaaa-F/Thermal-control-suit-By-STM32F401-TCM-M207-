/* m207.h — 垫片：让 project_tcs/tests 里 #include "m207.h" 的测试源（fuzz_m207.c）
 * 对 ESP32 这一份协议副本 m207_proto.[ch] 编译（S-G e2：M12-F-12）。
 * 只在主机单测的编译参数里把本目录放到 include 路径最前面；不进固件构建。 */
#include "m207_proto.h"
