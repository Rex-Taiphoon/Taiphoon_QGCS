/**
 * @file test_network_controller.cpp
 * @brief NetworkController：啟動、套用站點設定重建 DDS、核心重建時通知重新組裝 UI
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "app/app_settings.hpp"
#include "app/core_runtime.hpp"
#include "app/network_controller.hpp"
#include "app/ui_bridge.hpp"

#include <QSignalSpy>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace gcs;

namespace
{

/// 測試專用 domain
constexpr int TEST_DOMAIN_ID = 46;

/**
 * @class NetworkControllerTest
 * @brief 以暫存設定檔建立的網路控制
 */
class NetworkControllerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(dir_.isValid());
        settings_ = std::make_unique<AppSettings>(dir_.filePath(QStringLiteral("n.ini")));
        ASSERT_TRUE(settings_->saveSite(TEST_DOMAIN_ID, QString(), {}, true, -1).isEmpty());
        bridge_ = std::make_unique<UiBridge>(settings_.get());
        network_ = std::make_unique<NetworkController>(settings_.get(), &runtime_, bridge_.get());
    }

    void TearDown() override
    {
        runtime_.stop();
    }

    QTemporaryDir dir_;
    std::unique_ptr<AppSettings> settings_;
    CoreRuntime runtime_;
    std::unique_ptr<UiBridge> bridge_;
    std::unique_ptr<NetworkController> network_;
};

}  // namespace

TEST_F(NetworkControllerTest, StartReportsStatusAndRuntimeStarted)
{
    QSignalSpy started(network_.get(), &NetworkController::runtimeStarted);
    ASSERT_TRUE(network_->start());
    EXPECT_EQ(started.size(), 1);
    EXPECT_TRUE(bridge_->is_link_connected());
    EXPECT_TRUE(bridge_->link_message().contains(QStringLiteral("domain %1").arg(TEST_DOMAIN_ID)));
}

TEST_F(NetworkControllerTest, InvalidSiteNotAppliedAndRuntimeKept)
{
    ASSERT_TRUE(network_->start());
    const QString error = network_->applySite(TEST_DOMAIN_ID, QString(), QString(), false, -1);
    EXPECT_FALSE(error.isEmpty());
    EXPECT_TRUE(runtime_.is_running());
    EXPECT_TRUE(settings_->is_multicast_allowed());
}

TEST_F(NetworkControllerTest, ApplySiteRestartsWithoutRecreatingModules)
{
    ASSERT_TRUE(network_->start());
    auto* guard = runtime_.guard();
    QSignalSpy started(network_.get(), &NetworkController::runtimeStarted);

    const QString error = network_->applySite(TEST_DOMAIN_ID, QString(), QStringLiteral("127.0.0.1, 127.0.0.2"), true, -1);
    EXPECT_TRUE(error.isEmpty()) << error.toStdString();
    EXPECT_EQ(runtime_.guard(), guard);
    // 只重建 DDS，不重建模組，因此不需要重新組裝 UI
    EXPECT_TRUE(started.isEmpty());
    EXPECT_EQ(settings_->peers().size(), 2);
    EXPECT_TRUE(bridge_->link_message().contains(QStringLiteral("127.0.0.2")));
}

TEST_F(NetworkControllerTest, FailedStartThenFixedSiteStartsRuntime)
{
    link::DdsNetworkConfig broken;
    broken.domain_id = TEST_DOMAIN_ID;
    broken.is_multicast_allowed = false;
    network_->set_session_override(broken);
    EXPECT_FALSE(network_->start());
    EXPECT_FALSE(bridge_->is_link_connected());

    QSignalSpy started(network_.get(), &NetworkController::runtimeStarted);
    EXPECT_TRUE(network_->applySite(TEST_DOMAIN_ID, QString(), QString(), true, -1).isEmpty());
    EXPECT_EQ(started.size(), 1);
    EXPECT_TRUE(bridge_->is_link_connected());
}
