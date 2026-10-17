# PC 单元测试报告（任务书 §5.1，run_all.sh）

环境：Windows + MSVC cl 19.x（/std:c11 /utf-8，宿主机无 gcc，自动回退），
`tests/run_all.sh` 一键（gcc 存在时优先 gcc）。日期：2026-10-18。

```
[m207]      PASS   test_m207_parse:      PASS=22 FAIL=0
[registry]  PASS   test_param_registry:  PASS=50 FAIL=0
[control]   PASS   test_control:         PASS=36 FAIL=0
[proto_comm]PASS   test_proto_tcs:       PASS=32 FAIL=0
[tmp117]    PASS   test_tmp117（raw→°C 换算 + 10 错掉线/3 成功恢复健康机）
run_all: ALL GREEN        （合计 140+ 断言，5 套件全绿）
```

## 覆盖对照（§5.1 表）

| §5.1 要求 | 落点 | 关键向量 |
|---|---|---|
| m207_parse_line | test_m207_parse.c | `TC1:TCACTTEMP=28.5`✓；缺冒号/缺等号/空串/NULL/64超长/小写→大写/`TC3:`拒绝/负值/非数值/粘连双行出两帧/\r\n 空行不误触 |
| param_registry | test_param_registry.c | clamp 5.0/45.0/45.01→45.0；body_cold<body_hot 拒写(-2)双向；相等短路；dirty 触发；2s 防抖；EEPROM 序列化↔反序列化往返；CRC 坏/magic 坏/ver 坏→拒绝；30 条过/31 条拒（0x04+8×31=0xFC 越 0xF8 物理上限，任务书 count≤31 口径取更严者）|
| control FSM | test_control.c | 37.7+ε进制冷(目标15.0常量)/37.3-ε退出/36.3-ε进制热(45.0)/36.7+ε退出 ±0.001；滞回带内不抖动；传感器丢失→SAFE（手动模式也生效）；报警位人工确认才清（F3）；M207 失联→SAFE+ALM_M207；上电 5s 宽限；急停→SAFE+TEC停+风扇开+mode=手动+自持；>48°C ALM_OVERTEMP |
| proto/comm | test_proto_tcs.c | proto self_test（CRC 0xF4 向量+GS 帧编解码+RX 状态机+杂字节恢复）+ 0x10 STATUS 19B 精确十六进制对照（含负温 i16）+ 0x12/0x14/0x15/0x90 编解码 + comm ARQ：pending 登记/ACK 清除/3 重传后断链/重复帧去重 |
| tcs_proto 跨端 | tools/proto_ref.py selftest | Python struct 独立实现同一 STATUS 十六进制向量，与 C 端逐字节一致 → **OK** |

## 说明与偏差记录

1. **STATUS 19B vs 任务书 "17B"**：任务书 §4.2 字段逐项相加 = 1+4+2+2+2+1+1+1+1+2+1+1 = 19B，
   表尾 "17B" 为漏加 online+rfu 两字节的算术口径。以字段清单为准（双端同源 tcs_proto.h +
   proto_ref.py 保证一致），已在本文件留档。
2. **EEPROM 记录数上限**：0x04..0xF7 共 244B = 30 条物理上限；任务书 count 字段口径 ≤31，
   序列化按更严者（>30 拒绝）防记录覆盖 CRC 区（§3.3 布局冻结所致）。
3. **comm.c 一行扩展**：ACK 判定追加 `0x90`（TCS_ACK，载荷布局与 GS ACK 兼容：
   [2]=acked_seq [3]=acked_mt），其余逐字节原样；GS ACK 类型保留。
4. **eeprom.c**：GS at24 位弹层原样保留；配置层替换为 v2 镜像 API；自测字节 0xF6
   位于 CRC 覆盖区（0x00..0xF7）→ 自测后**恢复原值**保 CRC 一致（设计矛盾消解，已注释）。
5. **keypad.c**：只编译不初始化（KP_COL2=PD5 与接线表 #3 USART2_TX 冲突），链接器
   --gc-sections 剔除。
