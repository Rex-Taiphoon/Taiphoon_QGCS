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
