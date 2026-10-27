/**
 * @file test_mission_controller.cpp
 * @brief 航點任務介面：航點編輯、顯示原點換算、開始前確認與語音口令
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 */
#include "app/mission_controller.hpp"
#include "app/toast_model.hpp"
#include "fakes/fake_agent_link.hpp"
#include "voice/voice_keywords.hpp"

#include <QSignalSpy>
#include <QTest>
#include <QVariantMap>

#include <gtest/gtest.h>

using namespace gcs;
using gcs::test::make_state;

namespace
{

const QString AGENT_1 = QStringLiteral("uav_1_agent");
const QString AGENT_2 = QStringLiteral("uav_2_agent");
constexpr double COORDINATE_EPSILON = 1e-9;
/// 測試用確認時限（毫秒）
constexpr int SHORT_CONFIRM_MS = 1500;

/**
 * @brief 指定任務階段與高度的狀態
 * @param mode 任務階段
 * @param altitude 相對高度（公尺）
 * @return AgentState
 */
AgentStateData state_at(FlightState mode, double altitude = 7.26)
{
    AgentStateData state = make_state(mode, true, false);
    state.position_ned = {0.0F, 0.0F, static_cast<float>(-altitude)};
    state.position_global = {25.0, 121.5, 50.0};
    return state;
}

/**
 * @class MissionControllerTest
 * @brief 已選取單機且懸停中的控制器
 */
class MissionControllerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        controller_.set_toast_model(&toasts_);
        controller_.set_selected_agent(AGENT_1);
        controller_.on_state_updated(AGENT_1, state_at(FlightState::HOVER));
    }

    /** @brief 第 index 個航點的欄位 */
    QVariantMap waypoint_at(int index) const { return controller_.waypoints().at(index).toMap(); }

    /** @brief 送出的任務 */
    MissionPlan started_plan() { return started_.last().at(0).value<MissionPlan>(); }

    ToastModel toasts_;
    MissionController controller_{SHORT_CONFIRM_MS};
    QSignalSpy started_{&controller_, &MissionController::mission_start_requested};
    QSignalSpy stopped_{&controller_, &MissionController::mission_stop_requested};
};

}  // namespace

TEST_F(MissionControllerTest, AddsWaypointsAtCurrentAltitudeForSingleAgentOnly)
{
    controller_.addWaypoint(25.0001, 121.5);
    controller_.addWaypoint(25.0002, 121.5);
    ASSERT_EQ(controller_.waypoint_count(), 2);
    EXPECT_NEAR(waypoint_at(0).value(QStringLiteral("altitude")).toDouble(), 7.3, 1e-6);
    EXPECT_EQ(waypoint_at(1).value(QStringLiteral("distanceText")).toString(), QStringLiteral("11 m"));

    controller_.setWaypointAltitude(1, 12.0);
    controller_.setWaypointAltitude(1, 500.0);
    EXPECT_NEAR(waypoint_at(1).value(QStringLiteral("altitude")).toDouble(), 12.0, 1e-6);
    controller_.removeWaypoint(0);
    EXPECT_EQ(controller_.waypoint_count(), 1);

    // 全體或其他機看不到也改不到這台的航點
    controller_.set_selected_agent(ALL_AGENTS_TARGET);
    EXPECT_FALSE(controller_.can_edit());
    controller_.addWaypoint(25.0003, 121.5);
    EXPECT_EQ(controller_.waypoint_count(), 0);
    controller_.set_selected_agent(AGENT_1);
    EXPECT_EQ(controller_.waypoint_count(), 1);
    EXPECT_EQ(controller_.routes().size(), 1);
}

TEST_F(MissionControllerTest, GroundedAgentGetsDefaultAltitude)
{
    controller_.on_state_updated(AGENT_1, state_at(FlightState::IDLE, 0.0));
    controller_.addWaypoint(25.0001, 121.5);
    EXPECT_NEAR(waypoint_at(0).value(QStringLiteral("altitude")).toDouble(), MissionController::DEFAULT_ALTITUDE_M,
                1e-6);
}

TEST_F(MissionControllerTest, MapPointsUseDisplayOriginButPlanUsesReportedCoordinates)
{
    ui::DisplayOrigin origin{true, 22.5, 113.9, 25.0, 121.5};
    controller_.set_display_origin(origin);
    // 地圖上點在台灣（顯示座標），保存與送出的是深圳的回報座標
    controller_.addWaypoint(25.0001, 121.5);
    EXPECT_NEAR(waypoint_at(0).value(QStringLiteral("latitude")).toDouble(), 25.0001, COORDINATE_EPSILON);

    controller_.requestStart();
    controller_.confirmStart();
    ASSERT_EQ(started_.count(), 1);
    const MissionPlan plan = started_plan();
    EXPECT_EQ(plan.agent, AGENT_1);
    ASSERT_EQ(plan.waypoints.size(), 1);
    EXPECT_NEAR(plan.waypoints[0].latitude, 22.5001, COORDINATE_EPSILON);
    EXPECT_NEAR(plan.waypoints[0].longitude, 113.9, COORDINATE_EPSILON);
}

TEST_F(MissionControllerTest, StartNeedsHoverAndConfirmation)
{
    controller_.requestStart();
    EXPECT_FALSE(controller_.is_confirm_pending());
    EXPECT_EQ(toasts_.rowCount(), 1);

    controller_.addWaypoint(25.0001, 121.5);
    controller_.on_state_updated(AGENT_1, state_at(FlightState::TAKEOFF));
    EXPECT_FALSE(controller_.can_start());
    EXPECT_EQ(controller_.start_block_reason(), QStringLiteral("需在懸停狀態"));
    controller_.requestStart();
    EXPECT_FALSE(controller_.is_confirm_pending());

    controller_.on_state_updated(AGENT_1, state_at(FlightState::HOVER));
    controller_.requestStart();
    ASSERT_TRUE(controller_.is_confirm_pending());
    EXPECT_TRUE(controller_.confirm_text().contains(QStringLiteral("UAV_1")));
    EXPECT_EQ(started_.count(), 0);

    controller_.confirmStart();
    EXPECT_EQ(started_.count(), 1);
    EXPECT_FALSE(controller_.is_confirm_pending());
}

TEST_F(MissionControllerTest, PendingStartIsDroppedOnTimeoutSelectionOrStateChange)
{
    controller_.addWaypoint(25.0001, 121.5);

    controller_.requestStart();
    QTRY_VERIFY_WITH_TIMEOUT(!controller_.is_confirm_pending(), SHORT_CONFIRM_MS + 1000);

    controller_.requestStart();
    controller_.set_selected_agent(AGENT_2);
    EXPECT_FALSE(controller_.is_confirm_pending());
    controller_.set_selected_agent(AGENT_1);

    controller_.requestStart();
    controller_.on_state_updated(AGENT_1, state_at(FlightState::MOVING));
    EXPECT_FALSE(controller_.is_confirm_pending());

    controller_.confirmStart();
    EXPECT_EQ(started_.count(), 0);
}

TEST_F(MissionControllerTest, VoiceStartIsSingleAgentOnlyAndNeedsSeparateConfirm)
{
    controller_.addWaypoint(25.0001, 121.5);

    controller_.set_selected_agent(ALL_AGENTS_TARGET);
    controller_.on_voice_keyword(voice::KEYWORD_START_MISSION);
    EXPECT_FALSE(controller_.is_confirm_pending());
    controller_.set_selected_agent(AGENT_1);

    // 同一句話接著說「確認」不算確認，必須在提示出現後另外說
    controller_.on_voice_keyword(voice::KEYWORD_START_MISSION);
    ASSERT_TRUE(controller_.is_confirm_pending());
    controller_.on_voice_keyword(voice::KEYWORD_CONFIRM);
    EXPECT_EQ(started_.count(), 0);
    EXPECT_TRUE(controller_.is_confirm_pending());

    QTest::qWait(MissionController::VOICE_CONFIRM_MIN_DELAY_MS + 100);
    controller_.on_voice_keyword(voice::KEYWORD_CONFIRM);
    EXPECT_EQ(started_.count(), 1);
}

TEST_F(MissionControllerTest, VoiceCancelAndStop)
{
    controller_.addWaypoint(25.0001, 121.5);
    controller_.on_voice_keyword(voice::KEYWORD_START_MISSION);
    controller_.on_voice_keyword(voice::KEYWORD_CANCEL);
    EXPECT_FALSE(controller_.is_confirm_pending());

    // 沒有任務時說「停止任務」只提示
    controller_.on_voice_keyword(voice::KEYWORD_STOP_MISSION);
    EXPECT_EQ(stopped_.count(), 0);

    MissionStatus status;
    status.agent = AGENT_1;
    status.phase = MissionPhase::MOVING;
    status.waypoint_index = 0;
    status.waypoint_count = 1;
    status.message = QStringLiteral("前往第 1/1 個航點");
    controller_.on_mission_status(status);
    EXPECT_TRUE(controller_.is_running());
    EXPECT_FALSE(controller_.can_edit());
    EXPECT_EQ(controller_.status_text(), status.message);

    controller_.on_voice_keyword(voice::KEYWORD_STOP_MISSION);
    ASSERT_EQ(stopped_.count(), 1);
    EXPECT_EQ(stopped_.takeFirst().at(0).toString(), AGENT_1);
}

TEST_F(MissionControllerTest, OfflineAgentLosesWaypointsStatusAndPendingStart)
{
    controller_.addWaypoint(25.0001, 121.5);
    MissionStatus status;
    status.agent = AGENT_1;
    status.phase = MissionPhase::MOVING;
    status.waypoint_index = 0;
    status.waypoint_count = 1;
    status.message = QStringLiteral("前往第 1/1 個航點");
    controller_.on_mission_status(status);

    controller_.on_state_updated(AGENT_2, state_at(FlightState::HOVER));
    controller_.set_selected_agent(AGENT_2);
    controller_.addWaypoint(25.0002, 121.5);
    controller_.requestStart();
    ASSERT_TRUE(controller_.is_confirm_pending());
    ASSERT_EQ(controller_.routes().size(), 2);

    // 待確認的那台離線：航點與待確認的任務一起清除，另一台不受影響
    QSignalSpy changed(&controller_, &MissionController::changed);
    FleetSnapshot snapshot;
    snapshot.online = QStringList{AGENT_1};
    snapshot.offline = QStringList{AGENT_2};
    controller_.on_fleet_changed(snapshot);
    EXPECT_EQ(changed.count(), 1);
    EXPECT_FALSE(controller_.is_confirm_pending());
    EXPECT_EQ(controller_.waypoint_count(), 0);
    ASSERT_EQ(controller_.routes().size(), 1);
    EXPECT_EQ(controller_.routes().at(0).toMap().value(QStringLiteral("agent")).toString(), AGENT_1);
    controller_.confirmStart();
    EXPECT_EQ(started_.count(), 0);

    // 執行中的那台離線：地圖上不再有任何航點，重新選取也沒有任務狀態
    snapshot.online.clear();
    snapshot.offline = QStringList{AGENT_1, AGENT_2};
    controller_.on_fleet_changed(snapshot);
    EXPECT_TRUE(controller_.routes().isEmpty());
    EXPECT_TRUE(controller_.waypoint_markers().isEmpty());
    controller_.set_selected_agent(AGENT_1);
    EXPECT_FALSE(controller_.is_running());
    EXPECT_TRUE(controller_.status_text().isEmpty());
    EXPECT_EQ(controller_.waypoint_count(), 0);

    // 停滯期間每次掃描都會重複回報離線，已清除就不再通知畫面
    const int count = static_cast<int>(changed.count());
    controller_.on_fleet_changed(snapshot);
    EXPECT_EQ(changed.count(), count);
}

TEST_F(MissionControllerTest, AbortAndRejectArePoppedUp)
{
    MissionStatus status;
    status.agent = AGENT_1;
    status.phase = MissionPhase::REJECTED;
    status.message = QStringLiteral("需在懸停狀態才能開始任務");
    controller_.on_mission_status(status);
    EXPECT_TRUE(controller_.status_text().startsWith(QStringLiteral("無法開始")));
    EXPECT_EQ(toasts_.rowCount(), 1);

    status.phase = MissionPhase::COMPLETED;
    status.message = QStringLiteral("已到達最後一個航點，懸停中");
    controller_.on_mission_status(status);
    // 完成只寫日誌，不彈出
    EXPECT_EQ(toasts_.rowCount(), 1);
    EXPECT_FALSE(controller_.is_running());
}
