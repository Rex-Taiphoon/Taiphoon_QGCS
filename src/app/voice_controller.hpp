/**
 * @file voice_controller.hpp
 * @brief 按住說話：按住 F2 或畫面按鈕時收音，放開後辨識語音口令
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 *
 * 只在按住期間開啟麥克風，平時不收音。辨識在主執行緒進行（每 0.1 秒音訊解碼數毫秒），
 * 結果在放開後才送出，口令的處理（確認、只限單機等規則）由 MissionController 負責。
 * 缺少麥克風、權限、Qt Multimedia 或 sherpa-onnx 時 isAvailable 為 false，不影響其他功能。
 */
#pragma once

#include "voice/keyword_spotter.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>

namespace gcs
{

/**
 * @class VoiceController
 * @brief 語音口令的收音與辨識
 */
class VoiceController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isAvailable READ is_available NOTIFY availabilityChanged)
    Q_PROPERTY(QString unavailableReason READ unavailable_reason NOTIFY availabilityChanged)
    Q_PROPERTY(bool isListening READ is_listening NOTIFY listeningChanged)
    Q_PROPERTY(double level READ level NOTIFY levelChanged)
    Q_PROPERTY(QString lastResult READ last_result NOTIFY resultChanged)

public:
    /// 按住說話的鍵盤按鍵；不用空白鍵，避免觸發目前焦點所在的按鈕
    static constexpr int PUSH_TO_TALK_KEY = Qt::Key_F2;
    /// 單次收音上限（毫秒）：按鍵卡住或放開事件遺失時自動結束
    static constexpr int MAX_LISTEN_MS = 10000;

    explicit VoiceController(QObject* parent = nullptr);
    ~VoiceController() override;

    /**
     * @brief 載入辨識器並確認麥克風權限
     * @param voice_dir 語音資料夾
     */
    void initialize(const QString& voice_dir);

    bool is_available() const { return is_available_; }
    QString unavailable_reason() const { return unavailable_reason_; }
    bool is_listening() const;
    double level() const { return level_; }
    QString last_result() const { return last_result_; }

    /** @brief 開始收音（按下） */
    Q_INVOKABLE void beginListening();

    /** @brief 結束收音並送出辨識結果（放開） */
    Q_INVOKABLE void endListening();

    /**
     * @brief 攔截全程式的 F2 按下與放開（按鍵自動重複不算）；程式失去焦點時結束收音
     * @param watched 事件對象
     * @param event 事件
     * @return 是否已處理
     */
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    /** @brief 放開後辨識到的口令（依說出順序） */
    void keyword_detected(const QString& keyword);

    void availabilityChanged();
    void listeningChanged();
    void levelChanged();
    void resultChanged();

private:
    /** @brief 讀取麥克風資料並送入辨識器 */
    void on_audio_ready();

    /**
     * @brief 設定可用狀態
     * @param is_available 是否可用
     * @param reason 不可用原因
     */
    void set_available(bool is_available, const QString& reason);

    /**
     * @brief 更新最近一次結果文字
     * @param text 文字
     */
    void set_result(const QString& text);

    /// 收音中的麥克風（Qt Multimedia 型別只出現在實作檔）
    struct Capture;
    std::unique_ptr<Capture> capture_;
    voice::KeywordSpotter spotter_;
    QStringList found_;
    QTimer listen_limit_;
    bool is_available_ = false;
    QString unavailable_reason_;
    double level_ = 0.0;
    QString last_result_;
};

}  // namespace gcs
