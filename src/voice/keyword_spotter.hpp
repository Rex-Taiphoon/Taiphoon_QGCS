/**
 * @file keyword_spotter.hpp
 * @brief 語音口令辨識：以 sherpa-onnx 關鍵詞偵測辨識 voice_keywords.hpp 定義的口令
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 *
 * sherpa-onnx 以 QLibrary 在執行期載入（語音資料夾內的 C API 與 onnxruntime 動態函式庫），
 * 程式不在連結期依賴它：缺少檔案時只會停用語音功能，不影響主程式啟動。
 * 只依賴 QtCore，可在無介面工具與單元測試中使用；不是執行緒安全的，同一物件只能在單一執行緒使用。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace gcs::voice
{

/**
 * @class KeywordSpotter
 * @brief 一次按住說話對應一段辨識：begin → accept（多次）→ finish
 */
class KeywordSpotter
{
public:
    /// 按住說話放開後補上的靜音（秒），讓最後一個字解碼完成
    static constexpr double TAIL_SILENCE_SECONDS = 0.6;

    KeywordSpotter();
    ~KeywordSpotter();

    KeywordSpotter(const KeywordSpotter&) = delete;
    KeywordSpotter& operator=(const KeywordSpotter&) = delete;

    /**
     * @brief 從語音資料夾載入函式庫與模型
     * @param voice_dir 語音資料夾（含 sherpa-onnx 動態函式庫與 model/）
     * @param error 失敗原因（可為 nullptr）
     * @return 是否成功
     */
    bool load(const QString& voice_dir, QString* error);

    /** @brief 是否已載入 @return 是否可辨識 */
    bool is_loaded() const;

    /** @brief 開始一段新的辨識（捨棄前一段未結束的音訊） */
    void begin();

    /**
     * @brief 加入音訊並解碼
     * @param samples 單聲道浮點 PCM（-1～1）
     * @param count 樣本數
     * @param sample_rate 取樣率；與模型不同時由 sherpa-onnx 內部重新取樣
     * @return 這段音訊中辨識到的口令（依出現順序）
     */
    QStringList accept(const float* samples, int count, int sample_rate);

    /**
     * @brief 結束這段辨識：補靜音後解碼剩餘音訊
     * @param sample_rate 取樣率
     * @return 結尾處辨識到的口令
     */
    QStringList finish(int sample_rate);

    /**
     * @brief 便利函式：對一整段音訊執行 begin、accept、finish
     * @param samples 單聲道浮點 PCM
     * @param sample_rate 取樣率
     * @return 辨識到的口令
     */
    QStringList spot(const QVector<float>& samples, int sample_rate);

    /**
     * @brief 預設語音資料夾：Windows／Linux 為執行檔旁的 voice/，macOS 另找 bundle 的 Resources/voice
     * @return 第一個存在的候選資料夾；都不存在時回傳第一個候選
     */
    static QString default_voice_dir();

private:
    /** @brief 解碼目前可處理的音訊 @return 辨識到的口令 */
    QStringList decode_ready();

    /** @brief 釋放目前的辨識串流 */
    void release_stream();

    struct Api;
    std::unique_ptr<Api> api_;
    const void* spotter_ = nullptr;
    const void* stream_ = nullptr;
};

}  // namespace gcs::voice
