/**
 * @file guard_rules.cpp
 * @brief 防呆規則實作 — 安全邏輯集中處
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 轉移表內容由網頁版 GuardModule::initTransitionTable() 逐字移植，未變更任何規則。
 */
#include "modules/guard/guard_rules.hpp"

#include <map>
#include <set>

namespace gcs::guard_rules
{

namespace
{

using TransitionTable = std::map<FlightState, std::set<CmdType>>;

/**
 * @brief 建立轉移表
 *
 * 僅記錄「允許」的 (FlightState, CmdType) 組合；未列出的組合一律拒絕。
 * 新增狀態或命令時，僅需在此處增減對映關係，並同步更新全組合測試。
 *
 * @return 轉移表
 */
TransitionTable build_transition_table()
{
    using FS = FlightState;
    using CT = CmdType;
    TransitionTable table;

    // IDLE：可要求 Agent 切換至 Offboard 模式
    table[FS::IDLE] = {CT::MODE_OFFBOARD};

    // OFFBOARD_INIT：等待 Offboard 啟用，可再次送出切換命令
    table[FS::OFFBOARD_INIT] = {CT::MODE_OFFBOARD};

    // Arm 與 Takeoff 由已確認的 PX4 原生狀態驗證，不以任務階段推論。

    // ARMED：已解鎖，可 Disarm
    table[FS::ARMED] = {CT::DISARM};

    // TAKEOFF：Agent 起飛後未轉 HOVER 時仍允許六向移動
    table[FS::TAKEOFF] = {
        CT::MOVE_FORWARD, CT::MOVE_BACKWARD, CT::MOVE_LEFT, CT::MOVE_RIGHT, CT::MOVE_UP, CT::MOVE_DOWN,
    };

    // HOVER：懸停中，可移動、返航、降落、上鎖
    table[FS::HOVER] = {
        CT::MOVE_FORWARD, CT::MOVE_BACKWARD, CT::MOVE_LEFT, CT::MOVE_RIGHT, CT::MOVE_UP, CT::MOVE_DOWN,
        CT::YAW, CT::MOVE_POINT, CT::HOLD, CT::MODE_RTL, CT::MODE_LAND, CT::DISARM,
    };

    // MOVING：移動中，可繼續移動、懸停、返航、降落
    table[FS::MOVING] = {
        CT::MOVE_FORWARD, CT::MOVE_BACKWARD, CT::MOVE_LEFT, CT::MOVE_RIGHT, CT::MOVE_UP, CT::MOVE_DOWN,
        CT::YAW, CT::MOVE_POINT, CT::HOLD, CT::MODE_RTL, CT::MODE_LAND,
    };

    // ERROR：錯誤狀態，僅允許 Disarm 緊急上鎖
    table[FS::ERROR] = {CT::DISARM};

    // OFFBOARD_ACTIVE、RTL、LAND、POST_RTL 不接受外部命令（Arm／Takeoff 另依 PX4 規則）
    return table;
}

/**
 * @brief 取得唯一的轉移表實例
 * @return 轉移表
 */
const TransitionTable& transition_table()
{
    static const TransitionTable TABLE = build_transition_table();
    return TABLE;
}

/**
 * @brief PX4 已確認為 Offboard 且已落地
 * @param state 最新 AgentState
 * @return 是否成立
 */
bool is_landed_px4_offboard(const AgentStateData& state)
{
    return state.is_px4_state_valid && state.is_landed && state.px4_nav_state == PX4_NAV_STATE_OFFBOARD;
}

}  // namespace

bool is_formation_command(CmdType cmd)
{
    return cmd == CmdType::FORMATION_CIRCLE || cmd == CmdType::FORMATION_HORI_LINEAR
           || cmd == CmdType::FORMATION_VERT_LINEAR || cmd == CmdType::FORMATION_CANCEL;
}

bool is_command_allowed(FlightState state, CmdType cmd)
{
    // 編隊命令不經轉移表驗證（由 CommandModule 廣播至 /uav_all_agent/command）
    if (is_formation_command(cmd))
    {
        return true;
    }

    const auto& table = transition_table();
    const auto it = table.find(state);
    if (it == table.end())
    {
        return false;
    }
    return it->second.count(cmd) > 0;
}

bool is_arm_allowed_by_px4(const AgentStateData& state)
{
    return is_landed_px4_offboard(state) && !state.is_armed;
}

bool is_takeoff_allowed_by_px4(const AgentStateData& state)
{
    return is_landed_px4_offboard(state) && state.is_armed;
}

bool is_command_allowed_for_agent(const AgentStateData& state, CmdType cmd)
{
    if (!state.is_px4_state_valid)
    {
        return false;
    }
    if (cmd == CmdType::ARM)
    {
        return is_arm_allowed_by_px4(state);
    }
    if (cmd == CmdType::TAKEOFF)
    {
        return is_takeoff_allowed_by_px4(state);
    }
    return is_command_allowed(static_cast<FlightState>(state.flight_mode), cmd);
}

}  // namespace gcs::guard_rules
