/**
 * @file test_command_protocol.cpp
 * @brief 命令協定：目標格式、參數規格、NaN／Inf 與命令列組裝
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 由網頁版 test_ws_protocol.cpp 移植；JSON 與 ros2 CLI 相關測試隨功能移除。
 */
#include "core/command_protocol.hpp"

#include <gtest/gtest.h>

#include <limits>

using namespace gcs;
namespace protocol = gcs::command_protocol;

namespace
{

CommandRequest request_of(const QString& agent, const QString& command, const QVector<double>& params = {})
{
    CommandRequest request;
    request.agent = agent;
    request.command = command;
    request.params = params;
    return request;
}

}  // namespace

TEST(CommandProtocolTest, MakesCommandLineWithShortestNumbers)
{
    EXPECT_EQ(protocol::make_command_line(request_of(ALL_AGENTS_TARGET, QStringLiteral("FORMATION_CIRCLE"), {10, 20.5})),
              QStringLiteral("FORMATION_CIRCLE 10 20.5"));
    EXPECT_EQ(protocol::make_command_line(request_of(QStringLiteral("uav_1_agent"), QStringLiteral("YAW"), {-90})),
              QStringLiteral("YAW -90"));
    EXPECT_EQ(protocol::make_command_line(request_of(QStringLiteral("uav_1_agent"), QStringLiteral("TAKEOFF"), {0.1})),
              QStringLiteral("TAKEOFF 0.1"));
    EXPECT_EQ(protocol::make_command_line(request_of(QStringLiteral("uav_1_agent"), QStringLiteral("ARM"))),
              QStringLiteral("ARM"));
}

TEST(CommandProtocolTest, AcceptsOnlyValidAgentFormats)
{
    EXPECT_TRUE(protocol::is_agent_target_valid(QStringLiteral("all")));
    EXPECT_TRUE(protocol::is_agent_target_valid(QStringLiteral("uav_1_agent")));
    EXPECT_TRUE(protocol::is_agent_target_valid(QStringLiteral("uav_12_agent")));
    EXPECT_FALSE(protocol::is_agent_target_valid(QString()));
    EXPECT_FALSE(protocol::is_agent_target_valid(QStringLiteral("uav_1")));
    EXPECT_FALSE(protocol::is_agent_target_valid(QStringLiteral("all_agent")));
    EXPECT_FALSE(protocol::is_agent_target_valid(QStringLiteral("uav_agent")));
    EXPECT_FALSE(protocol::is_agent_target_valid(QStringLiteral("uav_0_agent")));
    EXPECT_FALSE(protocol::is_agent_target_valid(QStringLiteral("fmu1_agent")));
    EXPECT_FALSE(protocol::is_single_agent(QStringLiteral("all")));
    EXPECT_TRUE(protocol::is_single_agent(QStringLiteral("uav_3_agent")));
}

TEST(CommandProtocolTest, RejectsEmptyAgentAndCommand)
{
    EXPECT_TRUE(protocol::validate_request(request_of(QString(), QStringLiteral("ARM"))).contains(QStringLiteral("agent")));
    EXPECT_TRUE(protocol::validate_request(request_of(ALL_AGENTS_TARGET, QString())).contains(QStringLiteral("command")));
}

TEST(CommandProtocolTest, RejectsUnsupportedCommand)
{
    const QString error = protocol::validate_request(request_of(ALL_AGENTS_TARGET, QStringLiteral("SELF_DESTRUCT")));
    EXPECT_TRUE(error.contains(QStringLiteral("不支援")));
    EXPECT_EQ(protocol::parse_command(QStringLiteral("SELF_DESTRUCT")), CmdType::UNKNOWN);
}

TEST(CommandProtocolTest, RejectsWrongParamCount)
{
    EXPECT_TRUE(protocol::validate_request(request_of(QStringLiteral("uav_1_agent"), QStringLiteral("MOVE_FORWARD")))
                    .contains(QStringLiteral("需要")));
    EXPECT_TRUE(protocol::validate_request(request_of(QStringLiteral("uav_1_agent"), QStringLiteral("YAW"), {10, 20}))
                    .contains(QStringLiteral("需要")));
    EXPECT_TRUE(protocol::validate_request(request_of(QStringLiteral("uav_1_agent"), QStringLiteral("TAKEOFF"), {1, 2}))
                    .contains(QStringLiteral("至")));
}

TEST(CommandProtocolTest, AcceptsValidRequests)
{
    EXPECT_TRUE(protocol::validate_request(request_of(QStringLiteral("uav_1_agent"), QStringLiteral("YAW"), {-90}))
                    .isEmpty());
    EXPECT_TRUE(protocol::validate_request(request_of(QStringLiteral("uav_1_agent"), QStringLiteral("TAKEOFF")))
                    .isEmpty());
    EXPECT_TRUE(
        protocol::validate_request(request_of(QStringLiteral("uav_1_agent"), QStringLiteral("move_point"), {1, 2, -3, 0}))
            .isEmpty());
}

TEST(CommandProtocolTest, RejectsNonFiniteParams)
{
    const auto nan_request = request_of(QStringLiteral("uav_1_agent"), QStringLiteral("MOVE_FORWARD"),
                                        {std::numeric_limits<double>::quiet_NaN()});
    EXPECT_TRUE(protocol::has_non_finite_param(nan_request));
    EXPECT_TRUE(protocol::validate_request(nan_request).contains(QStringLiteral("NaN")));

    const auto inf_request = request_of(QStringLiteral("uav_1_agent"), QStringLiteral("MOVE_FORWARD"),
                                        {std::numeric_limits<double>::infinity()});
    EXPECT_TRUE(protocol::has_non_finite_param(inf_request));

    EXPECT_FALSE(
        protocol::has_non_finite_param(request_of(QStringLiteral("uav_1_agent"), QStringLiteral("MOVE_FORWARD"), {3})));
}

TEST(CommandProtocolTest, ParsesCompatibilityShorthand)
{
    EXPECT_EQ(protocol::parse_command(QStringLiteral("FORWARD")), CmdType::MOVE_FORWARD);
    EXPECT_EQ(protocol::parse_command(QStringLiteral(" left ")), CmdType::MOVE_LEFT);
    EXPECT_EQ(protocol::parse_command(QStringLiteral("mode_rtl")), CmdType::MODE_RTL);
}

TEST(CommandProtocolTest, DetectsFormationCommands)
{
    EXPECT_TRUE(protocol::is_formation_command(QStringLiteral("FORMATION_CIRCLE")));
    EXPECT_TRUE(protocol::is_formation_command(QStringLiteral("formation_cancel")));
    EXPECT_TRUE(protocol::is_formation_command(QStringLiteral("FORMATION_V_SHAPE")));
    EXPECT_EQ(protocol::parse_command(QStringLiteral("formation_v_shape")), CmdType::FORMATION_V_SHAPE);
    EXPECT_FALSE(protocol::is_formation_command(QStringLiteral("MODE_RTL")));
}

TEST(CommandProtocolTest, PrintsIntegersWithoutExponent)
{
    const QString agent = QStringLiteral("uav_1_agent");
    // 最短表示會把整數寫成 1e+06；Agent 以整數解析任務識別碼
    EXPECT_EQ(protocol::make_command_line(request_of(agent, QStringLiteral("MISSION_START"), {1000000})),
              QStringLiteral("MISSION_START 1000000"));
    EXPECT_EQ(protocol::make_command_line(request_of(agent, QStringLiteral("MISSION_START"), {4294967295.0})),
              QStringLiteral("MISSION_START 4294967295"));
    EXPECT_EQ(protocol::make_command_line(request_of(agent, QStringLiteral("MOVE_UP"), {-2000000})),
              QStringLiteral("MOVE_UP -2000000"));
    // 非整數維持最短可還原表示
    EXPECT_EQ(protocol::make_command_line(request_of(agent, QStringLiteral("MOVE_UP"), {0.000001})),
              QStringLiteral("MOVE_UP 1e-06"));
}

TEST(CommandProtocolTest, ValidatesMissionStartId)
{
    const QString agent = QStringLiteral("uav_1_agent");
    const QString start = QStringLiteral("MISSION_START");
    EXPECT_EQ(protocol::parse_command(start), CmdType::MISSION_START);
    EXPECT_TRUE(protocol::validate_request(request_of(agent, start, {123})).isEmpty());
    EXPECT_TRUE(protocol::validate_request(request_of(agent, start, {1})).isEmpty());
    EXPECT_TRUE(protocol::validate_request(request_of(agent, start, {4294967295.0})).isEmpty());
    EXPECT_FALSE(protocol::validate_request(request_of(agent, start, {0})).isEmpty());
    EXPECT_FALSE(protocol::validate_request(request_of(agent, start, {1.5})).isEmpty());
    EXPECT_FALSE(protocol::validate_request(request_of(agent, start, {-1})).isEmpty());
    EXPECT_FALSE(protocol::validate_request(request_of(agent, start, {4294967296.0})).isEmpty());
    EXPECT_FALSE(protocol::validate_request(request_of(agent, start)).isEmpty());
    EXPECT_FALSE(protocol::validate_request(request_of(agent, start, {1, 2})).isEmpty());
}

TEST(CommandProtocolTest, ValidatesMissionStopAndRebootParams)
{
    const QString agent = QStringLiteral("uav_1_agent");
    EXPECT_EQ(protocol::parse_command(QStringLiteral("mission_stop")), CmdType::MISSION_STOP);
    EXPECT_TRUE(protocol::validate_request(request_of(agent, QStringLiteral("MISSION_STOP"))).isEmpty());
    EXPECT_FALSE(protocol::validate_request(request_of(agent, QStringLiteral("MISSION_STOP"), {1})).isEmpty());

    const QString reboot = QStringLiteral("REBOOT_FC");
    EXPECT_EQ(protocol::parse_command(reboot), CmdType::REBOOT_FC);
    EXPECT_TRUE(protocol::validate_request(request_of(agent, reboot)).isEmpty());
    EXPECT_TRUE(protocol::validate_request(request_of(agent, reboot, {0})).isEmpty());
    EXPECT_TRUE(protocol::validate_request(request_of(agent, reboot, {1})).isEmpty());
    EXPECT_FALSE(protocol::validate_request(request_of(agent, reboot, {2})).isEmpty());
    EXPECT_FALSE(protocol::validate_request(request_of(agent, reboot, {0.5})).isEmpty());
    EXPECT_FALSE(protocol::validate_request(request_of(agent, reboot, {1, 1})).isEmpty());

    EXPECT_FALSE(protocol::is_forced_reboot(request_of(agent, reboot)));
    EXPECT_FALSE(protocol::is_forced_reboot(request_of(agent, reboot, {0})));
    EXPECT_TRUE(protocol::is_forced_reboot(request_of(agent, reboot, {1})));
    EXPECT_FALSE(protocol::is_forced_reboot(request_of(agent, QStringLiteral("MOVE_UP"), {1})));
}

TEST(CommandProtocolTest, SingleAgentOnlyCommandsRejectAllTarget)
{
    // Agent 會忽略全體 topic 上的 MISSION_* 與 REBOOT_FC
    EXPECT_FALSE(
        protocol::validate_request(request_of(ALL_AGENTS_TARGET, QStringLiteral("MISSION_START"), {5})).isEmpty());
    EXPECT_FALSE(protocol::validate_request(request_of(ALL_AGENTS_TARGET, QStringLiteral("MISSION_STOP"))).isEmpty());
    EXPECT_FALSE(protocol::validate_request(request_of(ALL_AGENTS_TARGET, QStringLiteral("REBOOT_FC"))).isEmpty());
    EXPECT_TRUE(protocol::is_single_agent_only(CmdType::MISSION_START));
    EXPECT_TRUE(protocol::is_single_agent_only(CmdType::MISSION_STOP));
    EXPECT_TRUE(protocol::is_single_agent_only(CmdType::REBOOT_FC));
    EXPECT_FALSE(protocol::is_single_agent_only(CmdType::HOLD));
}
