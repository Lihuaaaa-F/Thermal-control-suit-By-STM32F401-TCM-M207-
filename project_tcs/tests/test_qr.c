/* test_qr.c — H13 最小渲染验证（PC）：qrcodegen 裁剪版编码 → PBM 出图 →
 * 由 run_all.sh 调 tools/qr_verify.py(zxing-cpp) 解码回读比对。
 * 同时覆盖裁剪回归：ECI 移除后 BYTE 模式编码不受影响。
 */
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "qr/qrcodegen.h"

static int write_pbm(const char *path, const uint8_t *qrc)
{
    int n = qrcodegen_getSize(qrc);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P4\n%d %d\n", n, n);
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; ) {
            unsigned char b = 0;
            for (int k = 0; k < 8 && x < n; k++, x++)
                if (qrcodegen_getModule(qrc, x, y)) b |= (0x80 >> k);
            fputc(b, f);
        }
    }
    fclose(f);
    return n;
}

int main(void)
{
    int fail = 0, checks = 0;  /* X06-F-06：断言数基线门禁的取数口 */
    const char *url = "http://192.168.4.1";
    uint8_t qrc[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
    uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];

    bool ok = qrcodegen_encodeText(url, tmp, qrc, qrcodegen_Ecc_MEDIUM,
                                   2, 6, -1, true);
    checks++;
    if (!ok) { printf("FAIL: encode %s\n", url); return 1; }
    int n = qrcodegen_getSize(qrc);
    printf("qr encoded ver<=%d size=%d\n", 6, n);
    checks++;
    if (n < 21 || n > 41) { printf("FAIL: bad size %d\n", n); fail++; }

    /* 黑模块占比 sanity（URL 二维码约 40~50%）*/
    int black = 0;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            if (qrcodegen_getModule(qrc, x, y)) black++;
    float ratio = (float)black / (n * n);
    checks++;
    if (ratio < 0.30f || ratio > 0.60f) { printf("FAIL: black ratio %.2f\n", ratio); fail++; }

    /* 出图给解码器回读（run_all 后置步骤）*/
    int w = write_pbm("build/test_qr.pbm", qrc);
    checks++;
    if (w <= 0) { printf("FAIL: write pbm\n"); return 1; }

    /* 裁剪回归：BYTE 模式长文本（边界字符）仍可编码 */
    bool ok2 = qrcodegen_encodeText("http://192.168.4.1/?q=A1+z", tmp, qrc,
                                    qrcodegen_Ecc_MEDIUM, 1, 10, -1, true);
    checks++;
    if (!ok2) { printf("FAIL: byte-mode regression\n"); fail++; }

    printf("test_qr: PASS=%d FAIL=%d\n", checks - fail, fail);
    return fail;
}
