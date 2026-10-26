/**
 * @file test_app_support.cpp
 * @brief AppSettings、顯示格式、唯讀除錯工具與日誌檔
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "app/app_settings.hpp"
#include "app/diagnostics_controller.hpp"
#include "app/log_file.hpp"
#include "app/ui_format.hpp"

#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace gcs;

namespace
{
constexpr double EPSILON = 1e-6;
/// 經緯度比較容許誤差（度），約 0.1 毫米
constexpr double COORDINATE_EPSILON = 1e-9;
/// 每度緯度對應的距離（公尺，球體近似），只用於驗證位移不變
constexpr double METERS_PER_DEGREE = 111319.49;
constexpr double DEGREES_TO_RADIANS = 3.14159265358979323846 / 180.0;

/// 相對參考點的北／東位移（公尺）
struct NorthEast
{
    double north = 0.0;
    double east = 0.0;
};

/**
 * @brief 計算位置相對參考點的北／東位移
 * @param latitude 位置緯度
 * @param longitude 位置經度
 * @param reference_latitude 參考點緯度
 * @param reference_longitude 參考點經度
 * @return 位移（公尺）
 */
NorthEast offset_from(double latitude, double longitude, double reference_latitude, double reference_longitude)
{
    return {(latitude - reference_latitude) * METERS_PER_DEGREE,
            (longitude - reference_longitude) * METERS_PER_DEGREE * std::cos(reference_latitude * DEGREES_TO_RADIANS)};
}
}  // namespace

TEST(AppSettingsTest, DefaultsAndRangeValidation)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    AppSettings settings(dir.filePath(QStringLiteral("a.ini")));
    EXPECT_TRUE(settings.auto_follow());
    EXPECT_NEAR(settings.takeoff_height(), 3.0, EPSILON);

    settings.set_takeoff_height(500.0);
    EXPECT_NEAR(settings.takeoff_height(), 3.0, EPSILON);
    settings.set_takeoff_height(std::numeric_limits<double>::quiet_NaN());
    EXPECT_NEAR(settings.takeoff_height(), 3.0, EPSILON);
    settings.set_takeoff_height(8.5);
    EXPECT_NEAR(settings.takeoff_height(), 8.5, EPSILON);

    settings.set_auto_follow(false);
    settings.resetUiSettings();
    EXPECT_TRUE(settings.auto_follow());
    // 恢復預設值不影響飛行參數記憶
    EXPECT_NEAR(settings.takeoff_height(), 8.5, EPSILON);
}

TEST(AppSettingsTest, VShapeFormationSpacingKeepsAgentMinimumDistance)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    AppSettings settings(dir.filePath(QStringLiteral("v.ini")));
    EXPECT_NEAR(settings.form_v_shape_spacing(), 5.0, EPSILON);
    EXPECT_NEAR(settings.form_v_shape_height(), 20.0, EPSILON);

    // 低於 Agent 預設最小中心距離 2 m 的間距不寫入
    settings.set_form_v_shape_spacing(1.5);
    EXPECT_NEAR(settings.form_v_shape_spacing(), 5.0, EPSILON);
    settings.set_form_v_shape_spacing(2.0);
    EXPECT_NEAR(settings.form_v_shape_spacing(), 2.0, EPSILON);
    settings.set_form_v_shape_height(std::numeric_limits<double>::infinity());
    EXPECT_NEAR(settings.form_v_shape_height(), 20.0, EPSILON);
    settings.set_form_v_shape_height(12.0);
    EXPECT_NEAR(settings.form_v_shape_height(), 12.0, EPSILON);
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

TEST(AppSettingsTest, DisplayOriginValidationAndPersistence)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("d.ini"));
    const QString display_text = QStringLiteral("25.0339640, 121.5644680");
    {
        AppSettings settings(path);
        EXPECT_FALSE(settings.is_display_origin_enabled());
        EXPECT_TRUE(settings.source_origin_text().isEmpty());
        EXPECT_FALSE(settings.display_origin().is_enabled);

        // 啟用時兩組都必填；格式錯誤、多一個值或緯經度顛倒（緯度超出 ±85）一律拒絕且不保存
        EXPECT_FALSE(settings.saveDisplayOrigin(true, QString(), display_text).isEmpty());
        EXPECT_FALSE(settings.saveDisplayOrigin(true, QStringLiteral("abc"), display_text).isEmpty());
        EXPECT_FALSE(settings.saveDisplayOrigin(true, QStringLiteral("22.5, 113.9, 10"), display_text).isEmpty());
        EXPECT_FALSE(settings.saveDisplayOrigin(true, QStringLiteral("113.9364040, 22.5430960"), display_text).isEmpty());
        EXPECT_TRUE(settings.source_origin_text().isEmpty());
        EXPECT_FALSE(settings.is_display_origin_enabled());

        // 逗號或空白分隔皆可，前後空白忽略
        QSignalSpy spy(&settings, &AppSettings::displayOriginChanged);
        EXPECT_TRUE(settings
                        .saveDisplayOrigin(true, QStringLiteral(" 22.5430960 113.9364040 "),
                                           QStringLiteral("25.0339640,121.5644680"))
                        .isEmpty());
        EXPECT_EQ(spy.count(), 1);
        // 恢復預設值只重設介面設定，不清除顯示原點
        settings.resetUiSettings();
    }

    AppSettings reloaded(path);
    const ui::DisplayOrigin origin = reloaded.display_origin();
    EXPECT_TRUE(origin.is_enabled);
    EXPECT_NEAR(origin.source_latitude, 22.5430960, COORDINATE_EPSILON);
    EXPECT_NEAR(origin.source_longitude, 113.9364040, COORDINATE_EPSILON);
    EXPECT_NEAR(origin.display_latitude, 25.0339640, COORDINATE_EPSILON);
    EXPECT_NEAR(origin.display_longitude, 121.5644680, COORDINATE_EPSILON);
    EXPECT_EQ(reloaded.source_origin_text(), QStringLiteral("22.5430960, 113.9364040"));
    EXPECT_EQ(reloaded.display_origin_text(), display_text);

    // 停用時座標可留空並保留原值，下次啟用不必重填
    EXPECT_TRUE(reloaded.saveDisplayOrigin(false, QString(), QString()).isEmpty());
    EXPECT_FALSE(reloaded.is_display_origin_enabled());
    EXPECT_FALSE(reloaded.display_origin().is_enabled);
    EXPECT_EQ(reloaded.display_origin_text(), display_text);
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
    EXPECT_EQ(ui::format_signed_number(12.44, 1), QStringLiteral("+12.4"));
    EXPECT_EQ(ui::format_signed_number(-3.1, 1), QStringLiteral("−3.1"));
    EXPECT_EQ(ui::format_signed_number(-0.04, 1), QStringLiteral("+0.0"));
    EXPECT_EQ(ui::format_signed_number(std::numeric_limits<double>::infinity(), 1), QStringLiteral("—"));
    AgentStateData state;
    EXPECT_EQ(ui::px4_status_label(state), QStringLiteral("PX4 狀態未確認"));
    state.px4_nav_state = 14;
    EXPECT_EQ(ui::px4_mode_label(state), QStringLiteral("未確認"));
    state.is_px4_state_valid = true;
    EXPECT_EQ(ui::px4_mode_label(state), QStringLiteral("Offboard"));
    state.px4_nav_state = 4;
    EXPECT_EQ(ui::px4_mode_label(state), QStringLiteral("Hold"));
    state.px4_nav_state = 16;
    EXPECT_EQ(ui::px4_mode_label(state), QStringLiteral("未知 16"));
    state.px4_nav_state = 31;
    EXPECT_EQ(ui::px4_mode_label(state), QStringLiteral("未知 31"));
    EXPECT_EQ(ui::format_battery_percent(0.874F), QStringLiteral("87"));
    EXPECT_EQ(ui::format_battery_percent(-1.0F), QStringLiteral("—"));
    EXPECT_EQ(ui::format_battery_voltage(15.32F), QStringLiteral("15.3"));
    EXPECT_EQ(ui::format_battery_voltage(0.0F), QStringLiteral("—"));
    EXPECT_EQ(ui::format_battery_current(12.44F), QStringLiteral("12.4"));
    EXPECT_EQ(ui::format_battery_current(-1.0F), QStringLiteral("—"));
    EXPECT_EQ(ui::format_battery_current(0.0F), QStringLiteral("0.0"));
    EXPECT_FALSE(ui::has_valid_global_position({std::numeric_limits<double>::quiet_NaN(), 121.5, 0.0}));
    EXPECT_FALSE(ui::has_valid_global_position({95.0, 121.5, 0.0}));
    EXPECT_TRUE(ui::has_valid_global_position({25.0, 121.5, 0.0}));
}

TEST(UiFormatTest, DisplayPositionKeepsOffsetFromReference)
{
    ui::DisplayOrigin origin;
    origin.source_latitude = 22.5430960;
    origin.source_longitude = 113.9364040;
    origin.display_latitude = 25.0339640;
    origin.display_longitude = 121.5644680;
    // 參考點東北方約 11 公尺、10 公尺
    const std::array<double, 3> reported = {22.5431960, 113.9365040, 35.0};

    // 未啟用時原樣傳回
    const auto unchanged = ui::to_display_position(reported, origin);
    EXPECT_NEAR(unchanged[0], reported[0], COORDINATE_EPSILON);
    EXPECT_NEAR(unchanged[1], reported[1], COORDINATE_EPSILON);

    origin.is_enabled = true;
    const auto reference = ui::to_display_position({origin.source_latitude, origin.source_longitude, 0.0}, origin);
    EXPECT_NEAR(reference[0], origin.display_latitude, COORDINATE_EPSILON);
    EXPECT_NEAR(reference[1], origin.display_longitude, COORDINATE_EPSILON);

    // 相對參考點的北／東距離不變（兩地緯度不同，經度差會依緯度縮放），高度不變
    const auto shifted = ui::to_display_position(reported, origin);
    const NorthEast before = offset_from(reported[0], reported[1], origin.source_latitude, origin.source_longitude);
    const NorthEast after = offset_from(shifted[0], shifted[1], origin.display_latitude, origin.display_longitude);
    EXPECT_NEAR(after.north, before.north, EPSILON);
    EXPECT_NEAR(after.east, before.east, EPSILON);
    EXPECT_NEAR(shifted[2], 35.0, EPSILON);

    // 無效位置不換算，仍由 has_valid_global_position 擋下
    const auto invalid = ui::to_display_position({std::numeric_limits<double>::quiet_NaN(), 113.9, 0.0}, origin);
    EXPECT_FALSE(ui::has_valid_global_position(invalid));

    // 顯示位置跨越 180 度經線時折返到 -180～180
    origin.display_longitude = 179.9999990;
    const auto wrapped = ui::to_display_position(reported, origin);
    EXPECT_TRUE(ui::has_valid_global_position(wrapped));
    EXPECT_LT(wrapped[1], -179.9);
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

TEST(LogFileTest, WritesInfoAndAboveToDatedFile)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString log_dir = dir.filePath(QStringLiteral("logs"));
    ASSERT_TRUE(log_file::install(log_dir));

    QLoggingCategory category("gcs.test");
    qCDebug(category) << "debug-not-written";
    qCInfo(category).noquote() << QStringLiteral("ARM 已送出");
    qCWarning(category).noquote() << QStringLiteral("uav_1_agent 未解鎖");
    const QString path = log_file::current_path();
    log_file::uninstall();

    ASSERT_FALSE(path.isEmpty());
    EXPECT_TRUE(QFileInfo(path).fileName().startsWith(QStringLiteral("Taiphoon_QGCS_")));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());
    EXPECT_TRUE(content.contains(QStringLiteral("[INFO ] gcs.test: ARM 已送出")));
    EXPECT_TRUE(content.contains(QStringLiteral("[WARN ] gcs.test: uav_1_agent 未解鎖")));
    EXPECT_FALSE(content.contains(QStringLiteral("debug-not-written")));

    // 移除後不再寫入
    qCWarning(category) << "after-uninstall";
    file.close();
    ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    EXPECT_FALSE(QString::fromUtf8(file.readAll()).contains(QStringLiteral("after-uninstall")));
}
