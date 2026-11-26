/* test_param_registry.c — 参数注册表 PC 单测（任务书 §5.1 向量）
 * 覆盖：clamp 边界、body_cold<body_hot 拒写、dirty 触发、EEPROM 序列化往返、
 *       CRC 坏→默认值、容量上限（30 物理上限 / 31 口径拒绝）。
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "param_registry.h"
#include "tcs_proto.h"   /* ALM_* 位（X01-F-02 合并断言）*/
#include "proto.h"          /* proto_crc8 */

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

static int commit_cnt;
static int stub_commit(const uint8_t *img) { (void)img; commit_cnt++; return 0; }

int main(void)
{
    param_registry_init(&(param_hooks_t){ .eeprom_commit = stub_commit });

    /* 初值 */
    CHECK(param_get(PARAM_ID_TC1_SET) == 25.0f, "default tc1");
    CHECK(param_get(PARAM_ID_BODY_HOT) == 37.5f, "default body_hot");
    CHECK(param_get(PARAM_ID_MODE) == 0.0f, "default mode manual (F9)");
    /* P1-13：0x127（BTN 动作项）入表后 51 条；条数由初始化器决定（X01-F-06）
     * m1（模式页，2026-11-26）：再加 0x09 / 0x0A 两路 TEC 手动输出 → 53 条 */
    CHECK(param_count() == 53, "registry count=53 (incl. 0x127 BTN + 0x09/0x0A manual TEC)");
    {
        const param_desc_t *m1 = param_find(PARAM_ID_TEC1_MANUAL);
        const param_desc_t *m2 = param_find(PARAM_ID_TEC2_MANUAL);
        CHECK(m1 && m2 && m1->type == PARAM_TYPE_U8 && m2->type == PARAM_TYPE_U8,
              "0x09/0x0A registered as U8");
        CHECK(m1 && (m1->flags & PF_RW) && (m1->flags & PF_SCREEN)
              && !(m1->flags & PF_PERSIST) && !(m1->flags & PF_WEB),
              "0x09: RW + SCREEN, not persisted, not on web (mirror of the ESP32 table)");
        CHECK(m1 && m2 && m2->flags == m1->flags, "0x0A: same flags as 0x09");
        CHECK(param_get(PARAM_ID_TEC1_MANUAL) == 0.0f && param_get(PARAM_ID_TEC2_MANUAL) == 0.0f,
              "manual outputs default to pause");
        CHECK(param_set(PARAM_ID_TEC1_MANUAL, 2.0f, PARAM_SRC_TOUCH) == 0
              && param_get(PARAM_ID_TEC1_MANUAL) == 2.0f,
              "screen registry takes the write (it travels to the ESP32 as 0x12)");
    }
    {
        const param_desc_t *bt = param_find(0x127);
        CHECK(bt && bt->type == PARAM_TYPE_BTN && bt->flags == 0,
              "0x127 is BTN, not writable");
        CHECK(param_set(0x127, 0.0f, PARAM_SRC_CONSOLE) == -1,
              "0x127 rejected by param_set (action, no value write)");
        CHECK(bt && strcmp(bt->key, "m207_pushlist") == 0, "0x127 key canon");
        const param_desc_t *ki = param_find(0x101);
        const param_desc_t *kd = param_find(0x102);
        CHECK(ki && strcmp(ki->unit, "s") == 0 && kd && strcmp(kd->unit, "s") == 0,
              "0x101/0x102 unit=s (canon 2.5)");
    }

    /* commissioned 0x08（WP-A.5）：u8 默认 0=未布防 */
    CHECK(param_get(PARAM_ID_COMMISSIONED) == 0.0f, "default unarmed");
    CHECK(param_set(PARAM_ID_COMMISSIONED, 1.0f, 1) == 0
          && param_get(PARAM_ID_COMMISSIONED) == 1.0f, "arm=1");
    CHECK(param_set(PARAM_ID_COMMISSIONED, 0.0f, 1) == 0
          && param_get(PARAM_ID_COMMISSIONED) == 0.0f, "disarm back to 0");

    /* clamp 边界：5.0 / 45.0 / 45.01→45.0 */
    CHECK(param_set(PARAM_ID_TC1_SET, 5.0f, 1) == 0, "clamp low ok");
    CHECK(param_get(PARAM_ID_TC1_SET) == 5.0f, "clamp low value");
    CHECK(param_set(PARAM_ID_TC1_SET, 4.99f, 1) == 1, "clamp low clamped rv");
    CHECK(param_get(PARAM_ID_TC1_SET) == 5.0f, "clamp low value2");
    CHECK(param_set(PARAM_ID_TC1_SET, 45.0f, 1) == 0, "clamp high ok (红线 45)");
    CHECK(param_set(PARAM_ID_TC1_SET, 45.01f, 1) == 1, "clamp high rv");
    CHECK(param_get(PARAM_ID_TC1_SET) == 45.0f, "clamp high value (拒绝意义)");
    CHECK(param_set(PARAM_ID_TC1_SET, 999.0f, 1) == 1 && param_get(PARAM_ID_TC1_SET) == 45.0f,
          "clamp far");

    /* 坏 id */
    CHECK(param_set(0x5555, 1.0f, 1) == -1, "bad id");
    /* 相等短路 */
    CHECK(param_set(PARAM_ID_TC1_SET, 45.0f, 1) == 0, "equal short-circuit");

    /* body_cold < body_hot 联动校验 */
    CHECK(param_set(PARAM_ID_BODY_HOT, 36.0f, 1) == -2, "hot<=cold reject");  /* cold=36.5 */
    CHECK(param_set(PARAM_ID_BODY_COLD, 37.6f, 1) == -2, "cold>=hot reject");
    CHECK(param_set(PARAM_ID_BODY_HOT, 37.5f, 1) == 0, "hot set ok");
    CHECK(param_set(PARAM_ID_BODY_COLD, 36.5f, 1) == 0, "cold set ok");
    CHECK(param_set(PARAM_ID_BODY_HOT, 36.5f, 1) == -2, "hot==cold reject");

    /* mode u8 量化 */
    CHECK(param_set(PARAM_ID_MODE, 1.0f, 1) == 0 && param_get(PARAM_ID_MODE) == 1.0f, "mode=1");
    CHECK(g_tcs.mode == 1, "mode mirror");

    /* dirty + 防抖提交 */
    commit_cnt = 0;
    CHECK(param_set(PARAM_ID_TC2_SET, 30.0f, 1) == 0, "set tc2");
    param_desc_t *p = param_find(PARAM_ID_TC2_SET);
    CHECK(p->dirty_persist == 1, "dirty set");
    param_persist_flush(1000);            /* 起点 */
    CHECK(commit_cnt == 0, "debounce not yet");
    param_persist_flush(3001);            /* 2s 后 */
    CHECK(commit_cnt == 1, "committed after debounce");
    CHECK(p->dirty_persist == 0, "dirty cleared");

    /* 立即固化 */
    CHECK(param_set(PARAM_ID_HYST, 0.3f, 1) == 0, "set hyst");
    CHECK(param_persist_now() == 0 && commit_cnt == 2, "persist now");
    CHECK(param_persist_now() == 1 && commit_cnt == 2, "no dirty -> skip");

    /* EEPROM 序列化 → 反序列化往返 */
    {
        uint8_t img[EE_IMG_SIZE];
        CHECK(param_serialize(img) == 0, "serialize");
        CHECK(img[0] == 0xA6 && img[1] == 2 && img[2] == 8, "header magic/ver/count (10 持久化参数)");
        CHECK(img[3] == 0, "rfu");
        /* 记录 1：id=0x01 type=0(f32) value=45.0 位型小端 */
        CHECK(img[4] == 0x01 && img[5] == 0x00 && img[6] == 0 && img[7] == 0, "rec1 id/type");
        float f = 45.0f;
        uint8_t fb[4]; memcpy(fb, &f, 4);
        CHECK(img[8] == fb[0] && img[9] == fb[1] && img[10] == fb[2] && img[11] == fb[3],
              "rec1 f32 LE bits");
        /* CRC 覆盖 0x00..0xF7 */
        CHECK(img[0xF8] == proto_crc8(img, 0xF8), "crc8 at 0xF8");

        /* 改值 → 序列化 → 全默认 → 反序列化 → 值恢复 */
        CHECK(param_set(PARAM_ID_BODY_HOT, 38.0f, 1) == 0, "change hot");
        CHECK(param_serialize(img) == 0, "re-serialize");
        param_load_defaults();
        CHECK(param_get(PARAM_ID_BODY_HOT) == 37.5f, "defaults restored");
        CHECK(param_deserialize(img) == 0, "deserialize");
        CHECK(param_get(PARAM_ID_BODY_HOT) == 38.0f, "roundtrip hot");
        CHECK(param_get(PARAM_ID_TC2_SET) == 30.0f, "roundtrip tc2");
        CHECK(param_get(PARAM_ID_HYST) == 0.3f, "roundtrip hyst");
        CHECK(param_get(PARAM_ID_MODE) == 1.0f && g_tcs.mode == 1, "roundtrip mode");

        /* CRC 坏 → 拒绝（-1），不改当前值 */
        img[0x10] ^= 0xFF;
        CHECK(param_deserialize(img) == -1, "corrupt crc rejected");
        CHECK(param_get(PARAM_ID_BODY_HOT) == 38.0f, "value intact on crc fail");
        /* magic 坏 */
        img[0x10] ^= 0xFF;
        img[0] = 0xA5;
        CHECK(param_deserialize(img) == -1, "bad magic rejected");
        /* ver 坏 */
        img[0] = 0xA6;
        img[1] = 1;
        CHECK(param_deserialize(img) == -1, "bad ver rejected");
    }

    /* 容量上限：0x04..0xF7 共 244B = 30 条物理上限；31 条口径拒绝 */
    {
        /* 直接构造 31 条记录的镜像 → deserialize 拒绝 */
        uint8_t img[EE_IMG_SIZE];
        memset(img, 0, sizeof(img));
        img[0] = 0xA6; img[1] = 2; img[2] = 31;
        for (int i = 0; i < 31; i++) {
            int o = 4 + i * 8;
            if (o + 8 > 0xF8) break;   /* 31 条必然越界 CRC 区，构造到物理允许为止 */
            img[o] = 0x01;             /* id 重复没关系，只测容量判定 */
        }
        img[0xF8] = proto_crc8(img, 0xF8);
        CHECK(param_deserialize(img) == -1, "31 records rejected (0x04+8*31=0xFC 越界 CRC 区)");
        img[2] = 30;
        img[0xF8] = proto_crc8(img, 0xF8);
        CHECK(param_deserialize(img) == 0, "30 records accepted");
    }

    /* PARAM_EEPROM_MAX 口径常量在位 */
    CHECK(PARAM_EEPROM_MAX == 31, "spec count cap 31");

    /* R1 P1-05 判别性：NaN 必须在入口被拒（旧实现会穿过 clamp 写入并持久化）*/
    {
        float before = param_get(PARAM_ID_BODY_HOT);
        int rv = param_set(PARAM_ID_BODY_HOT, NAN, 4);
        CHECK(rv == -2, "NAN rejected at param_set");
        CHECK(param_get(PARAM_ID_BODY_HOT) == before, "NAN does not change value");
        param_link_sync(PARAM_ID_BODY_HOT, NAN);
        CHECK(param_get(PARAM_ID_BODY_HOT) == before, "NAN ignored by link_sync");
    }

    /* v2 参数镜像落表（ESP32 权威 → 屏）：可写值生效、NaN/未知 id 拒绝、clamp 生效 */
    {
        CHECK(param_mirror_apply(PARAM_ID_BODY_HOT, 38.0f) == 0, "mirror ok");
        CHECK(param_get(PARAM_ID_BODY_HOT) == 38.0f, "mirror applied");
        CHECK(param_mirror_apply(PARAM_ID_BODY_HOT, 99.0f) == 0
              && param_get(PARAM_ID_BODY_HOT) <= 40.0f, "mirror clamps");
        CHECK(param_mirror_apply(PARAM_ID_BODY_HOT, NAN) == -1, "mirror rejects NaN");
        CHECK(param_mirror_apply(0x999, 1.0f) == -1, "mirror rejects unknown id");
        CHECK(param_mirror_apply(0x127, 1.0f) == -1, "mirror rejects BTN (no value semantics)");
    }

    /* M05-F-02：恢复默认后必须真正落盘（load_defaults 会清 dirty，原实现静默不写）*/
    {
        param_set(PARAM_ID_BODY_HOT, 38.5f, PARAM_SRC_CONSOLE);
        (void)param_persist_flush(UINT32_MAX);          /* 先让脏值排出 */
        int before = commit_cnt;
        param_load_defaults();
        CHECK(param_get(PARAM_ID_BODY_HOT) == 37.5f, "defaults restored");
        int rv = param_persist_all();
        CHECK(rv == 0, "persist_all writes");
        CHECK(commit_cnt == before + 1, "eeprom_commit called once");
        /* 再调一次不应重复写（已无脏）*/
        CHECK(param_persist_now() == 1, "no dirty after persist_all");
    }

    /* 2026-11-23 裁决：布防模式 0x08 属危险参数（网页改需二次确认） */
    {
        const param_desc_t *cm = param_find(PARAM_ID_COMMISSIONED);
        CHECK(cm && (cm->flags & PF_DANGER), "0x08 commissioned is PF_DANGER");
    }

    /* r7f（独立复核 R7-12，2026-11-26）：交付档邮箱 p set 的拒绝表——执行器类参数（0x06 控制模式 /
     * 0x09、0x0A 两路手动输出）在交付档一律拒（等效于交付档删掉的 mode / tec / fan）；
     * 台架档放行（台架要能直控）；非执行器参数两档都放行（屏幕调参页本来就能写）。*/
    {
        CHECK(param_mailbox_blocked(PARAM_ID_MODE, 1) == 1, "R7-12 prod: p set 0x06 mode is refused");
        CHECK(param_mailbox_blocked(PARAM_ID_TEC1_MANUAL, 1) == 1, "R7-12 prod: p set 0x09 TEC1 output is refused");
        CHECK(param_mailbox_blocked(PARAM_ID_TEC2_MANUAL, 1) == 1, "R7-12 prod: p set 0x0A TEC2 output is refused");
        CHECK(param_mailbox_blocked(PARAM_ID_MODE, 0) == 0 && param_mailbox_blocked(PARAM_ID_TEC1_MANUAL, 0) == 0
              && param_mailbox_blocked(PARAM_ID_TEC2_MANUAL, 0) == 0,
              "R7-12 bench: the same ids stay writable (bench needs direct control)");
        CHECK(param_mailbox_blocked(PARAM_ID_BODY_HOT, 1) == 0
              && param_mailbox_blocked(PARAM_ID_COMMISSIONED, 1) == 0,
              "R7-12 prod: non-actuator parameters are still writable");
    }

    /* X01-F-02：对端快照不得抹掉本机独占报警位 */
    {
        const uint16_t own = ALM_EEPROM;
        CHECK(alarm_merge_peer(0, own, ALM_EEPROM) == ALM_EEPROM, "local bit survives peer 0");
        CHECK(alarm_merge_peer(ALM_OVERTEMP, own, 0) == ALM_OVERTEMP, "peer bits accepted");
        CHECK(alarm_merge_peer(0xFFFF, own, ALM_EEPROM) == 0xFFFF, "peer all + local");
    }

    printf("test_param_registry: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
