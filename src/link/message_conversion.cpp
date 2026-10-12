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

}  // namespace gcs::link
