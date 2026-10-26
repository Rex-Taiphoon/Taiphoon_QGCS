/**
 * @file keyword_spotter.cpp
 * @brief 語音口令辨識實作
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 */
#include "voice/keyword_spotter.hpp"

#include "voice/voice_keywords.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QLoggingCategory>

#include <cstring>

#if defined(GCS_HAS_SHERPA_ONNX)
#include "sherpa-onnx/c-api/c-api.h"
#endif

namespace
{
Q_LOGGING_CATEGORY(lc_voice, "gcs.voice")
}  // namespace

namespace gcs::voice
{

namespace
{

/// 模型的取樣率與特徵維度（sherpa-onnx 中文關鍵詞模型）
constexpr int MODEL_SAMPLE_RATE = 16000;
constexpr int MODEL_FEATURE_DIM = 80;
/// 搜尋路徑數與觸發條件；門檻 0.25 為官方範例值，試用時台灣口音與 TTS 皆可辨識且未誤觸
constexpr int MAX_ACTIVE_PATHS = 4;
constexpr int NUM_TRAILING_BLANKS = 1;
constexpr float KEYWORDS_SCORE = 1.0F;
constexpr float KEYWORDS_THRESHOLD = 0.25F;
/// 辨識只占用一個執行緒，避免與 UI、DDS 搶 CPU
constexpr int NUM_THREADS = 1;

const QString MODEL_DIR = QStringLiteral("model");
const QString ENCODER_FILE = QStringLiteral("encoder-epoch-12-avg-2-chunk-16-left-64.onnx");
const QString DECODER_FILE = QStringLiteral("decoder-epoch-12-avg-2-chunk-16-left-64.onnx");
const QString JOINER_FILE = QStringLiteral("joiner-epoch-12-avg-2-chunk-16-left-64.onnx");
const QString TOKENS_FILE = QStringLiteral("tokens.txt");

/**
 * @brief 在語音資料夾或 macOS bundle 的 Frameworks 找第一個符合名稱的檔案
 *
 * macOS 安裝版的語音資料夾為 Contents/Resources/voice，程式碼放在 Contents/Frameworks 才能一併簽章。
 * @param dir 語音資料夾
 * @param patterns 檔名樣式
 * @return 完整路徑；找不到時為空字串
 */
QString find_library_file(const QDir& dir, const QStringList& patterns)
{
    for (const QString& candidate : {dir.absolutePath(), dir.filePath(QStringLiteral("../../Frameworks"))})
    {
        const QFileInfoList files = QDir(candidate).entryInfoList(patterns, QDir::Files, QDir::Name);
        if (!files.isEmpty())
        {
            return files.first().absoluteFilePath();
        }
    }
    return QString();
}

}  // namespace

#if defined(GCS_HAS_SHERPA_ONNX)

/// 執行期載入的 sherpa-onnx C API
struct KeywordSpotter::Api
{
    using CreateSpotterFn = const SherpaOnnxKeywordSpotter* (*)(const SherpaOnnxKeywordSpotterConfig*);
    using DestroySpotterFn = void (*)(const SherpaOnnxKeywordSpotter*);
    using CreateStreamFn = const SherpaOnnxOnlineStream* (*)(const SherpaOnnxKeywordSpotter*);
    using DestroyStreamFn = void (*)(const SherpaOnnxOnlineStream*);
    using AcceptWaveformFn = void (*)(const SherpaOnnxOnlineStream*, int32_t, const float*, int32_t);
    using IsReadyFn = int32_t (*)(const SherpaOnnxKeywordSpotter*, const SherpaOnnxOnlineStream*);
    using StreamOpFn = void (*)(const SherpaOnnxKeywordSpotter*, const SherpaOnnxOnlineStream*);
    using GetResultFn = const SherpaOnnxKeywordResult* (*)(const SherpaOnnxKeywordSpotter*,
                                                            const SherpaOnnxOnlineStream*);
    using DestroyResultFn = void (*)(const SherpaOnnxKeywordResult*);

    /// onnxruntime 必須先載入：C API 依賴它，而 Windows 不會到 C API 所在資料夾尋找相依 DLL
    QLibrary runtime;
    QLibrary c_api;
    CreateSpotterFn create_spotter = nullptr;
    DestroySpotterFn destroy_spotter = nullptr;
    CreateStreamFn create_stream = nullptr;
    DestroyStreamFn destroy_stream = nullptr;
    AcceptWaveformFn accept_waveform = nullptr;
    IsReadyFn is_ready = nullptr;
    StreamOpFn decode = nullptr;
    StreamOpFn reset = nullptr;
    GetResultFn get_result = nullptr;
    DestroyResultFn destroy_result = nullptr;

    /**
     * @brief 取得函式位址
     * @param name 函式名稱
     * @param function 輸出
     * @return 是否找到
     */
    template <typename Function>
    bool resolve(const char* name, Function* function)
    {
        *function = reinterpret_cast<Function>(c_api.resolve(name));
        return *function != nullptr;
    }

    /**
     * @brief 載入動態函式庫並取得所有函式
     * @param dir 語音資料夾
     * @param error 失敗原因
     * @return 是否成功
     */
    bool load(const QDir& dir, QString* error)
    {
        const QString runtime_file = find_library_file(
            dir, {QStringLiteral("onnxruntime.dll"), QStringLiteral("libonnxruntime*.dylib"),
                  QStringLiteral("libonnxruntime.so*")});
        const QString c_api_file = find_library_file(
            dir, {QStringLiteral("sherpa-onnx-c-api.dll"), QStringLiteral("libsherpa-onnx-c-api.dylib"),
                  QStringLiteral("libsherpa-onnx-c-api.so")});
        if (runtime_file.isEmpty() || c_api_file.isEmpty())
        {
            *error = QStringLiteral("找不到 sherpa-onnx 動態函式庫：%1").arg(dir.absolutePath());
            return false;
        }
        runtime.setFileName(runtime_file);
        c_api.setFileName(c_api_file);
        if (!runtime.load() || !c_api.load())
        {
            *error = QStringLiteral("無法載入 sherpa-onnx：%1%2").arg(runtime.errorString(), c_api.errorString());
            return false;
        }
        const bool is_complete =
            resolve("SherpaOnnxCreateKeywordSpotter", &create_spotter)
            && resolve("SherpaOnnxDestroyKeywordSpotter", &destroy_spotter)
            && resolve("SherpaOnnxCreateKeywordStream", &create_stream)
            && resolve("SherpaOnnxDestroyOnlineStream", &destroy_stream)
            && resolve("SherpaOnnxOnlineStreamAcceptWaveform", &accept_waveform)
            && resolve("SherpaOnnxIsKeywordStreamReady", &is_ready)
            && resolve("SherpaOnnxDecodeKeywordStream", &decode) && resolve("SherpaOnnxResetKeywordStream", &reset)
            && resolve("SherpaOnnxGetKeywordResult", &get_result)
            && resolve("SherpaOnnxDestroyKeywordResult", &destroy_result);
        if (!is_complete)
        {
            *error = QStringLiteral("sherpa-onnx 函式庫版本不符，缺少關鍵詞偵測函式");
            return false;
        }
        return true;
    }
};

KeywordSpotter::KeywordSpotter() = default;

KeywordSpotter::~KeywordSpotter()
{
    release_stream();
    if (api_ != nullptr && spotter_ != nullptr)
    {
        api_->destroy_spotter(static_cast<const SherpaOnnxKeywordSpotter*>(spotter_));
        spotter_ = nullptr;
    }
}

bool KeywordSpotter::load(const QString& voice_dir, QString* error)
{
    QString local_error;
    QString* out_error = error != nullptr ? error : &local_error;
    if (is_loaded())
    {
        return true;
    }
    const QDir dir(voice_dir);
    const QDir model_dir(dir.filePath(MODEL_DIR));
    // sherpa-onnx 在模型檔缺少時可能直接結束程序，建立前先自行確認
    for (const QString& file : {ENCODER_FILE, DECODER_FILE, JOINER_FILE, TOKENS_FILE})
    {
        if (!QFileInfo::exists(model_dir.filePath(file)))
        {
            *out_error = QStringLiteral("找不到語音模型：%1").arg(model_dir.filePath(file));
            return false;
        }
    }
    auto api = std::make_unique<Api>();
    if (!api->load(dir, out_error))
    {
        return false;
    }

    const QByteArray encoder = QFile::encodeName(model_dir.filePath(ENCODER_FILE));
    const QByteArray decoder = QFile::encodeName(model_dir.filePath(DECODER_FILE));
    const QByteArray joiner = QFile::encodeName(model_dir.filePath(JOINER_FILE));
    const QByteArray tokens = QFile::encodeName(model_dir.filePath(TOKENS_FILE));
    SherpaOnnxKeywordSpotterConfig config;
    std::memset(&config, 0, sizeof(config));
    config.feat_config.sample_rate = MODEL_SAMPLE_RATE;
    config.feat_config.feature_dim = MODEL_FEATURE_DIM;
    config.model_config.transducer.encoder = encoder.constData();
    config.model_config.transducer.decoder = decoder.constData();
    config.model_config.transducer.joiner = joiner.constData();
    config.model_config.tokens = tokens.constData();
    config.model_config.num_threads = NUM_THREADS;
    config.model_config.provider = "cpu";
    config.max_active_paths = MAX_ACTIVE_PATHS;
    config.num_trailing_blanks = NUM_TRAILING_BLANKS;
    config.keywords_score = KEYWORDS_SCORE;
    config.keywords_threshold = KEYWORDS_THRESHOLD;
    config.keywords_buf = KEYWORD_DEFINITIONS;
    config.keywords_buf_size = static_cast<int32_t>(std::strlen(KEYWORD_DEFINITIONS));

    const SherpaOnnxKeywordSpotter* spotter = api->create_spotter(&config);
    if (spotter == nullptr)
    {
        *out_error = QStringLiteral("sherpa-onnx 無法建立關鍵詞偵測（模型或口令設定錯誤）");
        return false;
    }
    api_ = std::move(api);
    spotter_ = spotter;
    qCInfo(lc_voice) << "語音口令辨識已載入：" << voice_dir;
    return true;
}

bool KeywordSpotter::is_loaded() const
{
    return api_ != nullptr && spotter_ != nullptr;
}

void KeywordSpotter::begin()
{
    if (!is_loaded())
    {
        return;
    }
    release_stream();
    stream_ = api_->create_stream(static_cast<const SherpaOnnxKeywordSpotter*>(spotter_));
}

QStringList KeywordSpotter::accept(const float* samples, int count, int sample_rate)
{
    if (!is_loaded() || samples == nullptr || count <= 0 || sample_rate <= 0)
    {
        return {};
    }
    if (stream_ == nullptr)
    {
        begin();
    }
    api_->accept_waveform(static_cast<const SherpaOnnxOnlineStream*>(stream_), sample_rate, samples, count);
    return decode_ready();
}

QStringList KeywordSpotter::finish(int sample_rate)
{
    if (!is_loaded() || stream_ == nullptr)
    {
        return {};
    }
    const int rate = sample_rate > 0 ? sample_rate : MODEL_SAMPLE_RATE;
    const QVector<float> silence(static_cast<int>(TAIL_SILENCE_SECONDS * rate), 0.0F);
    api_->accept_waveform(static_cast<const SherpaOnnxOnlineStream*>(stream_), rate, silence.constData(),
                          static_cast<int32_t>(silence.size()));
    const QStringList found = decode_ready();
    release_stream();
    return found;
}

QStringList KeywordSpotter::decode_ready()
{
    QStringList found;
    const auto* spotter = static_cast<const SherpaOnnxKeywordSpotter*>(spotter_);
    const auto* stream = static_cast<const SherpaOnnxOnlineStream*>(stream_);
    while (api_->is_ready(spotter, stream) != 0)
    {
        api_->decode(spotter, stream);
        const SherpaOnnxKeywordResult* result = api_->get_result(spotter, stream);
        if (result == nullptr)
        {
            continue;
        }
        if (result->keyword != nullptr && result->keyword[0] != '\0')
        {
            found.append(QString::fromUtf8(result->keyword));
            // 辨識到口令後必須重設，否則同一段聲音會重複觸發
            api_->reset(spotter, stream);
        }
        api_->destroy_result(result);
    }
    return found;
}

void KeywordSpotter::release_stream()
{
    if (api_ != nullptr && stream_ != nullptr)
    {
        api_->destroy_stream(static_cast<const SherpaOnnxOnlineStream*>(stream_));
    }
    stream_ = nullptr;
}

#else  // GCS_HAS_SHERPA_ONNX

/// 未包含 sherpa-onnx 的版本
struct KeywordSpotter::Api
{
};

KeywordSpotter::KeywordSpotter() = default;
KeywordSpotter::~KeywordSpotter() = default;

bool KeywordSpotter::load(const QString& /*voice_dir*/, QString* error)
{
    if (error != nullptr)
    {
        *error = QStringLiteral("此版本未包含語音口令辨識");
    }
    return false;
}

bool KeywordSpotter::is_loaded() const
{
    return false;
}

void KeywordSpotter::begin()
{
}

QStringList KeywordSpotter::accept(const float* /*samples*/, int /*count*/, int /*sample_rate*/)
{
    return {};
}

QStringList KeywordSpotter::finish(int /*sample_rate*/)
{
    return {};
}

QStringList KeywordSpotter::decode_ready()
{
    return {};
}

void KeywordSpotter::release_stream()
{
}

#endif  // GCS_HAS_SHERPA_ONNX

QStringList KeywordSpotter::spot(const QVector<float>& samples, int sample_rate)
{
    begin();
    QStringList found = accept(samples.constData(), static_cast<int>(samples.size()), sample_rate);
    found += finish(sample_rate);
    return found;
}

QString KeywordSpotter::default_voice_dir()
{
    const QDir app_dir(QCoreApplication::applicationDirPath());
    // macOS：安裝版在 bundle 的 Resources，建置目錄的執行檔位於 bin/Taiphoon_QGCS.app/Contents/MacOS
    const QStringList candidates = {
        app_dir.filePath(QStringLiteral("voice")),
        app_dir.filePath(QStringLiteral("../Resources/voice")),
        app_dir.filePath(QStringLiteral("../../../voice")),
    };
    for (const QString& candidate : candidates)
    {
        if (QFileInfo(candidate).isDir())
        {
            return QDir::cleanPath(candidate);
        }
    }
    return QDir::cleanPath(candidates.first());
}

}  // namespace gcs::voice
