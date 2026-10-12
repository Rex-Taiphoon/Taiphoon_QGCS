/**
 * @file test_ros_naming.cpp
 * @brief ROS2 ↔ DDS 名稱對應規則測試
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "link/ros_naming.hpp"

#include <gtest/gtest.h>

using namespace gcs::link;

TEST(RosNaming, TopicUsesRtPrefix)
{
    EXPECT_EQ(to_dds_topic("/uav_1_agent/state"), "rt/uav_1_agent/state");
    EXPECT_EQ(to_dds_topic("/uav_all_agent/command"), "rt/uav_all_agent/command");
    EXPECT_EQ(to_dds_topic("/uav_2_agent/planning/trajectory"), "rt/uav_2_agent/planning/trajectory");
}

TEST(RosNaming, TopicRejectsNonQualifiedName)
{
    EXPECT_FALSE(to_dds_topic("uav_1_agent/state").has_value());
    EXPECT_FALSE(to_dds_topic("/").has_value());
    EXPECT_FALSE(to_dds_topic("/uav_1_agent/").has_value());
    EXPECT_FALSE(to_dds_topic("/uav 1/state").has_value());
}

TEST(RosNaming, FromDdsTopicOnlyAcceptsRegularTopics)
{
    EXPECT_EQ(from_dds_topic("rt/uav_2_agent/state"), "/uav_2_agent/state");
    EXPECT_FALSE(from_dds_topic("rq/uav_1_agent/shutdownRequest").has_value());
    EXPECT_FALSE(from_dds_topic("rr/uav_1_agent/shutdownReply").has_value());
    EXPECT_FALSE(from_dds_topic("DCPSPublication").has_value());
    EXPECT_FALSE(from_dds_topic("rt").has_value());
}

TEST(RosNaming, ServiceTopicsFollowRmwCyclonedds)
{
    EXPECT_EQ(to_service_request_topic("/uav_1_agent/shutdown"), "rq/uav_1_agent/shutdownRequest");
    EXPECT_EQ(to_service_reply_topic("/uav_1_agent/shutdown"), "rr/uav_1_agent/shutdownReply");
    EXPECT_FALSE(to_service_request_topic("uav_1_agent/shutdown").has_value());
}

TEST(RosNaming, AgentNamespaceIsOneBasedWithoutLeadingZero)
{
    EXPECT_TRUE(is_agent_namespace("uav_1_agent"));
    EXPECT_TRUE(is_agent_namespace("uav_12_agent"));
    EXPECT_FALSE(is_agent_namespace("uav_0_agent"));
    EXPECT_FALSE(is_agent_namespace("uav_01_agent"));
    EXPECT_FALSE(is_agent_namespace("uav_all_agent"));
    EXPECT_FALSE(is_agent_namespace("uav_1_agent_x"));
    EXPECT_FALSE(is_agent_namespace(""));
}

TEST(RosNaming, AgentNamespaceFromStateTopic)
{
    EXPECT_EQ(agent_namespace_from_state_topic("/uav_3_agent/state"), "uav_3_agent");
    EXPECT_FALSE(agent_namespace_from_state_topic("/uav_3_agent/state/extra").has_value());
    EXPECT_FALSE(agent_namespace_from_state_topic("/uav_3_agent/attitude").has_value());
    EXPECT_FALSE(agent_namespace_from_state_topic("/uav_all_agent/state").has_value());
    EXPECT_FALSE(agent_namespace_from_state_topic("uav_3_agent/state").has_value());
}

TEST(RosNaming, AgentTopicJoinsNamespaceAndSuffix)
{
    EXPECT_EQ(agent_topic("uav_1_agent", "planning/trajectory"), "/uav_1_agent/planning/trajectory");
}
