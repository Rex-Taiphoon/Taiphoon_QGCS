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
#include <limits>
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

/// WaypointMission.ALTITUDE_PX4_LOCAL：高度與 -AgentState.position_ned[2] 同基準
inline constexpr uint8_t MISSION_ALTITUDE_PX4_LOCAL = 1;

/// 航點任務的上傳限制；與 Agent 端預設參數相同，GuardModule 上傳前檢查，MissionController 編輯時限制
namespace mission_limits
{
/// 單一任務的航點上限
inline constexpr int MAX_WAYPOINTS = 50;
/// 航點高度範圍（公尺）
inline constexpr double MIN_ALTITUDE_M = 1.0;
inline constexpr double MAX_ALTITUDE_M = 120.0;
/// 航點與目前位置的水平距離上限（公尺）：防止顯示原點設錯時把航點放到極遠處
inline constexpr double MAX_RANGE_M = 1000.0;
/// 每個航點停留時間上限（秒）
inline constexpr double MAX_DWELL_S = 60.0;
}  // namespace mission_limits

/// MissionWaypoint（上傳至 Agent 的單一航點）
struct MissionWaypointData
{
    /// WGS84 緯度、經度（度）
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    /// 高度（公尺，向上為正），基準見 WaypointMissionData::altitude_frame
    float altitude_m = 0.0F;
    /// 到達後停留時間（秒）；最後一個航點不使用
    float dwell_s = 0.0F;
    /// 保留欄位：NaN 表示維持目前目標航向；介面目前只接受 NaN
    float yaw_deg = std::numeric_limits<float>::quiet_NaN();
};

/// 航點任務上傳結果（CommandAck.command）的名稱；上傳不是命令，回報只送給 MissionModule
inline const QString MISSION_UPLOAD_ACK_NAME = QStringLiteral("MISSION_PLAN");

/// WaypointMission（GCS → Agent 的航點任務上傳；stamp 由 link 層於發布時填入）
struct WaypointMissionData
{
    /// 任務識別碼，不得為 0；MISSION_START 以此指定要開始的任務
    uint32_t mission_id = 0;
    uint8_t altitude_frame = MISSION_ALTITUDE_PX4_LOCAL;
    std::vector<MissionWaypointData> waypoints;
};

/// WaypointMissionStatus.state
enum class AgentMissionState : uint8_t
{
    IDLE = 0,
    LOADED = 1,
    RUNNING = 2,
    DWELLING = 3,
    COMPLETED = 4,
    ABORTED = 5,
    REJECTED = 6
};

/// WaypointMissionStatus.reason
enum class AgentMissionReason : uint8_t
{
    NONE = 0,
    INVALID_PLAN = 1,
    OUT_OF_RANGE = 2,
    NOT_READY = 3,
    NOT_HOVERING = 4,
    BUSY = 5,
    ID_MISMATCH = 6,
    OPERATOR_STOP = 10,
    COMMAND_OVERRIDE = 11,
    FLIGHT_STATE = 12,
    LANDED = 13,
    LEG_TIMEOUT = 14
};

/// WaypointMissionStatus（Agent 執行航點任務的狀態）
struct WaypointMissionStatusData
{
    RosTime stamp;
    /// 狀態所屬任務；IDLE 時為 0
    uint32_t mission_id = 0;
    /// AgentMissionState 的原始數值；保留原值，未知值由接收端處理
    uint8_t state = 0;
    /// AgentMissionReason 的原始數值
    uint8_t reason = 0;
    /// 目前航點（0 起算）
    uint16_t current_index = 0;
    uint16_t waypoint_count = 0;
    /// Agent 的英文補充說明
    QString detail;
};

/// AgentEvent.severity
enum class AgentEventSeverity : uint8_t
{
    INFO = 0,
    WARNING = 1,
    ERROR = 2
};

/// AgentEvent.source
enum class AgentEventSource : uint8_t
{
    BASIC_CONTROLLER = 1,
    PLANNER = 2,
    SUPERVISOR = 3
};

/// AgentEvent.code（介面文件事件代碼表；代碼只增不改，未列出的代碼以 detail 顯示）
namespace agent_event_code
{
inline constexpr uint16_t COMMAND_UNKNOWN = 100;
inline constexpr uint16_t COMMAND_INVALID_PARAMS = 101;
inline constexpr uint16_t COMMAND_REJECTED = 102;
inline constexpr uint16_t COMMAND_FAILED = 103;
inline constexpr uint16_t PX4_COMMAND_REJECTED = 200;
inline constexpr uint16_t PX4_COMMAND_TIMEOUT = 201;
inline constexpr uint16_t PX4_COMMAND_ACCEPTED = 202;
inline constexpr uint16_t TAKEOFF_CANCELLED = 203;
inline constexpr uint16_t OFFBOARD_LOST = 204;
inline constexpr uint16_t PX4_STATUS_STALE = 300;
inline constexpr uint16_t PX4_STATUS_RECOVERED = 301;
inline constexpr uint16_t PX4_POSITION_STALE = 302;
inline constexpr uint16_t PX4_PREFLIGHT_FAILED = 303;
inline constexpr uint16_t PX4_FAILSAFE = 304;
inline constexpr uint16_t PX4_RESTART_DETECTED = 305;
inline constexpr uint16_t PLANNER_COMMAND_REJECTED = 400;
inline constexpr uint16_t PLANNER_EMERGENCY_BRAKE = 401;
inline constexpr uint16_t PLANNER_FAILURE_PERSISTENT = 402;
inline constexpr uint16_t SAFETY_POSITION_DEVIATION = 500;
}  // namespace agent_event_code

/// AgentEvent.px4_result 沒有對應 PX4 結果時的值
inline constexpr int16_t AGENT_EVENT_NO_PX4_RESULT = -1;

/// AgentEvent（Agent 發生時發布一次的錯誤、警告與資訊）
struct AgentEventData
{
    RosTime stamp;
    /// 各來源節點各自單調遞增
    uint32_t sequence = 0;
    /// AgentEventSeverity 的原始數值
    uint8_t severity = 0;
    /// AgentEventSource 的原始數值
    uint8_t source = 0;
    uint16_t code = 0;
    /// 相關命令名稱，沒有時為空字串
    QString command;
    /// PX4 VehicleCommandAck.result；沒有時為 AGENT_EVENT_NO_PX4_RESULT
    int16_t px4_result = AGENT_EVENT_NO_PX4_RESULT;
    /// Agent 的英文補充說明
    QString detail;
};

/// AgentHealth.failsafe_flags 的位元
namespace failsafe_flag
{
inline constexpr uint32_t LOCAL_POSITION_INVALID = 1U;
inline constexpr uint32_t GLOBAL_POSITION_INVALID = 2U;
inline constexpr uint32_t HOME_POSITION_INVALID = 4U;
inline constexpr uint32_t ATTITUDE_INVALID = 8U;
inline constexpr uint32_t MANUAL_CONTROL_LOST = 16U;
inline constexpr uint32_t OFFBOARD_SIGNAL_LOST = 32U;
inline constexpr uint32_t BATTERY_WARNING = 64U;
inline constexpr uint32_t BATTERY_UNHEALTHY = 128U;
inline constexpr uint32_t GEOFENCE_BREACHED = 256U;
inline constexpr uint32_t FD_CRITICAL_FAILURE = 512U;
inline constexpr uint32_t FD_ESC_ARMING_FAILURE = 1024U;
inline constexpr uint32_t LOCAL_POSITION_ACCURACY_LOW = 2048U;
}  // namespace failsafe_flag

/// AgentHealth.px4_arming_state（VehicleStatus.arming_state；PX4 資料逾時後保留最後收到的值）
namespace px4_arming
{
inline constexpr uint8_t NEVER_RECEIVED = 0;
inline constexpr uint8_t DISARMED = 1;
inline constexpr uint8_t ARMED = 2;
}  // namespace px4_arming

/// AgentHealth（Agent 與 PX4 連線健康狀態，約 1 Hz；Agent 啟動後不需 odometry 就會發布）
struct AgentHealthData
{
    RosTime stamp;
    /// 距離最後一筆 PX4 訊息的秒數；從未收到為負值
    float px4_status_age_s = -1.0F;
    float px4_odometry_age_s = -1.0F;
    float px4_global_position_age_s = -1.0F;
    float px4_land_detected_age_s = -1.0F;
    float px4_battery_age_s = -1.0F;
    float px4_command_ack_age_s = -1.0F;
    /// 以下 px4_* 欄位只在 VehicleStatus 新鮮時可信
    bool is_px4_pre_flight_checks_pass = false;
    bool is_px4_failsafe = false;
    /// px4_arming 的數值；VehicleStatus 逾時後仍代表最後收到的解鎖狀態
    uint8_t px4_arming_state = px4_arming::NEVER_RECEIVED;
    uint8_t px4_latest_arming_reason = 0;
    /// 有收到新鮮的 PX4 failsafe_flags 才為 true
    bool is_failsafe_flags_valid = false;
    /// failsafe_flag 位元
    uint32_t failsafe_flags = 0;
    bool is_offboard_session_active = false;
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

/// 航點任務階段（任務由 Agent 執行，QGCS 依 Agent 回報的狀態顯示）
enum class MissionPhase : uint8_t
{
    IDLE,
    /// 已上傳航點，等待 Agent 載入並開始
    STARTING,
    /// 前往目前航點
    MOVING,
    /// 已到達目前航點，停留中
    DWELLING,
    /// 最後一個航點已到達，Agent 懸停
    COMPLETED,
    /// 執行中被中止
    ABORTED,
    /// 開始條件不符或 Agent 拒絕，無人機未依任務移動
    REJECTED,
    /// 與 Agent 失去聯繫，無法得知進度；Agent 會繼續執行任務
    LOST
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
Q_DECLARE_METATYPE(gcs::WaypointMissionData)
Q_DECLARE_METATYPE(gcs::WaypointMissionStatusData)
Q_DECLARE_METATYPE(gcs::AgentEventData)
Q_DECLARE_METATYPE(gcs::AgentHealthData)
Q_DECLARE_METATYPE(gcs::CommandRequest)
Q_DECLARE_METATYPE(gcs::CommandAck)
Q_DECLARE_METATYPE(gcs::FleetSnapshot)
Q_DECLARE_METATYPE(gcs::MissionPlan)
Q_DECLARE_METATYPE(gcs::MissionStatus)
