/**
 * @file geofence_file.hpp
 * @brief 地理圍籬檔案：把頂點與 margin 存成 JSON 檔，以及從檔案讀回
 * @author Taiphoon GCS Team
 * @date 2026-11-18
 *
 * 純函式、無狀態。檔案只保存頂點（Agent 回報的 WGS84 座標，不經地圖顯示原點換算）與 margin，
 * 不含版本號；讀回的內容只會成為介面上的草稿，仍須經 GeofenceModule 與 GuardModule 才會發布。
 * 讀檔只檢查格式、頂點數與座標範圍；自交、面積等幾何規則由草稿驗證（geofence_geometry）提示。
 *
 * 檔案格式：
 * {
 *   "format": "taiphoon-geofence",
 *   "version": 1,
 *   "margin_m": 0.5,
 *   "vertices": [{"latitude_deg": 25.0, "longitude_deg": 121.5}, ...]
 * }
 */
#pragma once

#include "core/agent_types.hpp"

#include <QByteArray>
#include <QString>

#include <vector>

namespace gcs::geofence_file
{

/// 檔案的 format 欄位
inline const QString FORMAT_NAME = QStringLiteral("taiphoon-geofence");
/// 目前寫出的格式版本；讀檔接受 1 到此版本
inline constexpr int FORMAT_VERSION = 1;
/// 檔案大小上限（位元組）；64 個頂點的檔案約 5 KB，超過視為選錯檔案
inline constexpr qint64 MAX_FILE_BYTES = 1024 * 1024;

/// 檔案保存的圍籬內容
struct Content
{
    /// 依序相連、不重複第一點（Agent 回報的 WGS84 座標）
    std::vector<GeofenceVertexData> vertices;
    /// 機體中心與邊界的最小水平距離（公尺）
    double margin_m = geofence_limits::DEFAULT_MARGIN_M;
};

/**
 * @brief 圍籬內容轉為 JSON 文字
 * @param content 圍籬內容
 * @return UTF-8 的 JSON（含縮排）
 */
QByteArray to_json(const Content& content);

/**
 * @brief 解析 JSON 文字
 * @param json UTF-8 的 JSON
 * @param content 解析結果；失敗時不修改
 * @return 錯誤原因；成功時為空字串
 */
QString from_json(const QByteArray& json, Content* content);

/**
 * @brief 寫入圍籬檔案（先寫暫存檔再取代，失敗時不破壞原檔）
 * @param path 檔案路徑
 * @param content 圍籬內容
 * @return 錯誤原因；成功時為空字串
 */
QString write(const QString& path, const Content& content);

/**
 * @brief 讀取圍籬檔案
 * @param path 檔案路徑
 * @param content 讀取結果；失敗時不修改
 * @return 錯誤原因；成功時為空字串
 */
QString read(const QString& path, Content* content);

}  // namespace gcs::geofence_file
