/**
 * @file guard_rules.hpp
 * @brief 防呆規則：資料驅動轉移表、Arm／Takeoff／重新啟動飛控的 PX4 原生狀態規則與航點任務上傳檢查（ADR-0003）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 純函式、無狀態，GuardModule 以此做最終驗證；UiBridge 也以同一份規則決定按鈕是否可用，
 * 但實際閘門永遠是 GuardModule。修改本檔屬安全關鍵變更，必須同步更新轉移表全組合測試。
 */
#pragma once

#include "core/agent_types.hpp"
#include "core/command_protocol.hpp"

#include <optional>

namespace gcs::guard_rules
{

/**
 * @brief 查詢轉移表：指定任務階段是否允許指定命令
 *
 * 編隊命令不受任務階段限制（由 Agent 端 FormationEpoch 處理），一律回傳 true；
 * Arm 與 Takeoff 不查表，改由 PX4 原生狀態規則判斷。
 *
 * @param state 任務階段
 * @param cmd 命令型別
 * @return 是否允許
 */
bool is_command_allowed(FlightState state, CmdType cmd);

/**
 * @brief 檢查是否為編隊命令型別
 * @param cmd 命令型別
 * @return 是否為 FORMATION_*
 */
bool is_formation_command(CmdType cmd);

/**
 * @brief PX4 原生狀態是否允許 Arm：狀態有效、Offboard、已落地、未解鎖
 * @param state 最新 AgentState
 * @return 是否允許
 */
bool is_arm_allowed_by_px4(const AgentStateData& state);

/**
 * @brief PX4 原生狀態是否允許 Takeoff：狀態有效、Offboard、已落地、已解鎖
 * @param state 最新 AgentState
 * @return 是否允許
 */
bool is_takeoff_allowed_by_px4(const AgentStateData& state);

/// 判斷重新啟動飛控所需的已知資訊
struct RebootFacts
{
    /// 最新 AgentState；Agent 收到第一筆 odometry 前不會發布
    std::optional<AgentStateData> state;
    /// 最後一次 PX4 狀態有效時的 AgentState（Agent 離線後仍保留）；從未有效時為空
    std::optional<AgentStateData> last_valid_state;
    /// 最新 AgentHealth
    std::optional<AgentHealthData> health;
};

/**
 * @brief 檢查重新啟動飛控（REBOOT_FC）
 *
 * 一般重新啟動：PX4 狀態有效、已上鎖且已落地。
 * 強制重新啟動：只在 PX4 狀態不明（沒有 AgentState 或狀態無效）且收到 AgentHealth 時提供；
 * 最後有效狀態、AgentHealth 保留的解鎖狀態或 AgentState 保留的落地狀態，任一顯示可能在空中就拒絕。
 * 斷線後遙控器仍可能解鎖，最終保護是 PX4 解鎖時拒絕重新啟動（ADR-0020）。
 *
 * @param facts 已知資訊
 * @param is_force 是否強制
 * @return 空字串表示允許，否則為拒絕原因
 */
QString check_reboot(const RebootFacts& facts, bool is_force);

/**
 * @brief 單機完整判斷
 *
 * MISSION_STOP 只會讓 Agent 停在目前位置，任何狀態都允許（PX4 狀態無效時也允許）；
 * REBOOT_FC 只判斷一般重新啟動（強制重新啟動需要更多資訊，改用 check_reboot）；
 * 其餘命令需 PX4 狀態有效，Arm／Takeoff 依 PX4 規則、其餘依轉移表。
 *
 * @param state 最新 AgentState
 * @param cmd 命令型別
 * @return 是否允許
 */
bool is_command_allowed_for_agent(const AgentStateData& state, CmdType cmd);

/**
 * @brief 檢查航點任務上傳：開始條件與 MISSION_START 相同，並檢查航點內容與距離
 * @param state 最新 AgentState
 * @param mission 航點任務
 * @return 空字串表示允許，否則為拒絕原因
 */
QString check_mission_upload(const AgentStateData& state, const WaypointMissionData& mission);

}  // namespace gcs::guard_rules
