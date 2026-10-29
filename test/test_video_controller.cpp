/**
 * @file test_video_controller.cpp
 * @brief 影像串流設定：機群網址範本、單機網址、驗證與保存
 * @author Taiphoon GCS Team
 * @date 2026-10-29
 */
#include "app/video_controller.hpp"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace gcs;

namespace
{

const QString AGENT_1 = QStringLiteral("uav_1_agent");
const QString AGENT_2 = QStringLiteral("uav_2_agent");
const QString AGENT_12 = QStringLiteral("uav_12_agent");
const QString TEMPLATE = QStringLiteral("rtsp://192.168.1.10:8554/uav_{N}");

}  // namespace

TEST(VideoControllerTest, TemplateExpandsForEveryDiscoveredAgent)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    VideoController video(true, dir.filePath(QStringLiteral("t.ini")));
    EXPECT_FALSE(video.is_configured());
    EXPECT_TRUE(video.urlFor(AGENT_1).isEmpty());

    QSignalSpy changed(&video, &VideoController::changed);
    ASSERT_TRUE(video.setUrlTemplate(TEMPLATE).isEmpty());
    EXPECT_EQ(changed.count(), 1);
    EXPECT_TRUE(video.is_configured());
    EXPECT_EQ(video.urlFor(AGENT_1), QStringLiteral("rtsp://192.168.1.10:8554/uav_1"));
    EXPECT_EQ(video.urlFor(AGENT_12), QStringLiteral("rtsp://192.168.1.10:8554/uav_12"));

    // 相同內容不重複通知，避免各格無故重新連線
    ASSERT_TRUE(video.setUrlTemplate(QStringLiteral("  ") + TEMPLATE).isEmpty());
    EXPECT_EQ(changed.count(), 1);

    ASSERT_TRUE(video.setUrlTemplate(QStringLiteral("rtsp://cam-{N}.local/{agent}")).isEmpty());
    EXPECT_EQ(video.urlFor(AGENT_2), QStringLiteral("rtsp://cam-2.local/uav_2_agent"));
    EXPECT_EQ(video.previewUrl(TEMPLATE, AGENT_2), QStringLiteral("rtsp://192.168.1.10:8554/uav_2"));

    // 全體目標與不合法的 namespace 沒有影像
    EXPECT_TRUE(video.urlFor(QStringLiteral("all")).isEmpty());
    EXPECT_TRUE(video.urlFor(QStringLiteral("uav_0_agent")).isEmpty());
    EXPECT_TRUE(video.urlFor(QStringLiteral("ground_station")).isEmpty());

    // 清空範本即不使用機群網址
    ASSERT_TRUE(video.setUrlTemplate(QString()).isEmpty());
    EXPECT_FALSE(video.is_configured());
    EXPECT_TRUE(video.urlFor(AGENT_1).isEmpty());
}

TEST(VideoControllerTest, AgentUrlWinsOverTemplateUntilCleared)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    VideoController video(true, dir.filePath(QStringLiteral("o.ini")));
    ASSERT_TRUE(video.setUrlTemplate(TEMPLATE).isEmpty());
    QSignalSpy changed(&video, &VideoController::changed);

    const QString camera = QStringLiteral("rtsp://admin:secret@10.0.0.2/stream?channel=1");
    ASSERT_TRUE(video.setAgentUrl(AGENT_2, QStringLiteral(" ") + camera).isEmpty());
    EXPECT_EQ(changed.count(), 1);
    EXPECT_TRUE(video.hasAgentUrl(AGENT_2));
    EXPECT_FALSE(video.hasAgentUrl(AGENT_1));
    EXPECT_EQ(video.urlFor(AGENT_2), camera);
    EXPECT_EQ(video.urlFor(AGENT_1), QStringLiteral("rtsp://192.168.1.10:8554/uav_1"));

    // 未變更不通知
    ASSERT_TRUE(video.setAgentUrl(AGENT_2, camera).isEmpty());
    EXPECT_EQ(changed.count(), 1);

    // 修改機群網址不影響單機網址
    ASSERT_TRUE(video.setUrlTemplate(QStringLiteral("rtsp://192.168.1.20:8554/uav_{N}")).isEmpty());
    EXPECT_EQ(video.urlFor(AGENT_2), camera);
    EXPECT_EQ(video.urlFor(AGENT_1), QStringLiteral("rtsp://192.168.1.20:8554/uav_1"));

    // 清空後改用機群網址
    ASSERT_TRUE(video.setAgentUrl(AGENT_2, QString()).isEmpty());
    EXPECT_FALSE(video.hasAgentUrl(AGENT_2));
    EXPECT_EQ(video.urlFor(AGENT_2), QStringLiteral("rtsp://192.168.1.20:8554/uav_2"));

    // 填入與機群網址相同的內容也不另存單機網址，日後修改範本時會一起更新
    ASSERT_TRUE(video.setAgentUrl(AGENT_1, QStringLiteral("rtsp://192.168.1.20:8554/uav_1")).isEmpty());
    EXPECT_FALSE(video.hasAgentUrl(AGENT_1));

    // 沒有機群網址時，單機網址仍可單獨使用
    ASSERT_TRUE(video.setUrlTemplate(QString()).isEmpty());
    ASSERT_TRUE(video.setAgentUrl(AGENT_12, QStringLiteral("udp://@:5612")).isEmpty());
    EXPECT_TRUE(video.is_configured());
    EXPECT_TRUE(video.urlFor(AGENT_1).isEmpty());
    EXPECT_EQ(video.urlFor(AGENT_12), QStringLiteral("udp://@:5612"));
}

TEST(VideoControllerTest, RejectsAmbiguousOrInvalidSettingsWithoutChangingThem)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    VideoController video(true, dir.filePath(QStringLiteral("r.ini")));
    ASSERT_TRUE(video.setUrlTemplate(TEMPLATE).isEmpty());
    ASSERT_TRUE(video.setAgentUrl(AGENT_2, QStringLiteral("rtsp://10.0.0.2/a")).isEmpty());
    QSignalSpy changed(&video, &VideoController::changed);

    // 沒有編號的範本會讓每台顯示同一路影像
    EXPECT_FALSE(video.setUrlTemplate(QStringLiteral("rtsp://192.168.1.10:8554/live")).isEmpty());
    EXPECT_FALSE(video.setUrlTemplate(QStringLiteral("file:///C:/video/uav_{N}.mp4")).isEmpty());
    EXPECT_FALSE(video.setUrlTemplate(QStringLiteral("rtsp:///uav_{N}")).isEmpty());

    EXPECT_FALSE(video.setAgentUrl(AGENT_2, QStringLiteral("ftp://10.0.0.2/a")).isEmpty());
    EXPECT_FALSE(video.setAgentUrl(AGENT_2, QStringLiteral("rtsp://")).isEmpty());
    EXPECT_FALSE(video.setAgentUrl(QStringLiteral("all"), QStringLiteral("rtsp://10.0.0.9/a")).isEmpty());
    EXPECT_FALSE(video.setAgentUrl(QStringLiteral("uav_0_agent"), QStringLiteral("rtsp://10.0.0.9/a")).isEmpty());

    EXPECT_EQ(changed.count(), 0);
    EXPECT_EQ(video.url_template(), TEMPLATE);
    EXPECT_EQ(video.urlFor(AGENT_2), QStringLiteral("rtsp://10.0.0.2/a"));
}

TEST(VideoControllerTest, SettingsPersistAndPasswordsAreRedactedForDisplay)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("p.ini"));
    {
        VideoController video(true, path);
        ASSERT_TRUE(video.setUrlTemplate(TEMPLATE).isEmpty());
        ASSERT_TRUE(video.setAgentUrl(QStringLiteral("uav_3_agent"), QStringLiteral("srt://10.0.0.3:9000")).isEmpty());
        ASSERT_TRUE(video.setAgentUrl(AGENT_2, QStringLiteral("rtsp://10.0.0.2/s?a=1")).isEmpty());
        video.set_panel_open(true);
    }
    {
        // 保存的單機網址中有不合法的項目時只略過該項
        QSettings settings(path, QSettings::IniFormat);
        QStringList entries = settings.value(QStringLiteral("site/video/overrides")).toStringList();
        entries.append(QStringLiteral("uav_4_agent=ftp://10.0.0.4/a"));
        entries.append(QStringLiteral("broken"));
        settings.setValue(QStringLiteral("site/video/overrides"), entries);
    }
    VideoController video(false, path);
    EXPECT_FALSE(video.is_available());
    EXPECT_EQ(video.url_template(), TEMPLATE);
    EXPECT_EQ(video.urlFor(QStringLiteral("uav_3_agent")), QStringLiteral("srt://10.0.0.3:9000"));
    // 只以第一個「=」分隔，查詢字串保留
    EXPECT_EQ(video.urlFor(AGENT_2), QStringLiteral("rtsp://10.0.0.2/s?a=1"));
    EXPECT_FALSE(video.hasAgentUrl(QStringLiteral("uav_4_agent")));
    EXPECT_EQ(video.urlFor(QStringLiteral("uav_4_agent")), QStringLiteral("rtsp://192.168.1.10:8554/uav_4"));
    EXPECT_TRUE(video.is_panel_open());

    EXPECT_EQ(VideoController::redacted(QStringLiteral("rtsp://admin:secret@10.0.0.2/s")),
              QStringLiteral("rtsp://10.0.0.2/s"));
    EXPECT_EQ(VideoController::agent_number(AGENT_12).value_or(0), 12);
    EXPECT_FALSE(VideoController::agent_number(QStringLiteral("all")).has_value());
}
