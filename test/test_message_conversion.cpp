/**
 * @file test_message_conversion.cpp
 * @brief DDS 型別 → 核心型別轉換：欄位對應、WGS84 精度與控制點長度保護
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "link/message_conversion.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace gcs;

namespace
{
constexpr double EPSILON = 1e-12;
}  // namespace

TEST(MessageConversionTest, AgentStateFieldsMapOneToOne)
{
    taiphoon_agent_msg_dds__AgentState_ sample{};
    sample.stamp.sec = 10;
    sample.stamp.nanosec = 20;
    sample.position_ned[2] = -5.0F;
    sample.position_global[0] = 25.033964321;
    sample.position_global[1] = 121.564468123;
    sample.position_global[2] = std::numeric_limits<double>::quiet_NaN();
    sample.flight_mode = 5;
    sample.battery_remaining = 0.5F;
    sample.battery_voltage_v = 15.8F;
    sample.battery_current_a = 2.0F;
    sample.online = true;
    sample.armed = true;
    sample.landed = false;
    sample.px4_nav_state = 14;
    sample.px4_state_valid = true;

    const AgentStateData state = link::to_core(sample);
    EXPECT_EQ(state.stamp.sec, 10);
    EXPECT_EQ(state.stamp.nanosec, 20U);
    EXPECT_FLOAT_EQ(state.position_ned[2], -5.0F);
    EXPECT_NEAR(state.position_global[0], 25.033964321, EPSILON);
    EXPECT_NEAR(state.position_global[1], 121.564468123, EPSILON);
    EXPECT_TRUE(std::isnan(state.position_global[2]));
    EXPECT_EQ(state.flight_mode, 5);
    EXPECT_TRUE(state.is_online);
    EXPECT_TRUE(state.is_armed);
    EXPECT_FALSE(state.is_landed);
    EXPECT_EQ(state.px4_nav_state, 14);
    EXPECT_TRUE(state.is_px4_state_valid);
}

TEST(MessageConversionTest, TrajectoryDropsIncompleteTrailingPoint)
{
    double points[] = {25.0, 121.5, 10.0, 25.1, 121.6, 11.0, 25.2};
    taiphoon_agent_msg_dds__AgentTrajectory_ sample{};
    sample.trajectory_id = 3;
    sample.control_points._buffer = points;
    sample.control_points._length = 7;
    sample.terminal_hold = true;
    sample.valid = true;

    const AgentTrajectoryData trajectory = link::to_core(sample);
    EXPECT_EQ(trajectory.trajectory_id, 3U);
    ASSERT_EQ(trajectory.control_points.size(), 6U);
    EXPECT_NEAR(trajectory.control_points[5], 11.0, EPSILON);
    EXPECT_TRUE(trajectory.is_terminal_hold);
    EXPECT_TRUE(trajectory.is_valid);
}

TEST(MessageConversionTest, EmptySequencesAreSafe)
{
    taiphoon_agent_msg_dds__AgentTrajectory_ trajectory{};
    EXPECT_TRUE(link::to_core(trajectory).control_points.empty());

    taiphoon_agent_msg_dds__WaypointSequence_ waypoints{};
    EXPECT_TRUE(link::to_core(waypoints).waypoints.empty());
}

TEST(MessageConversionTest, WaypointsAndAttitude)
{
    taiphoon_agent_msg_dds__Waypoint_ points[2] = {};
    points[1].position_ned[0] = 3.0F;
    points[1].arrival_time.sec = 9;
    taiphoon_agent_msg_dds__WaypointSequence_ sequence{};
    sequence.waypoints._buffer = points;
    sequence.waypoints._length = 2;
    const WaypointSequenceData converted = link::to_core(sequence);
    ASSERT_EQ(converted.waypoints.size(), 2U);
    EXPECT_FLOAT_EQ(converted.waypoints[1].position_ned[0], 3.0F);
    EXPECT_EQ(converted.waypoints[1].arrival_time.sec, 9);

    taiphoon_agent_msg_dds__AgentAttitude_ attitude{};
    attitude.q[0] = 1.0F;
    attitude.q[3] = 0.5F;
    const AgentAttitudeData q = link::to_core(attitude);
    EXPECT_FLOAT_EQ(q.q[0], 1.0F);
    EXPECT_FLOAT_EQ(q.q[3], 0.5F);
}

TEST(MessageConversionTest, MissionStatusEventAndHealthFieldsMapOneToOne)
{
    char detail[] = "leg 2";
    taiphoon_agent_msg_dds__WaypointMissionStatus_ status{};
    status.stamp.sec = 7;
    status.mission_id = 4000000000U;
    status.state = 2;
    status.reason = 12;
    status.current_index = 1;
    status.waypoint_count = 3;
    status.detail = detail;
    const WaypointMissionStatusData converted_status = link::to_core(status);
    EXPECT_EQ(converted_status.stamp.sec, 7);
    EXPECT_EQ(converted_status.mission_id, 4000000000U);
    EXPECT_EQ(converted_status.state, 2);
    EXPECT_EQ(converted_status.reason, 12);
    EXPECT_EQ(converted_status.current_index, 1);
    EXPECT_EQ(converted_status.waypoint_count, 3);
    EXPECT_EQ(converted_status.detail, QStringLiteral("leg 2"));

    char command[] = "ARM";
    taiphoon_agent_msg_dds__AgentEvent_ event{};
    event.sequence_ = 42;
    event.severity = 3;
    event.source = 1;
    event.code = agent_event_code::PX4_COMMAND_REJECTED;
    event.command = command;
    event.px4_result = 4;
    const AgentEventData converted_event = link::to_core(event);
    EXPECT_EQ(converted_event.sequence, 42U);
    EXPECT_EQ(converted_event.severity, 3);
    EXPECT_EQ(converted_event.source, 1);
    EXPECT_EQ(converted_event.code, agent_event_code::PX4_COMMAND_REJECTED);
    EXPECT_EQ(converted_event.command, QStringLiteral("ARM"));
    EXPECT_EQ(converted_event.px4_result, 4);
    EXPECT_TRUE(converted_event.detail.isEmpty());

    taiphoon_agent_msg_dds__AgentHealth_ health{};
    health.px4_status_age_s = 2.5F;
    health.px4_command_ack_age_s = -1.0F;
    health.px4_pre_flight_checks_pass = true;
    health.px4_arming_state = 1;
    health.px4_latest_arming_reason = 7;
    health.failsafe_flags_valid = true;
    health.failsafe_flags = 0x5U;
    health.offboard_session_active = true;
    const AgentHealthData converted_health = link::to_core(health);
    EXPECT_FLOAT_EQ(converted_health.px4_status_age_s, 2.5F);
    EXPECT_FLOAT_EQ(converted_health.px4_command_ack_age_s, -1.0F);
    EXPECT_TRUE(converted_health.is_px4_pre_flight_checks_pass);
    EXPECT_FALSE(converted_health.is_px4_failsafe);
    EXPECT_EQ(converted_health.px4_arming_state, 1);
    EXPECT_EQ(converted_health.px4_latest_arming_reason, 7);
    EXPECT_TRUE(converted_health.is_failsafe_flags_valid);
    EXPECT_EQ(converted_health.failsafe_flags, 0x5U);
    EXPECT_TRUE(converted_health.is_offboard_session_active);
}

TEST(MessageConversionTest, WaypointMissionPointsAtCallerBuffer)
{
    WaypointMissionData mission;
    mission.mission_id = 9;
    mission.altitude_frame = MISSION_ALTITUDE_PX4_LOCAL;
    MissionWaypointData waypoint;
    waypoint.latitude_deg = 25.033964321;
    waypoint.longitude_deg = 121.564468123;
    waypoint.altitude_m = 10.0F;
    waypoint.dwell_s = 3.0F;
    mission.waypoints = {waypoint, waypoint};
    mission.waypoints[1].altitude_m = 15.0F;
    const RosTime stamp{11, 22};

    std::vector<taiphoon_agent_msg_dds__MissionWaypoint_> buffer;
    const taiphoon_agent_msg_dds__WaypointMission_ sample = link::to_dds(mission, stamp, &buffer);
    EXPECT_EQ(sample.stamp.sec, 11);
    EXPECT_EQ(sample.stamp.nanosec, 22U);
    EXPECT_EQ(sample.mission_id, 9U);
    EXPECT_EQ(sample.altitude_frame, MISSION_ALTITUDE_PX4_LOCAL);
    ASSERT_EQ(sample.waypoints._length, 2U);
    EXPECT_EQ(sample.waypoints._maximum, 2U);
    EXPECT_EQ(sample.waypoints._buffer, buffer.data());
    EXPECT_FALSE(sample.waypoints._release);
    EXPECT_NEAR(sample.waypoints._buffer[0].latitude_deg, 25.033964321, EPSILON);
    EXPECT_NEAR(sample.waypoints._buffer[0].longitude_deg, 121.564468123, EPSILON);
    EXPECT_FLOAT_EQ(sample.waypoints._buffer[0].dwell_s, 3.0F);
    EXPECT_FLOAT_EQ(sample.waypoints._buffer[1].altitude_m, 15.0F);
    EXPECT_TRUE(std::isnan(sample.waypoints._buffer[1].yaw_deg));

    const taiphoon_agent_msg_dds__WaypointMission_ without_buffer = link::to_dds(mission, stamp, nullptr);
    EXPECT_EQ(without_buffer.waypoints._length, 0U);
    EXPECT_EQ(without_buffer.waypoints._buffer, nullptr);
}
