/**
 * @file agent_types.hpp
 * @brief 核心模組間傳遞的 Agent 資料型別（取代網頁版 EventBus 的 JSON 字串）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 欄位與 Agent 介面文件第 3 節一一對應；本檔不依賴 DDS 產生的型別，
 * 由 link 層負責轉換（ADR-0015）。布林欄位依 CODE_CONSTRAINT L2-14 加上 is_ 字首。
 */
#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

#include <array>
#include <cstdint>
#include <vector>

namespace gcs
{

/// 單機命令目標以外的全體目標字串，對應 /uav_all_agent/command
inline const QString ALL_AGENTS_TARGET = QStringLiteral("all");

/// Agent 任務階段（介面文件第 3.1 節 FlightState）
enum class FlightState : uint8_t
{
    IDLE = 0,
    OFFBOARD_INIT = 1,
    OFFBOARD_ACTIVE = 2,
    ARMED = 3,
    TAKEOFF = 4,
    HOVER = 5,
    RTL = 6,
    LAND = 7,
    MOVING = 8,
    POST_RTL = 9,
    ERROR = 10
};

/// PX4 VehicleStatus::NAVIGATION_STATE_OFFBOARD（介面版本常數）
inline constexpr uint8_t PX4_NAV_STATE_OFFBOARD = 14;

/// builtin_interfaces/Time
struct RosTime
{
    int32_t sec = 0;
    uint32_t nanosec = 0;
};

/// AgentState
struct AgentStateData
{
    RosTime stamp;
    /// NED 位置（公尺）
    std::array<float, 3> position_ned{};
    /// WGS84 [緯度, 經度, AMSL 高度]，保留 double 精度；無資料時為 NaN
    std::array<double, 3> position_global{};
    uint8_t flight_mode = 0;
    /// 剩餘電量比例 0.0～1.0
    float battery_remaining = 0.0F;
    float battery_voltage_v = 0.0F;
    float battery_current_a = 0.0F;
    bool is_online = false;
    bool is_armed = false;
    bool is_landed = true;
    uint8_t px4_nav_state = 0;
    /// 只有為 true 時 is_armed、is_landed、px4_nav_state 才可信
    bool is_px4_state_valid = false;
};

/// AgentAttitude
struct AgentAttitudeData
{
    RosTime stamp;
    /// 四元數 (w, x, y, z)
    std::array<float, 4> q{};
};

/// NedTarget
struct NedTargetData
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    /// 目標航向（度），NaN 表示不控制
    float yaw = 0.0F;
};

/// Waypoint
struct WaypointData
{
    std::array<float, 3> position_ned{};
    RosTime arrival_time;
};

/// WaypointSequence
struct WaypointSequenceData
{
    RosTime stamp;
    std::vector<WaypointData> waypoints;
};

/// AgentTrajectory（EGO-Swarm WGS84 未來軌跡）
struct AgentTrajectoryData
{
    RosTime stamp;
    RosTime start_time;
    uint32_t trajectory_id = 0;
    uint32_t agent_index = 0;
    uint8_t coordinate_frame = 0;
    uint32_t frame_revision = 0;
    uint8_t order = 0;
    float knot_interval_s = 0.0F;
    /// 每 3 個元素為 [緯度, 經度, 高度]
    std::vector<double> control_points;
    float duration_s = 0.0F;
    bool is_terminal_hold = false;
    bool is_valid = false;
};

/// UI 送出的命令請求
struct CommandRequest
{
    /// uav_{N}_agent 或 ALL_AGENTS_TARGET
    QString agent;
    /// 命令名稱，例如 TAKEOFF
    QString command;
    /// 數值參數，依介面文件第 6 節
    QVector<double> params;
};

/// 命令處理結果
struct CommandAck
{
    QString agent;
    QString command;
    bool is_ok = false;
    QString message;
};

/// 機隊狀態快照
struct FleetSnapshot
{
    /// 有實際狀態資料流的 Agent
    QStringList online;
    /// 資料停滯或 topic 已消失的 Agent
    QStringList offline;
};

/// 航點任務的單一航點；經緯度為 Agent 回報的 WGS84 座標，不是地圖顯示座標
struct MissionWaypoint
{
    double latitude = 0.0;
    double longitude = 0.0;
    /// 相對高度（公尺，向上為正），與機群面板的高度相同，即 -position_ned[2]
    double altitude_m = 0.0;
};

/// 航點任務：單機依序飛往各航點
struct MissionPlan
{
    /// uav_{N}_agent；航點任務不支援全體目標
    QString agent;
    QVector<MissionWaypoint> waypoints;
};

/// 航點任務階段
enum class MissionPhase : uint8_t
{
    IDLE,
    /// 已送出航點命令，前往目前航點
    MOVING,
    /// 已到達目前航點，停留中
    DWELLING,
    /// 最後一個航點已到達，Agent 懸停
    COMPLETED,
    /// 執行中被中止
    ABORTED,
    /// 開始條件不符，未送出任何命令
    REJECTED
};

/// 航點任務狀態回報
struct MissionStatus
{
    QString agent;
    MissionPhase phase = MissionPhase::IDLE;
    /// 目前航點（0 起算）；尚未開始時為 -1
    int waypoint_index = -1;
    int waypoint_count = 0;
    QString message;
};

/**
 * @brief 註冊所有跨執行緒傳遞的核心型別
 *
 * queued connection 需要已註冊的 metatype；程式啟動時呼叫一次。
 */
void register_core_metatypes();

}  // namespace gcs

Q_DECLARE_METATYPE(gcs::AgentStateData)
Q_DECLARE_METATYPE(gcs::AgentAttitudeData)
Q_DECLARE_METATYPE(gcs::NedTargetData)
Q_DECLARE_METATYPE(gcs::WaypointSequenceData)
Q_DECLARE_METATYPE(gcs::AgentTrajectoryData)
Q_DECLARE_METATYPE(gcs::CommandRequest)
Q_DECLARE_METATYPE(gcs::CommandAck)
Q_DECLARE_METATYPE(gcs::FleetSnapshot)
Q_DECLARE_METATYPE(gcs::MissionPlan)
Q_DECLARE_METATYPE(gcs::MissionStatus)
