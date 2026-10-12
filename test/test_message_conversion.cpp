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
