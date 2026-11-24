cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# 固件版本（进 esp_app_desc_t.version）：网页"设备"页与屏幕连接页都显示它。
# 此前从未设置，自报版本是空串（2026-11-24 真机：屏幕"主控固件"一栏只能显示 "--"）。
# U 线改动（恢复逻辑 / 0x22 设备信息 / 新网页）验收前标 rc；验收通过后去掉 -rc1。
set(PROJECT_VER "1.3.0-rc1")
project(tcs_wireless)
