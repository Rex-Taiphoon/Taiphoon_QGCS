/**
 * @file keyword_spotter.hpp
 * @brief 語音口令辨識：以 sherpa-onnx 關鍵詞偵測辨識 voice_keywords.hpp 定義的口令
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 *
 * sherpa-onnx 以 QLibrary 在執行期載入（語音資料夾內的 C API 與 onnxruntime 動態函式庫），
 * 程式不在連結期依賴它：缺少檔案時只會停用語音功能，不影響主程式啟動。
 * 吵雜環境：音訊先經高通濾波去除螺旋槳與風切的低頻，再同時送入兩條辨識串流：原始音訊與 GTCRN 降噪後的音訊。
 * 降噪對安靜環境的口令可能略有失真，兩條串流任一辨識到即算數；降噪模型缺少時只用原始串流。
 * 只依賴 QtCore，可在無介面工具與單元測試中使用；不是執行緒安全的，同一物件只能在單一執行緒使用。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace gcs::voice
{

/// 吵雜環境的處理方式
enum class NoiseReduction
{
    /// 只辨識原始音訊（高通濾波後）
    OFF,
    /// 只辨識降噪後的音訊
    DENOISED_ONLY,
    /// 原始與降噪音訊同時辨識，任一辨識到即算數（預設）
    DUAL,
};

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

    /** @brief 是否已載入降噪模型 @return 是否可降噪 */
    bool is_denoiser_loaded() const;

    /**
     * @brief 設定吵雜環境的處理方式；下一次 begin 生效。未載入降噪模型時一律只辨識原始音訊
     * @param mode 處理方式
     */
    void set_noise_reduction(NoiseReduction mode) { noise_reduction_ = mode; }

    /** @brief 目前的處理方式 @return 處理方式 */
    NoiseReduction noise_reduction() const { return noise_reduction_; }

    /** @brief 開始一段新的辨識（捨棄前一段未結束的音訊） */
    void begin();

    /**
     * @brief 加入音訊並解碼
     * @param samples 單聲道浮點 PCM（-1～1）
     * @param count 樣本數
     * @param sample_rate 取樣率；與模型不同時由 sherpa-onnx 內部重新取樣
     * @return 這段音訊中新辨識到的口令（依出現順序；同一段辨識內重複的口令只回報一次）
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
    /**
     * @brief 解碼一條串流目前可處理的音訊
     * @param stream 辨識串流
     * @return 新辨識到的口令
     */
    QStringList decode_ready(const void* stream);

    /**
     * @brief 把音訊送入降噪串流，輸出送入降噪辨識串流
     * @param samples 單聲道浮點 PCM
     * @param count 樣本數
     * @param sample_rate 取樣率
     */
    void feed_denoiser(const float* samples, int count, int sample_rate);

    /**
     * @brief 載入可選的降噪模型；失敗只記錄日誌
     * @param path 模型路徑
     */
    void load_denoiser(const QString& path);

    /** @brief 釋放目前的辨識串流並重設降噪狀態 */
    void release_stream();

    /// 二階 Butterworth 高通濾波器（單段 biquad），狀態跨區塊保存
    struct HighPass
    {
        int sample_rate = 0;
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;
        double x1 = 0.0;
        double x2 = 0.0;
        double y1 = 0.0;
        double y2 = 0.0;
    };

    /**
     * @brief 高通濾波；取樣率改變時重新計算係數並清除狀態
     * @param samples 輸入
     * @param count 樣本數
     * @param sample_rate 取樣率
     * @return 濾波後的音訊
     */
    QVector<float> high_pass(const float* samples, int count, int sample_rate);

    struct Api;
    std::unique_ptr<Api> api_;
    const void* spotter_ = nullptr;
    const void* denoiser_ = nullptr;
    /// 原始音訊的辨識串流；DENOISED_ONLY 時為 nullptr
    const void* stream_ = nullptr;
    /// 降噪音訊的辨識串流；OFF 或沒有降噪模型時為 nullptr
    const void* denoised_stream_ = nullptr;
    /// 本段辨識是否已開始（begin 後到 finish 前）
    bool is_active_ = false;
    NoiseReduction noise_reduction_ = NoiseReduction::DUAL;
    HighPass high_pass_;
    /// 本段辨識已回報的口令：兩條串流會辨識到同一句話
    QStringList reported_;
};

}  // namespace gcs::voice
