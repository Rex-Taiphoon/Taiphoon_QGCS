/**
 * @file test_app_support.cpp
 * @brief AppSettings、顯示格式與唯讀除錯工具
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "app/app_settings.hpp"
#include "app/diagnostics_controller.hpp"
#include "app/ui_format.hpp"

#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace gcs;

namespace
{
constexpr double EPSILON = 1e-6;
}  // namespace

TEST(AppSettingsTest, DefaultsAndRangeValidation)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    AppSettings settings(dir.filePath(QStringLiteral("a.ini")));
    EXPECT_TRUE(settings.auto_follow());
    EXPECT_EQ(settings.cmd_timeout_ms(), 3000);
    EXPECT_NEAR(settings.takeoff_height(), 3.0, EPSILON);

    settings.set_cmd_timeout_ms(500);
    EXPECT_EQ(settings.cmd_timeout_ms(), 3000);
    settings.set_cmd_timeout_ms(5000);
    EXPECT_EQ(settings.cmd_timeout_ms(), 5000);

    settings.set_takeoff_height(500.0);
    EXPECT_NEAR(settings.takeoff_height(), 3.0, EPSILON);
    settings.set_takeoff_height(std::numeric_limits<double>::quiet_NaN());
    EXPECT_NEAR(settings.takeoff_height(), 3.0, EPSILON);
    settings.set_takeoff_height(8.5);
    EXPECT_NEAR(settings.takeoff_height(), 8.5, EPSILON);

    settings.resetUiSettings();
    EXPECT_EQ(settings.cmd_timeout_ms(), 3000);
    // 恢復預設值不影響飛行參數記憶
    EXPECT_NEAR(settings.takeoff_height(), 8.5, EPSILON);
}

TEST(AppSettingsTest, PersistsAcrossInstances)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("b.ini"));
    {
        AppSettings settings(path);
        settings.set_yaw_degrees(-45.0);
        ASSERT_TRUE(settings.saveSite(3, QStringLiteral("192.168.0.104"), {QStringLiteral("192.168.0.101")}, false, 50)
                        .isEmpty());
    }
    AppSettings reloaded(path);
    EXPECT_NEAR(reloaded.yaw_degrees(), -45.0, EPSILON);
    EXPECT_TRUE(reloaded.is_site_configured());
    const link::DdsNetworkConfig config = reloaded.network_config();
    EXPECT_EQ(config.domain_id, 3U);
    EXPECT_EQ(config.network_interface, "192.168.0.104");
    EXPECT_FALSE(config.is_multicast_allowed);
    ASSERT_EQ(config.peers.size(), 1U);
    EXPECT_EQ(config.peers[0], "192.168.0.101");
    ASSERT_TRUE(config.participant_index.has_value());
    EXPECT_EQ(*config.participant_index, 50U);
}

TEST(AppSettingsTest, RejectsInvalidSite)
{
    QTemporaryDir dir;
    AppSettings settings(dir.filePath(QStringLiteral("c.ini")));
    EXPECT_FALSE(settings.saveSite(300, QString(), {}, true, -1).isEmpty());
    EXPECT_FALSE(settings.saveSite(0, QString(), {}, false, -1).isEmpty());
    EXPECT_FALSE(settings.saveSite(0, QString(), {QStringLiteral("192.168.0.999")}, true, -1).isEmpty());
    EXPECT_FALSE(settings.saveSite(0, QString(), {}, true, 120).isEmpty());
    EXPECT_FALSE(settings.is_site_configured());
    EXPECT_TRUE(settings.saveSite(0, QString(), {QStringLiteral("agent-host.local")}, true, -1).isEmpty());
}

TEST(UiFormatTest, HeadingFromQuaternion)
{
    // 繞 Z 軸 90 度：航向 90（東）
    const double half = std::sqrt(0.5);
    const auto east = ui::heading_degrees({static_cast<float>(half), 0.0F, 0.0F, static_cast<float>(half)});
    ASSERT_TRUE(east.has_value());
    EXPECT_NEAR(*east, 90.0, 1e-3);
    // 繞 Z 軸 -90 度：航向 270（西），不得為負值
    const auto west = ui::heading_degrees({static_cast<float>(half), 0.0F, 0.0F, static_cast<float>(-half)});
    ASSERT_TRUE(west.has_value());
    EXPECT_NEAR(*west, 270.0, 1e-3);
    EXPECT_FALSE(ui::heading_degrees({0.0F, 0.0F, 0.0F, 0.0F}).has_value());
    EXPECT_FALSE(ui::heading_degrees({std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 1.0F}).has_value());
}

TEST(UiFormatTest, LabelsAndNumbers)
{
    EXPECT_EQ(ui::flight_mode_label(5), QStringLiteral("懸停中"));
    EXPECT_EQ(ui::flight_mode_label(42), QStringLiteral("模式 42"));
    EXPECT_EQ(ui::format_number(std::numeric_limits<double>::quiet_NaN(), 1), QStringLiteral("—"));
    EXPECT_EQ(ui::format_number(16.24, 1), QStringLiteral("16.2"));
    AgentStateData state;
    EXPECT_EQ(ui::px4_status_label(state), QStringLiteral("PX4 狀態未確認"));
    EXPECT_FALSE(ui::has_valid_global_position({std::numeric_limits<double>::quiet_NaN(), 121.5, 0.0}));
    EXPECT_FALSE(ui::has_valid_global_position({95.0, 121.5, 0.0}));
    EXPECT_TRUE(ui::has_valid_global_position({25.0, 121.5, 0.0}));
}

TEST(DiagnosticsTest, ReadOnlyCommands)
{
    DiagnosticsController diagnostics;
    diagnostics.set_topic_source(
        []()
        {
            return std::vector<DiagnosticsController::TopicEntry>{
                {QStringLiteral("rt/uav_1_agent/state"), QStringLiteral("taiphoon_agent::msg::dds_::AgentState_")}};
        });
    diagnostics.execute(QStringLiteral("ros2 topic list"));
    EXPECT_TRUE(diagnostics.output().contains(QStringLiteral("rt/uav_1_agent/state")));

    AgentStateData state;
    state.flight_mode = 5;
    diagnostics.on_state_updated(QStringLiteral("uav_1_agent"), state);
    diagnostics.execute(QStringLiteral("topic echo /uav_1_agent/state"));
    EXPECT_TRUE(diagnostics.output().contains(QStringLiteral("flight_mode: 5 (懸停中)")));

    diagnostics.clear();
    diagnostics.execute(QStringLiteral("ros2 topic pub /uav_1_agent/command std_msgs/String \"data: ARM\""));
    EXPECT_TRUE(diagnostics.output().contains(QStringLiteral("唯讀")));

    diagnostics.on_agent_online(QStringLiteral("uav_2_agent"));
    diagnostics.execute(QStringLiteral("events"));
    EXPECT_TRUE(diagnostics.output().contains(QStringLiteral("Agent 上線 uav_2_agent")));
}
