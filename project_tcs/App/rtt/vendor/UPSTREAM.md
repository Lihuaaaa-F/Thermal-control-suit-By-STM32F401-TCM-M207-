# RTT vendor 出处（W4.2，v13.4 §5.2）

- 上游：https://github.com/SEGGERMicro/RTT （**SEGGER 官方仓**——segger.com RTT 产品页
  https://www.segger.com/products/debug-probes/j-link/technology/about-real-time-transfer/
  直接背书的官方分发渠道，2026-10-27 实证；比官网 zip 直链出处更强：commit hash 可锁）
- 版本：HEAD **4d8feab**（浅克隆，含其上 Release **V8.56a**=ad6970d 后的 3 个清理提交）
- 获取：git clone --depth 8（2026-10-27）；文件为**零改动逐字拷贝**
- 许可：LICENSE.md（SEGGER 2026 版 BSD 单条件许可，随库）
- 完整性：见同目录 VENDOR_SHA256.txt（五文件 SHA256；复核命令 sha256sum 同表）
- 布局说明：上游 Config/SEGGER_RTT_Conf.h 为空壳用户配置文件（官方设计如此），
  本工程实例化于 ../SEGGER_RTT_Conf.h（App/rtt/，非 vendor 目录=允许用户编辑层）
