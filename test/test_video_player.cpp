/**
 * @file test_video_player.cpp
 * @brief 影像串流播放器：狀態文字、輸出管理與連線失敗後重新連線
 * @author Taiphoon GCS Team
 * @date 2026-10-29
 *
 * 不需要實際串流：連到本機未開啟的埠，驗證失敗後進入重新連線而不是停在連線中。
 * 實際播放以 MediaMTX 測試串流驗證（ADR-0019）。
 */
#include "app/video_player.hpp"

#include <QSignalSpy>
#include <QTest>
#include <QVideoSink>

#include <gtest/gtest.h>

using namespace gcs;

namespace
{

/// 本機沒有服務的埠（discard），連線會被拒絕
const QString UNREACHABLE_SOURCE = QStringLiteral("rtsp://127.0.0.1:9/uav_1");
/// 等待連線失敗的上限（毫秒）：網路逾時加第一張畫面逾時再留餘裕
constexpr int FAILURE_WAIT_MS = VideoPlayer::FIRST_FRAME_TIMEOUT_MS + VideoPlayer::NETWORK_TIMEOUT_MS;

}  // namespace

TEST(VideoPlayerTest, IdleStatusDependsOnSourceAndActive)
{
    VideoPlayer player;
    EXPECT_FALSE(player.is_playing());
    EXPECT_EQ(player.status_text(), QStringLiteral("未設定影像來源"));

    // 有來源但不需播放（面板收合、沒有獨立視窗）時不連線
    player.set_source(UNREACHABLE_SOURCE);
    EXPECT_EQ(player.status_text(), QStringLiteral("已暫停"));

    // 需播放但沒有來源
    player.set_source(QString());
    player.set_active(true);
    EXPECT_EQ(player.status_text(), QStringLiteral("未設定影像來源"));
}

TEST(VideoPlayerTest, FailedConnectionRetriesAndStopsWhenInactive)
{
    VideoPlayer player;
    QVideoSink output;
    player.attachOutput(&output);

    player.set_source(UNREACHABLE_SOURCE);
    player.set_active(true);
    EXPECT_EQ(player.status_text(), QStringLiteral("連線中…"));

    QTRY_VERIFY_WITH_TIMEOUT(player.status_text().contains(QStringLiteral("重新連線")), FAILURE_WAIT_MS);
    EXPECT_FALSE(player.error_text().isEmpty());
    EXPECT_FALSE(player.is_playing());
    EXPECT_FALSE(output.videoFrame().isValid());

    // 間隔後自動重新連線
    QTRY_COMPARE_WITH_TIMEOUT(player.status_text(), QStringLiteral("連線中…"),
                              VideoPlayer::RETRY_INTERVAL_MS + 1000);

    player.set_active(false);
    EXPECT_EQ(player.status_text(), QStringLiteral("已暫停"));
    EXPECT_TRUE(player.error_text().isEmpty());
}

TEST(VideoPlayerTest, OutputsMustBeVideoSinks)
{
    VideoPlayer player;
    QObject not_a_sink;
    player.attachOutput(&not_a_sink);
    player.attachOutput(nullptr);
    player.detachOutput(nullptr);

    // 已刪除的輸出不會被使用
    auto* output = new QVideoSink;
    player.attachOutput(output);
    delete output;
    output = nullptr;
    player.set_source(UNREACHABLE_SOURCE);
    player.set_active(true);
    player.set_active(false);
    EXPECT_EQ(player.status_text(), QStringLiteral("已暫停"));
}
