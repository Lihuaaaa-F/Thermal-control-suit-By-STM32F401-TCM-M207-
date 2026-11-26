/* main.c — 调温服 ESP32-S3 无线端（任务书 v5.1 WP-F）
 *
 * ESP32 = 主控（参数/控制闭环/锁存的权威）+ Web 服务器；H743 是显示与操作终端（v2 架构）。
 * 启动序：NVS → event loop → SoftAP(TCS-XXXX/12345678) → 回环自测(WP-F.1)
 *        → 链路任务(UART1 230400，含内置注册表快照+演示模式 WP-F.2/F3) → HTTP/WS :80
 * OTA（WP-F.4）：POST /api/ota?m=0 或 WS {"t":"ota"} 触发——运行镜像自拷贝到对槽+切换；
 *        m=1 直接把 boot 指回对槽（回滚），两次重启日志即证据。
 *        HTTP 的 m 必填、只认精确的 "0" / "1"，其余 400（M14-F-13）；同一时刻只跑一例，并发的回 409（X03-F-12）。
 *
 * 日常闭环：bash ~/.claude/skills/embedded-dev-loop/platforms/esp32/scripts/esp_loop.sh all
 * 引脚红线：GPIO17(TX)→H743 PA3(RX)，GPIO18(RX)←H743 PD5(TX)；17/18 非保留脚 ✓
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_heap_caps.h"   /* X03-F-12：OTA 拷贝缓冲 heap_caps_malloc（内部 RAM）*/

#include "softap.h"
#include "link_task.h"
#include "http_server.h"
#include "websocket.h"
#include "param_registry.h"
#include "nvs_store.h"
#include "m207_service.h"
#include "tmp117_service.h"
#include "fan_service.h"
#include "control.h"
#include "control_service.h"
#include "tcs_console.h"
#include "tcs_snap.h"
#include "r1e_probe.h"
#include "svc_status.h"
#include "esp_wifi.h"       /* P1-12：softap_probe 用 esp_wifi_get_mode */
#include "tcs_bootrec.h"
#include "ota_pure.h"        /* X03-F-12：OTA 单实例闸门（纯逻辑，主机可测）*/

static const char *TAG = "TCS";

/* control ops 适配（m207 API 返回 int，ops 签名 void；错误由服务内 NAK/重发计数承载）*/
static void op_tec_sw(uint8_t ch, uint8_t on) { (void)m207_tec(ch, on); }
static void op_tec_temp(uint8_t ch, float v) { (void)m207_set_temp(ch, v); }
/* P1-09 的另一半（i3）：安全关断通道——SAFE 的关断 / 500ms 重申 / cmd 11 停机都走它。
 * 模块离线时 m207_tec 回 -2、连旁路槽都进不了；m207_tec_force 离线也记下意图，复联第一帧补发。*/
static void op_tec_force(uint8_t ch, uint8_t on) { (void)m207_tec_force(ch, on); }
#if CONFIG_TCS_SIM
/* 注入源在生效的那一路（S 线 i3，M09-F-09 的注入档口径）：卡死判据豁免这一路——注入值是人给的常数，
 * "逐位不变"是它的本性。体温 = tmp117_sim_active()（body 命令 / E-21）；水温 = m207_sim_water_active()
 * （water 命令）。交付档不编译，op 为 NULL，行为不变。*/
static uint8_t op_stuck_exempt(uint8_t ch)
{
    if (ch == 1u) return tmp117_sim_active();
    if (ch == 2u) return m207_sim_water_active();
    return 0;
}
#endif
/* X02-F-05 / M12-F-08：参数钩子要把模块服务的结果带回来（离线 -2 / 队列满或忙 -1）。
 * 原先接的是 tcs_m207 里的 void 包装（m207_hook_*），返回值在那里就被丢了。*/
static int op_m207_set_temp(uint8_t ch, float v) { return m207_set_temp(ch, v); }
static int op_m207_map_set(uint16_t pid, float v) { return m207_map_set(pid, v); }

/* ====== WP-F.4 OTA：镜像实际长度（只拷贝镜像本身，不拷贝整槽 3MB）======
 * 长度算法与边界检查在 ota_pure.c 的 ota_image_len（主机可测，M08-F-12）；这里只把"读分区"接上去。 */
static int ota_part_rd(uint32_t off, uint8_t *dst, uint32_t len, void *ctx)
{
    return (esp_partition_read((const esp_partition_t *)ctx, off, dst, len) == ESP_OK) ? 0 : -1;
}

#define OTA_COPY_CHUNK 8192u

/* X03-F-12：同一时刻只允许一例在跑（闸门在 app_ota_trigger 里拿）。缓冲随任务从堆上申请、用完即释放——
 * 以前是 static buf[8192]，两个并发实例读写同一块，写进对槽的镜像会错乱。 */
static void ota_copy_task(void *arg)
{
    (void)arg;
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (!next) { ESP_LOGE(TAG, "ota FAIL: no next slot"); goto leave; }
    uint32_t len = 0;
    int img = ota_image_len(run->size, ota_part_rd, (void *)(uintptr_t)run, &len);
    if (img != OTA_IMG_OK) {
        /* M08-F-12：镜像头或段表不合法就不拷贝——不擦对槽、不重启。以前只读开头 256 字节、按偏移取段头，
         * 而段头排在各自段数据的后面，读到的其实是栈上的任意值，"长度"是随机的。*/
        ESP_LOGE(TAG, "ota FAIL: image header not usable (%d)", img);
        goto leave;
    }
    ESP_LOGI(TAG, "ota: copy %s@0x%08lx img=%luKB -> %s",
             run->label, (unsigned long)run->address,
             (unsigned long)(len / 1024), next->label);
    /* 缓冲先于 esp_ota_begin 申请：申请不到就不去擦对槽。放内部 RAM（同以前的 static 缓冲） */
    uint8_t *buf = heap_caps_malloc(OTA_COPY_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) { ESP_LOGE(TAG, "ota FAIL: no copy buffer"); goto leave; }
    esp_ota_handle_t h = 0;
    esp_err_t err = esp_ota_begin(next, len, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota begin=%s", esp_err_to_name(err));
        free(buf);
        goto leave;
    }
    for (uint32_t off = 0; off < len; off += OTA_COPY_CHUNK) {
        uint32_t chunk = (len - off > OTA_COPY_CHUNK) ? OTA_COPY_CHUNK : (len - off);
        ESP_ERROR_CHECK(esp_partition_read(run, off, buf, chunk));
        err = esp_ota_write(h, buf, chunk);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ota write=%s", esp_err_to_name(err));
            (void)esp_ota_abort(h);             /* 释放 OTA 句柄，否则下一次 esp_ota_begin 会受影响 */
            free(buf);
            goto leave;
        }
    }
    free(buf);
    err = esp_ota_end(h);
    if (err != ESP_OK) { ESP_LOGE(TAG, "ota end=%s", esp_err_to_name(err)); goto leave; }
    ESP_ERROR_CHECK(esp_ota_set_boot_partition(next));
    ESP_LOGI(TAG, "ota OK: boot -> %s, restart in 1s", next->label);
    /* M08-F-13：重启前强制落盘。参数的常规落盘是 2 秒防抖 + 500ms 冲洗（persist_worker），
     * 这 1 秒里改了参数、正好赶上重启，脏参就随 NVS 镜像一起丢了。落盘失败也照样重启
     * （只记日志）：boot 分区已经切了，停在运行态只会让网页/屏以为 OTA 没生效。*/
    {
        int prv = param_persist_now();
        if (prv < 0) ESP_LOGE(TAG, "ota: param persist before restart FAILED (rv=%d); restarting anyway", prv);
        else         ESP_LOGI(TAG, "ota: params persisted before restart (rv=%d)", prv);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
leave:
    /* 没走到 esp_restart() 的出口都要放开闸门：否则一次失败之后 OTA 永远回 409 */
    ota_gate_leave();
    vTaskDelete(NULL);
}

/* m=0：自拷贝到对槽并切换；m=1：回滚（boot 指回对槽，不拷贝）。
 * 返回 ESP_OK = 已受理（拷贝任务已起 / 随即重启）；ESP_ERR_INVALID_STATE = 已有一次 OTA 在进行
 * （X03-F-12：调用方回 409）；其余 = 这次没做成，设备不会重启。 */
esp_err_t app_ota_trigger(int rollback)
{
    /* 拷贝与回滚共用这一道闸门：回滚会把 boot 指向正在被拷贝任务写着的那个槽 */
    if (!ota_gate_try_enter()) {
        ESP_LOGW(TAG, "ota refused: another OTA is in progress");
        return ESP_ERR_INVALID_STATE;
    }
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (!next) { ESP_LOGE(TAG, "ota FAIL: no next slot"); ota_gate_leave(); return ESP_ERR_NOT_FOUND; }
    if (rollback) {
        ESP_LOGI(TAG, "ota ROLLBACK: boot %s -> %s, restart in 1s",
                 run->label, next->label);
        /* R1 修复 M08-F-09：不用 ESP_ERROR_CHECK（失败会 abort），改为记录并放弃回滚 */
        esp_err_t rb = esp_ota_set_boot_partition(next);
        if (rb != ESP_OK) {
            ESP_LOGE(TAG, "ota ROLLBACK set_boot fail=%s", esp_err_to_name(rb));
            ota_gate_leave();
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));      /* 这 1s 里闸门保持占用（重启后清零）*/
        /* M08-F-13：回滚也是重启，同样先落盘（见拷贝路径上的说明）*/
        {
            int prv = param_persist_now();
            if (prv < 0) ESP_LOGE(TAG, "ota: param persist before restart FAILED (rv=%d); restarting anyway", prv);
            else         ESP_LOGI(TAG, "ota: params persisted before restart (rv=%d)", prv);
        }
        esp_restart();
    }
    if (xTaskCreate(ota_copy_task, "ota_copy", 6144, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "ota FAIL: copy task not created");
        ota_gate_leave();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* R2 协议换向：屏（H743）0x12 → 本端权威注册表 → M207 verify → 回 ACK。
 * 经 link_on_setparam 回调注入（tcs_core 依赖 tcs_link，反向直调会成组件环）。*/
/* 网页/控制台改参数 → 反向 0x11 镜像给屏（v2 补口，2026-11-16）。
 * 屏是纯显示终端，本地注册表只是镜像；不回推会让屏显示旧值并被屏上操作覆盖。*/
static void esp_on_broadcast(param_desc_t *p, uint8_t src)
{
    if (!p) return;
    /* 网页：推 pv（带来源/回执）+ 记事件日志 + 注册表缓存失配（U 线 W1）*/
    ws_param_changed(p, src);
    link_demo_note_param(p->id, p->value);    /* 演示模型跟随（生产档编译为空操作）*/
    if (src == PARAM_SRC_LINK) return;        /* 屏幕自己的改动已经在对端生效 */
    (void)link_send_param(p->id, p->value);   /* 反向镜像：屏幕显示跟随 */
}

/* ====== P1-12：服务启动结果汇总 ======
 * 此前五个服务的 int 返回值在 app_main 里被直接丢掉，末尾无条件打印 all up。现在逐个登记进
 * svc_status，结束时由 svc_report 统一出报告：全起来才说 all up；有失败 → E 级日志 + 名单 +
 * ALM_SERVICE。link_start / web_start 原先是 void 接口，只能如实登记成"未核实"；
 * i3（2026-11-26）起两者带返回值（任务创建失败 = 非 0），同样进汇总。接口约定见
 * docs/results/S_G_e3a_结果.md 第七节与 S_G_i3_结果.md。*/
static void app_svc_log(void *arg, int level, const char *text)
{
    (void)arg;
    if (level == SVC_LOG_E)      ESP_LOGE(TAG, "%s", text);
    else if (level == SVC_LOG_W) ESP_LOGW(TAG, "%s", text);
    else                         ESP_LOGI(TAG, "%s", text);
}

/* softap_start() 是 void（tcs_wifi），内部每一步都是 ESP_ERROR_CHECK：失败即 abort，不会静默没起来。
 * 按效果核实：WiFi 已处于 AP 模式才算起了。*/
static int softap_probe(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK) return -1;
    return (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) ? 0 : -1;
}

/* v2：屏幕（H743）急停/报警确认 → 在本端执行（TEC/风扇在这里）*/
static void esp_on_cmd(uint8_t cmd, uint8_t seq)
{
    uint8_t st = TCS_ACK_OK;
    if (cmd == TCS_CMD_M207_SAVE) {          /* 10：依赖 tcs_m207，不进 control */
        st = (m207_save_all() >= 0) ? TCS_ACK_OK : TCS_ACK_BUSY;
    } else {
        /* 1..9 + 11 与网页 WS 走同一份语义（control_apply_cmd_src），避免两处 switch 漂移。
         * 来源记"屏幕"：事件日志据此写"屏幕：…"，而不是一律记成网页。*/
        int cr = control_apply_cmd_src(cmd, PARAM_SRC_TOUCH);
        if (cr == -2) {
            st = TCS_ACK_BUSY;
            ESP_LOGW(TAG, "H743 CMD %u rejected (SAFE, or mode locked) -> BUSY", (unsigned)cmd);
        } else if (cr != 0) {
            st = TCS_ACK_BAD_ID;
            ESP_LOGW(TAG, "H743 CMD %u unknown -> BAD_ID", (unsigned)cmd);
        } else {
            ESP_LOGI(TAG, "H743 CMD %u -> state=%d alarm=0x%04X",
                     (unsigned)cmd, (int)control_state(), (unsigned)g_tcs.alarm_bits);
        }
    }
    link_send_ack(seq, MT_TCS_CMD, st, (float)g_tcs.alarm_bits);
}

static void esp_on_setparam(uint16_t id, float v, uint8_t seq)
{
    int rv = param_set(id, v, PARAM_SRC_LINK);   /* 来自屏幕：不回推，防 0x11/0x12 成环 */
    uint8_t st = TCS_ACK_OK;
    if (rv == 1) st = TCS_ACK_CLAMP;
    else if (rv == -1) st = TCS_ACK_BAD_ID;
    else if (rv < 0 || rv == 2) st = TCS_ACK_BUSY;   /* rv==2（X02-F-05）：值已记下，但模块没受理（离线 / 忙）——不回"成功" */
    ESP_LOGI(TAG, "H743 SET id=0x%02x v=%.2f rv=%d -> ack st=%u val=%.2f",
             id, v, rv, st, param_get(id));
    link_send_ack(seq, MT_TCS_SET_PARAM, st, param_get(id));
}

/* R1 修复 P0-08：安全锁存落 NVS（失败只记日志，不阻断控制；下次 latch 变化会重试）。
 * X04-F-03：报警位随锁存一起落盘——存的是锁存变化那一刻 g_tcs.alarm_bits 的快照，
 * 上电由 safety_state_last_alarm() 读出作证据（不回灌成实时报警）。*/
static void safety_persist_cb(uint8_t latch)
{
    if (safety_state_save(latch, g_tcs.alarm_bits) != 0) ESP_LOGE(TAG, "safety latch persist failed");
}

void app_main(void)
{
    /* X04-F-03 / R5-13：第一件事。读复位原因、抄出上一轮的运行记录（RTC 慢速存储，魔数 + CRC
     * 校验，冷上电的随机内容会被识破）、打印 [SYSTEM] 启动行，并报告 flash 里是否留着上一次
     * 异常的 core dump。必须排在任何可能动 RTC NOINIT 段的初始化之前。*/
    tcs_bootrec_boot();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_app_desc_t *desc = esp_app_get_description();
    ESP_LOGI(TAG, "=== 调温服 ESP32-S3 bridge boot ===");

    /* R1a（任务书 v11.1）：注册表唯一真值迁至 ESP32。本步只起服务+恢复 NVS；
     * 链路/网页数据源切换在 R2，屏端语义不变。
     * R1b（v11.3）：PF_M207 写入口接 m207 服务（50ms 节流+写后读回）。 */
    param_hooks_t hooks = { .eeprom_commit = nvs_registry_commit,
                            .on_m207_set = op_m207_set_temp,
                            .on_m207_map_set = op_m207_map_set,
                            .on_mode_change = control_on_mode_change,
                            .on_broadcast = esp_on_broadcast,
                            .on_check = control_param_check };   /* 急停/超温锁存期间不得切自动 */
    param_registry_init(&hooks);
    uint8_t registry_alarm = 0;   /* X04-F-07：镜像读失败 / 校验不过 → control_init 之后置 ALM_EEPROM */
    {
        /* P1-12：把"首次上电"与"读失败 / 镜像校验不过"拆开。原先两者合用一句
         * "(defaults, first boot)"——参数损坏后以默认设定继续运行，日志看起来却像出厂。*/
        uint8_t img[EE_IMG_SIZE];
        esp_err_t lr = nvs_registry_load(img);
        int skipped = 0;
        int dr = (lr == ESP_OK) ? param_deserialize_ex(img, &skipped) : -1;
        registry_alarm = param_restore_alarm_needed(lr == ESP_ERR_NVS_NOT_FOUND,
                                                    (uint8_t)(dr == 0 && skipped == 0));
        if (dr == 0)   /* skipped > 0：有记录因类型 / 持久化标志 / 数值不符被拒（M10-F-04），那些参数回默认 → 同样报警 */
            ESP_LOGI(TAG, "R1a registry: %d params restored from NVS (%d record(s) skipped)",
                     param_count(), skipped);
        else if (lr == ESP_ERR_NVS_NOT_FOUND)
            ESP_LOGI(TAG, "R1a registry: %d params loaded (defaults, first boot)", param_count());
        else
            ESP_LOGE(TAG, "R1a registry: saved image unusable (%s%s) -> %d params loaded (DEFAULTS)",
                     esp_err_to_name(lr), lr == ESP_OK ? "; image check failed" : "", param_count());
    }
    ESP_LOGI(TAG, "boot part=%s proj=%s ver=%s", run->label, desc->project_name, desc->version);
    if (strcmp(desc->version, TCS_VER_STR) != 0)   /* X06-F-09：两处都来自 protocol/tcs_version.h，不该不一致 */
        ESP_LOGE(TAG, "version mismatch: app=%s, protocol/tcs_version.h=" TCS_VER_STR, desc->version);

    svc_status_t svc;                       /* P1-12：服务启动结果汇总 */
    svc_init(&svc);
    softap_start();
    svc_note(&svc, SVC_BIT_SOFTAP, softap_probe());
    link_loopback_run();          /* WP-F.1：真机自环自测（独占 UART1，~2.5s，证据=[LPBK]）*/
    /* P1-12 的另一半（i3）：link / web 改成带返回值，启动结果进汇总——任务没起来就不再说 all up */
    svc_note(&svc, SVC_BIT_LINK, link_start());
    link_on_setparam(esp_on_setparam);   /* R2：屏写参数回调装配 */
    link_on_cmd(esp_on_cmd);             /* v2：屏急停/确认 → 本端执行 */
    svc_note(&svc, SVC_BIT_WEB, web_start());   /* 带出 WebSocket 推送任务的结果 */
    /* R1b：M207 服务（UART2 21/47@57600）。回环自测先行（独占 UART2 ~1s，
     * 含物理短接探测），随后常驻服务钉核 1（第八章预算）。 */
    m207_lb_boottest();
    {
        int mrv = m207_service_start();
        svc_note(&svc, SVC_BIT_M207, mrv);
        if (mrv != 0)   /* M12-F-16（e2 转来）：启动失败必须可见 */
            ESP_LOGE(TAG, "M207 service did not start (see the [M207] log) — no M207 link; "
                          "control enters SAFE after the boot grace");
    }
    /* R1c：TMP117 服务（I2C0 8/9@100k）。自测先行（纯逻辑+健康机板上取证），
     * 随后 100ms 采样任务钉核 1（第八章预算 500µs）。硬件未接（T1-T3）=上电即
     * NACK→1s 后掉线沿 ALM_BODY_SENSOR=F4 预期行为；注入走 tmp117_sim_*（R1e console）。 */
    tmp117_lb_boottest();
    svc_note(&svc, SVC_BIT_TMP117, tmp117_service_start());
    /* R1d：风扇（GPIO42 低=开，LEDC 25kHz）。自测含两态电平回读。（M13-F-09，e2 转来：脚是 42 不是 48）*/
    fan_lb_boottest();
    /* R1e：control FSM 服务（ops→tcs_fan/tcs_m207；核 1 100ms 节拍，每拍控制评估 + 布防步进
     * （U 线 E1，原 500ms）+esp_task_wdt 3s 订阅）。console REPL（USB-Serial-JTAG，同口共存）。 */
    control_init(&(control_ops_t){
        .fan_set = fan_set_on,
        .tec_sw = op_tec_sw,
        .tec_temp = op_tec_temp,
        /* P1-09：接上安全关断通道（i3；此前退化成 tec_sw，模块离线时关断送不到执行器）。*/
        .tec_force = op_tec_force,
#if CONFIG_TCS_SIM
        .stuck_exempt = op_stuck_exempt,   /* 注入源在生效的那一路不判卡死（M09-F-09 的注入档口径，i3）*/
#endif
        .on_state = NULL,             /* 迁移沿由 post 步进日志承载 */
        .safety_persist = safety_persist_cb,   /* P0-08：锁存跨复位存活 */
        .safety_load    = safety_state_load,
    });
    /* R1 修复 M09-F-10 / N-01：任务创建失败必须可见，且必须 fail-safe——
     * 没有控制任务就没有闭环与执行器门控，继续跑等于"加热无人监管"。*/
    esp_err_t csr = control_service_start();
    svc_note(&svc, SVC_BIT_CTRL, (int)csr);
    if (csr != ESP_OK) {
        ESP_LOGE(TAG, "control_service_start FAILED (%s) -> fail-safe (estop + TEC off + fan on)",
                 esp_err_to_name(csr));
        tcs_emergency_stop();
        (void)m207_tec(1, 0);
        (void)m207_tec(2, 0);
        fan_set_on(1);
    }
    /* X04-F-07：上电时 NVS 读失败 / 镜像校验不过 → ALM_EEPROM（屏端与网页都有现成文案"参数校验失败"）。
     * 必须放在 control_init 之后：它会把报警位清零。首次上电不算。*/
    if (registry_alarm) param_set_image_ok(0);   /* M09-F-06：镜像坏过，自动布防要先等一次成功落盘 */
    if (registry_alarm) tcs_alarm_raise(ALM_EEPROM);
    /* X04-F-03：随锁存落盘的报警位快照——上一轮出事时屏上 / 网页上亮着什么。只作证据，不回灌。*/
    {
        uint16_t saved_alarm = safety_state_last_alarm();
        if (saved_alarm)
            ESP_LOGW(TAG, "safety record: alarm snapshot at last save = 0x%04X (evidence only, not re-raised)",
                     (unsigned)saved_alarm);
    }
    /* X04-F-03 / R5-13：1Hz 把运行时长 / 报警位 / 锁存 / 控制状态刷回 RTC 记录（纯 RAM 写）*/
    svc_note(&svc, SVC_BIT_BOOTREC, (int)tcs_bootrec_start_refresh());
    svc_note(&svc, SVC_BIT_CONSOLE, tcs_console_start());
    /* R2：0x20 快照（200ms 全量，核 0）+0x21 位图增量。旧屏固件对未知消息号
     * 静默丢弃（switch 无 default）→ 前向兼容；屏端读快照缓存在 R2-H743 侧接。 */
    svc_note(&svc, SVC_BIT_SNAP, tcs_snap_start());
#if CONFIG_TCS_M207_SELFLOOP
    m207_selfloop_run();          /* 取证构建：应答器注入全路径（[M207SL]，~20s）*/
#endif
#if CONFIG_TCS_M207_REALPROBE
    m207_realprobe_run();         /* 真机取证构建：P-2 判活+协议探针+E-06 录帧（[RP]/[P2]/[E06]）*/
#endif
#if CONFIG_TCS_R1E_PROBE
    r1e_probe_run();              /* R1e 取证构建：E-18 前 20 次+E-20/21/22 自驱（[R1E]）*/
#endif
    /* P1-12：汇总报告。有服务没起来 → E 级日志 + 名单（不再说 all up）+ ALM_SERVICE。*/
    if (svc_report(&svc, app_svc_log, NULL) != 0) tcs_alarm_raise(ALM_SERVICE);
}
