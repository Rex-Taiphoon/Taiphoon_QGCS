/**
 * @file guard_rules.cpp
 * @brief 防呆規則實作 — 安全邏輯集中處
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 轉移表內容由網頁版 GuardModule::initTransitionTable() 移植；之後新增的命令（航點任務、重新啟動飛控）
 * 依 ADR-0020 加入。
 */
#include "modules/guard/guard_rules.hpp"

#include <cmath>
#include <map>
#include <set>

namespace gcs::guard_rules
{

namespace
{

using TransitionTable = std::map<FlightState, std::set<CmdType>>;

/// 每度緯度對應的距離（公尺，球體近似）；1 km 範圍內誤差遠小於距離上限的意義
constexpr double METERS_PER_DEGREE = 111319.49;
constexpr double PI = 3.14159265358979323846;
constexpr double DEGREES_TO_RADIANS = PI / 180.0;
constexpr double FULL_TURN_DEGREES = 360.0;
constexpr double MAX_LATITUDE_DEGREES = 90.0;
constexpr double MAX_LONGITUDE_DEGREES = 180.0;

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

    // 航點任務只能從懸停開始；開始後 Agent 回報 HOVER 或 MOVING
    table[FS::HOVER].insert(CT::MISSION_START);

    // MISSION_STOP 只會讓 Agent 停在目前位置或清除已載入的任務，任何階段都允許
    for (const auto state : {FS::IDLE, FS::OFFBOARD_INIT, FS::OFFBOARD_ACTIVE, FS::ARMED, FS::TAKEOFF, FS::HOVER,
                             FS::RTL, FS::LAND, FS::MOVING, FS::POST_RTL, FS::ERROR})
    {
        table[state].insert(CT::MISSION_STOP);
    }

    // REBOOT_FC 不查表，由 PX4 原生狀態與最後有效狀態判斷（check_reboot）
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

/**
 * @brief 一般重新啟動：PX4 狀態有效、已上鎖且已落地
 * @param state 最新 AgentState；沒有時為空
 * @return 空字串表示允許，否則為拒絕原因
 */
QString check_normal_reboot(const std::optional<AgentStateData>& state)
{
    if (!state.has_value() || !state->is_px4_state_valid)
    {
        return QStringLiteral("飛控狀態不明，只能以強制重新啟動送出");
    }
    if (state->is_armed)
    {
        return QStringLiteral("飛控已解鎖，不可重新啟動");
    }
    if (!state->is_landed)
    {
        return QStringLiteral("飛控未確認落地，不可重新啟動");
    }
    return QString();
}

/**
 * @brief 強制重新啟動：PX4 狀態不明，且沒有任何已知資訊顯示可能在空中
 *
 * AgentState 在 PX4 逾時後保留最後收到的 armed／landed，但從未收到時兩者都是 false，
 * 因此 armed 為 true 一定可信，landed 為 false 則要以 health 確認曾收到落地偵測（介面文件 AgentState 一節）。
 *
 * @param facts 已知資訊
 * @return 空字串表示允許，否則為拒絕原因
 */
QString check_forced_reboot(const RebootFacts& facts)
{
    const auto& state = facts.state;
    if (state.has_value() && state->is_px4_state_valid)
    {
        return QStringLiteral("飛控狀態有效，請使用一般重新啟動");
    }
    if (!facts.health.has_value())
    {
        return QStringLiteral("尚未收到 Agent 健康狀態，無法確認飛控是否解鎖");
    }
    const auto& last_valid = facts.last_valid_state;
    if (last_valid.has_value() && last_valid->is_armed)
    {
        return QStringLiteral("最後一次飛控狀態有效時已解鎖，可能仍在空中，不可強制重新啟動");
    }
    if (last_valid.has_value() && !last_valid->is_landed)
    {
        return QStringLiteral("最後一次飛控狀態有效時未落地，可能仍在空中，不可強制重新啟動");
    }
    const AgentHealthData& health = *facts.health;
    if (health.px4_arming_state == px4_arming::ARMED || (state.has_value() && state->is_armed))
    {
        return QStringLiteral("Agent 最後收到的飛控狀態為已解鎖，可能仍在空中，不可強制重新啟動");
    }
    const bool has_land_detected = health.px4_land_detected_age_s >= 0.0F;
    if (state.has_value() && has_land_detected && !state->is_landed)
    {
        return QStringLiteral("Agent 最後收到的飛控狀態為未落地，可能仍在空中，不可強制重新啟動");
    }
    return QString();
}

/**
 * @brief WGS84 緯經度是否有效
 * @param latitude 緯度
 * @param longitude 經度
 * @return 皆為有限值且在合法範圍內
 */
bool is_valid_global_position(double latitude, double longitude)
{
    return std::isfinite(latitude) && std::isfinite(longitude) && std::abs(latitude) <= MAX_LATITUDE_DEGREES
           && std::abs(longitude) <= MAX_LONGITUDE_DEGREES;
}

/**
 * @brief 兩個 WGS84 位置的水平距離（局部平面近似）
 * @param from_latitude 起點緯度
 * @param from_longitude 起點經度
 * @param to_latitude 終點緯度
 * @param to_longitude 終點經度
 * @return 距離（公尺）
 */
double horizontal_distance(double from_latitude, double from_longitude, double to_latitude, double to_longitude)
{
    const double delta_longitude = std::remainder(to_longitude - from_longitude, FULL_TURN_DEGREES);
    const double north = (to_latitude - from_latitude) * METERS_PER_DEGREE;
    const double east = delta_longitude * METERS_PER_DEGREE * std::cos(from_latitude * DEGREES_TO_RADIANS);
    return std::hypot(north, east);
}

}  // namespace

bool is_formation_command(CmdType cmd)
{
    return cmd == CmdType::FORMATION_CIRCLE || cmd == CmdType::FORMATION_HORI_LINEAR
           || cmd == CmdType::FORMATION_VERT_LINEAR || cmd == CmdType::FORMATION_V_SHAPE
           || cmd == CmdType::FORMATION_CANCEL;
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

QString check_reboot(const RebootFacts& facts, bool is_force)
{
    return is_force ? check_forced_reboot(facts) : check_normal_reboot(facts.state);
}

bool is_command_allowed_for_agent(const AgentStateData& state, CmdType cmd)
{
    if (cmd == CmdType::MISSION_STOP)
    {
        return true;
    }
    if (cmd == CmdType::REBOOT_FC)
    {
        return check_normal_reboot(state).isEmpty();
    }
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

QString check_mission_upload(const AgentStateData& state, const WaypointMissionData& mission)
{
    if (!state.is_px4_state_valid)
    {
        return QStringLiteral("PX4 狀態尚未有效，拒絕上傳航點任務");
    }
    if (!is_command_allowed_for_agent(state, CmdType::MISSION_START))
    {
        return QStringLiteral("需在懸停狀態才能開始任務");
    }
    if (mission.mission_id == 0)
    {
        return QStringLiteral("任務識別碼不可為 0");
    }
    if (mission.altitude_frame != MISSION_ALTITUDE_PX4_LOCAL)
    {
        return QStringLiteral("不支援的高度基準");
    }
    const auto count = static_cast<int>(mission.waypoints.size());
    if (count == 0)
    {
        return QStringLiteral("沒有航點");
    }
    if (count > mission_limits::MAX_WAYPOINTS)
    {
        return QStringLiteral("航點最多 %1 個").arg(mission_limits::MAX_WAYPOINTS);
    }
    const double latitude = state.position_global[0];
    const double longitude = state.position_global[1];
    if (!is_valid_global_position(latitude, longitude))
    {
        return QStringLiteral("沒有有效的 GPS 位置");
    }

    for (int i = 0; i < count; ++i)
    {
        const MissionWaypointData& waypoint = mission.waypoints.at(static_cast<std::size_t>(i));
        if (!is_valid_global_position(waypoint.latitude_deg, waypoint.longitude_deg)
            || !std::isfinite(waypoint.altitude_m))
        {
            return QStringLiteral("第 %1 個航點座標無效").arg(i + 1);
        }
        if (waypoint.altitude_m < mission_limits::MIN_ALTITUDE_M
            || waypoint.altitude_m > mission_limits::MAX_ALTITUDE_M)
        {
            return QStringLiteral("第 %1 個航點高度需介於 %2～%3 公尺")
                .arg(i + 1)
                .arg(mission_limits::MIN_ALTITUDE_M)
                .arg(mission_limits::MAX_ALTITUDE_M);
        }
        if (!std::isfinite(waypoint.dwell_s) || waypoint.dwell_s < 0.0F
            || waypoint.dwell_s > mission_limits::MAX_DWELL_S)
        {
            return QStringLiteral("第 %1 個航點停留時間需介於 0～%2 秒").arg(i + 1).arg(mission_limits::MAX_DWELL_S);
        }
        // 航向欄位保留給未來版本；介面目前只接受 NaN（維持目前航向）
        if (!std::isnan(waypoint.yaw_deg))
        {
            return QStringLiteral("第 %1 個航點不支援指定航向").arg(i + 1);
        }
        const double range = horizontal_distance(latitude, longitude, waypoint.latitude_deg, waypoint.longitude_deg);
        if (range > mission_limits::MAX_RANGE_M)
        {
            return QStringLiteral("第 %1 個航點距離目前位置 %2 公尺，超過 %3 公尺上限")
                .arg(i + 1)
                .arg(std::round(range))
                .arg(mission_limits::MAX_RANGE_M);
        }
    }
    return QString();
}

}  // namespace gcs::guard_rules
