/**
 * @file test_ui_bridge.cpp
 * @brief UiBridge 與 UI 組裝：命令仍須經 Guard、按鈕狀態與 Guard 一致、待回應防重複、選取調整、
 *        成功只寫日誌而失敗與警告彈出提示、地圖顯示原點只改變畫面座標
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "app/fleet_model.hpp"
#include "app/toast_model.hpp"
#include "app/ui_bridge.hpp"
#include "app/ui_wiring.hpp"
#include "fakes/fake_agent_link.hpp"
#include "modules/command/command_module.hpp"
#include "modules/fleet/fleet_module.hpp"
#include "modules/guard/guard_module.hpp"
#include "modules/lifecycle/agent_lifecycle_module.hpp"
#include "modules/telemetry/telemetry_module.hpp"

#include <QSignalSpy>
#include <QStringList>
#include <QTest>

#include <gtest/gtest.h>

using namespace gcs;
using gcs::test::FakeAgentLink;
using gcs::test::make_state;

namespace
{

const QString AGENT_1 = QStringLiteral("uav_1_agent");
const QString AGENT_2 = QStringLiteral("uav_2_agent");
/// 命令與解鎖確認逾時（毫秒），縮短以加快測試
constexpr int SHORT_TIMEOUT_MS = 1000;
/// 等待逾時觸發的時間（毫秒）
constexpr int WAIT_PAST_TIMEOUT_MS = 1300;

/// 測試期間擷取的 gcs.ui 日誌
QStringList g_ui_logs;

/**
 * @brief 測試用訊息處理器：只收集 gcs.ui 類別
 * @param type 訊息等級（未使用）
 * @param context 來源資訊
 * @param message 訊息內容
 */
void capture_ui_log(QtMsgType /*type*/, const QMessageLogContext& context, const QString& message)
{
    if (context.category != nullptr && QByteArray(context.category) == "gcs.ui")
    {
        g_ui_logs.append(message);
    }
}

/**
 * @class UiBridgeTest
 * @brief 真實模組 + FakeAgentLink + UiBridge（同一執行緒，連線為直接呼叫）
 */
class UiBridgeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        g_ui_logs.clear();
        previous_handler_ = qInstallMessageHandler(capture_ui_log);
        bridge_ = std::make_unique<UiBridge>(SHORT_TIMEOUT_MS);
        ASSERT_TRUE(fleet_.init());
        ASSERT_TRUE(telemetry_.init());
        ASSERT_TRUE(guard_.init());
        ASSERT_TRUE(command_.init());
        ASSERT_TRUE(lifecycle_.init());
        const CoreModules modules{&fleet_, &telemetry_, &guard_, &command_, &lifecycle_};
        ASSERT_TRUE(wire_core_modules(modules));
        ASSERT_TRUE(wire_ui(modules, bridge_.get()));
        bridge_->set_link_status(true, QStringLiteral("測試"));
    }

    /**
     * @brief 讓 Agent 上線並送出狀態，再刷新介面
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
        bridge_->refresh_now();
    }

    /** @brief 最後一筆提示的標題 */
    QString last_toast_title() const
    {
        auto* model = bridge_->toasts().rowCount() > 0 ? &bridge_->toasts() : nullptr;
        if (model == nullptr)
        {
            return QString();
        }
        return model->data(model->index(model->rowCount() - 1), ToastModel::TitleRole).toString();
    }

    void TearDown() override
    {
        qInstallMessageHandler(previous_handler_);
    }

    /** @brief 提示總數 */
    int toast_count() const { return bridge_->toasts().rowCount(); }

    /** @brief 最後一筆 gcs.ui 日誌 */
    static QString last_log() { return g_ui_logs.isEmpty() ? QString() : g_ui_logs.last(); }

    QtMessageHandler previous_handler_ = nullptr;

    std::unique_ptr<UiBridge> bridge_;
    FakeAgentLink link_;
    FleetModule fleet_{&link_};
    TelemetryModule telemetry_{&link_};
    GuardModule guard_;
    CommandModule command_{&link_};
    AgentLifecycleModule lifecycle_{&link_};
};

}  // namespace

TEST_F(UiBridgeTest, AvailabilityFollowsGuardRules)
{
    // PX4 Offboard、已落地、未解鎖：可 Arm，不可 Takeoff、移動
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    bridge_->selectAgent(AGENT_1);
    EXPECT_TRUE(bridge_->can_arm());
    EXPECT_FALSE(bridge_->can_takeoff());
    EXPECT_FALSE(bridge_->can_move());
    EXPECT_TRUE(bridge_->can_offboard());
    EXPECT_FALSE(bridge_->can_formation());

    emit link_.state_received(AGENT_1, make_state(FlightState::HOVER, true, false));
    bridge_->refresh_now();
    EXPECT_FALSE(bridge_->can_arm());
    EXPECT_TRUE(bridge_->can_move());
    EXPECT_TRUE(bridge_->can_yaw());
    EXPECT_TRUE(bridge_->can_rtl());
}

TEST_F(UiBridgeTest, AllSelectionRequiresEveryAgent)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    bridge_->selectAgent(ALL_AGENTS_TARGET);
    // 單機沒有隊形，Agent 也會拒絕少於 2 台的編隊
    EXPECT_FALSE(bridge_->can_formation());

    bring_online(AGENT_2, make_state(FlightState::IDLE, true));
    bridge_->refresh_now();
    EXPECT_FALSE(bridge_->can_arm());
    EXPECT_FALSE(bridge_->can_yaw());
    EXPECT_TRUE(bridge_->can_formation());
}

TEST_F(UiBridgeTest, CommandGoesThroughGuardToLink)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    bridge_->selectAgent(AGENT_1);
    bridge_->sendCommand(QStringLiteral("ARM"));

    ASSERT_EQ(link_.published_commands.size(), 1U);
    EXPECT_EQ(link_.published_commands[0].target, AGENT_1);
    EXPECT_EQ(link_.published_commands[0].command_line, QStringLiteral("ARM"));
    // 命令協定為單向，發布成功只能稱為已送出；成功只寫日誌，不彈出提示
    EXPECT_TRUE(last_log().startsWith(QStringLiteral("ARM 已送出")));
    EXPECT_EQ(toast_count(), 0);
}

TEST_F(UiBridgeTest, GuardRejectionShownAndNothingPublished)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    bridge_->selectAgent(AGENT_1);
    // 即使 QML 繞過按鈕狀態直接呼叫，Guard 仍會拒絕
    bridge_->sendCommand(QStringLiteral("TAKEOFF"), {3.0});

    EXPECT_TRUE(link_.published_commands.empty());
    EXPECT_EQ(last_toast_title(), QStringLiteral("TAKEOFF 失敗"));
    EXPECT_TRUE(last_log().startsWith(QStringLiteral("TAKEOFF 失敗")));
}

TEST_F(UiBridgeTest, NonNumericParamRejectedByGuard)
{
    bring_online(AGENT_1, make_state(FlightState::HOVER, true, false));
    bridge_->selectAgent(AGENT_1);
    bridge_->sendCommand(QStringLiteral("MOVE_FORWARD"), {QStringLiteral("abc")});
    EXPECT_TRUE(link_.published_commands.empty());
    EXPECT_EQ(last_toast_title(), QStringLiteral("MOVE_FORWARD 失敗"));
}

TEST_F(UiBridgeTest, DuplicatePendingCommandBlocked)
{
    // 斷開 Guard → UI 的回報，模擬尚未收到回應
    QObject::disconnect(&guard_, &GuardModule::command_acked, bridge_.get(), &UiBridge::on_command_acked);
    QObject::disconnect(&command_, &CommandModule::command_acked, bridge_.get(), &UiBridge::on_command_acked);
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    bridge_->selectAgent(AGENT_1);
    bridge_->sendCommand(QStringLiteral("MODE_OFFBOARD"));
    bridge_->sendCommand(QStringLiteral("MODE_OFFBOARD"));
    EXPECT_EQ(link_.published_commands.size(), 1U);
    EXPECT_EQ(last_toast_title(), QStringLiteral("命令待回執"));
}

TEST_F(UiBridgeTest, CommandRefusedWhenLinkDown)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    bridge_->selectAgent(AGENT_1);
    bridge_->set_link_status(false, QStringLiteral("未啟動"));
    bridge_->sendCommand(QStringLiteral("ARM"));
    EXPECT_TRUE(link_.published_commands.empty());
    EXPECT_EQ(last_toast_title(), QStringLiteral("DDS 未連線"));
}

TEST_F(UiBridgeTest, SelectionFallsBackWhenAgentGoesOffline)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    bring_online(AGENT_2, make_state(FlightState::IDLE));
    bridge_->selectAgent(AGENT_1);

    link_.discovered_agents = {AGENT_2};
    fleet_.scan_now();
    EXPECT_EQ(bridge_->selected_agent(), ALL_AGENTS_TARGET);

    link_.discovered_agents.clear();
    fleet_.scan_now();
    EXPECT_EQ(bridge_->selected_agent(), ALL_AGENTS_TARGET);
}

TEST_F(UiBridgeTest, ShutdownGoesThroughLifecycle)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    bridge_->selectAgent(AGENT_1);
    ASSERT_TRUE(bridge_->can_shutdown_agent());
    bridge_->requestAgentShutdown();
    ASSERT_EQ(link_.shutdown_requests.size(), 1U);
    EXPECT_EQ(link_.shutdown_requests[0].agent_ns, AGENT_1);

    bridge_->selectAgent(ALL_AGENTS_TARGET);
    EXPECT_FALSE(bridge_->can_shutdown_agent());
}

TEST_F(UiBridgeTest, FleetModelExposesDisplayRoles)
{
    AgentStateData state = make_state(FlightState::HOVER, true, false);
    state.position_global = {25.0399700, 121.5119500, 10.0};
    state.position_ned = {1.0F, 2.0F, -5.0F};
    state.battery_remaining = 0.85F;
    bring_online(AGENT_1, state);
    bridge_->selectAgent(AGENT_1);

    auto* model = static_cast<FleetModel*>(bridge_->fleet_model());
    ASSERT_EQ(model->rowCount(), 1);
    const QModelIndex index = model->index(0);
    EXPECT_EQ(model->data(index, FleetModel::AgentRole).toString(), AGENT_1);
    EXPECT_TRUE(model->data(index, FleetModel::IsSelectedRole).toBool());
    EXPECT_FALSE(model->data(index, FleetModel::IsWaitingForFlightControllerRole).toBool());
    EXPECT_EQ(model->data(index, FleetModel::ModeLabelRole).toString(), QStringLiteral("懸停中"));
    EXPECT_EQ(model->data(index, FleetModel::BatteryTextRole).toString(), QStringLiteral("85"));
    EXPECT_EQ(model->data(index, FleetModel::AltitudeTextRole).toString(), QStringLiteral("5.0"));
    EXPECT_TRUE(model->data(index, FleetModel::HasPositionRole).toBool());
    EXPECT_EQ(bridge_->selected_summary().value(QStringLiteral("px4Label")).toString(),
              QStringLiteral("已解鎖 · 飛行中"));
    EXPECT_TRUE(bridge_->selected_summary().value(QStringLiteral("isArmed")).toBool());
    EXPECT_EQ(bridge_->selected_summary().value(QStringLiteral("nedText")).toString(),
              QStringLiteral("+1.0  +2.0  −5.0"));
}

TEST_F(UiBridgeTest, SummaryArmStateRequiresValidPx4State)
{
    AgentStateData state = make_state(FlightState::IDLE);
    state.is_armed = true;
    state.is_px4_state_valid = false;
    bring_online(AGENT_1, state);
    bridge_->selectAgent(AGENT_1);
    EXPECT_FALSE(bridge_->selected_summary().value(QStringLiteral("isArmed")).toBool());

    state.is_px4_state_valid = true;
    state.is_armed = false;
    bring_online(AGENT_1, state);
    bridge_->refresh_now();
    EXPECT_FALSE(bridge_->selected_summary().value(QStringLiteral("isArmed")).toBool());
}

TEST_F(UiBridgeTest, TrajectoriesForValidPathsOnly)
{
    bring_online(AGENT_1, make_state(FlightState::MOVING, true, false));
    AgentTrajectoryData trajectory;
    trajectory.is_valid = true;
    trajectory.control_points = {25.0, 121.5, 10.0, 25.001, 121.501, 10.0};
    emit link_.trajectory_received(AGENT_1, trajectory);
    bridge_->refresh_now();
    ASSERT_EQ(bridge_->trajectories().size(), 1);

    trajectory.is_valid = false;
    emit link_.trajectory_received(AGENT_1, trajectory);
    bridge_->refresh_now();
    EXPECT_TRUE(bridge_->trajectories().isEmpty());
}

TEST_F(UiBridgeTest, DisplayOriginShiftsMapPositionsOnly)
{
    constexpr double COORDINATE_EPSILON = 1e-9;
    AgentStateData state = make_state(FlightState::HOVER, true, false);
    // 參考點正北 0.0001 度（約 11 公尺）
    state.position_global = {22.5431960, 113.9364040, 10.0};
    bring_online(AGENT_1, state);
    bridge_->selectAgent(AGENT_1);
    AgentTrajectoryData trajectory;
    trajectory.is_valid = true;
    trajectory.control_points = {22.5430960, 113.9364040, 10.0, 22.5431960, 113.9364040, 10.0};
    emit link_.trajectory_received(AGENT_1, trajectory);

    ui::DisplayOrigin origin{true, 22.5430960, 113.9364040, 25.0339640, 121.5644680};
    bridge_->set_display_origin(origin);
    bridge_->refresh_now();

    // 選取摘要（自動跟隨）、機群標記與軌跡都改用顯示位置
    const QVariantMap summary = bridge_->selected_summary();
    EXPECT_TRUE(summary.value(QStringLiteral("hasPosition")).toBool());
    EXPECT_NEAR(summary.value(QStringLiteral("latitude")).toDouble(), 25.0340640, COORDINATE_EPSILON);
    EXPECT_NEAR(summary.value(QStringLiteral("longitude")).toDouble(), 121.5644680, COORDINATE_EPSILON);
    // 供「帶入選取機位置」使用的回報位置維持原始座標
    EXPECT_EQ(summary.value(QStringLiteral("reportedPositionText")).toString(),
              QStringLiteral("22.5431960, 113.9364040"));

    auto* model = static_cast<FleetModel*>(bridge_->fleet_model());
    ASSERT_EQ(model->rowCount(), 1);
    const QModelIndex index = model->index(0);
    EXPECT_NEAR(model->data(index, FleetModel::LatitudeRole).toDouble(), 25.0340640, COORDINATE_EPSILON);
    EXPECT_NEAR(model->data(index, FleetModel::LongitudeRole).toDouble(), 121.5644680, COORDINATE_EPSILON);

    ASSERT_EQ(bridge_->trajectories().size(), 1);
    const QVariantList path = bridge_->trajectories().at(0).toMap().value(QStringLiteral("path")).toList();
    ASSERT_EQ(path.size(), 2);
    EXPECT_NEAR(path.at(0).toMap().value(QStringLiteral("latitude")).toDouble(), 25.0339640, COORDINATE_EPSILON);
    EXPECT_NEAR(path.at(1).toMap().value(QStringLiteral("latitude")).toDouble(), 25.0340640, COORDINATE_EPSILON);

    // 按鈕狀態由遙測狀態決定，不受顯示原點影響
    EXPECT_TRUE(bridge_->can_move());

    // 停用後恢復原始位置
    origin.is_enabled = false;
    bridge_->set_display_origin(origin);
    bridge_->refresh_now();
    EXPECT_NEAR(bridge_->selected_summary().value(QStringLiteral("latitude")).toDouble(), 22.5431960,
                COORDINATE_EPSILON);
    EXPECT_NEAR(model->data(index, FleetModel::LongitudeRole).toDouble(), 113.9364040, COORDINATE_EPSILON);
}

TEST_F(UiBridgeTest, NonArmCommandShownAsSentWithoutConfirmation)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    bridge_->selectAgent(AGENT_1);
    bridge_->sendCommand(QStringLiteral("MODE_OFFBOARD"));
    EXPECT_TRUE(last_log().startsWith(QStringLiteral("MODE_OFFBOARD 已送出")));

    // 非 ARM／DISARM 不追蹤解鎖狀態，逾時後不應出現警告
    QTest::qWait(WAIT_PAST_TIMEOUT_MS);
    EXPECT_EQ(toast_count(), 0);
}

TEST_F(UiBridgeTest, ArmConfirmedWhenStateShowsArmed)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    bridge_->selectAgent(AGENT_1);
    bridge_->sendCommand(QStringLiteral("ARM"));
    EXPECT_TRUE(last_log().startsWith(QStringLiteral("ARM 已送出")));

    // 尚未解鎖的狀態不算確認
    emit link_.state_received(AGENT_1, make_state(FlightState::ARMED));
    EXPECT_TRUE(last_log().startsWith(QStringLiteral("ARM 已送出")));

    emit link_.state_received(AGENT_1, make_state(FlightState::ARMED, true));
    EXPECT_TRUE(last_log().startsWith(QStringLiteral("uav_1_agent 已解鎖")));

    // 已確認後逾時不再警告，後續狀態也不重複記錄
    const qsizetype log_count = g_ui_logs.size();
    emit link_.state_received(AGENT_1, make_state(FlightState::ARMED, true));
    QTest::qWait(WAIT_PAST_TIMEOUT_MS);
    EXPECT_EQ(g_ui_logs.size(), log_count);
    EXPECT_EQ(toast_count(), 0);
}

TEST_F(UiBridgeTest, ArmWarnsWhenPx4DoesNotArm)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    bridge_->selectAgent(AGENT_1);
    bridge_->sendCommand(QStringLiteral("ARM"));

    // 模擬 PX4 拒絕解鎖：Agent 持續回報未解鎖
    emit link_.state_received(AGENT_1, make_state(FlightState::ARMED));
    QTest::qWait(WAIT_PAST_TIMEOUT_MS);
    // 警告仍彈出提示，並同時寫入日誌
    EXPECT_EQ(last_toast_title(), QStringLiteral("uav_1_agent 未解鎖"));
    EXPECT_TRUE(last_log().startsWith(QStringLiteral("uav_1_agent 未解鎖")));

    // 逾時後才解鎖不再補發確認
    const int count = toast_count();
    emit link_.state_received(AGENT_1, make_state(FlightState::ARMED, true));
    EXPECT_EQ(toast_count(), count);
}

TEST_F(UiBridgeTest, ArmCheckIgnoresInvalidPx4State)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    bridge_->selectAgent(AGENT_1);
    bridge_->sendCommand(QStringLiteral("ARM"));

    // PX4 狀態無效時 armed 不可信，不能據此確認
    emit link_.state_received(AGENT_1, make_state(FlightState::ARMED, true, true, PX4_NAV_STATE_OFFBOARD, false));
    EXPECT_TRUE(last_log().startsWith(QStringLiteral("ARM 已送出")));

    emit link_.state_received(AGENT_1, make_state(FlightState::ARMED, true));
    EXPECT_TRUE(last_log().startsWith(QStringLiteral("uav_1_agent 已解鎖")));
}

TEST_F(UiBridgeTest, DisarmConfirmedWhenStateShowsDisarmed)
{
    bring_online(AGENT_1, make_state(FlightState::ARMED, true));
    bridge_->selectAgent(AGENT_1);
    ASSERT_TRUE(bridge_->can_disarm());
    bridge_->sendCommand(QStringLiteral("DISARM"));
    EXPECT_TRUE(last_log().startsWith(QStringLiteral("DISARM 已送出")));

    emit link_.state_received(AGENT_1, make_state(FlightState::IDLE));
    EXPECT_TRUE(last_log().startsWith(QStringLiteral("uav_1_agent 已上鎖")));
}

TEST_F(UiBridgeTest, AllTargetArmChecksEachOnlineAgent)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    bring_online(AGENT_2, make_state(FlightState::IDLE));
    bridge_->selectAgent(ALL_AGENTS_TARGET);
    ASSERT_TRUE(bridge_->can_arm());
    bridge_->sendCommand(QStringLiteral("ARM"));
    ASSERT_EQ(link_.published_commands.size(), 1U);
    EXPECT_EQ(link_.published_commands[0].target, ALL_AGENTS_TARGET);

    emit link_.state_received(AGENT_1, make_state(FlightState::ARMED, true));
    EXPECT_TRUE(last_log().startsWith(QStringLiteral("uav_1_agent 已解鎖")));

    QTest::qWait(WAIT_PAST_TIMEOUT_MS);
    EXPECT_EQ(last_toast_title(), QStringLiteral("uav_2_agent 未解鎖"));
    EXPECT_EQ(toast_count(), 1);
}

TEST_F(UiBridgeTest, ArmCheckDroppedWhenAgentGoesOffline)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    bridge_->selectAgent(AGENT_1);
    bridge_->sendCommand(QStringLiteral("ARM"));

    link_.discovered_agents.clear();
    fleet_.scan_now();
    EXPECT_EQ(last_toast_title(), QStringLiteral("uav_1_agent 解鎖狀態未確認"));

    // 離線已提示，逾時不再重複警告
    const int count = toast_count();
    QTest::qWait(WAIT_PAST_TIMEOUT_MS);
    EXPECT_EQ(toast_count(), count);
}

TEST_F(UiBridgeTest, ShutdownReplyLoggedAsSuccess)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    bridge_->selectAgent(AGENT_1);
    bridge_->requestAgentShutdown();
    ASSERT_EQ(link_.shutdown_requests.size(), 1U);

    // 關閉請求有 service 回應，仍可稱為成功；成功只寫日誌
    emit link_.shutdown_replied(AGENT_1, link_.shutdown_requests[0].request_id, true, QStringLiteral("已接受"));
    EXPECT_TRUE(last_log().startsWith(QStringLiteral("AGENT_SHUTDOWN 成功")));
    EXPECT_EQ(toast_count(), 0);
}
