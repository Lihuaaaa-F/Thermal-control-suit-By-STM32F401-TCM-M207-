cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# 固件版本（进 esp_app_desc_t.version）：网页"设备"页与屏幕连接页都显示它。
# 唯一源是 protocol/tcs_version.h（S 线 e3a，审查条目 X06-F-09）：改版本只改那一个文件，
# 或用 tools/version_bump.py --set X.Y.Z[-后缀]；这里不再出现任何版本号。
# 数值化的 fw_ver（屏幕的版本串）也从同一个头文件推导，同一个固件不会再有两个版本号。
# （历史：此前从未设置，自报版本是空串——2026-11-24 真机屏幕"主控固件"一栏只能显示 "--"。）
file(READ "${CMAKE_CURRENT_LIST_DIR}/../protocol/tcs_version.h" TCS_VER_H)
string(REGEX MATCH "TCS_VER_MAJOR[ \t]+([0-9]+)" _ "${TCS_VER_H}")
set(TCS_VER_MAJOR_CMAKE "${CMAKE_MATCH_1}")
string(REGEX MATCH "TCS_VER_MINOR[ \t]+([0-9]+)" _ "${TCS_VER_H}")
set(TCS_VER_MINOR_CMAKE "${CMAKE_MATCH_1}")
string(REGEX MATCH "TCS_VER_PATCH[ \t]+([0-9]+)" _ "${TCS_VER_H}")
set(TCS_VER_PATCH_CMAKE "${CMAKE_MATCH_1}")
string(REGEX MATCH "TCS_VER_SUFFIX[ \t]+\"([^\"]*)\"" _ "${TCS_VER_H}")
set(TCS_VER_SUFFIX_CMAKE "${CMAKE_MATCH_1}")
if(NOT TCS_VER_MAJOR_CMAKE MATCHES "^[0-9]+$" OR NOT TCS_VER_MINOR_CMAKE MATCHES "^[0-9]+$"
   OR NOT TCS_VER_PATCH_CMAKE MATCHES "^[0-9]+$")
    message(FATAL_ERROR "protocol/tcs_version.h 里读不出版本号（TCS_VER_MAJOR / MINOR / PATCH）")
endif()
set(PROJECT_VER "${TCS_VER_MAJOR_CMAKE}.${TCS_VER_MINOR_CMAKE}.${TCS_VER_PATCH_CMAKE}${TCS_VER_SUFFIX_CMAKE}")
project(tcs_wireless)
