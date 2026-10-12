/**
 * @file guard_rules.hpp
 * @brief 防呆規則：資料驅動轉移表與 Arm／Takeoff 的 PX4 原生狀態規則（ADR-0003）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 純函式、無狀態，GuardModule 以此做最終驗證；UiBridge 也以同一份規則決定按鈕是否可用，
 * 但實際閘門永遠是 GuardModule。修改本檔屬安全關鍵變更，必須同步更新轉移表全組合測試。
 */
#pragma once

#include "core/agent_types.hpp"
#include "core/command_protocol.hpp"

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

/**
 * @brief 單機完整判斷：PX4 狀態有效，且 Arm／Takeoff 依 PX4 規則、其餘依轉移表
 * @param state 最新 AgentState
 * @param cmd 命令型別
 * @return 是否允許
 */
bool is_command_allowed_for_agent(const AgentStateData& state, CmdType cmd);

}  // namespace gcs::guard_rules
