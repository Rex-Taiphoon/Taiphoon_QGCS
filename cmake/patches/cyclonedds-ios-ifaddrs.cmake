# =============================================================================
# @file cyclonedds-ios-ifaddrs.cmake
# @brief CycloneDDS 0.10.5 iOS 修補：介面類型判斷改用「類型未知」分支（ADR-0017）
# @author Taiphoon GCS Team
# @date 2026-10-26
#
# ddsrt 的 Apple 分支以 net/if_media.h 判斷有線／Wi-Fi，iOS SDK 沒有這個標頭而無法編譯。
# 介面類型只影響 CycloneDDS 的介面排序與 Wi-Fi 上 multicast 的可靠度判斷；iOS 版只用單播，影響可忽略。
# 只在 iOS 建置時由 FetchContent 的 PATCH_COMMAND 於 CycloneDDS 原始碼目錄執行；可重複執行。
# =============================================================================
set(GCS_PATCH_FILE "src/ddsrt/src/ifaddrs/posix/ifaddrs.c")
set(GCS_PATCH_ORIGINAL "#elif defined(__APPLE__) || defined(__QNXNTO__) || defined(__FreeBSD__)")
set(GCS_PATCH_REPLACEMENT "#elif 0 /* Taiphoon_QGCS iOS patch: iOS SDK has no net/if_media.h */")

file(READ "${GCS_PATCH_FILE}" GCS_PATCH_CONTENT)
string(FIND "${GCS_PATCH_CONTENT}" "${GCS_PATCH_REPLACEMENT}" GCS_PATCH_DONE)
if(NOT GCS_PATCH_DONE EQUAL -1)
    return()
endif()
string(FIND "${GCS_PATCH_CONTENT}" "${GCS_PATCH_ORIGINAL}" GCS_PATCH_AT)
if(GCS_PATCH_AT EQUAL -1)
    message(FATAL_ERROR "CycloneDDS iOS 修補失敗：${GCS_PATCH_FILE} 找不到預期的內容，請確認 CycloneDDS 版本")
endif()
string(REPLACE "${GCS_PATCH_ORIGINAL}" "${GCS_PATCH_REPLACEMENT}" GCS_PATCH_CONTENT "${GCS_PATCH_CONTENT}")
file(WRITE "${GCS_PATCH_FILE}" "${GCS_PATCH_CONTENT}")
message(STATUS "已套用 CycloneDDS iOS 修補：${GCS_PATCH_FILE}")
