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
 * @brief 地圖顯示原點：把 Agent 回報的 WGS84 位置平移到實際場地，只影響畫面
 *
 * 兩組座標指同一個參考點：source 是它在 Agent 回報座標中的位置（例如 Nooploop UWB 固定在深圳的原點），
 * display 是它在地圖上應顯示的位置。
 */
struct DisplayOrigin
{
    bool is_enabled = false;
    double source_latitude = 0.0;
    double source_longitude = 0.0;
    double display_latitude = 0.0;
    double display_longitude = 0.0;
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
 * @brief PX4 原生飛行模式名稱（VehicleStatus.nav_state，名稱與 QGroundControl 一致）
 * @param state AgentState
 * @return 例如「Offboard」「Position」；PX4 狀態無效時為「未確認」，未定義的值為「未知 N」
 */
QString px4_mode_label(const AgentStateData& state);

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
 * @brief 格式化有限數值並一律帶正負號（負號使用 U+2212），非有限值顯示「—」
 * @param value 數值
 * @param decimals 小數位數
 * @return 文字，例如 "+12.4"、"−3.1"
 */
QString format_signed_number(double value, int decimals);

/**
 * @brief 剩餘電量百分比（整數），PX4 BatteryStatus.remaining 的無效值 -1 顯示「—」
 * @param remaining 剩餘電量比例 0.0～1.0
 * @return 文字，例如 "87"
 */
QString format_battery_percent(float remaining);

/**
 * @brief 電池電壓（一位小數），PX4 BatteryStatus.voltage_v 的無效值 0 顯示「—」
 * @param voltage_v 電壓（V）
 * @return 文字，例如 "15.3"
 */
QString format_battery_voltage(float voltage_v);

/**
 * @brief 電池電流（一位小數），PX4 BatteryStatus.current_a 的無效值 -1 顯示「—」（SIH 模擬器即回報 -1）
 * @param current_a 電流（A）
 * @return 文字，例如 "12.4"
 */
QString format_battery_current(float current_a);

/**
 * @brief NED 位置：各軸帶正負號、以雙空白分隔，等寬字下各軸對齊
 * @param position_ned N／E／D（公尺）
 * @return 文字，例如 "+1.0  +2.0  −5.0"
 */
QString format_ned(const std::array<float, 3>& position_ned);

/**
 * @brief 經緯度文字（六位小數約 0.1 公尺，附半球）
 * @param position [緯度, 經度, 高度]
 * @return 文字，例如 "25.075600 N  121.537500 E"；位置無效時為「—」
 */
QString format_lat_lon(const std::array<double, 3>& position);

/**
 * @brief WGS84 座標是否可在地圖顯示
 * @param position [緯度, 經度, 高度]
 * @return 緯經度皆有限且在合法範圍內
 */
bool has_valid_global_position(const std::array<double, 3>& position);

/**
 * @brief 將 Agent 回報的 WGS84 位置換算成地圖顯示位置
 *
 * 相對參考點的北／東位移（公尺）保持不變，因此距離與形狀不變；只平移、不旋轉，高度不變。
 * @param position [緯度, 經度, 高度]
 * @param origin 顯示原點
 * @return 顯示用位置；未啟用或位置無效時原樣傳回
 */
std::array<double, 3> to_display_position(const std::array<double, 3>& position, const DisplayOrigin& origin);

/**
 * @brief to_display_position 的反向換算：地圖上的位置換回 Agent 回報座標（例如地圖點選的航點）
 * @param position 地圖顯示位置 [緯度, 經度, 高度]
 * @param origin 顯示原點
 * @return Agent 回報座標；未啟用或位置無效時原樣傳回
 */
std::array<double, 3> to_reported_position(const std::array<double, 3>& position, const DisplayOrigin& origin);


/**
 * @brief UAV 簡稱，例如 uav_1_agent → UAV_1
 * @param agent_ns Agent namespace
 * @return 簡稱
 */
QString agent_short_name(const QString& agent_ns);

/**
 * @brief Agent 事件代碼的中文名稱（介面文件 AgentEvent 一節）
 * @param code AgentEvent.code
 * @return 中文名稱；未定義的代碼顯示「Agent 事件 N」
 */
QString agent_event_label(uint16_t code);

/**
 * @brief PX4 VehicleCommandAck.result 的中文名稱
 * @param result 結果碼
 * @return 例如「暫時拒絕」；未定義的值顯示「未知」
 */
QString px4_command_result_label(int16_t result);

/**
 * @brief Agent 事件提示內容：相關命令、Agent 的英文說明與 PX4 結果碼
 * @param event Agent 事件
 * @return 例如「ARM；Arming denied（PX4 結果 1：暫時拒絕）」
 */
QString format_agent_event_message(const AgentEventData& event);

/**
 * @brief PX4 VehicleStatus 是否中斷（從未收到或超過 Agent 判斷狀態有效的時限）
 * @param health AgentHealth
 * @return 是否中斷
 */
bool is_px4_link_lost(const AgentHealthData& health);

/**
 * @brief PX4 連線文字
 * @param health AgentHealth
 * @return 「正常」「中斷 N 秒」或「未連線」
 */
QString px4_link_text(const AgentHealthData& health);

/**
 * @brief PX4 預檢是否未通過；PX4 連線中斷時預檢結果不可信，不視為未通過
 * @param health AgentHealth
 * @return 是否未通過
 */
bool is_preflight_failed(const AgentHealthData& health);

/**
 * @brief PX4 預檢文字
 * @param health AgentHealth
 * @return 「PASS」「FAIL」；PX4 連線中斷時為「—」
 */
QString preflight_text(const AgentHealthData& health);

/**
 * @brief 等待飛控的具體原因（取代籠統的「等待飛控」）
 * @param has_state 是否收到 AgentState
 * @param state 最新 AgentState
 * @param has_health 是否收到 AgentHealth
 * @param health 最新 AgentHealth
 * @return 原因；PX4 狀態有效時為空字串
 */
QString flight_controller_wait_reason(bool has_state, const AgentStateData& state, bool has_health,
                                      const AgentHealthData& health);

}  // namespace gcs::ui
