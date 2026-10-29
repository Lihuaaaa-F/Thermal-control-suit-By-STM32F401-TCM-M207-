# LVGL 专册执行状态（v12.5；终态索引，2026-10-29 修订）

> **2026-10-29 更正**：原文第 4/5 行与"G4 启动"条含未证实表述（"v11.3 已完成，D-L14 R6 边界解除"），
> 经全册对账（`LVGL_对账_20261029.md`）确认为无据声明，已删除。当前真实状态如下。

## 门禁实际状态

- **G0 模拟器底座：已关单**（六条 DoD；`LVGL_G0_结果.md`；附录 A 五要素在 `D:\Espressif\ws\tcs_lvgl\README.md`）
- **G1 token+P0/P1 复刻：主体交付，盲审未闭**（九条 DoD 主体过；④⑤⑦ 的 R2 隔时轮/R3 隔日轮仍挂账）
- **G2 新能力原型：已关单**（八条 DoD，L2；`LVGL_G2_结果.md`）
- **G3 决策门：用户已裁 GO**（决策线 5/7 打勾，两条时点性盲审 ⚠️；`LVGL_G3_决策.md`）
- **G4 真机底座：未关单**（4 全过 / 3 半 / 4 未过；详见对账文件 §3）
- **G5 UI 全量重建：未关单**（1 全过 / 3 半 / 9 未过）
- **G6：未开始**

## 当前硬阻塞

1. **v11.3 R6 未冻结** → D-L14② 的 G5/G6 硬边界在形式上不成立；G5 已开工属越界，待用户裁决。
2. **DAPLink 卡死**（CMSIS-DAP 命令 ID 错乱；需拔插）→ 无法烧录/复测。
3. **TL-8 已判定 FAIL**（P99.9≈14ms）→ G4③/G5② 原 PASS 作废；修复版 `49e3cb66` 待上板复测。

## 时点/用户门槛（制度性，不可同会话速成）

1. **盲审 R2**：VL-3 / VL-16 / U-1 三对象，隔 ≥4h 或新会话；盲号包已备
   （`evidence/LVGL/vl_crops/blind/B01-B34 + blind_map_T1.json`、`vl16/sheet_1..5.png`）——R2 记录只写 B 号，
   禁看 `verdicts_R1.md`/映射表。
2. **盲审 R3**：隔日或换执行者；三轮并集→修复→重出包全三轮重走→末轮全零才通过。
3. **VL-3 T2**：隔 ≥24h 重取全量帧+裁片（diff 判 0）。
4. **TL-7**：24h tick 漂移读回（保持供电，中间不得换固件）。
5. **VL-1**：7/8 页×状态基准帧建档 + 双时点 diff。
6. **PL/人眼项**：PL-1/2/4/5/6/7/8、VL-9/10/11/15、TL-14、TL-20、U-12。

## 本次新增资产（2026-10-29）

- `project_tcs/tools/tl8_read.py`：TL-8 v2 采样读取与判定（P50/P90/P99/P99.9/max）
- `project_tcs/tools/vl2_token_audit.py`：VL-2 token 真值锚定（真机帧，区域众数 + 精确像素计数）
- `project_tcs/App/lvgl_ui/tui_diag.c`：VL-12 极端内容页 / U-6 错误码人话表 / TL-18 全字符集轮播页（g_ui_cmd 100..103）
- `docs/results/LVGL_对账_20261029.md`：逐条对账与整改记录

## 一键复现（模拟器线）

```bash
cd /d/Espressif/ws/tcs_lvgl
bash tools/verify_locks.sh        # 版本锁（红=禁构建）
python tools/font_audit_lvgl.py   # 双轨字库门禁
bash build.sh                     # 构建（21s 级）
./build-vs/sim.exe p0 normal out/g1 && ./build-vs/sim.exe p1 normal out/g1
./build-vs/sim.exe g2chart normal out/g2 && ./build-vs/sim.exe g2stress normal out/g2 && ./build-vs/sim.exe g2perf normal out/g2
```

## 一键复现（真机线；调试器恢复后）

```bash
cd /d/Espressif/ws/tcs/project_tcs
cmake -S . -B build-g4lv -DTCS_USE_LVGL=ON && cmake --build build-g4lv -j 8
# 烧录必须低速 + 写后多点回读校验（1MHz 会出现半写镜像；见对账 §5）
pyocd flash -t stm32h743xx --probe <UID> -f 500k -e sector --connect attach \
    build-g4lv/tcs_main.bin --base-address 0x08000000
python tools/tl8_read.py
```
