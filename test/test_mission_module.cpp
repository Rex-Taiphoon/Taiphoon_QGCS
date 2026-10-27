/**
 * @file test_mission_module.cpp
 * @brief 航點任務模組：開始條件、逐段 MOVE_POINT、到達與停留（含定位跳動與資料中斷）、各種中止條件
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 *
 * 只測模組本身：command_requested 以 QSignalSpy 攔截，回報由測試模擬 Guard／CommandModule 送入。
 * 命令必須經 Guard 的組裝驗證在 test_gcs_wiring.cpp。
 */
#include "fakes/fake_agent_link.hpp"
#include "modules/mission/mission_module.hpp"

#include <QSignalSpy>
#include <QTest>

#include <gtest/gtest.h>

#include <cmath>

using namespace gcs;
using gcs::test::make_state;

namespace
{

const QString AGENT_1 = QStringLiteral("uav_1_agent");
const QString MOVE_POINT = QStringLiteral("MOVE_POINT");
/// 起點：緯度 25、經度 121.5，PX4 local 位置 (10, 20, -5)
constexpr double START_LATITUDE = 25.0;
constexpr double START_LONGITUDE = 121.5;
/// 0.0001 度緯度約 11.13 公尺
constexpr double NORTH_STEP_DEGREES = 0.0001;
constexpr double NORTH_STEP_METERS = 11.131949;
/// 等待逾時、停留等計時事件（毫秒），需大於下方測試參數
constexpr int WAIT_MS = 400;

/** @brief 縮短的時間參數，讓測試在數百毫秒內完成 */
MissionTiming fast_timing()
{
    MissionTiming timing;
    timing.dwell_ms = 50;
    timing.tick_ms = 10;
    timing.arrival_tolerance_m = 1.0;
    timing.hover_settle_ms = 60;
    timing.leg_base_timeout_ms = 200;
    timing.min_expected_speed_mps = 1000.0;
    timing.near_hold_ms = 30;
    // 多數測試以單筆狀態模擬整段移動；跳點判斷另以實際參數測試
    timing.max_plausible_speed_mps = 1000.0;
    return timing;
}

/**
 * @brief 判斷到達用的時間參數：逾時放寬，只測到達與暫停
 * @return 時間參數
 */
MissionTiming arrival_timing()
{
    MissionTiming timing = fast_timing();
    timing.leg_base_timeout_ms = 5000;
    return timing;
}

/**
 * @brief 懸停中的狀態
 * @param north PX4 local 北向位置
 * @param latitude_offset 相對起點的緯度差
 * @param mode 任務階段
 * @param down PX4 local Down 位置（公尺，高度取負值）
 * @return AgentState
 */
AgentStateData hover_state(double north = 10.0, double latitude_offset = 0.0, FlightState mode = FlightState::HOVER,
                           double down = -5.0)
{
    AgentStateData state = make_state(mode, true, false);
    state.position_ned = {static_cast<float>(north), 20.0F, static_cast<float>(down)};
    state.position_global = {START_LATITUDE + latitude_offset, START_LONGITUDE, 50.0};
    return state;
}

/** @brief 機頭朝東（90 度）的姿態 */
AgentAttitudeData east_attitude()
{
    AgentAttitudeData attitude;
    const auto half = static_cast<float>(std::sqrt(0.5));
    attitude.q = {half, 0.0F, 0.0F, half};
    return attitude;
}

/**
 * @brief 建立任務：每個航點在前一個的正北 0.0001 度
 * @param count 航點數
 * @param altitude 高度（公尺）
 * @return 任務
 */
MissionPlan north_plan(int count, double altitude = 8.0)
{
    MissionPlan plan;
    plan.agent = AGENT_1;
    for (int i = 1; i <= count; ++i)
    {
        plan.waypoints.append({START_LATITUDE + NORTH_STEP_DEGREES * i, START_LONGITUDE, altitude});
    }
    return plan;
}

/**
 * @brief 初始化模組並送入懸停狀態與姿態
 * @param mission 模組
 */
void prepare(MissionModule* mission)
{
    EXPECT_TRUE(mission->init());
    mission->on_state_updated(AGENT_1, hover_state());
    mission->on_attitude_updated(AGENT_1, east_attitude());
}

/** @brief 最後一筆任務狀態 */
MissionStatus last_of(const QSignalSpy& statuses)
{
    return statuses.last().at(0).value<MissionStatus>();
}

/**
 * @class MissionModuleTest
 * @brief 初始化完成、已收到懸停狀態與姿態的模組
 */
class MissionModuleTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(mission_.init());
        mission_.on_state_updated(AGENT_1, hover_state());
        mission_.on_attitude_updated(AGENT_1, east_attitude());
    }

    /** @brief 第 index 筆命令請求 */
    CommandRequest request_at(int index) { return commands_.at(index).at(0).value<CommandRequest>(); }

    /** @brief 最後一筆狀態 */
    MissionStatus last_status() { return statuses_.last().at(0).value<MissionStatus>(); }

    /** @brief 模擬 CommandModule 成功送出 */
    void ack_ok(const QString& command, const QString& agent = AGENT_1)
    {
        mission_.on_command_acked({agent, command, true, QString()});
    }

    /**
     * @brief 模擬 Agent 移動後在指定位置、航點高度 8 公尺懸停
     * @param north 到達的 PX4 local 北向位置
     * @param latitude_offset 到達位置的緯度差
     */
    void fly_to(double north, double latitude_offset)
    {
        constexpr double WAYPOINT_DOWN = -8.0;
        mission_.on_state_updated(AGENT_1, hover_state(north, latitude_offset, FlightState::MOVING, WAYPOINT_DOWN));
        mission_.on_state_updated(AGENT_1, hover_state(north, latitude_offset, FlightState::HOVER, WAYPOINT_DOWN));
    }

    MissionModule mission_{fast_timing()};
    QSignalSpy commands_{&mission_, &MissionModule::command_requested};
    QSignalSpy statuses_{&mission_, &MissionModule::mission_status_changed};
};

}  // namespace

TEST_F(MissionModuleTest, RejectsInvalidStartWithoutCommands)
{
    MissionPlan all = north_plan(1);
    all.agent = ALL_AGENTS_TARGET;
    mission_.start_mission(all);
    mission_.start_mission(north_plan(0));
    mission_.start_mission(north_plan(1, 0.5));
    mission_.start_mission(north_plan(1, 130.0));
    MissionPlan far = north_plan(1);
    far.waypoints[0].latitude += 0.01;  // 約 1.1 公里
    mission_.start_mission(far);

    EXPECT_EQ(commands_.count(), 0);
    ASSERT_EQ(statuses_.count(), 5);
    for (const auto& args : statuses_)
    {
        EXPECT_EQ(args.at(0).value<MissionStatus>().phase, MissionPhase::REJECTED);
    }
    EXPECT_FALSE(mission_.is_running(AGENT_1));
}

TEST_F(MissionModuleTest, RequiresHoverValidPx4AndAttitude)
{
    mission_.on_state_updated(AGENT_1, hover_state(10.0, 0.0, FlightState::TAKEOFF));
    mission_.start_mission(north_plan(1));
    EXPECT_TRUE(last_status().message.contains(QStringLiteral("懸停")));

    AgentStateData invalid = hover_state();
    invalid.is_px4_state_valid = false;
    mission_.on_state_updated(AGENT_1, invalid);
    mission_.start_mission(north_plan(1));
    EXPECT_TRUE(last_status().message.contains(QStringLiteral("飛控")));

    MissionModule no_attitude{fast_timing()};
    ASSERT_TRUE(no_attitude.init());
    no_attitude.on_state_updated(AGENT_1, hover_state());
    QSignalSpy no_attitude_commands(&no_attitude, &MissionModule::command_requested);
    no_attitude.start_mission(north_plan(1));
    EXPECT_EQ(no_attitude_commands.count(), 0);

    EXPECT_EQ(commands_.count(), 0);
}

TEST_F(MissionModuleTest, FirstLegTargetsWaypointInLocalFrameKeepingHeading)
{
    mission_.start_mission(north_plan(2, 8.0));

    ASSERT_EQ(commands_.count(), 1);
    const CommandRequest request = request_at(0);
    EXPECT_EQ(request.agent, AGENT_1);
    EXPECT_EQ(request.command, MOVE_POINT);
    ASSERT_EQ(request.params.size(), 4);
    // 目前 position_ned (10, 20, -5) 加上往北 11.13 公尺；高度 8 公尺 → Down -8；機頭維持 90 度
    EXPECT_NEAR(request.params[0], 10.0 + NORTH_STEP_METERS, 0.01);
    EXPECT_NEAR(request.params[1], 20.0, 0.01);
    EXPECT_NEAR(request.params[2], -8.0, 0.01);
    EXPECT_NEAR(request.params[3], 90.0, 0.1);
    EXPECT_EQ(last_status().phase, MissionPhase::MOVING);
    EXPECT_EQ(last_status().waypoint_index, 0);
    EXPECT_TRUE(mission_.is_running(AGENT_1));
}

TEST_F(MissionModuleTest, VisitsEachWaypointWithDwellThenCompletes)
{
    mission_.start_mission(north_plan(2));
    ack_ok(MOVE_POINT);

    fly_to(10.0 + NORTH_STEP_METERS, NORTH_STEP_DEGREES);
    QTRY_COMPARE_WITH_TIMEOUT(last_status().phase, MissionPhase::DWELLING, WAIT_MS);
    EXPECT_EQ(commands_.count(), 1);

    // 停留結束後才送第二段，且以目前位置換算
    QTRY_COMPARE_WITH_TIMEOUT(commands_.count(), 2, WAIT_MS);
    EXPECT_NEAR(request_at(1).params[0], 10.0 + 2 * NORTH_STEP_METERS, 0.01);
    EXPECT_EQ(last_status().waypoint_index, 1);
    ack_ok(MOVE_POINT);

    fly_to(10.0 + 2 * NORTH_STEP_METERS, 2 * NORTH_STEP_DEGREES);
    QTRY_VERIFY_WITH_TIMEOUT(!mission_.is_running(AGENT_1), WAIT_MS);
    EXPECT_EQ(last_status().phase, MissionPhase::COMPLETED);

    // 完成後懸停，不再送任何命令
    QTest::qWait(WAIT_MS);
    EXPECT_EQ(commands_.count(), 2);
}

TEST_F(MissionModuleTest, HoverBeforeMovingIsNotArrival)
{
    mission_.start_mission(north_plan(1));
    ack_ok(MOVE_POINT);
    // 命令剛送出，Agent 尚未轉 MOVING 仍在原地懸停
    mission_.on_state_updated(AGENT_1, hover_state());
    QTest::qWait(100);
    EXPECT_TRUE(mission_.is_running(AGENT_1));
    EXPECT_EQ(last_status().phase, MissionPhase::MOVING);
}

TEST_F(MissionModuleTest, SettledHoverAfterMovingCountsAsArrivalEvenIfOffset)
{
    mission_.start_mission(north_plan(1));
    ack_ok(MOVE_POINT);
    // Agent 的 planner 原點與 PX4 local 不同時，停下的位置會與預期相差一個固定量
    fly_to(10.0 + NORTH_STEP_METERS + 5.0, NORTH_STEP_DEGREES);
    EXPECT_TRUE(mission_.is_running(AGENT_1));
    QTRY_VERIFY_WITH_TIMEOUT(!mission_.is_running(AGENT_1), WAIT_MS);
    EXPECT_EQ(last_status().phase, MissionPhase::COMPLETED);
}

TEST_F(MissionModuleTest, ArrivalWithoutMovingStateWhenStoppedAwayFromStart)
{
    // SITL 實測：EGO-Swarm 執行 MOVE_POINT 時 flight_mode 一直是 HOVER；以「離開起點後停止」判斷到達
    mission_.start_mission(north_plan(1));
    ack_ok(MOVE_POINT);
    constexpr double WAYPOINT_DOWN = -8.0;
    // 中途仍在移動：每次位置都超過停止半徑，不算到達
    mission_.on_state_updated(AGENT_1, hover_state(14.0, 0.0, FlightState::HOVER, WAYPOINT_DOWN));
    QTest::qWait(30);
    mission_.on_state_updated(AGENT_1, hover_state(17.0, 0.0, FlightState::HOVER, WAYPOINT_DOWN));
    EXPECT_TRUE(mission_.is_running(AGENT_1));
    // 停在離預期位置 4 公尺處（planner 原點偏移）
    mission_.on_state_updated(AGENT_1, hover_state(10.0 + NORTH_STEP_METERS + 4.0, 0.0, FlightState::HOVER,
                                                   WAYPOINT_DOWN));
    QTRY_VERIFY_WITH_TIMEOUT(!mission_.is_running(AGENT_1), WAIT_MS);
    EXPECT_EQ(last_status().phase, MissionPhase::COMPLETED);
}

TEST(MissionArrivalTest, NearWaypointMustPersistBeforeArrival)
{
    MissionTiming timing = arrival_timing();
    timing.near_hold_ms = 200;
    // 只測接近：停止判斷放寬到測試期間不會成立
    timing.hover_settle_ms = 5000;
    MissionModule mission{timing};
    prepare(&mission);
    QSignalSpy statuses(&mission, &MissionModule::mission_status_changed);
    mission.start_mission(north_plan(1));
    mission.on_command_acked({AGENT_1, MOVE_POINT, true, QString()});

    // 定位單筆跳到航點，下一筆又回到原處：不算到達
    constexpr double WAYPOINT_DOWN = -8.0;
    const AgentStateData at_waypoint =
        hover_state(10.0 + NORTH_STEP_METERS, NORTH_STEP_DEGREES, FlightState::HOVER, WAYPOINT_DOWN);
    mission.on_state_updated(AGENT_1, at_waypoint);
    mission.on_state_updated(AGENT_1, hover_state());
    QTest::qWait(WAIT_MS);
    EXPECT_TRUE(mission.is_running(AGENT_1));

    // 持續在航點範圍內才算到達
    mission.on_state_updated(AGENT_1, at_waypoint);
    EXPECT_TRUE(mission.is_running(AGENT_1));
    QTRY_VERIFY_WITH_TIMEOUT(!mission.is_running(AGENT_1), WAIT_MS);
    EXPECT_EQ(last_of(statuses).phase, MissionPhase::COMPLETED);
}

TEST(MissionArrivalTest, PositionJumpIsNotDeparture)
{
    MissionTiming timing = arrival_timing();
    timing.max_plausible_speed_mps = 10.0;
    MissionModule mission{timing};
    prepare(&mission);
    QSignalSpy statuses(&mission, &MissionModule::mission_status_changed);
    mission.start_mission(north_plan(1));
    mission.on_command_acked({AGENT_1, MOVE_POINT, true, QString()});

    // 原地懸停時定位跳 5 公尺（超過離開起點的 2 公尺）：不算已離開，停在那裡也不算到達
    mission.on_state_updated(AGENT_1, hover_state(15.0));
    QTest::qWait(WAIT_MS);
    EXPECT_TRUE(mission.is_running(AGENT_1));
    EXPECT_EQ(last_of(statuses).phase, MissionPhase::MOVING);

    // 之後以合理速度實際移動 3 公尺並停下，才以停止判斷到達
    constexpr double STEP_METERS = 0.5;
    for (double north = 15.0 + STEP_METERS; north <= 18.0; north += STEP_METERS)
    {
        mission.on_state_updated(AGENT_1, hover_state(north));
    }
    QTRY_VERIFY_WITH_TIMEOUT(!mission.is_running(AGENT_1), WAIT_MS);
    EXPECT_EQ(last_of(statuses).phase, MissionPhase::COMPLETED);
}

TEST(MissionArrivalTest, StaleStatePausesArrivalAndNextLeg)
{
    // 與預設值相同的關係：狀態有效時間短於停止判斷時間
    MissionTiming timing = arrival_timing();
    timing.state_fresh_ms = 100;
    timing.hover_settle_ms = 200;
    timing.dwell_ms = 150;
    MissionModule mission{timing};
    prepare(&mission);
    QSignalSpy commands(&mission, &MissionModule::command_requested);
    QSignalSpy statuses(&mission, &MissionModule::mission_status_changed);
    mission.start_mission(north_plan(2));
    mission.on_command_acked({AGENT_1, MOVE_POINT, true, QString()});

    // SITL 實測：飛離起點 4 公尺後資料中斷，最後一筆位置不再變動，不能當成已停止
    mission.on_state_updated(AGENT_1, hover_state(14.0));
    QTest::qWait(WAIT_MS);
    EXPECT_EQ(last_of(statuses).phase, MissionPhase::MOVING);
    EXPECT_EQ(commands.count(), 1);

    // 資料恢復且在航點：重新計時後到達
    constexpr double WAYPOINT_DOWN = -8.0;
    const AgentStateData at_waypoint =
        hover_state(10.0 + NORTH_STEP_METERS, NORTH_STEP_DEGREES, FlightState::HOVER, WAYPOINT_DOWN);
    mission.on_state_updated(AGENT_1, at_waypoint);
    QTRY_COMPARE_WITH_TIMEOUT(last_of(statuses).phase, MissionPhase::DWELLING, WAIT_MS);

    // 停留時間到了但資料又中斷：不送下一段
    QTest::qWait(WAIT_MS);
    EXPECT_EQ(commands.count(), 1);

    // 恢復後才以目前位置換算並送出下一段
    mission.on_state_updated(AGENT_1, at_waypoint);
    QTRY_COMPARE_WITH_TIMEOUT(commands.count(), 2, WAIT_MS);
    EXPECT_NEAR(commands.last().at(0).value<CommandRequest>().params[0], 10.0 + 2 * NORTH_STEP_METERS, 0.01);
}

TEST(MissionArrivalTest, RejectsStartWithStaleState)
{
    MissionTiming timing = arrival_timing();
    timing.state_fresh_ms = 100;
    MissionModule mission{timing};
    prepare(&mission);
    QSignalSpy commands(&mission, &MissionModule::command_requested);
    QSignalSpy statuses(&mission, &MissionModule::mission_status_changed);

    QTest::qWait(WAIT_MS);
    mission.start_mission(north_plan(1));
    EXPECT_EQ(commands.count(), 0);
    EXPECT_EQ(last_of(statuses).phase, MissionPhase::REJECTED);
    EXPECT_TRUE(last_of(statuses).message.contains(QStringLiteral("未更新")));
}

TEST_F(MissionModuleTest, GuardRejectionAbortsMission)
{
    mission_.start_mission(north_plan(2));
    mission_.on_command_acked({AGENT_1, MOVE_POINT, false, QStringLiteral("目前狀態不允許此命令")});

    EXPECT_FALSE(mission_.is_running(AGENT_1));
    EXPECT_EQ(last_status().phase, MissionPhase::ABORTED);
    EXPECT_TRUE(last_status().message.contains(QStringLiteral("不允許")));
}

TEST_F(MissionModuleTest, OperatorCommandAbortsWithoutSendingCommands)
{
    mission_.start_mission(north_plan(2));
    ack_ok(MOVE_POINT);
    ack_ok(QStringLiteral("MODE_RTL"));

    EXPECT_FALSE(mission_.is_running(AGENT_1));
    EXPECT_EQ(last_status().phase, MissionPhase::ABORTED);
    QTest::qWait(WAIT_MS);
    EXPECT_EQ(commands_.count(), 1);
}

TEST_F(MissionModuleTest, AllTargetCommandAbortsMission)
{
    mission_.start_mission(north_plan(1));
    ack_ok(MOVE_POINT);
    ack_ok(QStringLiteral("MODE_LAND"), ALL_AGENTS_TARGET);
    EXPECT_FALSE(mission_.is_running(AGENT_1));
}

TEST_F(MissionModuleTest, LeavingHoverOrMovingAbortsMission)
{
    mission_.start_mission(north_plan(1));
    ack_ok(MOVE_POINT);
    mission_.on_state_updated(AGENT_1, hover_state(10.0, 0.0, FlightState::RTL));

    EXPECT_FALSE(mission_.is_running(AGENT_1));
    EXPECT_EQ(last_status().phase, MissionPhase::ABORTED);
}

TEST_F(MissionModuleTest, StopTargetsCurrentPositionAfterAborting)
{
    mission_.start_mission(north_plan(2));
    ack_ok(MOVE_POINT);
    // 飛到一半時停止：目標換成目前位置（HOLD 不會取消 EGO-Swarm 的軌跡）
    mission_.on_state_updated(AGENT_1, hover_state(14.5, 0.00004));
    mission_.stop_mission(AGENT_1);

    EXPECT_FALSE(mission_.is_running(AGENT_1));
    ASSERT_EQ(commands_.count(), 2);
    const CommandRequest stop = request_at(1);
    EXPECT_EQ(stop.command, MOVE_POINT);
    ASSERT_EQ(stop.params.size(), 4);
    EXPECT_NEAR(stop.params[0], 14.5, 0.01);
    EXPECT_NEAR(stop.params[1], 20.0, 0.01);
    EXPECT_NEAR(stop.params[2], -5.0, 0.01);
    EXPECT_NEAR(stop.params[3], 90.0, 0.1);
    // 停止命令的成功回報不再觸發任何處理
    ack_ok(MOVE_POINT);
    EXPECT_EQ(statuses_.count(), 2);

    // 沒有任務時停止不送命令
    mission_.stop_mission(AGENT_1);
    EXPECT_EQ(commands_.count(), 2);
}

TEST_F(MissionModuleTest, LegTimeoutAbortsWithoutCommands)
{
    mission_.start_mission(north_plan(1));
    ack_ok(MOVE_POINT);
    QTRY_VERIFY_WITH_TIMEOUT(!mission_.is_running(AGENT_1), WAIT_MS);
    EXPECT_EQ(last_status().phase, MissionPhase::ABORTED);
    EXPECT_TRUE(last_status().message.contains(QStringLiteral("逾時")));
    EXPECT_EQ(commands_.count(), 1);
}

TEST_F(MissionModuleTest, AgentOfflineAbortsMission)
{
    mission_.start_mission(north_plan(1));
    mission_.on_agent_offline(AGENT_1);
    EXPECT_FALSE(mission_.is_running(AGENT_1));
    EXPECT_EQ(last_status().phase, MissionPhase::ABORTED);
}

TEST_F(MissionModuleTest, StaleAgentInFleetSnapshotAbortsWithoutCommands)
{
    mission_.start_mission(north_plan(2));
    ack_ok(MOVE_POINT);

    FleetSnapshot snapshot;
    snapshot.online = QStringList{AGENT_1};
    mission_.on_fleet_changed(snapshot);
    EXPECT_TRUE(mission_.is_running(AGENT_1));

    // topic 仍在、只是資料停滯：不會收到 agent_offline，以顯示清單的離線為準
    snapshot.online.clear();
    snapshot.offline = QStringList{AGENT_1};
    mission_.on_fleet_changed(snapshot);
    EXPECT_FALSE(mission_.is_running(AGENT_1));
    EXPECT_EQ(last_status().phase, MissionPhase::ABORTED);
    EXPECT_EQ(commands_.count(), 1);

    // 停滯前的狀態已清除，資料恢復前不能以舊位置開始任務
    mission_.start_mission(north_plan(1));
    EXPECT_EQ(last_status().phase, MissionPhase::REJECTED);
    EXPECT_EQ(commands_.count(), 1);
}

TEST_F(MissionModuleTest, RejectsSecondStartWhileRunning)
{
    mission_.start_mission(north_plan(1));
    mission_.start_mission(north_plan(1));
    EXPECT_EQ(commands_.count(), 1);
    EXPECT_EQ(last_status().phase, MissionPhase::REJECTED);
    EXPECT_TRUE(mission_.is_running(AGENT_1));
}
