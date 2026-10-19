#!/bin/bash
# shot.sh — mock 批量截图（K0 候选/K7 视觉包/K9 diff 共用）
# 用法: bash mock/shot.sh <html路径或file://URL> <输出png路径(正斜杠)>
# 1024×600 固定（屏端 mock）；网页端 375px 用 mock/shot_web.sh
CHROME="C:/Program Files/Google/Chrome/Application/chrome.exe"
URL="$1" OUT="$2"
case "$URL" in file://*) ;; http*) ;; *) URL="file:///$URL";; esac
"$CHROME" --headless=new --disable-gpu --hide-scrollbars \
  --window-size=1024,600 --force-device-scale-factor=1 \
  --screenshot="$OUT" "$URL" 2>&1 | tail -1
