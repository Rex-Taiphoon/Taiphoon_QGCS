/**
 * @file test_agent_lifecycle_module.cpp
 * @brief AgentLifecycleModule：單機限制、service 未就緒、回應比對與逾時
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "fakes/fake_agent_link.hpp"
#include "modules/lifecycle/agent_lifecycle_module.hpp"

#include <QSignalSpy>

#include <gtest/gtest.h>

using namespace gcs;
using namespace std::chrono_literals;
using gcs::test::FakeAgentLink;

namespace
{

const QString AGENT_3 = QStringLiteral("uav_3_agent");

/// 測試用的回應逾時（毫秒），避免每個案例等待 5 秒
constexpr auto TEST_TIMEOUT = 50ms;
/// QSignalSpy::wait 的上限（毫秒）
constexpr int WAIT_LIMIT_MS = 2000;

/**
 * @class AgentLifecycleModuleTest
 * @brief 以 FakeAgentLink 驅動的生命週期模組
 */
class AgentLifecycleModuleTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        lifecycle_.set_response_timeout(TEST_TIMEOUT);
        ASSERT_TRUE(lifecycle_.init());
    }

    FakeAgentLink link_;
    AgentLifecycleModule lifecycle_{&link_};
};

/**
 * @brief 取出最後一筆回報
 * @param spy command_acked 的 QSignalSpy
 * @return 回報
 */
CommandAck last_ack(QSignalSpy& spy)
{
    return spy.isEmpty() ? CommandAck{} : spy.takeLast().at(0).value<CommandAck>();
}

}  // namespace

TEST_F(AgentLifecycleModuleTest, RejectsAllAgentShutdownRequest)
{
    QSignalSpy acked(&lifecycle_, &AgentLifecycleModule::command_acked);
    lifecycle_.request_shutdown(ALL_AGENTS_TARGET);

    const CommandAck ack = last_ack(acked);
    EXPECT_FALSE(ack.is_ok);
    EXPECT_EQ(ack.command, AgentLifecycleModule::SHUTDOWN_COMMAND);
    EXPECT_TRUE(ack.message.contains(QStringLiteral("單一")));
    EXPECT_TRUE(link_.shutdown_requests.empty());
}

TEST_F(AgentLifecycleModuleTest, RejectsShutdownWhenServiceUnavailable)
{
    link_.is_shutdown_service_ready = false;
    QSignalSpy acked(&lifecycle_, &AgentLifecycleModule::command_acked);
    QSignalSpy accepted(&lifecycle_, &AgentLifecycleModule::shutdown_accepted);
    lifecycle_.request_shutdown(AGENT_3);

    const CommandAck ack = last_ack(acked);
    EXPECT_FALSE(ack.is_ok);
    EXPECT_TRUE(ack.message.contains(QStringLiteral("shutdown service")));
    EXPECT_TRUE(accepted.isEmpty());
}

TEST_F(AgentLifecycleModuleTest, SuccessfulReplyIsolatesAgentThenAcks)
{
    QSignalSpy acked(&lifecycle_, &AgentLifecycleModule::command_acked);
    QSignalSpy accepted(&lifecycle_, &AgentLifecycleModule::shutdown_accepted);
    lifecycle_.request_shutdown(AGENT_3);
    ASSERT_EQ(link_.shutdown_requests.size(), 1U);
    EXPECT_TRUE(acked.isEmpty());

    const quint64 request_id = link_.shutdown_requests[0].request_id;
    emit link_.shutdown_replied(AGENT_3, request_id, true, QStringLiteral("關閉請求已接受"));

    ASSERT_EQ(accepted.size(), 1);
    EXPECT_EQ(accepted.takeFirst().at(0).toString(), AGENT_3);
    const CommandAck ack = last_ack(acked);
    EXPECT_TRUE(ack.is_ok);
    EXPECT_EQ(ack.agent, AGENT_3);
}

TEST_F(AgentLifecycleModuleTest, FailedReplyForwardsAgentMessage)
{
    QSignalSpy acked(&lifecycle_, &AgentLifecycleModule::command_acked);
    QSignalSpy accepted(&lifecycle_, &AgentLifecycleModule::shutdown_accepted);
    lifecycle_.request_shutdown(AGENT_3);
    emit link_.shutdown_replied(AGENT_3, link_.shutdown_requests[0].request_id, false,
                                QStringLiteral("data 必須為 true"));

    const CommandAck ack = last_ack(acked);
    EXPECT_FALSE(ack.is_ok);
    EXPECT_EQ(ack.message, QStringLiteral("data 必須為 true"));
    EXPECT_TRUE(accepted.isEmpty());
}

TEST_F(AgentLifecycleModuleTest, DuplicateRequestRejectedWhilePending)
{
    QSignalSpy acked(&lifecycle_, &AgentLifecycleModule::command_acked);
    lifecycle_.request_shutdown(AGENT_3);
    lifecycle_.request_shutdown(AGENT_3);

    EXPECT_EQ(link_.shutdown_requests.size(), 1U);
    EXPECT_TRUE(last_ack(acked).message.contains(QStringLiteral("等待回應")));
}

TEST_F(AgentLifecycleModuleTest, TimeoutReportsErrorAndIgnoresLateReply)
{
    QSignalSpy acked(&lifecycle_, &AgentLifecycleModule::command_acked);
    QSignalSpy accepted(&lifecycle_, &AgentLifecycleModule::shutdown_accepted);
    lifecycle_.request_shutdown(AGENT_3);
    const quint64 request_id = link_.shutdown_requests[0].request_id;

    ASSERT_TRUE(acked.wait(WAIT_LIMIT_MS));
    const CommandAck ack = last_ack(acked);
    EXPECT_FALSE(ack.is_ok);
    EXPECT_TRUE(ack.message.contains(QStringLiteral("逾時")));

    // 逾時後才到的回應不得再隔離 Agent 或產生第二筆回報
    emit link_.shutdown_replied(AGENT_3, request_id, true, QStringLiteral("遲到的回應"));
    EXPECT_TRUE(accepted.isEmpty());
    EXPECT_TRUE(acked.isEmpty());
}

TEST_F(AgentLifecycleModuleTest, IgnoresReplyForOtherRequest)
{
    QSignalSpy acked(&lifecycle_, &AgentLifecycleModule::command_acked);
    lifecycle_.request_shutdown(AGENT_3);
    const quint64 request_id = link_.shutdown_requests[0].request_id;

    emit link_.shutdown_replied(AGENT_3, request_id + 1, true, QStringLiteral("其他請求"));
    EXPECT_TRUE(acked.isEmpty());

    emit link_.shutdown_replied(AGENT_3, request_id, true, QStringLiteral("本次請求"));
    EXPECT_TRUE(last_ack(acked).is_ok);
}
