/**
 * @file test_fleet_module.cpp
 * @brief FleetModule：上線、離線、資料停滯與 shutdown 隔離
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "fakes/fake_agent_link.hpp"
#include "modules/fleet/fleet_module.hpp"

#include <QSignalSpy>

#include <gtest/gtest.h>

using namespace gcs;
using namespace std::chrono_literals;
using gcs::test::FakeAgentLink;

namespace
{

const QString AGENT_1 = QStringLiteral("uav_1_agent");
const QString AGENT_2 = QStringLiteral("uav_2_agent");

/**
 * @class FleetModuleTest
 * @brief 以注入時間驅動的 FleetModule
 */
class FleetModuleTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(fleet_.init());
    }

    /**
     * @brief 執行掃描並回傳機隊快照
     * @param now 掃描時間
     * @return 快照
     */
    FleetSnapshot scan(FleetModule::Clock::time_point now)
    {
        QSignalSpy changed(&fleet_, &FleetModule::fleet_changed);
        fleet_.scan_at(now);
        EXPECT_EQ(changed.size(), 1);
        return changed.isEmpty() ? FleetSnapshot{} : changed.takeFirst().at(0).value<FleetSnapshot>();
    }

    FakeAgentLink link_;
    FleetModule fleet_{&link_};
    const FleetModule::Clock::time_point start_ = FleetModule::Clock::now();
};

}  // namespace

TEST_F(FleetModuleTest, DiscoveredAgentGoesOnlineOnce)
{
    link_.discovered_agents = {AGENT_1};
    QSignalSpy online(&fleet_, &FleetModule::agent_online);

    const FleetSnapshot snapshot = scan(start_);
    ASSERT_EQ(online.size(), 1);
    EXPECT_EQ(online.takeFirst().at(0).toString(), AGENT_1);
    EXPECT_EQ(snapshot.online, QStringList{AGENT_1});

    scan(start_ + 1s);
    EXPECT_TRUE(online.isEmpty());
}

TEST_F(FleetModuleTest, StaleAgentHiddenButKeptSubscribed)
{
    link_.discovered_agents = {AGENT_1};
    QSignalSpy offline(&fleet_, &FleetModule::agent_offline);
    scan(start_);

    // 超過 5 秒沒有狀態資料：顯示為離線，但 topic 仍在，不發出 agent_offline
    const FleetSnapshot stale = scan(start_ + FleetModule::STALE_TIMEOUT + 1s);
    EXPECT_TRUE(stale.online.isEmpty());
    EXPECT_EQ(stale.offline, QStringList{AGENT_1});
    EXPECT_TRUE(offline.isEmpty());

    // 資料流恢復後重新顯示為線上
    fleet_.record_state_at(AGENT_1, start_ + 7s);
    const FleetSnapshot recovered = scan(start_ + 8s);
    EXPECT_EQ(recovered.online, QStringList{AGENT_1});
}

TEST_F(FleetModuleTest, VanishedTopicGoesOffline)
{
    link_.discovered_agents = {AGENT_1, AGENT_2};
    scan(start_);

    QSignalSpy offline(&fleet_, &FleetModule::agent_offline);
    link_.discovered_agents = {AGENT_2};
    const FleetSnapshot snapshot = scan(start_ + 1s);
    ASSERT_EQ(offline.size(), 1);
    EXPECT_EQ(offline.takeFirst().at(0).toString(), AGENT_1);
    EXPECT_EQ(snapshot.online, QStringList{AGENT_2});
    EXPECT_EQ(snapshot.offline, QStringList{AGENT_1});
}

TEST_F(FleetModuleTest, ShutdownAcceptedImmediatelyRemovesAndSuppressesAgent)
{
    link_.discovered_agents = {AGENT_1};
    scan(start_);
    ASSERT_TRUE(fleet_.online_agents().contains(AGENT_1));

    QSignalSpy offline(&fleet_, &FleetModule::agent_offline);
    fleet_.on_shutdown_accepted(AGENT_1);
    ASSERT_EQ(offline.size(), 1);
    EXPECT_TRUE(fleet_.online_agents().isEmpty());

    // topic 尚未從 DDS 探索消失時，下一次掃描也不可重新加入
    QSignalSpy online(&fleet_, &FleetModule::agent_online);
    scan(start_ + 3s);
    EXPECT_TRUE(online.isEmpty());

    // topic 消失後解除抑制，Agent 重新啟動即可再次上線
    link_.discovered_agents.clear();
    scan(start_ + 6s);
    link_.discovered_agents = {AGENT_1};
    scan(start_ + 9s);
    EXPECT_EQ(online.size(), 1);
}

TEST_F(FleetModuleTest, IgnoresNonInterfaceNamespaces)
{
    link_.discovered_agents = {QStringLiteral("fmu1_agent"), QStringLiteral("uav_0_agent"), QStringLiteral("all")};
    QSignalSpy online(&fleet_, &FleetModule::agent_online);
    const FleetSnapshot snapshot = scan(start_);
    EXPECT_TRUE(online.isEmpty());
    EXPECT_TRUE(snapshot.online.isEmpty());
}
