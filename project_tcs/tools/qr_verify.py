#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""qr_verify.py — H13 二维码解码回读验证（PC 单测后置步 + 屏端 fb_dump 扫码实测）

用法：
  python tools/qr_verify.py <图像> [期望内容]
  期望内容缺省 = http://192.168.4.1
图像支持 PBM/PNG（PIL 读取，zxing-cpp 解码）。退出码 0=解码内容与期望一致。
"""
import sys
from PIL import Image

EXPECT_DEFAULT = "http://192.168.4.1"


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: qr_verify.py <image> [expect]")
        return 2
    path = sys.argv[1]
    expect = sys.argv[2] if len(sys.argv) > 2 else EXPECT_DEFAULT
    import zxingcpp
    img = Image.open(path)
    results = zxingcpp.read_barcodes(img)
    if not results:
        print(f"qr_verify FAIL: no barcode found in {path}")
        return 1
    texts = [r.text for r in results]
    print(f"qr_verify decoded: {texts}")
    if expect not in texts:
        print(f"qr_verify FAIL: expect {expect!r}")
        return 1
    print("qr_verify PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
