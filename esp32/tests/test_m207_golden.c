/**
 * test_m207_golden.c — 工程质量三：golden RX 语料 → 固件解析器逐条断言
 * （sim 侧同一语法的 TX 向断言在 tests/test_m207_adv.py，语料 m207_golden.csv。
 *   两实现各守一方向，语法层由《现行M207协议_v1.md》正典与 PDF 8.2 示例三方锚定。）
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "m207_proto.h"

int main(void)
{
    FILE *f = fopen("tests/m207_golden_rx.csv", "r");
    if (!f) { printf("test_m207_golden: FAIL (corpus not found)\n"); return 1; }

    char line[256];
    int total = 0, fail = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;

        /* frame|ok|module|kind|param|num（字段可空，手写分段）*/
        char *fields[6] = {0};
        int nf = 0;
        char *cur = line;
        fields[nf++] = cur;
        for (char *p = line; *p && nf < 6; p++) {
            if (*p == '|') { *p = '\0'; fields[nf++] = p + 1; }
        }
        if (nf != 6) {
            printf("  BAD CORPUS LINE (%d fields): %s\n", nf, line);
            fail++; continue;
        }
        total++;

        char *frame = fields[0];
        int ok = atoi(fields[1]);
        int module = atoi(fields[2]);
        int kind = atoi(fields[3]);
        const char *param = fields[4];
        float num = (float)atof(fields[5]);

        m207_reply_t r;
        memset(&r, 0, sizeof(r));
        int parsed = m207_parse_line(frame, &r);
        if (parsed != ok) {
            printf("  MISMATCH: %-24s parse=%d expect=%d\n", frame, parsed, ok);
            fail++; continue;
        }
        if (!ok) continue;
        if (r.module != module || r.kind != kind
            || strcmp(r.param, param) != 0) {
            printf("  MISMATCH: %-24s got mod=%d kind=%d param=%s\n", frame,
                   r.module, r.kind, r.param);
            fail++; continue;
        }
        float actual = (r.kind == M207_KIND_ERRCODE) ? (float)r.errcode : r.value;
        if (actual != num) {
            printf("  MISMATCH: %-24s got num=%f expect=%f\n", frame,
                   (double)actual, (double)num);
            fail++; continue;
        }
    }
    fclose(f);
    printf("test_m207_golden: corpus=%d fail=%d\n", total, fail);
    printf("===================\n");
    if (fail == 0) { printf("test_m207_golden: PASS\n"); return 0; }
    printf("test_m207_golden: FAIL (%d)\n", fail);
    return 1;
}
