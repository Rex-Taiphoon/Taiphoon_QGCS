/**
 * @file test_ui_bridge.cpp
 * @brief UiBridge 與 UI 組裝：命令仍須經 Guard、按鈕狀態與 Guard 一致、待回應防重複、選取調整
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "app/app_settings.hpp"
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
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace gcs;
using gcs::test::FakeAgentLink;
using gcs::test::make_state;

namespace
{

const QString AGENT_1 = QStringLiteral("uav_1_agent");
const QString AGENT_2 = QStringLiteral("uav_2_agent");

/**
 * @class UiBridgeTest
 * @brief 真實模組 + FakeAgentLink + UiBridge（同一執行緒，連線為直接呼叫）
 */
class UiBridgeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(temp_dir_.isValid());
        settings_ = std::make_unique<AppSettings>(temp_dir_.filePath(QStringLiteral("settings.ini")));
        bridge_ = std::make_unique<UiBridge>(settings_.get());
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

    QTemporaryDir temp_dir_;
    std::unique_ptr<AppSettings> settings_;
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
    bring_online(AGENT_2, make_state(FlightState::IDLE, true));
    bridge_->selectAgent(ALL_AGENTS_TARGET);
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
    EXPECT_EQ(last_toast_title(), QStringLiteral("ARM 成功"));
}

TEST_F(UiBridgeTest, GuardRejectionShownAndNothingPublished)
{
    bring_online(AGENT_1, make_state(FlightState::IDLE));
    bridge_->selectAgent(AGENT_1);
    // 即使 QML 繞過按鈕狀態直接呼叫，Guard 仍會拒絕
    bridge_->sendCommand(QStringLiteral("TAKEOFF"), {3.0});

    EXPECT_TRUE(link_.published_commands.empty());
    EXPECT_EQ(last_toast_title(), QStringLiteral("TAKEOFF 失敗"));
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
