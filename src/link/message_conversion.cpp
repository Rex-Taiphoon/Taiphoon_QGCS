/**
 * @file message_conversion.cpp
 * @brief DDS 型別 → 核心型別的轉換實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "link/message_conversion.hpp"

#include <cstddef>

namespace gcs::link
{

namespace
{

/// 每個 WGS84 控制點的元素數（緯度、經度、高度）
constexpr std::size_t VALUES_PER_CONTROL_POINT = 3;

/**
 * @brief builtin_interfaces/Time 轉換
 * @param stamp DDS 時間
 * @return 核心時間
 */
RosTime to_core_time(const builtin_interfaces_msg_dds__Time_& stamp)
{
    RosTime time;
    time.sec = stamp.sec;
    time.nanosec = stamp.nanosec;
    return time;
}

/**
 * @brief DDS 字串轉換；未設定的字串為空
 * @param text DDS 字串（可為 nullptr）
 * @return QString
 */
QString to_core_string(const char* text)
{
    return QString::fromUtf8(text != nullptr ? text : "");
}

}  // namespace

AgentStateData to_core(const taiphoon_agent_msg_dds__AgentState_& sample)
{
    AgentStateData state;
    state.stamp = to_core_time(sample.stamp);
    for (std::size_t i = 0; i < state.position_ned.size(); ++i)
    {
        state.position_ned.at(i) = sample.position_ned[i];
        state.position_global.at(i) = sample.position_global[i];
    }
    state.flight_mode = sample.flight_mode;
    state.battery_remaining = sample.battery_remaining;
    state.battery_voltage_v = sample.battery_voltage_v;
    state.battery_current_a = sample.battery_current_a;
    state.is_online = sample.online;
    state.is_armed = sample.armed;
    state.is_landed = sample.landed;
    state.px4_nav_state = sample.px4_nav_state;
    state.is_px4_state_valid = sample.px4_state_valid;
    return state;
}

AgentAttitudeData to_core(const taiphoon_agent_msg_dds__AgentAttitude_& sample)
{
    AgentAttitudeData attitude;
    attitude.stamp = to_core_time(sample.stamp);
    for (std::size_t i = 0; i < attitude.q.size(); ++i)
    {
        attitude.q.at(i) = sample.q[i];
    }
    return attitude;
}

NedTargetData to_core(const taiphoon_agent_msg_dds__NedTarget_& sample)
{
    NedTargetData target;
    target.x = sample.x;
    target.y = sample.y;
    target.z = sample.z;
    target.yaw = sample.yaw;
    return target;
}

WaypointSequenceData to_core(const taiphoon_agent_msg_dds__WaypointSequence_& sample)
{
    WaypointSequenceData sequence;
    sequence.stamp = to_core_time(sample.stamp);
    if (sample.waypoints._buffer == nullptr)
    {
        return sequence;
    }
    sequence.waypoints.reserve(sample.waypoints._length);
    for (uint32_t i = 0; i < sample.waypoints._length; ++i)
    {
        const auto& source = sample.waypoints._buffer[i];
        WaypointData waypoint;
        for (std::size_t axis = 0; axis < waypoint.position_ned.size(); ++axis)
        {
            waypoint.position_ned.at(axis) = source.position_ned[axis];
        }
        waypoint.arrival_time = to_core_time(source.arrival_time);
        sequence.waypoints.push_back(waypoint);
    }
    return sequence;
}

AgentTrajectoryData to_core(const taiphoon_agent_msg_dds__AgentTrajectory_& sample)
{
    AgentTrajectoryData trajectory;
    trajectory.stamp = to_core_time(sample.stamp);
    trajectory.start_time = to_core_time(sample.start_time);
    trajectory.trajectory_id = sample.trajectory_id;
    trajectory.agent_index = sample.agent_index;
    trajectory.coordinate_frame = sample.coordinate_frame;
    trajectory.frame_revision = sample.frame_revision;
    trajectory.order = sample.order;
    trajectory.knot_interval_s = sample.knot_interval_s;
    trajectory.duration_s = sample.duration_s;
    trajectory.is_terminal_hold = sample.terminal_hold;
    trajectory.is_valid = sample.valid;
    if (sample.control_points._buffer != nullptr)
    {
        const std::size_t usable =
            sample.control_points._length - (sample.control_points._length % VALUES_PER_CONTROL_POINT);
        trajectory.control_points.assign(sample.control_points._buffer, sample.control_points._buffer + usable);
    }
    return trajectory;
}

WaypointMissionStatusData to_core(const taiphoon_agent_msg_dds__WaypointMissionStatus_& sample)
{
    WaypointMissionStatusData status;
    status.stamp = to_core_time(sample.stamp);
    status.mission_id = sample.mission_id;
    status.state = sample.state;
    status.reason = sample.reason;
    status.current_index = sample.current_index;
    status.waypoint_count = sample.waypoint_count;
    status.detail = to_core_string(sample.detail);
    return status;
}

AgentEventData to_core(const taiphoon_agent_msg_dds__AgentEvent_& sample)
{
    AgentEventData event;
    event.stamp = to_core_time(sample.stamp);
    // IDL 欄位名稱 sequence 與 IDL 關鍵字衝突，由 msg2idl 改為 sequence_
    event.sequence = sample.sequence_;
    event.severity = sample.severity;
    event.source = sample.source;
    event.code = sample.code;
    event.command = to_core_string(sample.command);
    event.px4_result = sample.px4_result;
    event.detail = to_core_string(sample.detail);
    return event;
}

AgentHealthData to_core(const taiphoon_agent_msg_dds__AgentHealth_& sample)
{
    AgentHealthData health;
    health.stamp = to_core_time(sample.stamp);
    health.px4_status_age_s = sample.px4_status_age_s;
    health.px4_odometry_age_s = sample.px4_odometry_age_s;
    health.px4_global_position_age_s = sample.px4_global_position_age_s;
    health.px4_land_detected_age_s = sample.px4_land_detected_age_s;
    health.px4_battery_age_s = sample.px4_battery_age_s;
    health.px4_command_ack_age_s = sample.px4_command_ack_age_s;
    health.is_px4_pre_flight_checks_pass = sample.px4_pre_flight_checks_pass;
    health.is_px4_failsafe = sample.px4_failsafe;
    health.px4_arming_state = sample.px4_arming_state;
    health.px4_latest_arming_reason = sample.px4_latest_arming_reason;
    health.is_failsafe_flags_valid = sample.failsafe_flags_valid;
    health.failsafe_flags = sample.failsafe_flags;
    health.is_offboard_session_active = sample.offboard_session_active;
    return health;
}

taiphoon_agent_msg_dds__WaypointMission_ to_dds(const WaypointMissionData& mission, const RosTime& stamp,
                                                std::vector<taiphoon_agent_msg_dds__MissionWaypoint_>* waypoint_buffer)
{
    taiphoon_agent_msg_dds__WaypointMission_ sample{};
    sample.stamp.sec = stamp.sec;
    sample.stamp.nanosec = stamp.nanosec;
    sample.mission_id = mission.mission_id;
    sample.altitude_frame = mission.altitude_frame;
    if (waypoint_buffer == nullptr)
    {
        return sample;
    }
    waypoint_buffer->clear();
    waypoint_buffer->reserve(mission.waypoints.size());
    for (const MissionWaypointData& waypoint : mission.waypoints)
    {
        taiphoon_agent_msg_dds__MissionWaypoint_ target{};
        target.latitude_deg = waypoint.latitude_deg;
        target.longitude_deg = waypoint.longitude_deg;
        target.altitude_m = waypoint.altitude_m;
        target.dwell_s = waypoint.dwell_s;
        target.yaw_deg = waypoint.yaw_deg;
        waypoint_buffer->push_back(target);
    }
    sample.waypoints._buffer = waypoint_buffer->empty() ? nullptr : waypoint_buffer->data();
    sample.waypoints._length = static_cast<uint32_t>(waypoint_buffer->size());
    sample.waypoints._maximum = static_cast<uint32_t>(waypoint_buffer->size());
    // 緩衝屬於呼叫端，DDS 不得釋放
    sample.waypoints._release = false;
    return sample;
}

}  // namespace gcs::link
