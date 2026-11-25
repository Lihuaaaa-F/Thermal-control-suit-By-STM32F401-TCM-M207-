/**
 * m207_proto.h — M207 协议纯函数（ESP32 侧复用件；R1b，任务书 v11.3 第十一章）
 *
 * 逐字提取自 project_tcs/App/tcs/m207.h（H743）纯函数声明区——Q5 单向同步：
 * 改协议逻辑只许改 project_tcs 源侧再复制过来；R4 H743 瘦身后源侧删除即终局。
 * 提取时刻源 md5（2026-10-26）：见 docs/results/R1.md R1b 节。
 * 含 M207_KIND_ERRCODE（正典《现行M207协议_v1.md》§2/§3 CMD:REPLY 分路）。
 */
#ifndef TCS_M207_PROTO_H
#define TCS_M207_PROTO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====== 解析结果 ====== */
#define M207_MOD_NONE  0
#define M207_MOD_TC1   1
#define M207_MOD_TC2   2
#define M207_MOD_PCB     3   /* M12-F-09：同总线非温控模块（原解析器按名字长度直接丢弃）*/
#define M207_MOD_SYSTEM  4
#define M207_MOD_PCRS232 5
#define M207_MOD_MAX     6   /* module 作数组下标时的上界 */

typedef enum {
    M207_KIND_NONE = 0,
    M207_KIND_POLL,        /* '？' 查询（本机下发方向）*/
    M207_KIND_REPLY,       /* '=' 回复/回显 */
    M207_KIND_ERRCODE,     /* CMD:REPLY=N 错误/执行应答（现行M207协议_v1.md §2/§3）*/
} m207_kind_t;

typedef struct {
    uint8_t module;              /* M207_MOD_*（0=未知；1/2=TC1/TC2；3/4/5=PCB/SYSTEM/PCRS232）*/
    char    param[16];           /* "TCACTTEMP"/"TCADJTEMP"/"TCSW"（大写）*/
    m207_kind_t kind;
    float   value;               /* '=' 后的浮点值（缺等号时 0）*/
    uint8_t errcode;             /* KIND_ERRCODE：ERRORCODE 0-8 */
} m207_reply_t;

/* 行长上限（任务书：行长上限 64）*/
#define M207_LINE_MAX 64

/* ====== 纯逻辑 1：行拼装（\r 结尾；超长/溢出安全）======
 * 喂一字节；凑成完整一行（不含 \r）返回 1 且 line 填出（带 '\0'）。
 * M12-F-10：超过行长上限的行丢弃至行尾、不上送截断的前缀（返回 0），之后状态干净。*/
typedef struct {
    uint8_t len;
    uint8_t over;   /* M12-F-10：1=本行已超长，后续字节丢弃至行尾，整行不上送 */
    char    buf[M207_LINE_MAX + 1];
} m207_lineasm_t;

void m207_lineasm_reset(m207_lineasm_t *la);
int  m207_lineasm_feed(m207_lineasm_t *la, uint8_t b, char *line, size_t cap);

/* ====== 纯逻辑 2：协议行解析（自 F401 ProtocolAnalysis 移植改造）======
 * 小写自动大写；`模块:参数=值` → M207_KIND_REPLY；缺冒号/缺等号/空串/超长 → 0。
 * 返回 1=解析成功（out 填出），0=忽略。 */
int m207_parse_line(const char *line, m207_reply_t *out);
const char *m207_mod_name(uint8_t mod);   /* M12-F-09：日志用模块名 */

/* ====== H10 映射表（纯逻辑，PC 可测）====== */
/* 参数 id → M207 指令模板。文档到达后填表即可接入真参数（扩展成本=一行）。
 * 指令名全大写；ch=1/2（TC1/TC2 通道）。 */
typedef struct {
    uint16_t pid;          /* 注册表参数 id（0x100+）*/
    uint8_t  ch;           /* 下发通道 1/2 */
    char     cmd[14];      /* M207 参数名（如 "TCADJTEMP"/占位 "PIDKP"）*/
    uint8_t  decimals;     /* 下发小数位（回读容差 0.5×10^-decimals）*/
    uint8_t  mod;          /* M12-F-05：0=按 ch 组 TC 前缀；非 0=模块号（M207_MOD_*），
                            * 前缀用模块名（PCB:/SYSTEM:/PCRS232:）。追加在末尾，
                            * 旧初始化器（4 项）语义不变。*/
} m207_map_entry_t;

/* 模板格式化（纯函数）：`TC{ch}:{cmd}=%.{}f\r`。返回 0=ok，-1=cap 不足/表项非法 */
int m207_map_format(const m207_map_entry_t *e, float v, char *out, size_t cap);

/* ====== 纯逻辑 3：轮询等待期 ERRCODE 应答处置分类（R1b 轴分离修复）======
 * 原则：CMD:REPLY 应答本身=链路双向活的证据，永不计入通信失败（comm fail 只由
 * 应答窗超时静默判定）。errcode=2 且在等温度查询=参数族被固件隐藏=传感器断线
 * （正典/手册：传感器开路时该通道参数族整体不可见；FMEA F6）。 */
typedef enum {
    M207_POLLERR_UNEXPECTED = 0,   /* 无轮询在途 / 查询不该回的 ACK（1/8）→ 留痕 */
    M207_POLLERR_SENSOR_OPEN,      /* errcode=2 且等待参数==TCACTTEMP → 通道断线态 */
    M207_POLLERR_LINK_ALIVE,       /* 其他 errcode（0/3-7）→ 链路活，留痕不计败 */
} m207_pollerr_act_t;

m207_pollerr_act_t m207_classify_poll_errcode(uint8_t errcode, int waiting,
                                              const char *wait_param);

#ifdef __cplusplus
}
#endif
#endif /* TCS_M207_PROTO_H */
