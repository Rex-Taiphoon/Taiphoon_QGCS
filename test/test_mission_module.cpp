/**
 * @file test_mission_module.cpp
 * @brief 航點任務模組：上傳內容、載入後才開始、依 Agent 狀態追蹤進度、逾時、停止、失去聯繫與接手追蹤
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 *
 * 只測模組本身：上傳與命令請求以 QSignalSpy 攔截，回報由測試模擬 Guard／CommandModule 與 Agent 送入。
 * 上傳與命令必須經 Guard 的組裝驗證在 test_gcs_wiring.cpp。
 */
#include "core/command_protocol.hpp"
#include "modules/mission/mission_module.hpp"

#include <QSignalSpy>
#include <QTest>

#include <gtest/gtest.h>

#include <cmath>

using namespace gcs;

namespace
{

const QString AGENT_1 = QStringLiteral("uav_1_agent");
const QString MISSION_START = QStringLiteral("MISSION_START");
const QString MISSION_STOP = QStringLiteral("MISSION_STOP");
/// 等待逾時（毫秒），需大於下方測試參數
constexpr int WAIT_MS = 1000;
/// 測試中由其他地面站開始的任務識別碼
constexpr uint32_t FOREIGN_MISSION_ID = 77;

/** @brief 縮短的時間參數，讓測試在數百毫秒內完成 */
MissionTiming fast_timing()
{
    MissionTiming timing;
    timing.dwell_ms = 2500;
    timing.tick_ms = 10;
    timing.handshake_timeout_ms = 100;
    timing.status_timeout_ms = 150;
    return timing;
}

/**
 * @brief 往北排列的任務
 * @param count 航點數
 * @return 任務
 */
MissionPlan north_plan(int count)
{
    MissionPlan plan;
    plan.agent = AGENT_1;
    for (int i = 1; i <= count; ++i)
    {
        plan.waypoints.append({25.0 + 0.0001 * i, 121.5, 5.0 + i});
    }
    return plan;
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
 * @class MissionModuleTest
 * @brief 初始化完成的模組與各輸出的 spy
 */
class MissionModuleTest : public ::testing::Test
{
protected:
    void SetUp() override { ASSERT_TRUE(mission_.init()); }

    /** @brief 最後一筆任務狀態 */
    MissionStatus last_status() const { return statuses_.last().at(0).value<MissionStatus>(); }

    /** @brief 第 index 筆命令請求 */
    CommandRequest command_at(int index) const { return commands_.at(index).at(0).value<CommandRequest>(); }

    /** @brief 最後一次上傳的任務 */
    WaypointMissionData last_upload() const { return uploads_.last().at(1).value<WaypointMissionData>(); }

    /**
     * @brief 開始任務並模擬上傳成功
     * @param count 航點數
     * @return 上傳的任務識別碼
     */
    uint32_t start_uploaded(int count = 2)
    {
        mission_.start_mission(north_plan(count));
        EXPECT_FALSE(uploads_.isEmpty());
        if (uploads_.isEmpty())
        {
            return 0;
        }
        mission_.on_mission_upload_acked(command_protocol::make_ack(AGENT_1, MISSION_UPLOAD_ACK_NAME, true));
        return last_upload().mission_id;
    }

    /**
     * @brief 開始任務並模擬 Agent 載入、MISSION_START 送出與開始執行
     * @return 任務識別碼
     */
    uint32_t start_running()
    {
        const uint32_t mission_id = start_uploaded();
        mission_.on_mission_status_updated(AGENT_1, agent_status(mission_id, AgentMissionState::LOADED));
        mission_.on_command_acked(command_protocol::make_ack(AGENT_1, MISSION_START, true));
        mission_.on_mission_status_updated(AGENT_1, agent_status(mission_id, AgentMissionState::RUNNING));
        return mission_id;
    }

    MissionModule mission_{fast_timing()};
    QSignalSpy uploads_{&mission_, &MissionModule::mission_upload_requested};
    QSignalSpy commands_{&mission_, &MissionModule::command_requested};
    QSignalSpy statuses_{&mission_, &MissionModule::mission_status_changed};
};

}  // namespace

TEST_F(MissionModuleTest, StartUploadsWaypointsWithoutSendingCommands)
{
    mission_.start_mission(north_plan(3));
    ASSERT_EQ(uploads_.count(), 1);
    EXPECT_EQ(uploads_.last().at(0).toString(), AGENT_1);
    const WaypointMissionData upload = last_upload();
    EXPECT_NE(upload.mission_id, 0U);
    EXPECT_EQ(upload.altitude_frame, MISSION_ALTITUDE_PX4_LOCAL);
    ASSERT_EQ(upload.waypoints.size(), 3U);
    EXPECT_DOUBLE_EQ(upload.waypoints[2].latitude_deg, 25.0003);
    EXPECT_DOUBLE_EQ(upload.waypoints[2].longitude_deg, 121.5);
    EXPECT_FLOAT_EQ(upload.waypoints[2].altitude_m, 8.0F);
    for (const MissionWaypointData& waypoint : upload.waypoints)
    {
        EXPECT_FLOAT_EQ(waypoint.dwell_s, 2.5F);
        EXPECT_TRUE(std::isnan(waypoint.yaw_deg));
    }
    EXPECT_EQ(commands_.count(), 0);
    ASSERT_EQ(statuses_.count(), 1);
    EXPECT_EQ(last_status().phase, MissionPhase::STARTING);
    EXPECT_EQ(last_status().waypoint_count, 3);
    EXPECT_TRUE(mission_.is_running(AGENT_1));
}

TEST_F(MissionModuleTest, MissionStartOnlyAfterUploadAckAndLoaded)
{
    mission_.start_mission(north_plan(2));
    const uint32_t mission_id = last_upload().mission_id;

    // 上傳尚未確認送出前的 LOADED 不算
    mission_.on_mission_status_updated(AGENT_1, agent_status(mission_id, AgentMissionState::LOADED));
    EXPECT_EQ(commands_.count(), 0);

    mission_.on_mission_upload_acked(command_protocol::make_ack(AGENT_1, MISSION_UPLOAD_ACK_NAME, true));
    // 其他任務的 LOADED 不算
    mission_.on_mission_status_updated(AGENT_1, agent_status(mission_id + 1, AgentMissionState::LOADED));
    EXPECT_EQ(commands_.count(), 0);

    mission_.on_mission_status_updated(AGENT_1, agent_status(mission_id, AgentMissionState::LOADED));
    ASSERT_EQ(commands_.count(), 1);
    EXPECT_EQ(command_at(0).agent, AGENT_1);
    EXPECT_EQ(command_at(0).command, MISSION_START);
    ASSERT_EQ(command_at(0).params.size(), 1);
    EXPECT_EQ(command_at(0).params.at(0), static_cast<double>(mission_id));
    EXPECT_TRUE(command_protocol::validate_request(command_at(0)).isEmpty());

    // 週期重送的 LOADED 不會再送一次
    mission_.on_mission_status_updated(AGENT_1, agent_status(mission_id, AgentMissionState::LOADED));
    EXPECT_EQ(commands_.count(), 1);
}

TEST_F(MissionModuleTest, UploadRejectionReportsReasonAndIgnoresLateLoaded)
{
    mission_.start_mission(north_plan(2));
    const uint32_t mission_id = last_upload().mission_id;
    mission_.on_mission_upload_acked(
        command_protocol::make_ack(AGENT_1, MISSION_UPLOAD_ACK_NAME, false, QStringLiteral("需在懸停狀態才能開始任務")));
    EXPECT_EQ(last_status().phase, MissionPhase::REJECTED);
    EXPECT_EQ(last_status().message, QStringLiteral("需在懸停狀態才能開始任務"));
    EXPECT_FALSE(mission_.is_running(AGENT_1));

    mission_.on_mission_status_updated(AGENT_1, agent_status(mission_id, AgentMissionState::LOADED));
    EXPECT_EQ(commands_.count(), 0);
}

TEST_F(MissionModuleTest, AgentRejectionReportsWaypointAndReason)
{
    const uint32_t mission_id = start_uploaded();
    WaypointMissionStatusData rejected = agent_status(mission_id, AgentMissionState::REJECTED, 1);
    rejected.reason = static_cast<uint8_t>(AgentMissionReason::OUT_OF_RANGE);
    mission_.on_mission_status_updated(AGENT_1, rejected);
    EXPECT_EQ(last_status().phase, MissionPhase::REJECTED);
    EXPECT_TRUE(last_status().message.contains(QStringLiteral("第 2 個航點"))) << last_status().message.toStdString();
    EXPECT_FALSE(mission_.is_running(AGENT_1));
    EXPECT_EQ(commands_.count(), 0);
}

TEST_F(MissionModuleTest, StatusOfOtherMissionIgnoredWhileStarting)
{
    const uint32_t mission_id = start_uploaded();
    const int count = statuses_.count();
    // 上一個任務的 COMPLETED 仍在週期重送
    mission_.on_mission_status_updated(AGENT_1, agent_status(mission_id + 1, AgentMissionState::COMPLETED, 1));
    EXPECT_EQ(statuses_.count(), count);
    EXPECT_TRUE(mission_.is_running(AGENT_1));
}

TEST_F(MissionModuleTest, ProgressFollowsAgentStatusWithoutRepeats)
{
    const uint32_t mission_id = start_running();
    EXPECT_EQ(last_status().phase, MissionPhase::MOVING);
    EXPECT_EQ(last_status().waypoint_index, 0);
    EXPECT_EQ(last_status().waypoint_count, 2);
    const int count = statuses_.count();

    // 每秒重送的相同狀態不重複回報
    mission_.on_mission_status_updated(AGENT_1, agent_status(mission_id, AgentMissionState::RUNNING));
    EXPECT_EQ(statuses_.count(), count);

    mission_.on_mission_status_updated(AGENT_1, agent_status(mission_id, AgentMissionState::DWELLING));
    EXPECT_EQ(last_status().phase, MissionPhase::DWELLING);
    mission_.on_mission_status_updated(AGENT_1, agent_status(mission_id, AgentMissionState::RUNNING, 1));
    EXPECT_EQ(last_status().phase, MissionPhase::MOVING);
    EXPECT_EQ(last_status().waypoint_index, 1);
    mission_.on_mission_status_updated(AGENT_1, agent_status(mission_id, AgentMissionState::COMPLETED, 1));
    EXPECT_EQ(last_status().phase, MissionPhase::COMPLETED);
    EXPECT_FALSE(mission_.is_running(AGENT_1));
    EXPECT_EQ(statuses_.count(), count + 3);
    // 任務期間只送過 MISSION_START
    EXPECT_EQ(commands_.count(), 1);
}

TEST_F(MissionModuleTest, AbortedReportsAgentReason)
{
    const uint32_t mission_id = start_running();
    WaypointMissionStatusData aborted = agent_status(mission_id, AgentMissionState::ABORTED);
    aborted.reason = static_cast<uint8_t>(AgentMissionReason::COMMAND_OVERRIDE);
    aborted.detail = QStringLiteral("HOLD");
    mission_.on_mission_status_updated(AGENT_1, aborted);
    EXPECT_EQ(last_status().phase, MissionPhase::ABORTED);
    EXPECT_TRUE(last_status().message.contains(QStringLiteral("HOLD")));
    EXPECT_FALSE(mission_.is_running(AGENT_1));
    EXPECT_EQ(commands_.count(), 1);
}

TEST_F(MissionModuleTest, MissionStartNotSentReportsRejected)
{
    const uint32_t mission_id = start_uploaded();
    mission_.on_mission_status_updated(AGENT_1, agent_status(mission_id, AgentMissionState::LOADED));
    ASSERT_EQ(commands_.count(), 1);
    mission_.on_command_acked(
        command_protocol::make_ack(AGENT_1, MISSION_START, false, QStringLiteral("目前狀態不允許此命令")));
    EXPECT_EQ(last_status().phase, MissionPhase::REJECTED);
    EXPECT_TRUE(last_status().message.contains(QStringLiteral("目前狀態不允許此命令")));
    EXPECT_FALSE(mission_.is_running(AGENT_1));
}

TEST_F(MissionModuleTest, OtherCommandAcksDoNotChangeMission)
{
    start_running();
    const int count = statuses_.count();
    // 是否中止由 Agent 判斷並回報，QGCS 不自行推論
    mission_.on_command_acked(command_protocol::make_ack(AGENT_1, QStringLiteral("HOLD"), true));
    mission_.on_command_acked(command_protocol::make_ack(ALL_AGENTS_TARGET, QStringLiteral("MODE_RTL"), true));
    EXPECT_EQ(statuses_.count(), count);
    EXPECT_TRUE(mission_.is_running(AGENT_1));
}

TEST_F(MissionModuleTest, NoLoadedWithinTimeoutRejects)
{
    start_uploaded();
    QTRY_COMPARE_WITH_TIMEOUT(last_status().phase, MissionPhase::REJECTED, WAIT_MS);
    EXPECT_TRUE(last_status().message.contains(QStringLiteral("未回應"))) << last_status().message.toStdString();
    EXPECT_FALSE(mission_.is_running(AGENT_1));
    EXPECT_EQ(commands_.count(), 0);
}

TEST_F(MissionModuleTest, LateRunningAfterStartTimeoutIsFollowed)
{
    const uint32_t mission_id = start_uploaded();
    mission_.on_mission_status_updated(AGENT_1, agent_status(mission_id, AgentMissionState::LOADED));
    mission_.on_command_acked(command_protocol::make_ack(AGENT_1, MISSION_START, true));
    QTRY_COMPARE_WITH_TIMEOUT(last_status().phase, MissionPhase::REJECTED, WAIT_MS);

    // Agent 之後才開始：依週期重送的狀態接手追蹤
    mission_.on_mission_status_updated(AGENT_1, agent_status(mission_id, AgentMissionState::RUNNING));
    EXPECT_EQ(last_status().phase, MissionPhase::MOVING);
    EXPECT_TRUE(mission_.is_running(AGENT_1));
}

TEST_F(MissionModuleTest, AdoptsRunningMissionWhenJoiningLate)
{
    // 晚加入時收到的舊任務結果不回報
    mission_.on_mission_status_updated(AGENT_1, agent_status(FOREIGN_MISSION_ID, AgentMissionState::COMPLETED, 1));
    mission_.on_mission_status_updated(AGENT_1, agent_status(0, AgentMissionState::IDLE));
    EXPECT_EQ(statuses_.count(), 0);

    mission_.on_mission_status_updated(AGENT_1, agent_status(FOREIGN_MISSION_ID, AgentMissionState::RUNNING, 1, 4));
    EXPECT_EQ(last_status().phase, MissionPhase::MOVING);
    EXPECT_EQ(last_status().waypoint_index, 1);
    EXPECT_EQ(last_status().waypoint_count, 4);
    EXPECT_TRUE(mission_.is_running(AGENT_1));

    // 接手後的結果照常回報，也可停止
    mission_.stop_mission(AGENT_1);
    ASSERT_EQ(commands_.count(), 1);
    EXPECT_EQ(command_at(0).command, MISSION_STOP);
    mission_.on_mission_status_updated(AGENT_1, agent_status(FOREIGN_MISSION_ID, AgentMissionState::COMPLETED, 3, 4));
    EXPECT_EQ(last_status().phase, MissionPhase::COMPLETED);
}

TEST_F(MissionModuleTest, StopSendsMissionStopAndWaitsForAgent)
{
    const uint32_t mission_id = start_running();
    const int count = statuses_.count();
    mission_.stop_mission(AGENT_1);
    ASSERT_EQ(commands_.count(), 2);
    EXPECT_EQ(command_at(1).agent, AGENT_1);
    EXPECT_EQ(command_at(1).command, MISSION_STOP);
    EXPECT_TRUE(command_at(1).params.isEmpty());
    // 停下與否以 Agent 回報為準
    EXPECT_EQ(statuses_.count(), count);

    WaypointMissionStatusData aborted = agent_status(mission_id, AgentMissionState::ABORTED);
    aborted.reason = static_cast<uint8_t>(AgentMissionReason::OPERATOR_STOP);
    mission_.on_mission_status_updated(AGENT_1, aborted);
    EXPECT_EQ(last_status().phase, MissionPhase::ABORTED);
    EXPECT_FALSE(mission_.is_running(AGENT_1));
}

TEST_F(MissionModuleTest, StopWhileStartingAbortsAndClearsAgent)
{
    const uint32_t mission_id = start_uploaded();
    mission_.stop_mission(AGENT_1);
    EXPECT_EQ(last_status().phase, MissionPhase::ABORTED);
    EXPECT_FALSE(mission_.is_running(AGENT_1));
    ASSERT_EQ(commands_.count(), 1);
    EXPECT_EQ(command_at(0).command, MISSION_STOP);

    // 停止後才到的 LOADED 不會再送出 MISSION_START
    mission_.on_mission_status_updated(AGENT_1, agent_status(mission_id, AgentMissionState::LOADED));
    EXPECT_EQ(commands_.count(), 1);
}

TEST_F(MissionModuleTest, StopWithoutMissionSendsNothing)
{
    mission_.stop_mission(AGENT_1);
    EXPECT_EQ(commands_.count(), 0);
    EXPECT_EQ(statuses_.count(), 0);
}

TEST_F(MissionModuleTest, OfflineReportsLostAndResumesWithAgentStatus)
{
    const uint32_t mission_id = start_running();
    mission_.on_agent_offline(AGENT_1);
    EXPECT_EQ(last_status().phase, MissionPhase::LOST);
    EXPECT_EQ(last_status().waypoint_index, 0);
    // Agent 繼續執行：不視為中止，也不送任何命令
    EXPECT_EQ(commands_.count(), 1);

    // 重新連上後由 transient_local 取得最新狀態
    mission_.on_mission_status_updated(AGENT_1, agent_status(mission_id, AgentMissionState::COMPLETED, 1));
    EXPECT_EQ(last_status().phase, MissionPhase::COMPLETED);
}

TEST_F(MissionModuleTest, StaleFleetReportsLostOnce)
{
    start_running();
    FleetSnapshot snapshot;
    snapshot.offline = {AGENT_1};
    mission_.on_fleet_changed(snapshot);
    EXPECT_EQ(last_status().phase, MissionPhase::LOST);
    const int count = statuses_.count();
    mission_.on_fleet_changed(snapshot);
    EXPECT_EQ(statuses_.count(), count);
}

TEST_F(MissionModuleTest, OfflineWhileStartingReportsLost)
{
    start_uploaded();
    mission_.on_agent_offline(AGENT_1);
    EXPECT_EQ(last_status().phase, MissionPhase::LOST);
    EXPECT_FALSE(mission_.is_running(AGENT_1));
}

TEST_F(MissionModuleTest, MissingStatusReportsLost)
{
    start_running();
    QTRY_COMPARE_WITH_TIMEOUT(last_status().phase, MissionPhase::LOST, WAIT_MS);
    EXPECT_EQ(commands_.count(), 1);
}

TEST_F(MissionModuleTest, AgentIdleWhileRunningEndsMission)
{
    start_running();
    // Agent 重新啟動後任務遺失，回報 IDLE
    mission_.on_mission_status_updated(AGENT_1, agent_status(0, AgentMissionState::IDLE));
    EXPECT_EQ(last_status().phase, MissionPhase::ABORTED);
    EXPECT_FALSE(mission_.is_running(AGENT_1));
}

TEST_F(MissionModuleTest, SecondStartWhileRunningRejectedWithoutUpload)
{
    start_running();
    mission_.start_mission(north_plan(1));
    EXPECT_EQ(last_status().phase, MissionPhase::REJECTED);
    EXPECT_EQ(uploads_.count(), 1);
    EXPECT_TRUE(mission_.is_running(AGENT_1));
}

TEST_F(MissionModuleTest, NewMissionAfterCompletedUsesNewId)
{
    const uint32_t first_id = start_running();
    mission_.on_mission_status_updated(AGENT_1, agent_status(first_id, AgentMissionState::COMPLETED, 1));
    mission_.start_mission(north_plan(1));
    ASSERT_EQ(uploads_.count(), 2);
    EXPECT_NE(last_upload().mission_id, first_id);
    EXPECT_EQ(last_status().phase, MissionPhase::STARTING);
}

TEST_F(MissionModuleTest, RejectsAllTargetAndUninitialized)
{
    MissionPlan plan = north_plan(1);
    plan.agent = ALL_AGENTS_TARGET;
    mission_.start_mission(plan);
    EXPECT_EQ(last_status().phase, MissionPhase::REJECTED);
    EXPECT_EQ(uploads_.count(), 0);

    MissionModule uninitialized(fast_timing());
    QSignalSpy statuses(&uninitialized, &MissionModule::mission_status_changed);
    QSignalSpy uploads(&uninitialized, &MissionModule::mission_upload_requested);
    uninitialized.start_mission(north_plan(1));
    EXPECT_EQ(uploads.count(), 0);
    ASSERT_EQ(statuses.count(), 1);
    EXPECT_EQ(statuses.last().at(0).value<MissionStatus>().phase, MissionPhase::REJECTED);
}

TEST_F(MissionModuleTest, UnknownAgentStateIgnored)
{
    start_running();
    const int count = statuses_.count();
    WaypointMissionStatusData unknown = agent_status(1, AgentMissionState::IDLE);
    unknown.state = 99;
    mission_.on_mission_status_updated(AGENT_1, unknown);
    EXPECT_EQ(statuses_.count(), count);
    EXPECT_TRUE(mission_.is_running(AGENT_1));
}
