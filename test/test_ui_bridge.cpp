/**
 * @file test_ui_bridge.cpp
 * @brief UiBridge 與 UI 組裝：命令仍須經 Guard、按鈕狀態與 Guard 一致、待回應防重複、選取調整、
 *        成功只寫日誌而失敗與警告彈出提示、地圖顯示原點只改變畫面座標、Agent 事件提示、健康狀態與重新啟動飛控
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
#include "modules/mission/mission_module.hpp"
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
 * @brief 建立 Agent 事件
 * @param severity 嚴重程度
 * @param code 事件代碼
 * @param command 相關命令
 * @return AgentEvent
 */
AgentEventData make_event(AgentEventSeverity severity, uint16_t code, const QString& command = QString())
{
    AgentEventData event;
    event.severity = static_cast<uint8_t>(severity);
    event.code = code;
    event.command = command;
    return event;
}

/**
 * @brief PX4 連線正常的健康狀態
 * @param is_preflight_pass 預檢是否通過
 * @return AgentHealth
 */
AgentHealthData make_health(bool is_preflight_pass)
{
    constexpr float FRESH_AGE_S = 0.5F;
    AgentHealthData health;
    health.px4_status_age_s = FRESH_AGE_S;
    health.px4_odometry_age_s = FRESH_AGE_S;
    health.px4_land_detected_age_s = FRESH_AGE_S;
    health.px4_arming_state = px4_arming::DISARMED;
    health.is_px4_pre_flight_checks_pass = is_preflight_pass;
    return health;
}

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
        bridge_ = std::make_unique<UiBridge>(SHORT_TIMEOUT_MS, SHORT_TIMEOUT_MS);
        ASSERT_TRUE(fleet_.init());
        ASSERT_TRUE(telemetry_.init());
        ASSERT_TRUE(guard_.init());
        ASSERT_TRUE(command_.init());
        ASSERT_TRUE(lifecycle_.init());
        ASSERT_TRUE(mission_.init());
        const CoreModules modules{&fleet_, &telemetry_, &guard_, &command_, &lifecycle_, &mission_};
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

    /** @brief 最後一筆提示的種類（success、error、warning） */
    QString last_toast_kind() const { return last_toast_value(ToastModel::KindRole); }

    /** @brief 最後一筆提示的內容 */
    QString last_toast_message() const { return last_toast_value(ToastModel::MessageRole); }

    /**
     * @brief 最後一筆提示的指定欄位
     * @param role ToastModel 角色
     * @return 欄位文字；沒有提示時為空字串
     */
    QString last_toast_value(int role) const
    {
        const ToastModel& model = bridge_->toasts();
        return model.rowCount() > 0 ? model.data(model.index(model.rowCount() - 1), role).toString() : QString();
    }

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
    MissionModule mission_;
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
    // 影像獨立視窗使用的各機完整狀態，與狀態列（選取機）一致
    EXPECT_EQ(model->data(index, FleetModel::Px4StatusLabelRole).toString(), QStringLiteral("已解鎖 · 飛行中"));
    EXPECT_EQ(model->data(index, FleetModel::NedTextRole).toString(), QStringLiteral("+1.0  +2.0  −5.0"));
    EXPECT_EQ(model->data(index, FleetModel::PositionTextRole).toString(),
              QStringLiteral("25.039970 N  121.511950 E"));
    EXPECT_EQ(bridge_->selected_summary().value(QStringLiteral("px4Label")).toString(),
              QStringLiteral("已解鎖 · 飛行中"));
    EXPECT_TRUE(bridge_->selected_summary().value(QStringLiteral("isArmed")).toBool());
    EXPECT_EQ(bridge_->selected_summary().value(QStringLiteral("nedText")).toString(),
              QStringLiteral("+1.0  +2.0  −5.0"));
}

TEST(FleetModelTest, MembershipChangesKeepExistingRows)
{
    const QString agent_3 = QStringLiteral("uav_3_agent");
    const QHash<QString, AgentView> views;
    FleetModel model;
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);

    model.update({AGENT_1, agent_3}, views, QString(), ui::DisplayOrigin{});
    ASSERT_EQ(model.rowCount(), 2);
    EXPECT_EQ(inserted.count(), 2);
    const QPersistentModelIndex third(model.index(1));

    // 其他 Agent 上線或離線時，既有列（影像縮圖、獨立視窗、地圖標記）保留，只調整位置
    model.update({AGENT_1, AGENT_2, agent_3}, views, QString(), ui::DisplayOrigin{});
    EXPECT_EQ(inserted.count(), 3);
    ASSERT_TRUE(third.isValid());
    EXPECT_EQ(third.row(), 2);
    EXPECT_EQ(model.data(model.index(1), FleetModel::AgentRole).toString(), AGENT_2);

    model.update({AGENT_2, agent_3}, views, QString(), ui::DisplayOrigin{});
    EXPECT_EQ(removed.count(), 1);
    ASSERT_TRUE(third.isValid());
    EXPECT_EQ(third.row(), 1);
    EXPECT_EQ(reset.count(), 0);

    // 既有列的順序改變時才整體重設
    model.update({agent_3, AGENT_2}, views, QString(), ui::DisplayOrigin{});
    EXPECT_EQ(reset.count(), 1);
    EXPECT_EQ(model.data(model.index(0), FleetModel::AgentRole).toString(), agent_3);
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

TEST_F(UiBridgeTest, AgentEventsToastBySeverityAndDeduplicate)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));

    // 一般資訊只寫日誌
    emit link_.event_received(AGENT_1, make_event(AgentEventSeverity::INFO, agent_event_code::PX4_STATUS_RECOVERED));
    EXPECT_EQ(toast_count(), 0);
    EXPECT_TRUE(last_log().startsWith(QStringLiteral("UAV_1 飛控資料恢復")));

    // 錯誤：紅色提示，附命令、Agent 說明與 PX4 結果碼
    AgentEventData rejected = make_event(AgentEventSeverity::ERROR, agent_event_code::PX4_COMMAND_REJECTED,
                                         QStringLiteral("ARM"));
    rejected.px4_result = 1;
    rejected.detail = QStringLiteral("Arming denied");
    emit link_.event_received(AGENT_1, rejected);
    ASSERT_EQ(toast_count(), 1);
    EXPECT_EQ(last_toast_title(), QStringLiteral("UAV_1 飛控拒絕命令"));
    EXPECT_EQ(last_toast_kind(), QStringLiteral("error"));
    EXPECT_EQ(last_toast_message(), QStringLiteral("ARM；Arming denied（PX4 結果 1：暫時拒絕）"));

    // 同一事件短時間內重複只寫日誌；不同命令另外提示
    emit link_.event_received(AGENT_1, rejected);
    EXPECT_EQ(toast_count(), 1);
    rejected.command = QStringLiteral("DISARM");
    emit link_.event_received(AGENT_1, rejected);
    EXPECT_EQ(toast_count(), 2);

    // 警告：黃色提示
    emit link_.event_received(AGENT_1, make_event(AgentEventSeverity::WARNING, agent_event_code::OFFBOARD_LOST));
    ASSERT_EQ(toast_count(), 3);
    EXPECT_EQ(last_toast_title(), QStringLiteral("UAV_1 飛控離開 Offboard"));
    EXPECT_EQ(last_toast_kind(), QStringLiteral("warning"));
}

TEST_F(UiBridgeTest, PreflightEventWaitsForHealthConfirmation)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    const AgentEventData preflight =
        make_event(AgentEventSeverity::WARNING, agent_event_code::PX4_PREFLIGHT_FAILED);

    // 正常降落上鎖時短暫未通過：健康狀態隨即恢復，不提示
    emit link_.event_received(AGENT_1, preflight);
    emit link_.health_received(AGENT_1, make_health(true));
    QTest::qWait(WAIT_PAST_TIMEOUT_MS);
    EXPECT_EQ(toast_count(), 0);

    // 健康狀態持續未通過：確認時間到才提示
    emit link_.event_received(AGENT_1, preflight);
    emit link_.health_received(AGENT_1, make_health(false));
    EXPECT_EQ(toast_count(), 0);
    QTest::qWait(WAIT_PAST_TIMEOUT_MS);
    ASSERT_EQ(toast_count(), 1);
    EXPECT_EQ(last_toast_title(), QStringLiteral("UAV_1 飛控預檢未通過"));
}

TEST_F(UiBridgeTest, HealthShownInSummaryFleetModelAndArmWaitReason)
{
    link_.discovered_agents.append(AGENT_1);
    fleet_.scan_now();
    bridge_->selectAgent(AGENT_1);

    // 收到 odometry 前只有健康狀態
    AgentHealthData health = make_health(true);
    health.px4_odometry_age_s = -1.0F;
    emit link_.health_received(AGENT_1, health);
    bridge_->refresh_now();
    QVariantMap summary = bridge_->selected_summary();
    EXPECT_TRUE(summary.value(QStringLiteral("isWaitingForFlightController")).toBool());
    EXPECT_EQ(summary.value(QStringLiteral("waitReason")).toString(), QStringLiteral("未收到 PX4 位置資料"));
    EXPECT_EQ(summary.value(QStringLiteral("px4LinkText")).toString(), QStringLiteral("正常"));
    EXPECT_EQ(summary.value(QStringLiteral("preflightText")).toString(), QStringLiteral("PASS"));

    // PX4 資料中斷 12 秒
    health.px4_status_age_s = 12.0F;
    emit link_.health_received(AGENT_1, health);
    emit link_.state_received(AGENT_1, make_state(FlightState::ERROR, false, true, PX4_NAV_STATE_OFFBOARD, false));
    bridge_->refresh_now();
    summary = bridge_->selected_summary();
    EXPECT_EQ(summary.value(QStringLiteral("waitReason")).toString(), QStringLiteral("PX4 資料中斷 12 秒"));
    EXPECT_EQ(summary.value(QStringLiteral("px4LinkText")).toString(), QStringLiteral("中斷 12 秒"));
    EXPECT_TRUE(summary.value(QStringLiteral("isPx4LinkLost")).toBool());
    EXPECT_EQ(summary.value(QStringLiteral("preflightText")).toString(), QStringLiteral("—"));

    auto* model = static_cast<FleetModel*>(bridge_->fleet_model());
    ASSERT_EQ(model->rowCount(), 1);
    const QModelIndex index = model->index(0);
    EXPECT_EQ(model->data(index, FleetModel::HealthWarningRole).toString(), QStringLiteral("PX4 中斷 12 秒"));
    EXPECT_TRUE(model->data(index, FleetModel::IsPx4LinkLostRole).toBool());

    // 狀態有效但預檢未通過
    emit link_.health_received(AGENT_1, make_health(false));
    emit link_.state_received(AGENT_1, make_state(FlightState::IDLE));
    bridge_->refresh_now();
    EXPECT_EQ(model->data(index, FleetModel::HealthWarningRole).toString(), QStringLiteral("PX4 預檢未通過"));
    EXPECT_FALSE(model->data(index, FleetModel::IsPx4LinkLostRole).toBool());
    EXPECT_TRUE(bridge_->selected_summary().value(QStringLiteral("isPreflightFailed")).toBool());
    EXPECT_TRUE(bridge_->selected_summary().value(QStringLiteral("waitReason")).toString().isEmpty());
}

TEST_F(UiBridgeTest, RebootAvailabilityFollowsGuardRules)
{
    // 全體選取：只支援單機
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    bridge_->selectAgent(ALL_AGENTS_TARGET);
    EXPECT_FALSE(bridge_->can_reboot_fc());
    EXPECT_FALSE(bridge_->is_reboot_force_mode());

    // 狀態有效、上鎖、落地：一般重新啟動
    bridge_->selectAgent(AGENT_1);
    EXPECT_TRUE(bridge_->can_reboot_fc());
    EXPECT_FALSE(bridge_->can_force_reboot_fc());
    EXPECT_FALSE(bridge_->is_reboot_force_mode());
    EXPECT_TRUE(bridge_->reboot_blocked_reason().isEmpty());

    // 狀態不明且最後一次有效時上鎖落地：只能強制
    emit link_.health_received(AGENT_1, make_health(true));
    emit link_.state_received(AGENT_1, make_state(FlightState::ERROR, false, true, PX4_NAV_STATE_OFFBOARD, false));
    bridge_->refresh_now();
    EXPECT_TRUE(bridge_->is_reboot_force_mode());
    EXPECT_FALSE(bridge_->can_reboot_fc());
    EXPECT_TRUE(bridge_->can_force_reboot_fc());

    // 最後一次有效時在空中：滑桿停用並顯示原因
    emit link_.state_received(AGENT_1, make_state(FlightState::HOVER, true, false));
    emit link_.state_received(AGENT_1, make_state(FlightState::ERROR, false, true, PX4_NAV_STATE_OFFBOARD, false));
    bridge_->refresh_now();
    EXPECT_TRUE(bridge_->is_reboot_force_mode());
    EXPECT_FALSE(bridge_->can_force_reboot_fc());
    EXPECT_TRUE(bridge_->reboot_blocked_reason().contains(QStringLiteral("最後一次")));

    // Guard 同樣拒絕，Link 沒有收到命令
    bridge_->sendCommand(QStringLiteral("REBOOT_FC"), {1});
    EXPECT_TRUE(link_.published_commands.empty());
    EXPECT_EQ(last_toast_title(), QStringLiteral("REBOOT_FC 失敗"));
}

TEST_F(UiBridgeTest, ForcedRebootWithHealthOnlyReachesLink)
{
    link_.discovered_agents.append(AGENT_1);
    fleet_.scan_now();
    emit link_.health_received(AGENT_1, make_health(true));
    bridge_->selectAgent(AGENT_1);
    EXPECT_TRUE(bridge_->is_reboot_force_mode());
    EXPECT_TRUE(bridge_->can_force_reboot_fc());

    bridge_->sendCommand(QStringLiteral("REBOOT_FC"), {1});
    ASSERT_EQ(link_.published_commands.size(), 1U);
    EXPECT_EQ(link_.published_commands[0].command_line, QStringLiteral("REBOOT_FC 1"));
}
