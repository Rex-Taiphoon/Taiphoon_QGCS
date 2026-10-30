/**
 * @file test_gcs_wiring.cpp
 * @brief 組裝根整合測試：完整事件流，並證明命令無法繞過 Guard
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 以真實模組與 FakeAgentLink 組成核心，模擬 UI 只能使用的兩個入口：
 * GuardModule::request_command 與 AgentLifecycleModule::request_shutdown；
 * 航點任務模組的上傳與 MISSION_START／MISSION_STOP 同樣必須經 Guard。
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
    timing.dwell_ms = 3000;
    timing.tick_ms = 10;
    timing.handshake_timeout_ms = 1000;
    timing.status_timeout_ms = 1000;
    return timing;
}

/**
 * @brief Agent 回報的任務狀態
 * @param mission_id 任務識別碼
 * @param state 狀態
 * @param index 目前航點
 * @param count 航點數
 * @return WaypointMissionStatus
 */
WaypointMissionStatusData agent_status(uint32_t mission_id, AgentMissionState state, uint16_t index = 0,
                                       uint16_t count = 2)
{
    WaypointMissionStatusData status;
    status.mission_id = mission_id;
    status.state = static_cast<uint8_t>(state);
    status.current_index = index;
    status.waypoint_count = count;
    return status;
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
     * @brief 讓 Agent 懸停上線，可開始航點任務
     * @param latitude 目前緯度
     */
    void bring_online_hovering(double latitude) { bring_online(AGENT_1, hover_at(latitude)); }

    /**
     * @brief 懸停上線後開始兩個航點的任務，並讓 Agent 回報已載入與執行中
     * @return Agent 收到的任務識別碼；上傳失敗時為 0
     */
    uint32_t start_running_mission()
    {
        bring_online_hovering(25.0);
        mission_.start_mission(two_waypoint_plan());
        if (link_.published_missions.size() != 1U)
        {
            return 0;
        }
        const uint32_t mission_id = link_.published_missions[0].mission.mission_id;
        emit link_.mission_status_received(AGENT_1, agent_status(mission_id, AgentMissionState::LOADED));
        emit link_.mission_status_received(AGENT_1, agent_status(mission_id, AgentMissionState::RUNNING));
        return mission_id;
    }

    /** @brief 往北兩個航點的任務 @return 任務 */
    static MissionPlan two_waypoint_plan()
    {
        MissionPlan plan;
        plan.agent = AGENT_1;
        plan.waypoints = {{25.0001, 121.5, 5.0}, {25.0002, 121.5, 6.0}};
        return plan;
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

TEST_F(GcsWiringTest, MissionUploadAndStartReachLinkThroughGuard)
{
    bring_online_hovering(25.0);
    QSignalSpy statuses(&mission_, &MissionModule::mission_status_changed);
    mission_.start_mission(two_waypoint_plan());

    // 先上傳航點，不送命令；高度基準為 PX4 local、每點停留 3 秒、維持目前航向
    ASSERT_EQ(link_.published_missions.size(), 1U);
    EXPECT_TRUE(link_.published_commands.empty());
    const auto& upload = link_.published_missions[0];
    EXPECT_EQ(upload.agent_ns, AGENT_1);
    EXPECT_NE(upload.mission.mission_id, 0U);
    EXPECT_EQ(upload.mission.altitude_frame, MISSION_ALTITUDE_PX4_LOCAL);
    ASSERT_EQ(upload.mission.waypoints.size(), 2U);
    EXPECT_DOUBLE_EQ(upload.mission.waypoints[1].latitude_deg, 25.0002);
    EXPECT_FLOAT_EQ(upload.mission.waypoints[1].altitude_m, 6.0F);
    EXPECT_FLOAT_EQ(upload.mission.waypoints[0].dwell_s, 3.0F);
    EXPECT_TRUE(std::isnan(upload.mission.waypoints[0].yaw_deg));

    // Agent 載入後才送出 MISSION_START，識別碼以整數表示
    const uint32_t mission_id = upload.mission.mission_id;
    emit link_.mission_status_received(AGENT_1, agent_status(mission_id, AgentMissionState::LOADED));
    ASSERT_EQ(link_.published_commands.size(), 1U);
    EXPECT_EQ(link_.published_commands[0].target, AGENT_1);
    EXPECT_EQ(link_.published_commands[0].command_line, QStringLiteral("MISSION_START %1").arg(mission_id));

    emit link_.mission_status_received(AGENT_1, agent_status(mission_id, AgentMissionState::RUNNING));
    EXPECT_TRUE(mission_.is_running(AGENT_1));
    EXPECT_EQ(statuses.last().at(0).value<MissionStatus>().phase, MissionPhase::MOVING);
}

TEST_F(GcsWiringTest, MissionUploadRejectedByGuardNeverReachesLink)
{
    // 未在懸停：Guard 擋下上傳，Agent 收不到航點
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    QSignalSpy statuses(&mission_, &MissionModule::mission_status_changed);
    mission_.start_mission(two_waypoint_plan());
    EXPECT_TRUE(link_.published_missions.empty());
    EXPECT_TRUE(link_.published_commands.empty());
    EXPECT_EQ(statuses.last().at(0).value<MissionStatus>().phase, MissionPhase::REJECTED);
    EXPECT_FALSE(mission_.is_running(AGENT_1));

    // 懸停但 PX4 狀態無效
    emit link_.state_received(AGENT_1, hover_at(25.0, false));
    mission_.start_mission(two_waypoint_plan());
    EXPECT_TRUE(link_.published_missions.empty());
    EXPECT_EQ(statuses.last().at(0).value<MissionStatus>().phase, MissionPhase::REJECTED);
}

TEST_F(GcsWiringTest, MissionStartRejectedByGuardWhenStateChanged)
{
    bring_online_hovering(25.0);
    mission_.start_mission(two_waypoint_plan());
    ASSERT_EQ(link_.published_missions.size(), 1U);
    const uint32_t mission_id = link_.published_missions[0].mission.mission_id;

    // 載入前飛行狀態已離開懸停：MISSION_START 被 Guard 擋下
    QSignalSpy statuses(&mission_, &MissionModule::mission_status_changed);
    AgentStateData moving = hover_at(25.0);
    moving.flight_mode = static_cast<uint8_t>(FlightState::MOVING);
    emit link_.state_received(AGENT_1, moving);
    emit link_.mission_status_received(AGENT_1, agent_status(mission_id, AgentMissionState::LOADED));
    EXPECT_TRUE(link_.published_commands.empty());
    ASSERT_EQ(statuses.count(), 1);
    EXPECT_EQ(statuses.last().at(0).value<MissionStatus>().phase, MissionPhase::REJECTED);
    EXPECT_FALSE(mission_.is_running(AGENT_1));
}

TEST_F(GcsWiringTest, MissionStopGoesThroughGuardEvenWhenPx4StateInvalid)
{
    ASSERT_NE(start_running_mission(), 0U);
    ASSERT_EQ(link_.published_commands.size(), 1U);

    // 任務中 PX4 狀態暫時無效：其他命令被擋，MISSION_STOP 仍必須送得出去
    emit link_.state_received(AGENT_1, hover_at(25.0, false));
    guard_.request_command(make_request(AGENT_1, QStringLiteral("HOLD")));
    EXPECT_EQ(link_.published_commands.size(), 1U);
    mission_.stop_mission(AGENT_1);
    ASSERT_EQ(link_.published_commands.size(), 2U);
    EXPECT_EQ(link_.published_commands[1].command_line, QStringLiteral("MISSION_STOP"));
}

TEST_F(GcsWiringTest, MissionProgressFollowsAgentStatusOnly)
{
    const uint32_t mission_id = start_running_mission();
    ASSERT_NE(mission_id, 0U);
    QSignalSpy statuses(&mission_, &MissionModule::mission_status_changed);

    // 操作人員的其他命令由 Agent 判斷是否中止任務；QGCS 依回報顯示，不自行送出任何命令
    guard_.request_command(make_request(AGENT_1, QStringLiteral("HOLD")));
    ASSERT_EQ(link_.published_commands.size(), 2U);
    EXPECT_TRUE(mission_.is_running(AGENT_1));
    WaypointMissionStatusData aborted = agent_status(mission_id, AgentMissionState::ABORTED);
    aborted.reason = static_cast<uint8_t>(AgentMissionReason::COMMAND_OVERRIDE);
    emit link_.mission_status_received(AGENT_1, aborted);
    EXPECT_FALSE(mission_.is_running(AGENT_1));
    EXPECT_EQ(statuses.last().at(0).value<MissionStatus>().phase, MissionPhase::ABORTED);
    EXPECT_EQ(link_.published_commands.size(), 2U);
}

TEST_F(GcsWiringTest, StaleAgentReportsLostWithoutCommands)
{
    ASSERT_NE(start_running_mission(), 0U);
    QSignalSpy statuses(&mission_, &MissionModule::mission_status_changed);

    // topic 仍在、資料停滯超過 STALE_TIMEOUT：Agent 會繼續執行，QGCS 只回報失去聯繫
    QSignalSpy offline(&fleet_, &FleetModule::agent_offline);
    fleet_.scan_at(FleetModule::Clock::now() + FleetModule::STALE_TIMEOUT + std::chrono::seconds(1));
    EXPECT_EQ(offline.count(), 0);
    ASSERT_GE(statuses.count(), 1);
    EXPECT_EQ(statuses.last().at(0).value<MissionStatus>().phase, MissionPhase::LOST);
    QTest::qWait(100);
    EXPECT_EQ(link_.published_commands.size(), 1U);
}

TEST_F(GcsWiringTest, ForcedRebootReachesLinkOnlyWhenPx4StateUnknown)
{
    const auto normal = make_request(AGENT_1, QStringLiteral("REBOOT_FC"));
    const auto forced = make_request(AGENT_1, QStringLiteral("REBOOT_FC"), {1.0});

    // Agent 收到 odometry 前沒有 AgentState：還沒收到健康狀態時不可強制
    link_.discovered_agents.append(AGENT_1);
    fleet_.scan_now();
    guard_.request_command(forced);
    EXPECT_TRUE(link_.published_commands.empty());

    // 健康狀態經 Telemetry 送到 Guard：只能強制
    AgentHealthData health;
    health.px4_arming_state = px4_arming::DISARMED;
    emit link_.health_received(AGENT_1, health);
    guard_.request_command(normal);
    EXPECT_TRUE(link_.published_commands.empty());
    guard_.request_command(forced);
    ASSERT_EQ(link_.published_commands.size(), 1U);
    EXPECT_EQ(link_.published_commands[0].command_line, QStringLiteral("REBOOT_FC 1"));

    // 狀態有效、上鎖落地：只接受一般重新啟動
    emit link_.state_received(AGENT_1, make_state(FlightState::IDLE));
    guard_.request_command(forced);
    EXPECT_EQ(link_.published_commands.size(), 1U);
    guard_.request_command(normal);
    ASSERT_EQ(link_.published_commands.size(), 2U);
    EXPECT_EQ(link_.published_commands[1].command_line, QStringLiteral("REBOOT_FC"));

    // 最後一次有效時在空中，PX4 斷線後不可強制
    emit link_.state_received(AGENT_1, make_state(FlightState::HOVER, true, false));
    emit link_.state_received(AGENT_1, make_state(FlightState::ERROR, false, true, PX4_NAV_STATE_OFFBOARD, false));
    guard_.request_command(forced);
    EXPECT_EQ(link_.published_commands.size(), 2U);
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
