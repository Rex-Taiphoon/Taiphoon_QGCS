/**
 * @file test_core_runtime.cpp
 * @brief CoreRuntime：核心執行緒的啟動、網路重建與釋放
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 模組在核心執行緒建立計時器，釋放時必須先移回主執行緒；本測試防止跨執行緒釋放造成中止。
 */
#include "app/core_runtime.hpp"
#include "modules/fleet/fleet_module.hpp"
#include "modules/guard/guard_module.hpp"

#include <QSignalSpy>
#include <QThread>

#include <gtest/gtest.h>

using namespace gcs;

namespace
{

/// 測試專用 domain
constexpr uint32_t TEST_DOMAIN_ID = 45;
/// 等待第一次機隊掃描的上限（毫秒）；掃描週期為 3 秒
constexpr int SCAN_WAIT_MS = 5000;

link::DdsNetworkConfig test_config()
{
    link::DdsNetworkConfig config;
    config.domain_id = TEST_DOMAIN_ID;
    return config;
}

}  // namespace

TEST(CoreRuntimeTest, ModulesRunInCoreThreadAndStopCleanly)
{
    CoreRuntime runtime;
    QString error;
    ASSERT_TRUE(runtime.start(test_config(), &error)) << error.toStdString();
    ASSERT_TRUE(runtime.is_running());
    EXPECT_NE(runtime.guard()->thread(), QThread::currentThread());

    // 計時器在核心執行緒運作，主執行緒以佇列收到機隊快照
    QSignalSpy changed(runtime.fleet(), &FleetModule::fleet_changed);
    EXPECT_TRUE(changed.wait(SCAN_WAIT_MS));

    runtime.stop();
    EXPECT_FALSE(runtime.is_running());
    EXPECT_EQ(runtime.guard(), nullptr);
}

TEST(CoreRuntimeTest, RestartNetworkKeepsModules)
{
    CoreRuntime runtime;
    QString error;
    ASSERT_TRUE(runtime.start(test_config(), &error)) << error.toStdString();
    GuardModule* guard = runtime.guard();
    ASSERT_TRUE(runtime.restart_network(test_config(), &error)) << error.toStdString();
    EXPECT_EQ(runtime.guard(), guard);
}

TEST(CoreRuntimeTest, CanStartAgainAfterStop)
{
    CoreRuntime runtime;
    QString error;
    ASSERT_TRUE(runtime.start(test_config(), &error)) << error.toStdString();
    runtime.stop();
    ASSERT_TRUE(runtime.start(test_config(), &error)) << error.toStdString();
    EXPECT_TRUE(runtime.is_running());
}

TEST(CoreRuntimeTest, InvalidNetworkConfigFailsWithoutLeak)
{
    link::DdsNetworkConfig config = test_config();
    config.is_multicast_allowed = false;
    CoreRuntime runtime;
    QString error;
    EXPECT_FALSE(runtime.start(config, &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(runtime.is_running());
}
