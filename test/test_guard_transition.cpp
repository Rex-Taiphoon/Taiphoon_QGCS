/**
 * @file test_guard_transition.cpp
 * @brief GuardModule 轉移表全組合、PX4 原生狀態規則與全體模式驗證
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 由網頁版測試移植：期望轉移表與所有情境維持不變，改以 signal／slot 驅動。
 * 另新增模組層級的全組合測試，確認 GuardModule 的狀態追蹤與轉移表一致。
 */
#include "fakes/fake_agent_link.hpp"
#include "modules/guard/guard_module.hpp"
#include "modules/guard/guard_rules.hpp"

#include <QSignalSpy>

#include <gtest/gtest.h>

#include <array>
#include <map>
#include <set>

using namespace gcs;
using gcs::test::make_request;
using gcs::test::make_state;

namespace
{

const std::array<FlightState, 11> ALL_STATES = {
    FlightState::IDLE,   FlightState::OFFBOARD_INIT, FlightState::OFFBOARD_ACTIVE, FlightState::ARMED,
    FlightState::TAKEOFF, FlightState::HOVER,        FlightState::RTL,             FlightState::LAND,
    FlightState::MOVING, FlightState::POST_RTL,      FlightState::ERROR,
};

const std::array<CmdType, 20> ALL_COMMANDS = {
    CmdType::ARM,           CmdType::DISARM,          CmdType::TAKEOFF,          CmdType::MOVE_FORWARD,
    CmdType::MOVE_BACKWARD, CmdType::MOVE_LEFT,       CmdType::MOVE_RIGHT,       CmdType::MOVE_UP,
    CmdType::MOVE_DOWN,     CmdType::YAW,             CmdType::MOVE_POINT,       CmdType::HOLD,
    CmdType::MODE_RTL,      CmdType::MODE_LAND,       CmdType::MODE_OFFBOARD,    CmdType::FORMATION_CIRCLE,
    CmdType::FORMATION_HORI_LINEAR, CmdType::FORMATION_VERT_LINEAR, CmdType::FORMATION_CANCEL, CmdType::UNKNOWN,
};

const std::set<CmdType> FORMATION_COMMANDS = {
    CmdType::FORMATION_CIRCLE, CmdType::FORMATION_HORI_LINEAR, CmdType::FORMATION_VERT_LINEAR,
    CmdType::FORMATION_CANCEL,
};

/// 測試用的單機 Agent
const QString AGENT_1 = QStringLiteral("uav_1_agent");
const QString AGENT_2 = QStringLiteral("uav_2_agent");

/**
 * @brief 期望的轉移表（與網頁版測試相同）
 * @return 每個狀態允許的命令
 */
std::map<FlightState, std::set<CmdType>> build_expected_table()
{
    std::map<FlightState, std::set<CmdType>> table;
    for (const auto state : ALL_STATES)
    {
        table[state] = FORMATION_COMMANDS;
    }
    table[FlightState::IDLE].insert(CmdType::MODE_OFFBOARD);
    table[FlightState::OFFBOARD_INIT].insert(CmdType::MODE_OFFBOARD);
    table[FlightState::ARMED].insert(CmdType::DISARM);
    table[FlightState::ERROR].insert(CmdType::DISARM);
    for (const auto cmd : {CmdType::MOVE_FORWARD, CmdType::MOVE_BACKWARD, CmdType::MOVE_LEFT, CmdType::MOVE_RIGHT,
                           CmdType::MOVE_UP, CmdType::MOVE_DOWN})
    {
        table[FlightState::TAKEOFF].insert(cmd);
    }
    for (const auto cmd : {CmdType::MOVE_FORWARD, CmdType::MOVE_BACKWARD, CmdType::MOVE_LEFT, CmdType::MOVE_RIGHT,
                           CmdType::MOVE_UP, CmdType::MOVE_DOWN, CmdType::YAW, CmdType::MOVE_POINT, CmdType::HOLD,
                           CmdType::MODE_RTL, CmdType::MODE_LAND})
    {
        table[FlightState::HOVER].insert(cmd);
        table[FlightState::MOVING].insert(cmd);
    }
    table[FlightState::HOVER].insert(CmdType::DISARM);
    return table;
}

/// 單機命令：命令名稱與符合介面規格的參數
struct CommandSample
{
    CmdType type;
    const char* name;
    QVector<double> params;
};

/// 不含編隊命令（編隊僅限全體）與 UNKNOWN 的單機命令樣本
const std::vector<CommandSample> SINGLE_AGENT_COMMANDS = {
    {CmdType::ARM, "ARM", {}},
    {CmdType::DISARM, "DISARM", {}},
    {CmdType::TAKEOFF, "TAKEOFF", {3.0}},
    {CmdType::MOVE_FORWARD, "MOVE_FORWARD", {2.0}},
    {CmdType::MOVE_BACKWARD, "MOVE_BACKWARD", {2.0}},
    {CmdType::MOVE_LEFT, "MOVE_LEFT", {2.0}},
    {CmdType::MOVE_RIGHT, "MOVE_RIGHT", {2.0}},
    {CmdType::MOVE_UP, "MOVE_UP", {1.0}},
    {CmdType::MOVE_DOWN, "MOVE_DOWN", {1.0}},
    {CmdType::YAW, "YAW", {90.0}},
    {CmdType::MOVE_POINT, "MOVE_POINT", {1.0, 2.0, -3.0, 0.0}},
    {CmdType::HOLD, "HOLD", {}},
    {CmdType::MODE_RTL, "MODE_RTL", {}},
    {CmdType::MODE_LAND, "MODE_LAND", {}},
    {CmdType::MODE_OFFBOARD, "MODE_OFFBOARD", {}},
};

/**
 * @class GuardModuleTest
 * @brief 已初始化的 GuardModule 與命令結果收集
 */
class GuardModuleTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(guard_.init());
    }

    /**
     * @brief 送出命令並回傳是否通過驗證
     * @param request 命令請求
     * @param ack 被拒絕時的回報（可為 nullptr）
     * @return 是否通過
     */
    bool submit(const CommandRequest& request, CommandAck* ack = nullptr)
    {
        QSignalSpy validated(&guard_, &GuardModule::command_validated);
        QSignalSpy acked(&guard_, &GuardModule::command_acked);
        guard_.request_command(request);
        // 同一請求必須恰好產生一個結果：通過或拒絕
        EXPECT_EQ(validated.size() + acked.size(), 1);
        if (ack != nullptr && !acked.isEmpty())
        {
            *ack = acked.takeFirst().at(0).value<CommandAck>();
        }
        return validated.size() == 1;
    }

    GuardModule guard_;
};

}  // namespace

TEST(GuardRulesTest, TransitionTableCoversEveryStateAndCommand)
{
    const auto expected = build_expected_table();
    for (const auto state : ALL_STATES)
    {
        for (const auto cmd : ALL_COMMANDS)
        {
            const bool should_allow = expected.at(state).count(cmd) > 0;
            EXPECT_EQ(guard_rules::is_command_allowed(state, cmd), should_allow)
                << "state=" << static_cast<int>(state) << " cmd=" << static_cast<int>(cmd);
        }
    }
}

TEST(GuardRulesTest, UnknownFlightStateRejectsNonFormationCommands)
{
    constexpr uint8_t UNDEFINED_FLIGHT_STATE = 200;
    const auto state = static_cast<FlightState>(UNDEFINED_FLIGHT_STATE);
    EXPECT_FALSE(guard_rules::is_command_allowed(state, CmdType::DISARM));
    EXPECT_TRUE(guard_rules::is_command_allowed(state, CmdType::FORMATION_CANCEL));
}

TEST_F(GuardModuleTest, ModuleMatchesTransitionTableForEverySingleAgentCommand)
{
    // 狀態固定為 PX4 有效、Offboard、已落地、未解鎖：
    // Arm 依 PX4 規則一律允許，Takeoff 因未解鎖一律拒絕，其餘命令依轉移表。
    const auto expected = build_expected_table();
    for (const auto state : ALL_STATES)
    {
        guard_.on_state_updated(AGENT_1, make_state(state));
        for (const auto& sample : SINGLE_AGENT_COMMANDS)
        {
            bool should_allow = expected.at(state).count(sample.type) > 0;
            if (sample.type == CmdType::ARM)
            {
                should_allow = true;
            }
            else if (sample.type == CmdType::TAKEOFF)
            {
                should_allow = false;
            }
            EXPECT_EQ(submit(make_request(AGENT_1, QString::fromLatin1(sample.name), sample.params)), should_allow)
                << "state=" << static_cast<int>(state) << " cmd=" << sample.name;
        }
    }
}

TEST_F(GuardModuleTest, AllowsOffboardModeFromIdle)
{
    guard_.on_state_updated(AGENT_1, make_state(FlightState::IDLE));
    EXPECT_TRUE(submit(make_request(AGENT_1, QStringLiteral("MODE_OFFBOARD"))));
}

TEST_F(GuardModuleTest, AllModeRequiresEveryOnlineAgentAllowed)
{
    guard_.on_state_updated(AGENT_1, make_state(FlightState::OFFBOARD_ACTIVE));
    guard_.on_state_updated(AGENT_2, make_state(FlightState::OFFBOARD_ACTIVE));
    EXPECT_TRUE(submit(make_request(ALL_AGENTS_TARGET, QStringLiteral("ARM"))));

    guard_.on_state_updated(AGENT_2, make_state(FlightState::OFFBOARD_ACTIVE, true));
    CommandAck ack;
    EXPECT_FALSE(submit(make_request(ALL_AGENTS_TARGET, QStringLiteral("ARM")), &ack));
    EXPECT_FALSE(ack.is_ok);
    EXPECT_TRUE(ack.message.contains(AGENT_2));
}

TEST_F(GuardModuleTest, AllModeRejectsWhenNoAgentOnline)
{
    CommandAck ack;
    EXPECT_FALSE(submit(make_request(ALL_AGENTS_TARGET, QStringLiteral("ARM")), &ack));
    EXPECT_FALSE(ack.is_ok);
}

TEST_F(GuardModuleTest, OfflineAgentRemovedFromAllModeValidation)
{
    guard_.on_state_updated(AGENT_1, make_state(FlightState::OFFBOARD_ACTIVE));
    guard_.on_agent_offline(AGENT_1);
    EXPECT_FALSE(submit(make_request(ALL_AGENTS_TARGET, QStringLiteral("ARM"))));
}

TEST_F(GuardModuleTest, UnknownSingleAgentRejected)
{
    CommandAck ack;
    EXPECT_FALSE(submit(make_request(QStringLiteral("uav_9_agent"), QStringLiteral("MODE_OFFBOARD")), &ack));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("不在線上")));
}

TEST_F(GuardModuleTest, OfflineSingleAgentRejected)
{
    guard_.on_state_updated(AGENT_1, make_state(FlightState::IDLE));
    guard_.on_agent_offline(AGENT_1);
    CommandAck ack;
    EXPECT_FALSE(submit(make_request(AGENT_1, QStringLiteral("MODE_OFFBOARD")), &ack));
    EXPECT_FALSE(ack.is_ok);
}

TEST_F(GuardModuleTest, OnlineAgentWithoutStateRejected)
{
    guard_.on_agent_online(AGENT_1);
    CommandAck ack;
    EXPECT_FALSE(submit(make_request(AGENT_1, QStringLiteral("MODE_OFFBOARD")), &ack));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("PX4 狀態尚未有效")));
}

TEST_F(GuardModuleTest, OnlineFalseClearsOldState)
{
    guard_.on_state_updated(AGENT_1, make_state(FlightState::OFFBOARD_ACTIVE));
    AgentStateData offline = make_state(FlightState::OFFBOARD_ACTIVE);
    offline.is_online = false;
    guard_.on_state_updated(AGENT_1, offline);
    EXPECT_FALSE(submit(make_request(AGENT_1, QStringLiteral("ARM"))));
}

TEST_F(GuardModuleTest, FormationCommandRejectedForSingleAgent)
{
    CommandAck ack;
    EXPECT_FALSE(submit(make_request(AGENT_1, QStringLiteral("FORMATION_CIRCLE"), {10.0, 20.0}), &ack));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("FORMATION")));
}

TEST_F(GuardModuleTest, FormationCommandAllowedForAll)
{
    EXPECT_TRUE(submit(make_request(ALL_AGENTS_TARGET, QStringLiteral("FORMATION_CIRCLE"), {10.0, 20.0})));
}

TEST_F(GuardModuleTest, RejectsMalformedAgentAndParams)
{
    CommandAck ack;
    EXPECT_FALSE(submit(make_request(QStringLiteral("uav_1"), QStringLiteral("MODE_OFFBOARD")), &ack));
    EXPECT_TRUE(ack.message.startsWith(QStringLiteral("命令格式錯誤")));

    EXPECT_FALSE(submit(make_request(AGENT_1, QStringLiteral("MOVE_FORWARD")), &ack));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("需要")));

    EXPECT_FALSE(
        submit(make_request(ALL_AGENTS_TARGET, QStringLiteral("FORMATION_CIRCLE"), {10.0, qQNaN()}), &ack));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("NaN")));
}

TEST_F(GuardModuleTest, CommandDoesNotInferFlightState)
{
    guard_.on_state_updated(AGENT_1, make_state(FlightState::ARMED, true));
    ASSERT_EQ(guard_.agent_flight_state(AGENT_1), FlightState::ARMED);

    ASSERT_TRUE(submit(make_request(AGENT_1, QStringLiteral("TAKEOFF"))));
    EXPECT_EQ(guard_.agent_flight_state(AGENT_1), FlightState::ARMED);

    guard_.on_state_updated(AGENT_1, make_state(FlightState::TAKEOFF, true, false));
    EXPECT_EQ(guard_.agent_flight_state(AGENT_1), FlightState::TAKEOFF);
}

TEST_F(GuardModuleTest, RejectsCommandsWhenPx4StateIsInvalid)
{
    guard_.on_state_updated(AGENT_1, make_state(FlightState::OFFBOARD_ACTIVE, false, true,
                                                PX4_NAV_STATE_OFFBOARD, false));
    CommandAck ack;
    EXPECT_FALSE(submit(make_request(AGENT_1, QStringLiteral("ARM")), &ack));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("PX4 狀態尚未有效")));
}

TEST_F(GuardModuleTest, AllModeRejectsWhenAnyAgentPx4StateIsInvalid)
{
    guard_.on_state_updated(AGENT_1, make_state(FlightState::OFFBOARD_ACTIVE));
    guard_.on_state_updated(AGENT_2, make_state(FlightState::OFFBOARD_ACTIVE, false, true,
                                                PX4_NAV_STATE_OFFBOARD, false));
    CommandAck ack;
    EXPECT_FALSE(submit(make_request(ALL_AGENTS_TARGET, QStringLiteral("ARM")), &ack));
    EXPECT_TRUE(ack.message.contains(AGENT_2));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("PX4 狀態尚未有效")));
}

TEST_F(GuardModuleTest, AllowsSingleArmFromValidDisarmedLandedPx4Offboard)
{
    guard_.on_state_updated(AGENT_1, make_state(FlightState::IDLE, false, true));
    EXPECT_TRUE(submit(make_request(AGENT_1, QStringLiteral("ARM"))));
}

TEST_F(GuardModuleTest, AllowsSingleTakeoffFromValidArmedLandedPx4Offboard)
{
    guard_.on_state_updated(AGENT_1, make_state(FlightState::IDLE, true, true));
    EXPECT_TRUE(submit(make_request(AGENT_1, QStringLiteral("TAKEOFF"))));
}

TEST_F(GuardModuleTest, RejectsArmWhenAnyPx4PreconditionIsNotMet)
{
    struct ArmCase
    {
        bool is_armed;
        bool is_landed;
        uint8_t nav_state;
    };
    constexpr uint8_t PX4_NAV_STATE_POSITION = 2;
    const std::array<ArmCase, 3> cases = {{
        {true, true, PX4_NAV_STATE_OFFBOARD},
        {false, false, PX4_NAV_STATE_OFFBOARD},
        {false, true, PX4_NAV_STATE_POSITION},
    }};
    for (const auto& item : cases)
    {
        guard_.on_state_updated(AGENT_1, make_state(FlightState::IDLE, item.is_armed, item.is_landed, item.nav_state));
        CommandAck ack;
        EXPECT_FALSE(submit(make_request(AGENT_1, QStringLiteral("ARM")), &ack));
        EXPECT_TRUE(ack.message.contains(QStringLiteral("PX4 原生狀態")));
    }
}

TEST_F(GuardModuleTest, RejectsTakeoffWhenAnyPx4PreconditionIsNotMet)
{
    struct TakeoffCase
    {
        bool is_armed;
        bool is_landed;
        uint8_t nav_state;
    };
    constexpr uint8_t PX4_NAV_STATE_POSITION = 2;
    const std::array<TakeoffCase, 3> cases = {{
        {false, true, PX4_NAV_STATE_OFFBOARD},
        {true, false, PX4_NAV_STATE_OFFBOARD},
        {true, true, PX4_NAV_STATE_POSITION},
    }};
    for (const auto& item : cases)
    {
        guard_.on_state_updated(AGENT_1, make_state(FlightState::IDLE, item.is_armed, item.is_landed, item.nav_state));
        CommandAck ack;
        EXPECT_FALSE(submit(make_request(AGENT_1, QStringLiteral("TAKEOFF")), &ack));
        EXPECT_TRUE(ack.message.contains(QStringLiteral("PX4 原生狀態")));
    }
}

TEST_F(GuardModuleTest, AllModeRejectsTakeoffWhenAnyAgentIsNotPx4Armed)
{
    guard_.on_state_updated(AGENT_1, make_state(FlightState::IDLE, true, true));
    guard_.on_state_updated(AGENT_2, make_state(FlightState::IDLE, false, true));
    CommandAck ack;
    EXPECT_FALSE(submit(make_request(ALL_AGENTS_TARGET, QStringLiteral("TAKEOFF")), &ack));
    EXPECT_TRUE(ack.message.contains(AGENT_2));
}

TEST_F(GuardModuleTest, RejectsBeforeInit)
{
    GuardModule uninitialized;
    QSignalSpy validated(&uninitialized, &GuardModule::command_validated);
    QSignalSpy acked(&uninitialized, &GuardModule::command_acked);
    uninitialized.on_state_updated(AGENT_1, make_state(FlightState::IDLE));
    uninitialized.request_command(make_request(AGENT_1, QStringLiteral("MODE_OFFBOARD")));
    EXPECT_TRUE(validated.isEmpty());
    ASSERT_EQ(acked.size(), 1);
}
