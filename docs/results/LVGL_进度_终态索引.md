# LVGL 专册执行状态（v12.5；终态索引，2026-10-26）

- **G0 模拟器底座：已关单**（六条 DoD；`LVGL_G0_结果.md`）
- **G1 token+P0/P1 复刻：主体交付**（九条 DoD；`LVGL_G1_结果.md`）
- **G2 新能力原型：已关单**（八条 DoD，L2；`LVGL_G2_结果.md`）
- **G3 决策材料：已呈阅，待用户裁决**（`LVGL_G3_决策.md` §7 裁决栏）
- 规格草案（D-L13 预稿）：`LVGL_G2_chart页规格草案.md`

## 时点/用户门槛（制度性，不可同会话速成）
1. **盲审 R2**：VL-3 / VL-16 / U-1 三对象，隔 ≥4h 或新会话；盲号包已备
   （`evidence/LVGL/vl_crops/blind/B01-B34 + blind_map_T1.json`、`vl16/sheet_1..5.png`）——R2 记录只写 B 号，
   禁看 `verdicts_R1.md`/映射表。
2. **盲审 R3**：隔日或换执行者；三轮并集→修复→重出包全三轮重走→末轮全零才通过。
3. **VL-3 T2**：隔 ≥24h 重取全量帧+裁片（diff 判 0）。
4. **G3 裁决**（D-L2）：GO/NO-GO + chart 页入正式 UI + G4 启动时点——用户当面定，代理不代填。
5. **G4**：用户 GO 后启动（v11.3 已完成，D-L14 R6 边界解除）。

## 一键复现（接管者）
```bash
cd /d/Espressif/ws/tcs_lvgl
bash tools/verify_locks.sh        # 版本锁（红=禁构建）
python tools/font_audit_lvgl.py   # 双轨字库门禁
bash build.sh                     # 构建（21s 级）
./build-vs/sim.exe p0 normal out/g1 && ./build-vs/sim.exe p1 normal out/g1
./build-vs/sim.exe g2chart normal out/g2 && ./build-vs/sim.exe g2stress out/g2 && ./build-vs/sim.exe g2perf out/g2
```
