/**
 * @file ui_format.hpp
 * @brief 介面顯示用的文字與數值換算（對應網頁版 types/ws.ts 與各元件的格式化函式）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#pragma once

#include "core/agent_types.hpp"

#include <QString>

#include <array>
#include <optional>

namespace gcs::ui
{

/// 姿態（度）
struct EulerDegrees
{
    double roll = 0.0;
    double pitch = 0.0;
    double yaw = 0.0;
};

/**
 * @brief 任務階段中文名稱
 * @param flight_mode AgentState.flight_mode
 * @return 中文名稱；未定義的值顯示「模式 N」
 */
QString flight_mode_label(int flight_mode);

/**
 * @brief PX4 原生狀態摘要（網頁版 StatusBar 的 px4StatusLabel）
 * @param state AgentState
 * @return 例如「已解鎖 · 飛行中」
 */
QString px4_status_label(const AgentStateData& state);

/**
 * @brief 四元數轉北為零度、順時針為正的航向
 * @param q 四元數 (w, x, y, z)
 * @return 0～360 度；四元數無效時回傳 std::nullopt
 */
std::optional<double> heading_degrees(const std::array<float, 4>& q);

/**
 * @brief 四元數轉 roll／pitch／yaw（度）
 * @param q 四元數 (w, x, y, z)
 * @return 歐拉角；四元數無效時回傳 std::nullopt
 */
std::optional<EulerDegrees> euler_degrees(const std::array<float, 4>& q);

/**
 * @brief 格式化有限數值，非有限值顯示「—」
 * @param value 數值
 * @param decimals 小數位數
 * @return 文字
 */
QString format_number(double value, int decimals);

/**
 * @brief WGS84 座標是否可在地圖顯示
 * @param position [緯度, 經度, 高度]
 * @return 緯經度皆有限且在合法範圍內
 */
bool has_valid_global_position(const std::array<double, 3>& position);

}  // namespace gcs::ui
