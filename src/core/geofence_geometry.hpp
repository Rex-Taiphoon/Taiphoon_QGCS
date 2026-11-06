/**
 * @file geofence_geometry.hpp
 * @brief 地理圍籬幾何：WGS84 多邊形的合法性、包含判斷、與邊界距離及 margin 內縮
 * @author Taiphoon GCS Team
 * @date 2026-11-06
 *
 * 純函式、無狀態。GuardModule 以此驗證圍籬設定與預檢航點，介面以同一份規則提示草稿是否可套用與畫出 margin。
 * 場域只有數十至數千公尺，以第一個頂點為原點做局部平面（等距圓柱）近似，誤差遠小於 margin；
 * 最終仍以 Agent 判定為準（介面文件 4.4）。
 */
#pragma once

#include "core/agent_types.hpp"

#include <QString>

#include <vector>

namespace gcs::geofence_geometry
{

/// 局部平面座標（公尺）
struct LocalPoint
{
    double east_m = 0.0;
    double north_m = 0.0;
};

/// QGCS 端的最小面積（平方公尺）；Agent 也會拒絕面積過小的圍籬，門檻未公開，這裡只排除退化多邊形
inline constexpr double MIN_AREA_M2 = 1.0;

/**
 * @brief WGS84 位置換算為以 origin 為原點的局部平面座標
 * @param point 位置
 * @param origin 原點
 * @return 局部平面座標
 */
LocalPoint to_local(const GeofenceVertexData& point, const GeofenceVertexData& origin);

/**
 * @brief 局部平面座標換回 WGS84
 * @param point 局部平面座標
 * @param origin 原點
 * @return WGS84 位置
 */
GeofenceVertexData to_global(const LocalPoint& point, const GeofenceVertexData& origin);

/**
 * @brief 多邊形是否自交（任兩條不相鄰的邊相交或重疊，或有長度為 0 的邊）
 * @param vertices 依序相連的頂點，不重複第一點
 * @return 是否自交；少於 3 點時為 false
 */
bool is_self_intersecting(const std::vector<GeofenceVertexData>& vertices);

/**
 * @brief 多邊形面積
 * @param vertices 依序相連的頂點
 * @return 面積（平方公尺）；少於 3 點時為 0
 */
double area_m2(const std::vector<GeofenceVertexData>& vertices);

/**
 * @brief 驗證圍籬設定內容（與 Agent 的檢查一致：頂點數、座標、自交、面積與 margin 範圍）
 * @param vertices 頂點
 * @param margin_m margin（公尺）
 * @return 錯誤原因；合法時為空字串
 */
QString validate(const std::vector<GeofenceVertexData>& vertices, float margin_m);

/**
 * @brief 點是否在多邊形內（邊界上視為在內）
 * @param polygon 頂點
 * @param point 位置
 * @return 是否在內；少於 3 點時為 false
 */
bool contains(const std::vector<GeofenceVertexData>& polygon, const GeofenceVertexData& point);

/**
 * @brief 點到多邊形邊界的最短水平距離
 * @param polygon 頂點
 * @param point 位置
 * @return 距離（公尺）；少於 2 點時為 0
 */
double distance_to_boundary_m(const std::vector<GeofenceVertexData>& polygon, const GeofenceVertexData& point);

/**
 * @brief 點是否在圍籬內且與邊界至少相距 margin
 * @param polygon 頂點
 * @param margin_m margin（公尺）
 * @param point 位置
 * @return 是否符合
 */
bool is_inside_with_margin(const std::vector<GeofenceVertexData>& polygon, double margin_m,
                           const GeofenceVertexData& point);

/**
 * @brief 依 margin 向內縮的多邊形（顯示用）
 *
 * 每條邊向內平移 margin 後與相鄰邊求交點；凹角很尖或 margin 大於場域時結果會自交，此時回傳空清單不顯示。
 *
 * @param vertices 頂點
 * @param margin_m margin（公尺）
 * @return 內縮後的頂點；margin 為 0 或無法內縮時為空
 */
std::vector<GeofenceVertexData> inset(const std::vector<GeofenceVertexData>& vertices, double margin_m);

}  // namespace gcs::geofence_geometry
