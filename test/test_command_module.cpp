/**
 * @file test_command_module.cpp
 * @brief CommandModule：路由、命令列組裝、回報與發布失敗處理
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "fakes/fake_agent_link.hpp"
#include "modules/command/command_module.hpp"

#include <QSignalSpy>

#include <gtest/gtest.h>

using namespace gcs;
using gcs::test::FakeAgentLink;
using gcs::test::make_request;

namespace
{

/**
 * @class CommandModuleTest
 * @brief 以 FakeAgentLink 驅動的 CommandModule
 */
class CommandModuleTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(command_.init());
    }

    /**
     * @brief 發布命令並取得唯一的回報
     * @param request 命令請求
     * @return 回報
     */
    CommandAck publish(const CommandRequest& request)
    {
        QSignalSpy acked(&command_, &CommandModule::command_acked);
        command_.publish_validated(request);
        EXPECT_EQ(acked.size(), 1);
        return acked.isEmpty() ? CommandAck{} : acked.takeFirst().at(0).value<CommandAck>();
    }

    FakeAgentLink link_;
    CommandModule command_{&link_};
};

}  // namespace

TEST_F(CommandModuleTest, SingleAgentCommandPublishedAndAckedOk)
{
    const CommandAck ack = publish(make_request(QStringLiteral("uav_1_agent"), QStringLiteral("TAKEOFF"), {5}));
    EXPECT_TRUE(ack.is_ok);
    EXPECT_EQ(ack.agent, QStringLiteral("uav_1_agent"));
    EXPECT_EQ(ack.command, QStringLiteral("TAKEOFF"));
    ASSERT_EQ(link_.published_commands.size(), 1U);
    EXPECT_EQ(link_.published_commands[0].target, QStringLiteral("uav_1_agent"));
    EXPECT_EQ(link_.published_commands[0].command_line, QStringLiteral("TAKEOFF 5"));
}

TEST_F(CommandModuleTest, AllModeCommandRoutedToAllTarget)
{
    const CommandAck ack = publish(make_request(ALL_AGENTS_TARGET, QStringLiteral("FORMATION_CIRCLE"), {10, 20}));
    EXPECT_TRUE(ack.is_ok);
    ASSERT_EQ(link_.published_commands.size(), 1U);
    EXPECT_EQ(link_.published_commands[0].target, ALL_AGENTS_TARGET);
    EXPECT_EQ(link_.published_commands[0].command_line, QStringLiteral("FORMATION_CIRCLE 10 20"));
}

TEST_F(CommandModuleTest, MalformedRequestNotPublished)
{
    const CommandAck ack = publish(make_request(QStringLiteral("uav_1"), QStringLiteral("ARM")));
    EXPECT_FALSE(ack.is_ok);
    EXPECT_TRUE(ack.message.startsWith(QStringLiteral("命令格式錯誤")));
    EXPECT_TRUE(link_.published_commands.empty());
}

TEST_F(CommandModuleTest, NonFiniteParamNeverReachesLink)
{
    const CommandAck ack =
        publish(make_request(QStringLiteral("uav_1_agent"), QStringLiteral("MOVE_FORWARD"), {qInf()}));
    EXPECT_FALSE(ack.is_ok);
    EXPECT_TRUE(link_.published_commands.empty());
}

TEST_F(CommandModuleTest, LinkFailureReportedAsError)
{
    // 尚無訂閱者時命令會在 volatile topic 上遺失，必須回報失敗而非成功。
    link_.is_command_channel_ready = false;
    const CommandAck ack = publish(make_request(QStringLiteral("uav_1_agent"), QStringLiteral("HOLD")));
    EXPECT_FALSE(ack.is_ok);
    EXPECT_TRUE(ack.message.contains(QStringLiteral("尚無訂閱者")));
}

TEST(CommandModuleInitTest, RequiresLink)
{
    CommandModule command(nullptr);
    EXPECT_FALSE(command.init());
    EXPECT_FALSE(command.health_check());
}

TEST_F(CommandModuleTest, MissionPlanPublishedWithSeparateAck)
{
    WaypointMissionData mission;
    mission.mission_id = 9;
    mission.waypoints.resize(2);
    QSignalSpy command_acked(&command_, &CommandModule::command_acked);
    QSignalSpy upload_acked(&command_, &CommandModule::mission_upload_acked);
    command_.publish_mission_plan(QStringLiteral("uav_2_agent"), mission);

    ASSERT_EQ(link_.published_missions.size(), 1U);
    EXPECT_EQ(link_.published_missions[0].agent_ns, QStringLiteral("uav_2_agent"));
    EXPECT_EQ(link_.published_missions[0].mission.mission_id, 9U);
    EXPECT_EQ(link_.published_missions[0].mission.waypoints.size(), 2U);
    // 上傳不是命令：只發出上傳回報，不經 command_acked（UI 的命令提示）
    EXPECT_EQ(command_acked.size(), 0);
    ASSERT_EQ(upload_acked.size(), 1);
    const auto ack = upload_acked.at(0).at(0).value<CommandAck>();
    EXPECT_TRUE(ack.is_ok);
    EXPECT_EQ(ack.command, MISSION_UPLOAD_ACK_NAME);
}

TEST_F(CommandModuleTest, MissionPlanRejectedForAllAndOnLinkFailure)
{
    QSignalSpy upload_acked(&command_, &CommandModule::mission_upload_acked);
    command_.publish_mission_plan(ALL_AGENTS_TARGET, WaypointMissionData{});
    EXPECT_TRUE(link_.published_missions.empty());
    ASSERT_EQ(upload_acked.size(), 1);
    EXPECT_FALSE(upload_acked.at(0).at(0).value<CommandAck>().is_ok);

    // 舊版 Agent 沒有訂閱 mission/plan：必須回報失敗
    link_.is_mission_channel_ready = false;
    command_.publish_mission_plan(QStringLiteral("uav_1_agent"), WaypointMissionData{});
    EXPECT_TRUE(link_.published_missions.empty());
    ASSERT_EQ(upload_acked.size(), 2);
    const auto ack = upload_acked.at(1).at(0).value<CommandAck>();
    EXPECT_FALSE(ack.is_ok);
    EXPECT_TRUE(ack.message.contains(QStringLiteral("航點任務")));
}
