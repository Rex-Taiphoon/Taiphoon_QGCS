/**
 * @file video_player.hpp
 * @brief 單台無人機的影像串流播放：一路連線解碼，畫面同時送到多個 VideoOutput
 * @author Taiphoon GCS Team
 * @date 2026-10-29
 *
 * 縮圖與獨立視窗共用同一個播放器，避免同一台無人機開兩條連線（機上串流伺服器與無線鏈路頻寬都有限）。
 * 播放使用 Qt Multimedia 的 FFmpeg 後端並設為低延遲串流；連線失敗、串流結束或超過一段時間沒有新畫面時，
 * 間隔一段時間自動重新連線。只在主執行緒使用。
 */
#pragma once

#include <QList>
#include <QMediaPlayer>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

namespace gcs
{

/**
 * @class VideoPlayer
 * @brief 可在 QML 建立的串流播放器（TaiphoonQgcs.Video 1.0）
 */
class VideoPlayer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE set_source NOTIFY sourceChanged)
    Q_PROPERTY(bool active READ is_active WRITE set_active NOTIFY activeChanged)
    Q_PROPERTY(bool isPlaying READ is_playing NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ status_text NOTIFY statusChanged)
    Q_PROPERTY(QString errorText READ error_text NOTIFY statusChanged)
    Q_PROPERTY(QString resolutionText READ resolution_text NOTIFY statusChanged)

public:
    /// 重新連線前的等待（毫秒）
    static constexpr int RETRY_INTERVAL_MS = 3000;
    /// 開始連線後多久沒有第一張畫面視為失敗（毫秒）
    static constexpr int FIRST_FRAME_TIMEOUT_MS = 10000;
    /// 播放中多久沒有新畫面視為中斷（毫秒）
    static constexpr int STALL_TIMEOUT_MS = 5000;
    /// FFmpeg 網路操作逾時（毫秒）
    static constexpr int NETWORK_TIMEOUT_MS = 5000;

    /**
     * @brief 建立播放器（尚未播放，需設定 source 並設為 active）
     * @param parent 父物件
     */
    explicit VideoPlayer(QObject* parent = nullptr);
    ~VideoPlayer() override;

    QString source() const { return source_; }

    /**
     * @brief 設定串流 URL；播放中會改連新的位置
     * @param source URL，空字串表示未設定
     */
    void set_source(const QString& source);
    bool is_active() const { return is_active_; }

    /**
     * @brief 是否需要播放；面板收合且沒有獨立視窗時停止，釋放連線
     * @param is_active 是否播放
     */
    void set_active(bool is_active);
    bool is_playing() const { return state_ == State::PLAYING; }
    QString status_text() const;
    QString error_text() const { return error_text_; }
    QString resolution_text() const;

    /**
     * @brief 加入畫面輸出（QML VideoOutput.videoSink）；有最新畫面時立即顯示
     * @param sink QVideoSink
     */
    Q_INVOKABLE void attachOutput(QObject* sink);

    /**
     * @brief 移除畫面輸出
     * @param sink QVideoSink
     */
    Q_INVOKABLE void detachOutput(QObject* sink);

signals:
    void sourceChanged();
    void activeChanged();
    void statusChanged();

private:
    /// 播放狀態
    enum class State
    {
        IDLE,        ///< 未設定來源或不需播放
        CONNECTING,  ///< 連線中，尚未收到畫面
        PLAYING,     ///< 持續收到畫面
        RETRYING,    ///< 中斷或失敗，等待重新連線
    };

    /** @brief 依來源與 active 開始或停止 */
    void apply();

    /** @brief 重新開啟串流 */
    void restart();

    /** @brief 停止播放並清除畫面 */
    void stop();

    /**
     * @brief 中斷後排定重新連線
     * @param reason 原因（顯示於提示）
     */
    void schedule_retry(const QString& reason);

    /**
     * @brief 收到解碼後的畫面：轉送到各輸出
     * @param frame 畫面
     */
    void on_frame(const QVideoFrame& frame);

    /**
     * @brief 媒體狀態改變
     * @param status 狀態
     */
    void on_media_status(QMediaPlayer::MediaStatus status);

    /**
     * @brief 播放錯誤
     * @param error 錯誤種類
     * @param message 錯誤訊息
     */
    void on_error(QMediaPlayer::Error error, const QString& message);

    /**
     * @brief 更新狀態並通知畫面
     * @param state 狀態
     */
    void set_state(State state);

    /**
     * @brief 送出畫面到所有輸出
     * @param frame 畫面；無效畫面會清除輸出
     */
    void publish(const QVideoFrame& frame);

    QString source_;
    bool is_active_ = false;
    State state_ = State::IDLE;
    QString error_text_;
    QSize frame_size_;
    QVideoFrame last_frame_;
    /// 播放器的畫面先送到這裡，再轉送到各個 VideoOutput；宣告在播放器之前，確保比播放器晚解構
    QVideoSink sink_;
    QMediaPlayer player_;
    QList<QPointer<QVideoSink>> outputs_;
    QTimer retry_timer_;
    QTimer watchdog_timer_;
};

}  // namespace gcs
