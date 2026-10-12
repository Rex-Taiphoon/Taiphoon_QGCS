/**
 * @file test_telemetry_module.cpp
 * @brief TelemetryModule：依上下線管理訂閱、只轉發已訂閱 Agent、保留 WGS84 精度
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "fakes/fake_agent_link.hpp"
#include "modules/telemetry/telemetry_module.hpp"

#include <QSignalSpy>

#include <gtest/gtest.h>

using namespace gcs;
using gcs::test::FakeAgentLink;
using gcs::test::make_state;

namespace
{

const QString AGENT_1 = QStringLiteral("uav_1_agent");

/// 雙精度比較容許誤差
constexpr double EPSILON = 1e-12;

/**
 * @class TelemetryModuleTest
 * @brief 以 FakeAgentLink 驅動的 TelemetryModule
 */
class TelemetryModuleTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(telemetry_.init());
    }

    FakeAgentLink link_;
    TelemetryModule telemetry_{&link_};
};

}  // namespace

TEST_F(TelemetryModuleTest, SubscribesOnOnlineAndUnsubscribesOnOffline)
{
    telemetry_.on_agent_online(AGENT_1);
    EXPECT_TRUE(link_.subscribed_agents.contains(AGENT_1));
    EXPECT_TRUE(telemetry_.is_subscribed(AGENT_1));

    telemetry_.on_agent_offline(AGENT_1);
    EXPECT_FALSE(link_.subscribed_agents.contains(AGENT_1));
    EXPECT_FALSE(telemetry_.is_subscribed(AGENT_1));
}

TEST_F(TelemetryModuleTest, ForwardsOnlySubscribedAgents)
{
    QSignalSpy updated(&telemetry_, &TelemetryModule::state_updated);
    emit link_.state_received(AGENT_1, make_state(FlightState::HOVER));
    EXPECT_TRUE(updated.isEmpty());

    telemetry_.on_agent_online(AGENT_1);
    emit link_.state_received(AGENT_1, make_state(FlightState::HOVER));
    ASSERT_EQ(updated.size(), 1);

    // 離線後佇列中殘留的資料不得讓 Agent 重新出現
    telemetry_.on_agent_offline(AGENT_1);
    emit link_.state_received(AGENT_1, make_state(FlightState::HOVER));
    EXPECT_EQ(updated.size(), 1);
}

TEST_F(TelemetryModuleTest, PreservesSmallWgs84Movement)
{
    telemetry_.on_agent_online(AGENT_1);
    QSignalSpy updated(&telemetry_, &TelemetryModule::state_updated);

    AgentStateData state = make_state(FlightState::HOVER);
    state.position_global = {25.033964321, 121.564468123, 123.456789012};
    emit link_.state_received(AGENT_1, state);
    // 低於 float32 量化間隔的厘米尺度移動也必須保留
    state.position_global[1] += 0.0000001;
    emit link_.state_received(AGENT_1, state);

    ASSERT_EQ(updated.size(), 2);
    const auto first = updated.at(0).at(1).value<AgentStateData>();
    const auto second = updated.at(1).at(1).value<AgentStateData>();
    EXPECT_NEAR(first.position_global[0], 25.033964321, EPSILON);
    EXPECT_NEAR(second.position_global[1] - first.position_global[1], 0.0000001, EPSILON);
}

TEST_F(TelemetryModuleTest, ForwardsTrajectoryControlPoints)
{
    telemetry_.on_agent_online(AGENT_1);
    QSignalSpy updated(&telemetry_, &TelemetryModule::trajectory_updated);

    AgentTrajectoryData trajectory;
    trajectory.trajectory_id = 7;
    trajectory.control_points = {25.0, 121.5, 10.0, 25.1, 121.6, 11.0};
    emit link_.trajectory_received(AGENT_1, trajectory);

    ASSERT_EQ(updated.size(), 1);
    const auto received = updated.at(0).at(1).value<AgentTrajectoryData>();
    EXPECT_EQ(received.trajectory_id, 7U);
    EXPECT_EQ(received.control_points.size(), 6U);
}

TEST_F(TelemetryModuleTest, ShutdownReleasesAllSubscriptions)
{
    telemetry_.on_agent_online(AGENT_1);
    telemetry_.on_agent_online(QStringLiteral("uav_2_agent"));
    telemetry_.shutdown();
    EXPECT_TRUE(link_.subscribed_agents.isEmpty());
}
