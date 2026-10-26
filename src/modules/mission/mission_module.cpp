/**
 * @file mission_module.cpp
 * @brief 航點任務模組實作
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 */
#include "modules/mission/mission_module.hpp"

#include "core/command_protocol.hpp"

#include <QLoggingCategory>

#include <cmath>
#include <optional>

namespace
{
Q_LOGGING_CATEGORY(lc_mission, "gcs.mission")
}  // namespace

namespace gcs
{

namespace
{

/// 每度緯度對應的距離（公尺，球體近似）；任務範圍 1 km 內誤差遠小於 Agent 到達容許誤差
constexpr double METERS_PER_DEGREE = 111319.49;
constexpr double PI = 3.14159265358979323846;
constexpr double DEGREES_TO_RADIANS = PI / 180.0;
constexpr double RADIANS_TO_DEGREES = 180.0 / PI;
constexpr double FULL_TURN_DEGREES = 360.0;
constexpr double MAX_LATITUDE_DEGREES = 90.0;
constexpr double MAX_LONGITUDE_DEGREES = 180.0;
constexpr double MS_PER_SECOND = 1000.0;
/// 四元數範數低於此值視為無效姿態
constexpr double MIN_QUATERNION_NORM = 1e-6;
/// 命令參數四捨五入到公分與 0.1 度，避免命令列出現過長的小數
constexpr double CENTIMETERS_PER_METER = 100.0;
constexpr double TENTHS_PER_DEGREE = 10.0;
const QString MOVE_POINT = QStringLiteral("MOVE_POINT");
const QString HOLD = QStringLiteral("HOLD");

/// 北／東位移（公尺）
struct NorthEast
{
    double north = 0.0;
    double east = 0.0;
};

/**
 * @brief 計算兩個 WGS84 位置之間的北／東位移（局部平面近似）
 * @param from_latitude 起點緯度
 * @param from_longitude 起點經度
 * @param to_latitude 終點緯度
 * @param to_longitude 終點經度
 * @return 位移（公尺）
 */
NorthEast north_east_offset(double from_latitude, double from_longitude, double to_latitude, double to_longitude)
{
    const double delta_longitude = std::remainder(to_longitude - from_longitude, FULL_TURN_DEGREES);
    return {(to_latitude - from_latitude) * METERS_PER_DEGREE,
            delta_longitude * METERS_PER_DEGREE * std::cos(from_latitude * DEGREES_TO_RADIANS)};
}

/**
 * @brief 四元數轉機頭方向
 * @param q 四元數 (w, x, y, z)，NED
 * @return 0～360 度（北為 0、順時針）；無效時為 std::nullopt
 */
std::optional<double> heading_from_quaternion(const std::array<float, 4>& q)
{
    const double w = q[0];
    const double x = q[1];
    const double y = q[2];
    const double z = q[3];
    const double norm = std::sqrt(w * w + x * x + y * y + z * z);
    if (!std::isfinite(norm) || norm < MIN_QUATERNION_NORM)
    {
        return std::nullopt;
    }
    // 分母寫成 w²+x²−y²−z²，四元數未正規化時兩項同比例縮放，atan2 結果不變
    const double yaw = std::atan2(2.0 * (w * z + x * y), w * w + x * x - y * y - z * z);
    return std::fmod(yaw * RADIANS_TO_DEGREES + FULL_TURN_DEGREES, FULL_TURN_DEGREES);
}

/**
 * @brief 三維距離
 * @param a 位置
 * @param b 位置
 * @return 距離（公尺）
 */
double distance_between(const std::array<double, 3>& a, const std::array<double, 3>& b)
{
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

/**
 * @brief AgentState 的 position_ned 轉為 double
 * @param state AgentState
 * @return 位置（公尺）
 */
std::array<double, 3> position_of(const AgentStateData& state)
{
    return {state.position_ned[0], state.position_ned[1], state.position_ned[2]};
}

/**
 * @brief 四捨五入到指定倍數
 * @param value 數值
 * @param scale 倍數，例如 100 表示到 0.01
 * @return 結果
 */
double round_to(double value, double scale)
{
    return std::round(value * scale) / scale;
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

}  // namespace

MissionModule::MissionModule(const MissionTiming& timing, QObject* parent)
    : GcsModule(parent), timing_(timing), tick_timer_(this)
{
    tick_timer_.setInterval(timing_.tick_ms);
    connect(&tick_timer_, &QTimer::timeout, this, &MissionModule::on_tick);
}

bool MissionModule::init()
{
    clock_.start();
    is_initialized_ = true;
    qCInfo(lc_mission) << "初始化完成";
    return true;
}

void MissionModule::shutdown()
{
    tick_timer_.stop();
    missions_.clear();
    states_.clear();
    attitudes_.clear();
    is_initialized_ = false;
}

bool MissionModule::health_check() const
{
    return is_initialized_;
}

bool MissionModule::is_running(const QString& agent_ns) const
{
    return missions_.contains(agent_ns);
}

qint64 MissionModule::now_ms() const
{
    return clock_.isValid() ? clock_.elapsed() : 0;
}

QString MissionModule::validate_start(const MissionPlan& plan, double* heading_deg) const
{
    if (heading_deg == nullptr)
    {
        return QStringLiteral("內部錯誤：未提供機頭方向輸出");
    }
    if (!is_initialized_)
    {
        return QStringLiteral("航點任務模組未初始化");
    }
    if (!command_protocol::is_single_agent(plan.agent))
    {
        return QStringLiteral("航點任務只支援單機");
    }
    if (plan.waypoints.isEmpty())
    {
        return QStringLiteral("沒有航點");
    }
    if (plan.waypoints.size() > MAX_WAYPOINTS)
    {
        return QStringLiteral("航點最多 %1 個").arg(MAX_WAYPOINTS);
    }
    if (missions_.contains(plan.agent))
    {
        return QStringLiteral("已在執行航點任務");
    }
    const auto state_it = states_.constFind(plan.agent);
    if (state_it == states_.cend() || !state_it->is_px4_state_valid)
    {
        return QStringLiteral("尚未收到有效的飛控狀態");
    }
    if (state_it->flight_mode != static_cast<uint8_t>(FlightState::HOVER))
    {
        return QStringLiteral("需在懸停狀態才能開始任務");
    }
    const double latitude = state_it->position_global[0];
    const double longitude = state_it->position_global[1];
    if (!is_valid_global_position(latitude, longitude))
    {
        return QStringLiteral("沒有有效的 GPS 位置");
    }
    const auto attitude_it = attitudes_.constFind(plan.agent);
    const auto heading = attitude_it == attitudes_.cend() ? std::nullopt : heading_from_quaternion(attitude_it->q);
    if (!heading.has_value())
    {
        return QStringLiteral("尚未收到姿態資料，無法維持機頭方向");
    }

    for (int i = 0; i < plan.waypoints.size(); ++i)
    {
        const MissionWaypoint& waypoint = plan.waypoints.at(i);
        if (!is_valid_global_position(waypoint.latitude, waypoint.longitude) || !std::isfinite(waypoint.altitude_m))
        {
            return QStringLiteral("第 %1 個航點座標無效").arg(i + 1);
        }
        if (waypoint.altitude_m < MIN_ALTITUDE_M || waypoint.altitude_m > MAX_ALTITUDE_M)
        {
            return QStringLiteral("第 %1 個航點高度需介於 %2～%3 公尺").arg(i + 1).arg(MIN_ALTITUDE_M).arg(MAX_ALTITUDE_M);
        }
        const NorthEast offset = north_east_offset(latitude, longitude, waypoint.latitude, waypoint.longitude);
        const double range = std::hypot(offset.north, offset.east);
        if (range > MAX_RANGE_M)
        {
            return QStringLiteral("第 %1 個航點距離目前位置 %2 公尺，超過 %3 公尺上限")
                .arg(i + 1)
                .arg(std::round(range))
                .arg(MAX_RANGE_M);
        }
    }
    *heading_deg = *heading;
    return QString();
}

void MissionModule::start_mission(const MissionPlan& plan)
{
    double heading_deg = 0.0;
    const QString error = validate_start(plan, &heading_deg);
    if (!error.isEmpty())
    {
        qCWarning(lc_mission) << "拒絕開始航點任務：" << plan.agent << error;
        MissionStatus status;
        status.agent = plan.agent;
        status.phase = MissionPhase::REJECTED;
        status.waypoint_count = static_cast<int>(plan.waypoints.size());
        status.message = error;
        emit mission_status_changed(status);
        return;
    }

    ActiveMission mission;
    mission.plan = plan;
    mission.heading_deg = heading_deg;
    missions_.insert(plan.agent, mission);
    qCInfo(lc_mission) << "開始航點任務：" << plan.agent << "航點" << plan.waypoints.size() << "個";
    if (!tick_timer_.isActive())
    {
        tick_timer_.start();
    }
    send_leg(plan.agent);
}

void MissionModule::send_leg(const QString& agent_ns)
{
    const auto it = missions_.find(agent_ns);
    if (it == missions_.end())
    {
        return;
    }
    const AgentStateData state = states_.value(agent_ns);
    const MissionWaypoint waypoint = it->plan.waypoints.at(it->index);
    const NorthEast offset = north_east_offset(state.position_global[0], state.position_global[1],
                                               waypoint.latitude, waypoint.longitude);
    const std::array<double, 3> current = position_of(state);
    const std::array<double, 3> target = {current[0] + offset.north, current[1] + offset.east, -waypoint.altitude_m};

    it->target_ned = target;
    it->start_ned = current;
    it->phase = LegPhase::AWAITING_ACK;
    it->has_departed = false;
    it->settle_anchor_ned = current;
    it->settle_since_ms = now_ms();
    it->leg_started_ms = now_ms();
    it->leg_timeout_ms = static_cast<qint64>(timing_.leg_base_timeout_ms
                                             + distance_between(current, target) / timing_.min_expected_speed_mps
                                                   * MS_PER_SECOND);

    CommandRequest request;
    request.agent = agent_ns;
    request.command = MOVE_POINT;
    request.params = {round_to(target[0], CENTIMETERS_PER_METER), round_to(target[1], CENTIMETERS_PER_METER),
                      round_to(target[2], CENTIMETERS_PER_METER), round_to(it->heading_deg, TENTHS_PER_DEGREE)};
    const int index = it->index;
    const int count = static_cast<int>(it->plan.waypoints.size());
    report(agent_ns, MissionPhase::MOVING, QStringLiteral("前往第 %1/%2 個航點").arg(index + 1).arg(count));
    // Guard 與 CommandModule 以 DirectConnection 在此呼叫內同步回報，之後不得再使用 it
    emit command_requested(request);
}

void MissionModule::on_command_acked(const CommandAck& ack)
{
    if (ack.agent == ALL_AGENTS_TARGET)
    {
        if (!ack.is_ok)
        {
            return;
        }
        // 全體命令也會作用在執行任務的 Agent 上，視同操作人員介入
        for (const QString& agent : missions_.keys())
        {
            finish(agent, MissionPhase::ABORTED, QStringLiteral("操作人員對全體送出 %1，任務中止").arg(ack.command));
        }
        return;
    }

    const auto it = missions_.find(ack.agent);
    if (it == missions_.end())
    {
        return;
    }
    if (it->phase == LegPhase::AWAITING_ACK && ack.command == MOVE_POINT)
    {
        if (!ack.is_ok)
        {
            const int index = it->index;
            finish(ack.agent, MissionPhase::ABORTED,
                   QStringLiteral("第 %1 個航點命令未送出：%2").arg(index + 1).arg(ack.message));
            return;
        }
        it->phase = LegPhase::MOVING;
        it->leg_started_ms = now_ms();
        return;
    }
    if (ack.is_ok)
    {
        finish(ack.agent, MissionPhase::ABORTED, QStringLiteral("操作人員送出 %1，任務中止").arg(ack.command));
    }
}

void MissionModule::on_state_updated(const QString& agent_ns, const AgentStateData& state)
{
    states_.insert(agent_ns, state);
    if (!missions_.contains(agent_ns) || !state.is_px4_state_valid)
    {
        // PX4 狀態暫時無效不中止；若持續無效，逾時檢查仍會結束任務
        return;
    }
    const auto mode = static_cast<FlightState>(state.flight_mode);
    if (mode != FlightState::HOVER && mode != FlightState::MOVING)
    {
        finish(agent_ns, MissionPhase::ABORTED,
               QStringLiteral("飛行狀態已離開懸停／移動（flight_mode=%1），任務中止").arg(state.flight_mode));
        return;
    }
    evaluate_arrival(agent_ns);
}

void MissionModule::on_attitude_updated(const QString& agent_ns, const AgentAttitudeData& attitude)
{
    attitudes_.insert(agent_ns, attitude);
}

void MissionModule::evaluate_arrival(const QString& agent_ns)
{
    const auto it = missions_.find(agent_ns);
    if (it == missions_.end() || it->phase != LegPhase::MOVING)
    {
        return;
    }
    const AgentStateData state = states_.value(agent_ns);
    const std::array<double, 3> position = position_of(state);
    const bool is_moving_state = static_cast<FlightState>(state.flight_mode) == FlightState::MOVING;
    const qint64 now = now_ms();
    if (is_moving_state || distance_between(position, it->start_ned) > timing_.departure_distance_m)
    {
        it->has_departed = true;
    }
    if (distance_between(position, it->settle_anchor_ned) > timing_.settle_radius_m)
    {
        it->settle_anchor_ned = position;
        it->settle_since_ms = now;
    }
    if (is_moving_state)
    {
        return;
    }
    const double distance = distance_between(position, it->target_ned);
    const bool is_near = distance <= timing_.arrival_tolerance_m;
    const bool is_settled = it->has_departed && now - it->settle_since_ms >= timing_.hover_settle_ms;
    if (!is_near && !is_settled)
    {
        return;
    }
    if (!is_near)
    {
        qCWarning(lc_mission) << agent_ns << "已停止移動，但與預期位置相差" << distance
                              << "公尺（Agent planner 原點可能與 PX4 local 原點不同）";
    }

    const int index = it->index;
    const int count = static_cast<int>(it->plan.waypoints.size());
    if (index + 1 >= count)
    {
        finish(agent_ns, MissionPhase::COMPLETED, QStringLiteral("已到達最後一個航點，懸停中"));
        return;
    }
    it->phase = LegPhase::DWELLING;
    it->dwell_started_ms = now;
    report(agent_ns, MissionPhase::DWELLING,
           QStringLiteral("已到達第 %1/%2 個航點，停留 %3 秒")
               .arg(index + 1)
               .arg(count)
               .arg(timing_.dwell_ms / MS_PER_SECOND));
}

void MissionModule::on_tick()
{
    const qint64 now = now_ms();
    for (const QString& agent : missions_.keys())
    {
        evaluate_arrival(agent);
        const auto it = missions_.find(agent);
        if (it == missions_.end())
        {
            continue;
        }
        if (it->phase == LegPhase::DWELLING)
        {
            if (now - it->dwell_started_ms >= timing_.dwell_ms)
            {
                ++it->index;
                send_leg(agent);
            }
            continue;
        }
        if (now - it->leg_started_ms > it->leg_timeout_ms)
        {
            const int index = it->index;
            finish(agent, MissionPhase::ABORTED,
                   QStringLiteral("第 %1 個航點逾時未到達（%2 秒），任務中止")
                       .arg(index + 1)
                       .arg(it->leg_timeout_ms / MS_PER_SECOND));
        }
    }
    if (missions_.isEmpty())
    {
        tick_timer_.stop();
    }
}

void MissionModule::stop_mission(const QString& agent_ns)
{
    const auto it = missions_.constFind(agent_ns);
    if (it == missions_.cend())
    {
        qCInfo(lc_mission) << agent_ns << "沒有執行中的航點任務";
        return;
    }
    // SITL 實測 HOLD 不會取消 EGO-Swarm 已規劃的軌跡，Agent 仍會飛往原目標；
    // 改以 MOVE_POINT 把目標換成目前位置，planner 才會停下。位置無效時才退回 HOLD。
    const double heading_deg = it->heading_deg;
    const std::array<double, 3> current = position_of(states_.value(agent_ns));
    const bool is_position_valid = std::isfinite(current[0]) && std::isfinite(current[1]) && std::isfinite(current[2]);
    CommandRequest request;
    request.agent = agent_ns;
    if (is_position_valid)
    {
        request.command = MOVE_POINT;
        request.params = {round_to(current[0], CENTIMETERS_PER_METER), round_to(current[1], CENTIMETERS_PER_METER),
                          round_to(current[2], CENTIMETERS_PER_METER), round_to(heading_deg, TENTHS_PER_DEGREE)};
    }
    else
    {
        request.command = HOLD;
    }
    // 先結束任務再送命令，該命令的回報才不會被當成操作人員介入
    finish(agent_ns, MissionPhase::ABORTED, QStringLiteral("操作人員停止任務，已要求停在目前位置"));
    emit command_requested(request);
}

void MissionModule::on_agent_offline(const QString& agent_ns)
{
    states_.remove(agent_ns);
    attitudes_.remove(agent_ns);
    if (missions_.contains(agent_ns))
    {
        finish(agent_ns, MissionPhase::ABORTED, QStringLiteral("Agent 離線，任務中止"));
    }
}

void MissionModule::finish(const QString& agent_ns, MissionPhase phase, const QString& message)
{
    const auto it = missions_.find(agent_ns);
    if (it == missions_.end())
    {
        return;
    }
    MissionStatus status;
    status.agent = agent_ns;
    status.phase = phase;
    status.waypoint_index = it->index;
    status.waypoint_count = static_cast<int>(it->plan.waypoints.size());
    status.message = message;
    missions_.erase(it);
    if (missions_.isEmpty())
    {
        tick_timer_.stop();
    }
    if (phase == MissionPhase::COMPLETED)
    {
        qCInfo(lc_mission) << agent_ns << message;
    }
    else
    {
        qCWarning(lc_mission) << agent_ns << message;
    }
    emit mission_status_changed(status);
}

void MissionModule::report(const QString& agent_ns, MissionPhase phase, const QString& message)
{
    const auto it = missions_.constFind(agent_ns);
    if (it == missions_.cend())
    {
        return;
    }
    MissionStatus status;
    status.agent = agent_ns;
    status.phase = phase;
    status.waypoint_index = it->index;
    status.waypoint_count = static_cast<int>(it->plan.waypoints.size());
    status.message = message;
    qCInfo(lc_mission) << agent_ns << message;
    emit mission_status_changed(status);
}

}  // namespace gcs
