/**
 * @file test_gcs_wiring.cpp
 * @brief 組裝根整合測試：完整事件流，並證明命令無法繞過 Guard
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 以真實模組與 FakeAgentLink 組成核心，模擬 UI 只能使用的兩個入口：
 * GuardModule::request_command 與 AgentLifecycleModule::request_shutdown；
 * 航點任務模組的命令同樣必須經 Guard。
 */
#include "app/gcs_wiring.hpp"
#include "fakes/fake_agent_link.hpp"
#include "modules/command/command_module.hpp"
#include "modules/fleet/fleet_module.hpp"
#include "modules/guard/guard_module.hpp"
#include "modules/lifecycle/agent_lifecycle_module.hpp"
#include "modules/mission/mission_module.hpp"
#include "modules/telemetry/telemetry_module.hpp"

#include <QSignalSpy>
#include <QTest>

#include <gtest/gtest.h>

#include <cmath>

using namespace gcs;
using gcs::test::FakeAgentLink;
using gcs::test::make_request;
using gcs::test::make_state;

namespace
{

const QString AGENT_1 = QStringLiteral("uav_1_agent");
const QString AGENT_2 = QStringLiteral("uav_2_agent");

/** @brief 縮短的航點任務時間參數 */
MissionTiming fast_mission_timing()
{
    MissionTiming timing;
    timing.dwell_ms = 30;
    timing.tick_ms = 10;
    timing.hover_settle_ms = 30;
    timing.leg_base_timeout_ms = 1000;
    return timing;
}

/**
 * @brief 有 GPS 位置的懸停狀態
 * @param latitude 緯度
 * @param is_px4_state_valid PX4 狀態是否有效
 * @return AgentState
 */
AgentStateData hover_at(double latitude, bool is_px4_state_valid = true)
{
    AgentStateData state = make_state(FlightState::HOVER, true, false, PX4_NAV_STATE_OFFBOARD, is_px4_state_valid);
    state.position_global = {latitude, 121.5, 50.0};
    state.position_ned = {0.0F, 0.0F, -5.0F};
    return state;
}

/**
 * @class GcsWiringTest
 * @brief 完整組裝的核心模組
 */
class GcsWiringTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(fleet_.init());
        ASSERT_TRUE(telemetry_.init());
        ASSERT_TRUE(guard_.init());
        ASSERT_TRUE(command_.init());
        ASSERT_TRUE(lifecycle_.init());
        ASSERT_TRUE(mission_.init());
        ASSERT_TRUE(wire_core_modules({&fleet_, &telemetry_, &guard_, &command_, &lifecycle_, &mission_}));
    }

    /**
     * @brief 讓 Agent 懸停上線並送出姿態，可開始航點任務
     * @param latitude 目前緯度
     */
    void bring_online_hovering(double latitude)
    {
        bring_online(AGENT_1, hover_at(latitude));
        AgentAttitudeData attitude;
        attitude.q = {1.0F, 0.0F, 0.0F, 0.0F};
        emit link_.attitude_received(AGENT_1, attitude);
    }

    /**
     * @brief 讓 Agent 經探索上線並送出一筆狀態
     * @param agent Agent namespace
     * @param state AgentState
     */
    void bring_online(const QString& agent, const AgentStateData& state)
    {
        if (!link_.discovered_agents.contains(agent))
        {
            link_.discovered_agents.append(agent);
        }
        fleet_.scan_now();
        emit link_.state_received(agent, state);
    }

    FakeAgentLink link_;
    FleetModule fleet_{&link_};
    TelemetryModule telemetry_{&link_};
    GuardModule guard_;
    CommandModule command_{&link_};
    AgentLifecycleModule lifecycle_{&link_};
    MissionModule mission_{fast_mission_timing()};
};

}  // namespace

TEST_F(GcsWiringTest, RejectsIncompleteModules)
{
    EXPECT_FALSE(wire_core_modules({&fleet_, &telemetry_, nullptr, &command_, &lifecycle_, &mission_}));
    EXPECT_FALSE(wire_core_modules({&fleet_, &telemetry_, &guard_, &command_, &lifecycle_, nullptr}));
}

TEST_F(GcsWiringTest, MissionLegsReachLinkThroughGuard)
{
    constexpr double START = 25.0;
    constexpr double STEP = 0.0001;
    bring_online_hovering(START);
    MissionPlan plan;
    plan.agent = AGENT_1;
    plan.waypoints = {{START + STEP, 121.5, 5.0}, {START + 2 * STEP, 121.5, 5.0}};

    mission_.start_mission(plan);
    ASSERT_EQ(link_.published_commands.size(), 1U);
    EXPECT_EQ(link_.published_commands[0].target, AGENT_1);
    EXPECT_TRUE(link_.published_commands[0].command_line.startsWith(QStringLiteral("MOVE_POINT ")));
    EXPECT_TRUE(mission_.is_running(AGENT_1));

    // 到達第一個航點後，PX4 狀態暫時無效：任務不中止，但 Guard 必須擋下第二段
    AgentStateData moving = hover_at(START + STEP);
    moving.flight_mode = static_cast<uint8_t>(FlightState::MOVING);
    emit link_.state_received(AGENT_1, moving);
    emit link_.state_received(AGENT_1, hover_at(START + STEP));
    emit link_.state_received(AGENT_1, hover_at(START + STEP, false));
    QTRY_VERIFY_WITH_TIMEOUT(!mission_.is_running(AGENT_1), 1000);
    EXPECT_EQ(link_.published_commands.size(), 1U);
}

TEST_F(GcsWiringTest, MissionStopGoesThroughGuard)
{
    bring_online_hovering(25.0);
    MissionPlan plan;
    plan.agent = AGENT_1;
    plan.waypoints = {{25.0001, 121.5, 5.0}};
    mission_.start_mission(plan);
    mission_.stop_mission(AGENT_1);

    // 停在目前位置 (0, 0, -5)、機頭 0 度
    ASSERT_EQ(link_.published_commands.size(), 2U);
    EXPECT_EQ(link_.published_commands[1].command_line, QStringLiteral("MOVE_POINT 0 0 -5 0"));
    EXPECT_FALSE(mission_.is_running(AGENT_1));
}

TEST_F(GcsWiringTest, OperatorCommandThroughGuardAbortsMission)
{
    bring_online_hovering(25.0);
    MissionPlan plan;
    plan.agent = AGENT_1;
    plan.waypoints = {{25.0001, 121.5, 5.0}};
    mission_.start_mission(plan);
    ASSERT_TRUE(mission_.is_running(AGENT_1));

    guard_.request_command(make_request(AGENT_1, QStringLiteral("MODE_RTL")));
    EXPECT_FALSE(mission_.is_running(AGENT_1));
}

TEST_F(GcsWiringTest, DiscoveryCreatesTelemetrySubscription)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    EXPECT_TRUE(link_.subscribed_agents.contains(AGENT_1));
}

TEST_F(GcsWiringTest, AllowedCommandReachesLinkThroughGuard)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    QSignalSpy acked(&command_, &CommandModule::command_acked);

    guard_.request_command(make_request(AGENT_1, QStringLiteral("ARM")));

    ASSERT_EQ(link_.published_commands.size(), 1U);
    EXPECT_EQ(link_.published_commands[0].target, AGENT_1);
    EXPECT_EQ(link_.published_commands[0].command_line, QStringLiteral("ARM"));
    ASSERT_EQ(acked.size(), 1);
    EXPECT_TRUE(acked.takeFirst().at(0).value<CommandAck>().is_ok);
}

TEST_F(GcsWiringTest, CommandsRejectedByGuardNeverReachLink)
{
    // 情境：PX4 狀態無效、Agent 未上線、任務階段不允許、全體中有一台不允許
    bring_online(AGENT_1, make_state(FlightState::IDLE, false, true, PX4_NAV_STATE_OFFBOARD, false));
    guard_.request_command(make_request(AGENT_1, QStringLiteral("ARM")));
    guard_.request_command(make_request(QStringLiteral("uav_5_agent"), QStringLiteral("MODE_OFFBOARD")));

    emit link_.state_received(AGENT_1, make_state(FlightState::IDLE));
    guard_.request_command(make_request(AGENT_1, QStringLiteral("MODE_RTL")));
    guard_.request_command(make_request(AGENT_1, QStringLiteral("TAKEOFF")));

    bring_online(AGENT_2, make_state(FlightState::IDLE, true));
    guard_.request_command(make_request(ALL_AGENTS_TARGET, QStringLiteral("ARM")));

    EXPECT_TRUE(link_.published_commands.empty());
}

TEST_F(GcsWiringTest, StateUpdateFromLinkReachesGuard)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    emit link_.state_received(AGENT_1, make_state(FlightState::HOVER, true, false));
    EXPECT_EQ(guard_.agent_flight_state(AGENT_1), FlightState::HOVER);

    guard_.request_command(make_request(AGENT_1, QStringLiteral("MOVE_FORWARD"), {3}));
    ASSERT_EQ(link_.published_commands.size(), 1U);
    EXPECT_EQ(link_.published_commands[0].command_line, QStringLiteral("MOVE_FORWARD 3"));
}

TEST_F(GcsWiringTest, ShutdownAcceptedIsolatesAgentFromCommands)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    lifecycle_.request_shutdown(AGENT_1);
    ASSERT_EQ(link_.shutdown_requests.size(), 1U);

    emit link_.shutdown_replied(AGENT_1, link_.shutdown_requests[0].request_id, true, QStringLiteral("已接受"));

    EXPECT_FALSE(fleet_.online_agents().contains(AGENT_1));
    EXPECT_FALSE(link_.subscribed_agents.contains(AGENT_1));

    // 隔離後即使 DDS 探索仍殘留 topic，單機命令也必須被拒絕
    QSignalSpy rejected(&guard_, &GuardModule::command_acked);
    fleet_.scan_now();
    guard_.request_command(make_request(AGENT_1, QStringLiteral("MODE_OFFBOARD")));
    EXPECT_TRUE(link_.published_commands.empty());
    ASSERT_EQ(rejected.size(), 1);
    EXPECT_TRUE(rejected.takeFirst().at(0).value<CommandAck>().message.contains(QStringLiteral("不在線上")));
}

TEST_F(GcsWiringTest, VanishedAgentIsRejectedAndUnsubscribed)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    link_.discovered_agents.clear();
    fleet_.scan_now();

    EXPECT_FALSE(link_.subscribed_agents.contains(AGENT_1));
    guard_.request_command(make_request(AGENT_1, QStringLiteral("MODE_OFFBOARD")));
    EXPECT_TRUE(link_.published_commands.empty());
}
