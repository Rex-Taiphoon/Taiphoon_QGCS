/**
 * @file test_guard_transition.cpp
 * @brief GuardModule 轉移表全組合、PX4 原生狀態規則（含重新啟動飛控）、航點任務上傳與全體模式驗證
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
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <vector>

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

const std::array<CmdType, 24> ALL_COMMANDS = {
    CmdType::ARM,           CmdType::DISARM,          CmdType::TAKEOFF,          CmdType::MOVE_FORWARD,
    CmdType::MOVE_BACKWARD, CmdType::MOVE_LEFT,       CmdType::MOVE_RIGHT,       CmdType::MOVE_UP,
    CmdType::MOVE_DOWN,     CmdType::YAW,             CmdType::MOVE_POINT,       CmdType::HOLD,
    CmdType::MODE_RTL,      CmdType::MODE_LAND,       CmdType::MODE_OFFBOARD,    CmdType::FORMATION_CIRCLE,
    CmdType::FORMATION_HORI_LINEAR, CmdType::FORMATION_VERT_LINEAR, CmdType::FORMATION_V_SHAPE,
    CmdType::FORMATION_CANCEL,      CmdType::MISSION_START,         CmdType::MISSION_STOP,
    CmdType::REBOOT_FC,             CmdType::UNKNOWN,
};

/// AgentHealth.px4_arming_state 的所有值
const std::array<uint8_t, 3> ALL_ARMING_STATES = {
    px4_arming::NEVER_RECEIVED, px4_arming::DISARMED, px4_arming::ARMED,
};

/// 健康狀態中曾收到 VehicleLandDetected 的時間（秒）
constexpr float LAND_DETECTED_AGE_S = 12.0F;
/// 從未收到 VehicleLandDetected
constexpr float NEVER_RECEIVED_AGE_S = -1.0F;

const std::set<CmdType> FORMATION_COMMANDS = {
    CmdType::FORMATION_CIRCLE, CmdType::FORMATION_HORI_LINEAR, CmdType::FORMATION_VERT_LINEAR,
    CmdType::FORMATION_V_SHAPE, CmdType::FORMATION_CANCEL,
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
    // 航點任務只能從懸停開始；停止任務任何階段都允許；REBOOT_FC 不查表
    table[FlightState::HOVER].insert(CmdType::MISSION_START);
    for (const auto state : ALL_STATES)
    {
        table[state].insert(CmdType::MISSION_STOP);
    }
    return table;
}

/**
 * @brief 建立健康狀態
 * @param arming_state px4_arming 的數值
 * @param land_detected_age_s 距離最後一筆 VehicleLandDetected 的秒數；從未收到為負值
 * @return AgentHealth
 */
AgentHealthData make_health(uint8_t arming_state, float land_detected_age_s = NEVER_RECEIVED_AGE_S)
{
    AgentHealthData health;
    health.px4_arming_state = arming_state;
    health.px4_land_detected_age_s = land_detected_age_s;
    return health;
}

/**
 * @brief 期望的重新啟動飛控規則（ADR-0020）
 *
 * 一般：PX4 狀態有效、上鎖、落地。強制：PX4 狀態不明、收到健康狀態，且最後有效狀態、
 * 健康狀態保留的解鎖狀態、AgentState 保留的 armed／landed（landed 需曾收到落地偵測）都不顯示可能在空中。
 *
 * @param facts 已知資訊
 * @param is_force 是否強制
 * @return 是否允許
 */
bool expected_reboot_allowed(const guard_rules::RebootFacts& facts, bool is_force)
{
    const auto& state = facts.state;
    const bool is_state_valid = state.has_value() && state->is_px4_state_valid;
    if (!is_force)
    {
        return is_state_valid && !state->is_armed && state->is_landed;
    }
    if (is_state_valid || !facts.health.has_value())
    {
        return false;
    }
    const auto& last_valid = facts.last_valid_state;
    const bool was_airborne = last_valid.has_value() && (last_valid->is_armed || !last_valid->is_landed);
    const bool is_retained_armed =
        facts.health->px4_arming_state == px4_arming::ARMED || (state.has_value() && state->is_armed);
    const bool is_retained_not_landed =
        state.has_value() && facts.health->px4_land_detected_age_s >= 0.0F && !state->is_landed;
    return !was_airborne && !is_retained_armed && !is_retained_not_landed;
}

/**
 * @brief 列出測試用的 AgentState：沒有狀態，以及每個任務階段 × PX4 有效 × 解鎖 × 落地
 * @return AgentState 清單
 */
std::vector<std::optional<AgentStateData>> all_reboot_states()
{
    std::vector<std::optional<AgentStateData>> states{std::nullopt};
    for (const auto mode : ALL_STATES)
    {
        for (const bool is_valid : {true, false})
        {
            for (const bool is_armed : {true, false})
            {
                for (const bool is_landed : {true, false})
                {
                    states.emplace_back(make_state(mode, is_armed, is_landed, PX4_NAV_STATE_OFFBOARD, is_valid));
                }
            }
        }
    }
    return states;
}

/**
 * @brief 列出測試用的最後有效狀態：從未有效，以及解鎖 × 落地
 * @return 最後有效狀態清單
 */
std::vector<std::optional<AgentStateData>> all_last_valid_states()
{
    std::vector<std::optional<AgentStateData>> states{std::nullopt};
    for (const bool is_armed : {true, false})
    {
        for (const bool is_landed : {true, false})
        {
            states.emplace_back(make_state(FlightState::IDLE, is_armed, is_landed));
        }
    }
    return states;
}

/**
 * @brief 列出測試用的健康狀態：沒有收到，以及解鎖狀態 × 是否曾收到落地偵測
 * @return 健康狀態清單
 */
std::vector<std::optional<AgentHealthData>> all_healths()
{
    std::vector<std::optional<AgentHealthData>> healths{std::nullopt};
    for (const auto arming_state : ALL_ARMING_STATES)
    {
        for (const float age : {LAND_DETECTED_AGE_S, NEVER_RECEIVED_AGE_S})
        {
            healths.emplace_back(make_health(arming_state, age));
        }
    }
    return healths;
}

/**
 * @brief 懸停中、GPS 有效的狀態
 * @return AgentState
 */
AgentStateData hovering_with_gps()
{
    AgentStateData state = make_state(FlightState::HOVER, true, false);
    state.position_global = {25.0, 121.5, 50.0};
    return state;
}

/**
 * @brief 合法的航點任務：往北排列、高度 5 公尺、停留 3 秒、維持航向
 * @param count 航點數
 * @return 航點任務
 */
WaypointMissionData valid_mission(int count = 2)
{
    WaypointMissionData mission;
    mission.mission_id = 42;
    mission.altitude_frame = MISSION_ALTITUDE_PX4_LOCAL;
    for (int i = 1; i <= count; ++i)
    {
        MissionWaypointData waypoint;
        waypoint.latitude_deg = 25.0 + 0.00001 * i;
        waypoint.longitude_deg = 121.5;
        waypoint.altitude_m = 5.0F;
        waypoint.dwell_s = 3.0F;
        mission.waypoints.push_back(waypoint);
    }
    return mission;
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
    {CmdType::MISSION_START, "MISSION_START", {5.0}},
    {CmdType::MISSION_STOP, "MISSION_STOP", {}},
    {CmdType::REBOOT_FC, "REBOOT_FC", {}},
    {CmdType::REBOOT_FC, "REBOOT_FC", {1.0}},
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
     * @brief 送出航點任務上傳並回傳是否通過驗證
     * @param agent 目標
     * @param mission 航點任務
     * @param error 被拒絕時的原因（可為 nullptr）
     * @return 是否通過
     */
    bool submit_mission(const QString& agent, const WaypointMissionData& mission, QString* error = nullptr)
    {
        QSignalSpy validated(&guard_, &GuardModule::mission_upload_validated);
        QSignalSpy acked(&guard_, &GuardModule::mission_upload_acked);
        QSignalSpy command_acked(&guard_, &GuardModule::command_acked);
        guard_.request_mission_upload(agent, mission);
        EXPECT_EQ(validated.size() + acked.size(), 1);
        // 上傳結果不經命令回報，UI 的命令提示不受影響
        EXPECT_EQ(command_acked.size(), 0);
        if (!acked.isEmpty())
        {
            const CommandAck ack = acked.takeFirst().at(0).value<CommandAck>();
            EXPECT_FALSE(ack.is_ok);
            EXPECT_EQ(ack.command, MISSION_UPLOAD_ACK_NAME);
            if (error != nullptr)
            {
                *error = ack.message;
            }
        }
        return validated.size() == 1;
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
            else if (sample.type == CmdType::REBOOT_FC)
            {
                // 狀態有效、上鎖、落地：一般重新啟動允許，強制一律拒絕
                should_allow = sample.params.isEmpty();
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

TEST_F(GuardModuleTest, VShapeFormationFollowsFormationRules)
{
    CommandAck ack;
    EXPECT_FALSE(submit(make_request(AGENT_1, QStringLiteral("FORMATION_V_SHAPE"), {5.0, 20.0}), &ack));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("FORMATION")));
    EXPECT_TRUE(submit(make_request(ALL_AGENTS_TARGET, QStringLiteral("FORMATION_V_SHAPE"), {5.0, 20.0})));
    // 參數數量與 FORMATION_HORI_LINEAR 相同：間距與高度
    EXPECT_FALSE(submit(make_request(ALL_AGENTS_TARGET, QStringLiteral("FORMATION_V_SHAPE"), {5.0}), &ack));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("需要")));
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

TEST(GuardRulesTest, RebootRuleCoversEveryStateHealthAndLastValidCombination)
{
    for (const auto& state : all_reboot_states())
    {
        for (const auto& last_valid : all_last_valid_states())
        {
            for (const auto& health : all_healths())
            {
                const guard_rules::RebootFacts facts{state, last_valid, health};
                for (const bool is_force : {false, true})
                {
                    const QString reason = guard_rules::check_reboot(facts, is_force);
                    EXPECT_EQ(reason.isEmpty(), expected_reboot_allowed(facts, is_force))
                        << "has_state=" << state.has_value()
                        << " mode=" << (state.has_value() ? static_cast<int>(state->flight_mode) : -1)
                        << " valid=" << (state.has_value() && state->is_px4_state_valid)
                        << " armed=" << (state.has_value() && state->is_armed)
                        << " landed=" << (state.has_value() && state->is_landed)
                        << " has_last_valid=" << last_valid.has_value() << " has_health=" << health.has_value()
                        << " force=" << is_force;
                }
            }
        }
        // 按鈕判斷只看 AgentState，因此只代表一般重新啟動
        if (state.has_value())
        {
            const guard_rules::RebootFacts facts{state, std::nullopt, std::nullopt};
            EXPECT_EQ(guard_rules::is_command_allowed_for_agent(*state, CmdType::REBOOT_FC),
                      expected_reboot_allowed(facts, false));
        }
    }
}

TEST(GuardRulesTest, MissionStopAllowedEvenWhenPx4StateInvalid)
{
    for (const auto state : ALL_STATES)
    {
        const AgentStateData agent_state = make_state(state, true, false, PX4_NAV_STATE_OFFBOARD, false);
        EXPECT_TRUE(guard_rules::is_command_allowed_for_agent(agent_state, CmdType::MISSION_STOP))
            << "state=" << static_cast<int>(state);
        EXPECT_FALSE(guard_rules::is_command_allowed_for_agent(agent_state, CmdType::MISSION_START))
            << "state=" << static_cast<int>(state);
    }
}

TEST_F(GuardModuleTest, ForcedRebootNeedsHealthAndNoAirborneEvidence)
{
    const auto normal = make_request(AGENT_1, QStringLiteral("REBOOT_FC"));
    const auto forced = make_request(AGENT_1, QStringLiteral("REBOOT_FC"), {1.0});
    CommandAck ack;

    // 離線時送達的健康狀態不保存；上線後沒有 AgentState 也沒有健康狀態時不可強制
    guard_.on_health_updated(AGENT_1, make_health(px4_arming::NEVER_RECEIVED));
    guard_.on_agent_online(AGENT_1);
    EXPECT_FALSE(submit(forced, &ack));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("健康")));

    // 收到 odometry 前只有健康狀態：只能強制
    guard_.on_health_updated(AGENT_1, make_health(px4_arming::NEVER_RECEIVED));
    EXPECT_FALSE(submit(normal, &ack));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("強制")));
    EXPECT_TRUE(submit(forced));

    // 健康狀態保留的解鎖狀態為 ARMED
    guard_.on_health_updated(AGENT_1, make_health(px4_arming::ARMED));
    EXPECT_FALSE(submit(forced, &ack));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("已解鎖")));

    // AgentState 保留未落地，且健康狀態顯示曾收到落地偵測
    guard_.on_health_updated(AGENT_1, make_health(px4_arming::DISARMED, LAND_DETECTED_AGE_S));
    guard_.on_state_updated(AGENT_1, make_state(FlightState::ERROR, false, false, PX4_NAV_STATE_OFFBOARD, false));
    EXPECT_FALSE(submit(forced, &ack));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("未落地")));
    guard_.on_health_updated(AGENT_1, make_health(px4_arming::DISARMED));
    EXPECT_TRUE(submit(forced));
}

TEST_F(GuardModuleTest, ForcedRebootRemembersLastValidStateAcrossOffline)
{
    const auto forced = make_request(AGENT_1, QStringLiteral("REBOOT_FC"), {1.0});
    const auto px4_lost = make_state(FlightState::ERROR, false, true, PX4_NAV_STATE_OFFBOARD, false);
    CommandAck ack;

    // 最後一次狀態有效時在空中：PX4 斷線後 Agent 保留的值看似已落地也不可強制
    guard_.on_state_updated(AGENT_1, make_state(FlightState::HOVER, true, false));
    guard_.on_health_updated(AGENT_1, make_health(px4_arming::DISARMED));
    EXPECT_FALSE(submit(forced, &ack));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("一般")));
    guard_.on_state_updated(AGENT_1, px4_lost);
    EXPECT_FALSE(submit(forced, &ack));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("最後一次")));

    // Agent 離線再上線仍保留最後有效狀態
    guard_.on_agent_offline(AGENT_1);
    guard_.on_agent_online(AGENT_1);
    guard_.on_health_updated(AGENT_1, make_health(px4_arming::DISARMED));
    guard_.on_state_updated(AGENT_1, px4_lost);
    EXPECT_FALSE(submit(forced, &ack));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("最後一次")));

    // 最後一次狀態有效時已上鎖落地：可強制
    guard_.on_state_updated(AGENT_1, make_state(FlightState::IDLE, false, true));
    EXPECT_TRUE(submit(make_request(AGENT_1, QStringLiteral("REBOOT_FC"))));
    guard_.on_state_updated(AGENT_1, px4_lost);
    EXPECT_TRUE(submit(forced));

    // 狀態有效：解鎖或未落地都拒絕
    guard_.on_state_updated(AGENT_1, make_state(FlightState::ARMED, true, true));
    EXPECT_FALSE(submit(make_request(AGENT_1, QStringLiteral("REBOOT_FC")), &ack));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("解鎖")));
    guard_.on_state_updated(AGENT_1, make_state(FlightState::IDLE, false, false));
    EXPECT_FALSE(submit(make_request(AGENT_1, QStringLiteral("REBOOT_FC")), &ack));
    EXPECT_TRUE(ack.message.contains(QStringLiteral("落地")));

    // shutdown 清除所有記憶
    guard_.shutdown();
    ASSERT_TRUE(guard_.init());
    guard_.on_agent_online(AGENT_1);
    guard_.on_health_updated(AGENT_1, make_health(px4_arming::DISARMED));
    guard_.on_state_updated(AGENT_1, px4_lost);
    EXPECT_TRUE(submit(forced));
}

TEST_F(GuardModuleTest, MissionStopAllowedWhenPx4StateInvalid)
{
    guard_.on_state_updated(AGENT_1, make_state(FlightState::HOVER, true, false, PX4_NAV_STATE_OFFBOARD, false));
    EXPECT_TRUE(submit(make_request(AGENT_1, QStringLiteral("MISSION_STOP"))));
    EXPECT_FALSE(submit(make_request(AGENT_1, QStringLiteral("HOLD"))));
}

TEST_F(GuardModuleTest, SingleAgentOnlyCommandsRejectedForAll)
{
    guard_.on_state_updated(AGENT_1, make_state(FlightState::HOVER, true, false));
    EXPECT_FALSE(submit(make_request(ALL_AGENTS_TARGET, QStringLiteral("MISSION_START"), {5.0})));
    EXPECT_FALSE(submit(make_request(ALL_AGENTS_TARGET, QStringLiteral("MISSION_STOP"))));
    EXPECT_FALSE(submit(make_request(ALL_AGENTS_TARGET, QStringLiteral("REBOOT_FC"))));
}

TEST_F(GuardModuleTest, MissionUploadAllowedOnlyWhenHoveringWithValidPlan)
{
    // 沒有線上 Agent 或沒有狀態
    EXPECT_FALSE(submit_mission(AGENT_1, valid_mission()));
    guard_.on_agent_online(AGENT_1);
    EXPECT_FALSE(submit_mission(AGENT_1, valid_mission()));

    guard_.on_state_updated(AGENT_1, hovering_with_gps());
    EXPECT_TRUE(submit_mission(AGENT_1, valid_mission()));

    // 全體目標、非懸停、PX4 狀態無效
    EXPECT_FALSE(submit_mission(ALL_AGENTS_TARGET, valid_mission()));
    AgentStateData moving = hovering_with_gps();
    moving.flight_mode = static_cast<uint8_t>(FlightState::MOVING);
    guard_.on_state_updated(AGENT_1, moving);
    EXPECT_FALSE(submit_mission(AGENT_1, valid_mission()));
    AgentStateData invalid = hovering_with_gps();
    invalid.is_px4_state_valid = false;
    guard_.on_state_updated(AGENT_1, invalid);
    EXPECT_FALSE(submit_mission(AGENT_1, valid_mission()));
    AgentStateData no_gps = hovering_with_gps();
    no_gps.position_global = {std::numeric_limits<double>::quiet_NaN(), 121.5, 50.0};
    guard_.on_state_updated(AGENT_1, no_gps);
    EXPECT_FALSE(submit_mission(AGENT_1, valid_mission()));
}

TEST_F(GuardModuleTest, MissionUploadPassesContentThrough)
{
    guard_.on_state_updated(AGENT_1, hovering_with_gps());
    QSignalSpy validated(&guard_, &GuardModule::mission_upload_validated);
    guard_.request_mission_upload(AGENT_1, valid_mission(3));
    ASSERT_EQ(validated.size(), 1);
    EXPECT_EQ(validated.at(0).at(0).toString(), AGENT_1);
    const auto mission = validated.at(0).at(1).value<WaypointMissionData>();
    EXPECT_EQ(mission.mission_id, 42U);
    ASSERT_EQ(mission.waypoints.size(), 3U);
    EXPECT_DOUBLE_EQ(mission.waypoints[2].latitude_deg, 25.00003);
}

TEST_F(GuardModuleTest, MissionUploadChecksEveryWaypointField)
{
    guard_.on_state_updated(AGENT_1, hovering_with_gps());
    QString error;

    // 邊界值允許：50 個航點、高度 1／120 公尺、停留 0／60 秒
    EXPECT_TRUE(submit_mission(AGENT_1, valid_mission(mission_limits::MAX_WAYPOINTS)));
    WaypointMissionData boundary = valid_mission(2);
    boundary.waypoints[0].altitude_m = 1.0F;
    boundary.waypoints[1].altitude_m = 120.0F;
    boundary.waypoints[0].dwell_s = 0.0F;
    boundary.waypoints[1].dwell_s = 60.0F;
    EXPECT_TRUE(submit_mission(AGENT_1, boundary));

    EXPECT_FALSE(submit_mission(AGENT_1, valid_mission(0)));
    EXPECT_FALSE(submit_mission(AGENT_1, valid_mission(mission_limits::MAX_WAYPOINTS + 1)));

    WaypointMissionData zero_id = valid_mission();
    zero_id.mission_id = 0;
    EXPECT_FALSE(submit_mission(AGENT_1, zero_id));
    WaypointMissionData wrong_frame = valid_mission();
    wrong_frame.altitude_frame = 0;
    EXPECT_FALSE(submit_mission(AGENT_1, wrong_frame));

    const auto expect_waypoint_rejected = [&](auto modify, const char* label)
    {
        WaypointMissionData mission = valid_mission(3);
        modify(&mission.waypoints[1]);
        EXPECT_FALSE(submit_mission(AGENT_1, mission, &error)) << label;
        EXPECT_TRUE(error.contains(QStringLiteral("第 2 個航點"))) << label << " " << error.toStdString();
    };
    expect_waypoint_rejected([](MissionWaypointData* w) { w->altitude_m = 0.9F; }, "altitude low");
    expect_waypoint_rejected([](MissionWaypointData* w) { w->altitude_m = 120.5F; }, "altitude high");
    expect_waypoint_rejected([](MissionWaypointData* w) { w->altitude_m = std::numeric_limits<float>::infinity(); },
                             "altitude inf");
    expect_waypoint_rejected([](MissionWaypointData* w) { w->dwell_s = -0.1F; }, "dwell negative");
    expect_waypoint_rejected([](MissionWaypointData* w) { w->dwell_s = 60.5F; }, "dwell high");
    expect_waypoint_rejected([](MissionWaypointData* w) { w->dwell_s = std::numeric_limits<float>::quiet_NaN(); },
                             "dwell nan");
    expect_waypoint_rejected([](MissionWaypointData* w) { w->yaw_deg = 90.0F; }, "yaw set");
    expect_waypoint_rejected([](MissionWaypointData* w) { w->latitude_deg = 91.0; }, "latitude");
    expect_waypoint_rejected(
        [](MissionWaypointData* w) { w->longitude_deg = std::numeric_limits<double>::quiet_NaN(); }, "longitude nan");
    // 約 1.1 公里外
    expect_waypoint_rejected([](MissionWaypointData* w) { w->latitude_deg = 25.01; }, "out of range");
}
