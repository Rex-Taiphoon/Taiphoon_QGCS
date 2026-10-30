/**
 * @file test_qos_profile.cpp
 * @brief QoS 表與 Agent 介面文件第 8 節一致性測試
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 介面文件 QoS 變更時，本測試的期望值必須同步更新。
 */
#include "link/qos_profile.hpp"

#include <gtest/gtest.h>

#include <vector>

using namespace gcs::link;

namespace
{

/// 介面文件第 8 節的期望值
struct ExpectedQos
{
    AgentChannel channel;
    EndpointRole role;
    Reliability reliability;
    Durability durability;
    int32_t depth;
};

constexpr int32_t EXPECTED_DEPTH = 10;

const std::vector<ExpectedQos> INTERFACE_TABLE = {
    {AgentChannel::STATE, EndpointRole::READER, Reliability::RELIABLE, Durability::VOLATILE, 10},
    {AgentChannel::ATTITUDE, EndpointRole::READER, Reliability::RELIABLE, Durability::VOLATILE, 10},
    {AgentChannel::NED_TARGET, EndpointRole::READER, Reliability::BEST_EFFORT, Durability::VOLATILE, 10},
    {AgentChannel::NED_TARGET, EndpointRole::WRITER, Reliability::RELIABLE, Durability::VOLATILE, 10},
    {AgentChannel::WAYPOINTS, EndpointRole::READER, Reliability::BEST_EFFORT, Durability::VOLATILE, 10},
    {AgentChannel::WAYPOINTS, EndpointRole::WRITER, Reliability::RELIABLE, Durability::VOLATILE, 10},
    {AgentChannel::PLANNING_TRAJECTORY, EndpointRole::READER, Reliability::RELIABLE, Durability::VOLATILE, 10},
    {AgentChannel::PLANNING_STATUS, EndpointRole::READER, Reliability::RELIABLE, Durability::VOLATILE, 10},
    {AgentChannel::FORMATION_EPOCHS, EndpointRole::READER, Reliability::RELIABLE, Durability::TRANSIENT_LOCAL, 10},
    {AgentChannel::COMMAND, EndpointRole::WRITER, Reliability::RELIABLE, Durability::VOLATILE, 10},
    {AgentChannel::SHUTDOWN_SERVICE, EndpointRole::WRITER, Reliability::RELIABLE, Durability::VOLATILE, 10},
    {AgentChannel::SHUTDOWN_SERVICE, EndpointRole::READER, Reliability::RELIABLE, Durability::VOLATILE, 10},
    {AgentChannel::MISSION_PLAN, EndpointRole::WRITER, Reliability::RELIABLE, Durability::VOLATILE, 10},
    {AgentChannel::MISSION_STATUS, EndpointRole::READER, Reliability::RELIABLE, Durability::TRANSIENT_LOCAL, 1},
    {AgentChannel::EVENTS, EndpointRole::READER, Reliability::RELIABLE, Durability::VOLATILE, 20},
    {AgentChannel::HEALTH, EndpointRole::READER, Reliability::RELIABLE, Durability::VOLATILE, 1},
};

}  // namespace

TEST(QosProfile, MatchesInterfaceSection8)
{
    for (const auto& expected : INTERFACE_TABLE)
    {
        const QosProfile profile = qos_for(expected.channel, expected.role);
        SCOPED_TRACE(static_cast<int>(expected.channel));
        EXPECT_EQ(profile.reliability, expected.reliability);
        EXPECT_EQ(profile.durability, expected.durability);
        EXPECT_EQ(profile.depth, expected.depth);
    }
}

TEST(QosProfile, WriterForcesXcdr1)
{
    const auto qos = create_dds_qos(qos_for(AgentChannel::COMMAND, EndpointRole::WRITER), EndpointRole::WRITER);
    ASSERT_NE(qos, nullptr);

    uint32_t count = 0;
    dds_data_representation_id_t* values = nullptr;
    ASSERT_TRUE(dds_qget_data_representation(qos.get(), &count, &values));
    ASSERT_EQ(count, 1U);
    ASSERT_NE(values, nullptr);
    EXPECT_EQ(values[0], DDS_DATA_REPRESENTATION_XCDR1);
    dds_free(values);
}

TEST(QosProfile, DdsQosReflectsProfile)
{
    const auto qos =
        create_dds_qos(qos_for(AgentChannel::FORMATION_EPOCHS, EndpointRole::READER), EndpointRole::READER);
    ASSERT_NE(qos, nullptr);

    dds_reliability_kind_t reliability{};
    dds_duration_t max_blocking_time = 0;
    ASSERT_TRUE(dds_qget_reliability(qos.get(), &reliability, &max_blocking_time));
    EXPECT_EQ(reliability, DDS_RELIABILITY_RELIABLE);

    dds_durability_kind_t durability{};
    ASSERT_TRUE(dds_qget_durability(qos.get(), &durability));
    EXPECT_EQ(durability, DDS_DURABILITY_TRANSIENT_LOCAL);

    dds_history_kind_t history{};
    int32_t depth = 0;
    ASSERT_TRUE(dds_qget_history(qos.get(), &history, &depth));
    EXPECT_EQ(history, DDS_HISTORY_KEEP_LAST);
    EXPECT_EQ(depth, EXPECTED_DEPTH);

    // Reader 不限制資料表示法，可接收 XCDR1 與 XCDR2。
    uint32_t count = 0;
    dds_data_representation_id_t* values = nullptr;
    EXPECT_FALSE(dds_qget_data_representation(qos.get(), &count, &values));
}
