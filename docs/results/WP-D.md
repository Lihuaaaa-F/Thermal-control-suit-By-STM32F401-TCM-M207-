# WP-D 结果文档 — 控制台解锁（到货前软件部分全量完成）

日期：2026-10-18 ｜ 状态：**软件部分完成（关单）**；USB-TTL 终态接线待 U2 到货
基线：任务书 v5.1 §3 WP-D

## 1. 完成内容

| 项 | 内容 | 结果 |
|---|---|---|
| 控制台通道 | **无 COM/USB-TTL 条件下的固件交互**：console.c 增加 RAM 邮箱（s_mb_line/s_mb_magic/s_mb_seq），主机 `tools/mb_console.py` 经 probe-rs write 注入命令；printf 输出镜像进 RTT 环并由脚本按 WrOff 增量直读 | ✓ 全命令可用 |
| RTT 交互实测 | `ver` / `stat` / `p list` / `p set` / `p save` / `perf` / `scrtest` / `wdttest` 全套实测记录（各 WP 证据内引用） | ✓ |
| EEPROM 往返 | `p set 0x01 30.5` → `ok 30.50`；（2s 防抖自动固化，`p save` 返回 rv=1 无脏参=已写）；`probe-rs reset` 硬复位 → `p list` 显示 **0x01 tc1_set 30.50 保持** | ✓ **复位往返通过** |
| 附带闭环 | 固化后配置镜像有效化：复位后 `alarm=0x0000`（原"EPRROM 配置无效"提示横幅消失，WP-A 遗留项关闭） | ✓ |
| COM 口 DTR 试 | **跳过（有据决策）**：任务书 §0 红线"COM 口不插（CH340 连 NRST 硬件特性）"，插 COM 试 DTR 与红线冲突且需物理插线；邮箱通道已完全覆盖控制台语义 | 记入待决 |

## 2. 运行时证据

- `p list`（8 参数全表，含 0x08 commissioned——WP-A.5）
- `p set 0x01 30.5` → `ok 30.50`；硬复位后 `p list` 首行 `0x01 tc1_set 30.50 [5.0,45.0] 0x1f`
- 复位后 `stat`：`alarm=0x0000 loop=867`（配置加载成功，活性正常）
- `ver`：`tcs v1.0.0 build=... params=v1(8+0x100resv) proto=v1`

## 3. 自主决策与理由

- **邮箱而非 RTT 下行**：probe-rs CLI 无 RTT 下行通道，RAM 邮箱 + probe-rs write 是零接线的等价实现；命令消费以 s_mb_seq 自增确认，无竞态（单写单读+magic 握手）。
- **真"断电"往返**：本环境无法物理断电（USB 供电保持），以 probe-rs 硬复位（NRST）往返替代——EEPROM 非挥发，跨复位保持已证；真断电由用户上电即验（首次上电 p list 应仍 30.5）。
- **p save rv=1 说明**：防抖自动固化先于手动命令落盘（邮箱命令间隔>2s），rv=1=无脏参非失败；语义已核实并在文档注明。

## 4. 遗留与依赖（U2 到货后）

- USB-TTL 接 PA9/PA10/GND → COM 终态验证（115200，同命令集，无新增工作）。
- 真断电往返（用户拔电 10s 再上电，`p list` 应保持 30.5）。

## 5. 待决清单

| # | 事项 | 已采用默认方案 |
|---|---|---|
| D-1 | COM 口 DTR 试（与 §0 红线冲突） | 跳过，USB-TTL 到货直接走 PA9/PA10 终态 |
| D-2 | 出厂前是否把 tc1_set 恢复 25.0 默认 | 保持 30.5（作为 EEPROM 有效性的可见痕迹），交付前统一 p defaults Y |
