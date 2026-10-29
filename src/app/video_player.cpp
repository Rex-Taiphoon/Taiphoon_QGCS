/**
 * @file video_player.cpp
 * @brief 影像串流播放實作
 * @author Taiphoon GCS Team
 * @date 2026-10-29
 */
#include "app/video_player.hpp"

#include <QLoggingCategory>
#include <QPlaybackOptions>
#include <QUrl>

#include <chrono>

namespace
{
Q_LOGGING_CATEGORY(lc_video_player, "gcs.ui.video")

constexpr int MS_PER_SECOND = 1000;

/**
 * @brief 移除 URL 中的帳號密碼，供日誌使用
 * @param source URL
 * @return 不含帳號密碼的 URL
 */
QString log_url(const QString& source)
{
    return QUrl(source).toString(QUrl::RemoveUserInfo);
}

}  // namespace

namespace gcs
{

VideoPlayer::VideoPlayer(QObject* parent) : QObject(parent)
{
    // 即時影像以低延遲為優先：縮小緩衝與探測量，畫面晚到就丟棄而不是累積延遲
    QPlaybackOptions options;
    options.setPlaybackIntent(QPlaybackOptions::PlaybackIntent::LowLatencyStreaming);
    options.setNetworkTimeout(std::chrono::milliseconds(NETWORK_TIMEOUT_MS));
    player_.setPlaybackOptions(options);
    // 不設定音訊輸出：機上影像的聲音（多為螺旋槳噪音）沒有用途
    player_.setVideoSink(&sink_);

    connect(&sink_, &QVideoSink::videoFrameChanged, this, &VideoPlayer::on_frame);
    connect(&player_, &QMediaPlayer::mediaStatusChanged, this, &VideoPlayer::on_media_status);
    connect(&player_, &QMediaPlayer::errorOccurred, this, &VideoPlayer::on_error);

    retry_timer_.setSingleShot(true);
    connect(&retry_timer_, &QTimer::timeout, this, &VideoPlayer::restart);
    watchdog_timer_.setSingleShot(true);
    connect(&watchdog_timer_, &QTimer::timeout, this,
            [this]()
            {
                schedule_retry(state_ == State::PLAYING ? QStringLiteral("超過 %1 秒沒有新畫面")
                                                              .arg(STALL_TIMEOUT_MS / MS_PER_SECOND)
                                                        : QStringLiteral("連線逾時"));
            });
}

VideoPlayer::~VideoPlayer()
{
    retry_timer_.stop();
    watchdog_timer_.stop();
    player_.stop();
    player_.setVideoOutput(nullptr);
}

void VideoPlayer::set_source(const QString& source)
{
    const QString trimmed = source.trimmed();
    if (trimmed == source_)
    {
        return;
    }
    source_ = trimmed;
    emit sourceChanged();
    apply();
}

void VideoPlayer::set_active(bool is_active)
{
    if (is_active == is_active_)
    {
        return;
    }
    is_active_ = is_active;
    emit activeChanged();
    apply();
}

QString VideoPlayer::status_text() const
{
    switch (state_)
    {
    case State::IDLE:
        if (source_.isEmpty())
        {
            return QStringLiteral("未設定影像來源");
        }
        return is_active_ ? QString() : QStringLiteral("已暫停");
    case State::CONNECTING:
        return QStringLiteral("連線中…");
    case State::PLAYING:
        return QString();
    case State::RETRYING:
        return QStringLiteral("影像中斷，重新連線中…");
    }
    return QString();
}

QString VideoPlayer::resolution_text() const
{
    if (state_ != State::PLAYING || !frame_size_.isValid())
    {
        return QString();
    }
    return QStringLiteral("%1×%2").arg(frame_size_.width()).arg(frame_size_.height());
}

void VideoPlayer::attachOutput(QObject* sink)
{
    auto* output = qobject_cast<QVideoSink*>(sink);
    if (output == nullptr)
    {
        qCWarning(lc_video_player) << "attachOutput 需要 QVideoSink";
        return;
    }
    outputs_.removeIf([](const QPointer<QVideoSink>& item) { return item.isNull(); });
    if (!outputs_.contains(output))
    {
        outputs_.append(output);
    }
    // 新開的獨立視窗立即顯示目前畫面，不必等下一張
    output->setVideoFrame(last_frame_);
}

void VideoPlayer::detachOutput(QObject* sink)
{
    auto* output = qobject_cast<QVideoSink*>(sink);
    if (output == nullptr)
    {
        return;
    }
    outputs_.removeAll(output);
}

void VideoPlayer::apply()
{
    if (!is_active_ || source_.isEmpty())
    {
        stop();
        return;
    }
    restart();
}

void VideoPlayer::restart()
{
    retry_timer_.stop();
    // 先切到連線中：開啟來源時若立即回報錯誤，才會正確排定重新連線
    set_state(State::CONNECTING);
    player_.stop();
    player_.setSource(QUrl());
    qCInfo(lc_video_player).noquote() << "連線影像串流：" << log_url(source_);
    player_.setSource(QUrl(source_));
    player_.play();
    if (state_ == State::CONNECTING)
    {
        watchdog_timer_.start(FIRST_FRAME_TIMEOUT_MS);
    }
}

void VideoPlayer::stop()
{
    retry_timer_.stop();
    watchdog_timer_.stop();
    player_.stop();
    player_.setSource(QUrl());
    error_text_.clear();
    frame_size_ = QSize();
    last_frame_ = QVideoFrame();
    publish(QVideoFrame());
    set_state(State::IDLE);
}

void VideoPlayer::schedule_retry(const QString& reason)
{
    if (!is_active_ || source_.isEmpty() || retry_timer_.isActive())
    {
        return;
    }
    watchdog_timer_.stop();
    player_.stop();
    error_text_ = reason;
    // 中斷時清除畫面：凍結的最後一張容易被誤認為即時影像
    frame_size_ = QSize();
    last_frame_ = QVideoFrame();
    publish(QVideoFrame());
    qCWarning(lc_video_player).noquote() << QStringLiteral("影像串流中斷：%1（%2），%3 秒後重新連線")
                                                .arg(log_url(source_), reason)
                                                .arg(RETRY_INTERVAL_MS / MS_PER_SECOND);
    retry_timer_.start(RETRY_INTERVAL_MS);
    set_state(State::RETRYING);
}

void VideoPlayer::on_frame(const QVideoFrame& frame)
{
    // 停止或等待重連時仍可能收到排隊中的畫面，不再顯示
    if (!frame.isValid() || (state_ != State::CONNECTING && state_ != State::PLAYING))
    {
        return;
    }
    watchdog_timer_.start(STALL_TIMEOUT_MS);
    last_frame_ = frame;
    publish(frame);
    if (state_ != State::PLAYING || frame.size() != frame_size_)
    {
        if (state_ != State::PLAYING)
        {
            qCInfo(lc_video_player).noquote() << "影像串流播放中：" << log_url(source_) << frame.size();
        }
        // 解析度改變時也要通知畫面，不能只靠 set_state
        state_ = State::PLAYING;
        frame_size_ = frame.size();
        error_text_.clear();
        emit statusChanged();
    }
}

void VideoPlayer::on_media_status(QMediaPlayer::MediaStatus status)
{
    if (state_ == State::IDLE)
    {
        return;
    }
    if (status == QMediaPlayer::EndOfMedia)
    {
        schedule_retry(QStringLiteral("串流結束"));
    }
    else if (status == QMediaPlayer::InvalidMedia)
    {
        schedule_retry(player_.errorString().isEmpty() ? QStringLiteral("無法開啟串流") : player_.errorString());
    }
}

void VideoPlayer::on_error(QMediaPlayer::Error error, const QString& message)
{
    if (error == QMediaPlayer::NoError || state_ == State::IDLE)
    {
        return;
    }
    schedule_retry(message.isEmpty() ? QStringLiteral("無法連線") : message);
}

void VideoPlayer::set_state(State state)
{
    if (state == state_)
    {
        return;
    }
    state_ = state;
    emit statusChanged();
}

void VideoPlayer::publish(const QVideoFrame& frame)
{
    for (const QPointer<QVideoSink>& output : std::as_const(outputs_))
    {
        if (!output.isNull())
        {
            output->setVideoFrame(frame);
        }
    }
}

}  // namespace gcs
