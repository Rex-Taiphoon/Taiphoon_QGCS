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
