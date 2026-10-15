/**
 * @file log_file.hpp
 * @brief 日誌檔：將 Qt logging 訊息（info 以上）附加寫入依日期分檔的文字檔
 * @author Taiphoon GCS Team
 * @date 2026-10-15
 *
 * 安裝後會攔截全程式的 Qt 訊息，寫入檔案後再交給原本的處理器（主控台或除錯器輸出不受影響）。
 * 訊息可能來自 DDS、核心與 UI 任一執行緒，寫入以互斥鎖保護。
 */
#pragma once

#include <QString>

namespace gcs::log_file
{

/**
 * @brief 安裝日誌檔訊息處理器
 * @param directory 日誌目錄，不存在時自動建立；檔名為 Taiphoon_QGCS_yyyyMMdd.log（本地日期）
 * @return 目錄可建立時為 true；已安裝時更換目錄並回傳 true
 */
bool install(const QString& directory);

/** @brief 移除日誌檔訊息處理器並關閉檔案，恢復原本的處理器 */
void uninstall();

/**
 * @brief 目前寫入中的日誌檔路徑
 * @return 尚未安裝或尚未寫入時為空字串
 */
QString current_path();

}  // namespace gcs::log_file
