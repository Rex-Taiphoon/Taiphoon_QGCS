/**
 * @file test_keyword_spotter.cpp
 * @brief 語音口令辨識：以 Windows 中文語音合成（Microsoft Hanhan，zh-TW）錄製的口令驗證辨識、誤觸與吵雜環境，
 *        並串接 MissionController 驗證「巡邏 → 倒數 → 開始任務」的流程
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 *
 * test/data/voice/ 的音檔為 16 kHz、16 位元單聲道：arm／takeoff／patrol／land 為四個口令（*_slow 為慢速念法），
 * start／stop／confirm／cancel 為舊版口令「請執行任務」「停止任務」「確認」「取消」，與 negative*.wav、
 * confusable*.wav（含飛機、解決、起床、下降、巡查、落地等相近發音的語句）一起作為不應觸發的語句。
 * 吵雜環境以程式產生的螺旋槳噪音（諧波加寬頻噪音）與多人交談噪音依指定 SNR 混入。
 * 未建置 sherpa-onnx（GCS_ENABLE_VOICE=OFF 或不支援的平台）時略過。
 */
#include "app/mission_controller.hpp"
#include "fakes/fake_agent_link.hpp"
#include "voice/keyword_spotter.hpp"
#include "voice/voice_keywords.hpp"
#include "voice/wav_file.hpp"

#include <QDir>
#include <QSignalSpy>
#include <QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <random>

using namespace gcs;
using namespace gcs::voice;

namespace
{

/// 串流辨識每次送入的長度（秒），與桌面程式收音的區塊大小同級
constexpr double CHUNK_SECONDS = 0.1;
/// 口令前後補上的環境噪音長度（秒）：實際按住說話時，說話前後都會錄到環境聲音
constexpr double NOISE_PADDING_SECONDS = 0.5;
constexpr int SAMPLE_RATE = 16000;
constexpr double PI = 3.14159265358979323846;

/// 口令音檔與預期結果
const QList<QPair<QString, QString>> KEYWORD_CASES = {
    {QStringLiteral("arm.wav"), KEYWORD_ARM},
    {QStringLiteral("takeoff.wav"), KEYWORD_TAKEOFF},
    {QStringLiteral("patrol.wav"), KEYWORD_PATROL},
    {QStringLiteral("land.wav"), KEYWORD_LAND},
    {QStringLiteral("arm_slow.wav"), KEYWORD_ARM},
    {QStringLiteral("takeoff_slow.wav"), KEYWORD_TAKEOFF},
    {QStringLiteral("patrol_slow.wav"), KEYWORD_PATROL},
    {QStringLiteral("land_slow.wav"), KEYWORD_LAND},
};

/// 不應觸發任何口令的語句
const QStringList NEGATIVE_FILES = {
    QStringLiteral("negative.wav"),    QStringLiteral("negative2.wav"),   QStringLiteral("confusable1.wav"),
    QStringLiteral("confusable2.wav"), QStringLiteral("confusable3.wav"), QStringLiteral("start.wav"),
    QStringLiteral("stop.wav"),        QStringLiteral("confirm.wav"),     QStringLiteral("cancel.wav"),
};

/**
 * @brief 讀取測試音檔
 * @param name 檔名
 * @return 音訊
 */
MonoAudio load_audio(const QString& name)
{
    MonoAudio audio;
    QString error;
    const QString path = QDir(QStringLiteral(GCS_TEST_VOICE_DATA_DIR)).filePath(name);
    EXPECT_TRUE(read_wav_file(path, &audio, &error)) << error.toStdString();
    return audio;
}

/**
 * @brief 均方根
 * @param samples 音訊
 * @return RMS
 */
double rms(const QVector<float>& samples)
{
    double sum = 0.0;
    for (const float sample : samples)
    {
        sum += static_cast<double>(sample) * sample;
    }
    return samples.isEmpty() ? 0.0 : std::sqrt(sum / samples.size());
}

/**
 * @brief 多旋翼螺旋槳噪音：四顆馬達葉片通過頻率的諧波（轉速緩慢飄移）加上偏低頻的寬頻氣流噪音
 * @param count 樣本數
 * @param seed 亂數種子
 * @return 噪音
 */
QVector<float> make_propeller_noise(int count, unsigned seed)
{
    constexpr int MOTORS = 4;
    constexpr int HARMONICS = 12;
    const std::array<double, MOTORS> base_hz = {172.0, 181.0, 189.0, 196.0};
    std::mt19937 random(seed);
    std::normal_distribution<double> gaussian(0.0, 1.0);
    std::array<std::array<double, HARMONICS>, MOTORS> phases = {};
    QVector<float> noise(count);
    double low = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const double t = static_cast<double>(i) / SAMPLE_RATE;
        double value = 0.0;
        for (int motor = 0; motor < MOTORS; ++motor)
        {
            const double f0 = base_hz.at(motor) * (1.0 + 0.02 * std::sin(2.0 * PI * (0.3 + 0.1 * motor) * t));
            for (int harmonic = 1; harmonic <= HARMONICS; ++harmonic)
            {
                double& phase = phases.at(motor).at(harmonic - 1);
                phase += 2.0 * PI * f0 * harmonic / SAMPLE_RATE;
                value += std::sin(phase) / harmonic;
            }
        }
        low = 0.97 * low + 0.03 * gaussian(random);
        value += 6.0 * low + 0.3 * gaussian(random);
        noise[i] = static_cast<float>(value);
    }
    return noise;
}

/**
 * @brief 多人交談噪音：不相關語句錯開後疊加
 * @param count 樣本數
 * @return 噪音
 */
QVector<float> make_babble_noise(int count)
{
    const std::array<QVector<float>, 2> sources = {load_audio(QStringLiteral("negative.wav")).samples,
                                                   load_audio(QStringLiteral("negative2.wav")).samples};
    const std::array<int, 4> offsets = {0, 7919, 15013, 23003};
    QVector<float> noise(count, 0.0F);
    for (int voice = 0; voice < static_cast<int>(offsets.size()); ++voice)
    {
        const QVector<float>& source = sources.at(voice % sources.size());
        if (source.isEmpty())
        {
            continue;
        }
        for (int i = 0; i < count; ++i)
        {
            noise[i] += source.at((i + offsets.at(voice)) % source.size());
        }
    }
    return noise;
}

/// 噪音種類
enum class NoiseKind
{
    PROPELLER,
    BABBLE,
};

/**
 * @brief 產生噪音
 * @param kind 種類
 * @param count 樣本數
 * @param seed 亂數種子
 * @return 噪音
 */
QVector<float> make_noise(NoiseKind kind, int count, unsigned seed)
{
    return kind == NoiseKind::PROPELLER ? make_propeller_noise(count, seed) : make_babble_noise(count);
}

/**
 * @brief 在口令前後補上噪音並依 SNR 混入
 * @param speech 口令
 * @param kind 噪音種類
 * @param snr_db 語音與噪音的功率比（dB），以整段口令音檔的 RMS 計算
 * @param seed 亂數種子
 * @return 混合後的音訊
 */
MonoAudio mix_noise(const MonoAudio& speech, NoiseKind kind, double snr_db, unsigned seed)
{
    const int padding = static_cast<int>(NOISE_PADDING_SECONDS * speech.sample_rate);
    const int count = static_cast<int>(speech.samples.size()) + 2 * padding;
    const QVector<float> noise = make_noise(kind, count, seed);
    const double gain = rms(speech.samples) / (rms(noise) * std::pow(10.0, snr_db / 20.0));
    MonoAudio mixed;
    mixed.sample_rate = speech.sample_rate;
    mixed.samples.resize(count);
    for (int i = 0; i < count; ++i)
    {
        const int index = i - padding;
        const double voice = (index >= 0 && index < speech.samples.size()) ? speech.samples.at(index) : 0.0;
        mixed.samples[i] = static_cast<float>(std::clamp(voice + gain * noise.at(i), -1.0, 1.0));
    }
    return mixed;
}

/**
 * @brief 以桌面程式相同的區塊大小串流辨識
 * @param spotter 辨識器
 * @param audio 音訊
 * @return 辨識到的口令
 */
QStringList spot_streaming(KeywordSpotter* spotter, const MonoAudio& audio)
{
    const int chunk = static_cast<int>(CHUNK_SECONDS * audio.sample_rate);
    QStringList found;
    spotter->begin();
    for (int offset = 0; offset < audio.samples.size(); offset += chunk)
    {
        const int count = std::min(chunk, static_cast<int>(audio.samples.size()) - offset);
        found += spotter->accept(audio.samples.constData() + offset, count, audio.sample_rate);
    }
    found += spotter->finish(audio.sample_rate);
    return found;
}

/**
 * @brief 在指定噪音與 SNR 下，結果恰為預期口令的音檔數
 * @param spotter 辨識器
 * @param kind 噪音種類
 * @param snr_db SNR（dB）
 * @return 正確數
 */
int count_correct(KeywordSpotter* spotter, NoiseKind kind, double snr_db)
{
    int correct = 0;
    unsigned seed = 1;
    for (const auto& item : KEYWORD_CASES)
    {
        const MonoAudio mixed = mix_noise(load_audio(item.first), kind, snr_db, seed++);
        if (spot_streaming(spotter, mixed) == QStringList{item.second})
        {
            ++correct;
        }
    }
    return correct;
}

/**
 * @class KeywordSpotterTest
 * @brief 已載入建置目錄 bin/voice 的辨識器
 */
class KeywordSpotterTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
#if !defined(GCS_HAS_SHERPA_ONNX)
        GTEST_SKIP() << "未建置 sherpa-onnx";
#endif
        QString error;
        ASSERT_TRUE(spotter_.load(QStringLiteral(GCS_VOICE_RUNTIME_DIR), &error)) << error.toStdString();
    }

    KeywordSpotter spotter_;
};

}  // namespace

TEST(KeywordSpotterLoadTest, MissingFilesFailWithReason)
{
    KeywordSpotter spotter;
    QString error;
    EXPECT_FALSE(spotter.load(QStringLiteral("does-not-exist"), &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(spotter.is_loaded());
    EXPECT_FALSE(spotter.is_denoiser_loaded());
    // 未載入時一律沒有結果，不會當機
    const float silence[4] = {};
    EXPECT_TRUE(spotter.accept(silence, 4, SAMPLE_RATE).isEmpty());
    EXPECT_TRUE(spotter.finish(SAMPLE_RATE).isEmpty());
}

TEST(WavFileTest, RejectsNonWav)
{
    MonoAudio audio;
    QString error;
    EXPECT_FALSE(read_wav_file(QStringLiteral(GCS_TEST_VOICE_DATA_DIR "/missing.wav"), &audio, &error));
    EXPECT_FALSE(error.isEmpty());
}

TEST_F(KeywordSpotterTest, DenoiserModelIsBuilt)
{
    EXPECT_TRUE(spotter_.is_denoiser_loaded());
    EXPECT_EQ(spotter_.noise_reduction(), NoiseReduction::DUAL);
}

TEST_F(KeywordSpotterTest, RecognizesEachKeyword)
{
    for (const NoiseReduction mode : {NoiseReduction::OFF, NoiseReduction::DUAL})
    {
        spotter_.set_noise_reduction(mode);
        for (const auto& item : KEYWORD_CASES)
        {
            const MonoAudio audio = load_audio(item.first);
            EXPECT_EQ(spotter_.spot(audio.samples, audio.sample_rate), QStringList{item.second})
                << item.first.toStdString() << " mode " << static_cast<int>(mode);
        }
    }
}

TEST_F(KeywordSpotterTest, OrdinarySpeechTriggersNothing)
{
    for (const NoiseReduction mode : {NoiseReduction::OFF, NoiseReduction::DUAL})
    {
        spotter_.set_noise_reduction(mode);
        for (const QString& file : NEGATIVE_FILES)
        {
            const MonoAudio audio = load_audio(file);
            EXPECT_TRUE(spotter_.spot(audio.samples, audio.sample_rate).isEmpty())
                << file.toStdString() << " mode " << static_cast<int>(mode);
        }
    }
}

TEST_F(KeywordSpotterTest, NoisyOrdinarySpeechRarelyTriggers)
{
    // 吵雜環境中說其他話：降低門檻換取辨識率的代價。目前 54 段（每段 3～9 秒）只允許 1 次誤觸，
    // 實際按住說話只有 1～2 秒，且口令執行前有 5 秒倒數可取消
    constexpr int MAX_FALSE_TRIGGERS = 1;
    int false_triggers = 0;
    unsigned seed = 11;
    for (const NoiseKind kind : {NoiseKind::PROPELLER, NoiseKind::BABBLE})
    {
        for (const double snr : {10.0, 5.0, 0.0})
        {
            for (const QString& file : NEGATIVE_FILES)
            {
                const MonoAudio mixed = mix_noise(load_audio(file), kind, snr, seed++);
                const QStringList found = spot_streaming(&spotter_, mixed);
                if (!found.isEmpty())
                {
                    ++false_triggers;
                    std::printf("[false] %s kind %d SNR %+5.1f dB: %s\n", qPrintable(file), static_cast<int>(kind),
                                snr, qPrintable(found.join(QLatin1Char(','))));
                }
            }
        }
    }
    EXPECT_LE(false_triggers, MAX_FALSE_TRIGGERS);
}

TEST_F(KeywordSpotterTest, StreamingChunksGiveSameResult)
{
    const MonoAudio audio = load_audio(QStringLiteral("patrol.wav"));
    EXPECT_EQ(spot_streaming(&spotter_, audio), QStringList{KEYWORD_PATROL});
}

TEST_F(KeywordSpotterTest, AbandonedUtteranceDoesNotLeak)
{
    // 收音中途重新開始（例如格式改變）不得把前一段的降噪緩衝帶到下一段
    const MonoAudio audio = load_audio(QStringLiteral("land.wav"));
    spotter_.begin();
    const int half = static_cast<int>(audio.samples.size() / 2);
    spotter_.accept(audio.samples.constData(), half, audio.sample_rate);
    EXPECT_EQ(spot_streaming(&spotter_, load_audio(QStringLiteral("arm.wav"))), QStringList{KEYWORD_ARM});
}

TEST_F(KeywordSpotterTest, NoiseOnlyTriggersNothing)
{
    // 按住後沒說話，只錄到環境噪音
    constexpr double LEVEL = 0.1;
    constexpr int SECONDS = 3;
    for (const NoiseKind kind : {NoiseKind::PROPELLER, NoiseKind::BABBLE})
    {
        const QVector<float> raw = make_noise(kind, SAMPLE_RATE * SECONDS, 7);
        MonoAudio noise;
        noise.sample_rate = SAMPLE_RATE;
        noise.samples.resize(raw.size());
        const double gain = LEVEL / rms(raw);
        for (int i = 0; i < raw.size(); ++i)
        {
            noise.samples[i] = static_cast<float>(gain * raw.at(i));
        }
        EXPECT_TRUE(spot_streaming(&spotter_, noise).isEmpty()) << static_cast<int>(kind);
    }
}

TEST_F(KeywordSpotterTest, NoiseReductionImprovesNoisyRecognition)
{
    struct Score
    {
        int off = 0;
        int denoised = 0;
        int dual = 0;
    };
    Score total;
    for (const NoiseKind kind : {NoiseKind::PROPELLER, NoiseKind::BABBLE})
    {
        for (const double snr : {10.0, 5.0, 0.0, -5.0})
        {
            Score score;
            spotter_.set_noise_reduction(NoiseReduction::OFF);
            score.off = count_correct(&spotter_, kind, snr);
            spotter_.set_noise_reduction(NoiseReduction::DENOISED_ONLY);
            score.denoised = count_correct(&spotter_, kind, snr);
            spotter_.set_noise_reduction(NoiseReduction::DUAL);
            score.dual = count_correct(&spotter_, kind, snr);
            // 逐一列出各 SNR 的結果，調整門檻或模型時方便比較
            std::printf("[noise] %s SNR %+5.1f dB: off %d, denoised %d, dual %d / %d\n",
                        kind == NoiseKind::PROPELLER ? "propeller" : "babble", snr, score.off, score.denoised,
                        score.dual, static_cast<int>(KEYWORD_CASES.size()));
            total.off += score.off;
            total.denoised += score.denoised;
            total.dual += score.dual;
        }
    }
    EXPECT_GE(total.dual, total.off);
    EXPECT_GE(total.dual, total.denoised);
}

TEST_F(KeywordSpotterTest, SpokenPatrolCountsDownThenStartsMission)
{
    constexpr int AUTO_START_MS = 300;
    MissionController controller(MissionController::CONFIRM_TIMEOUT_MS, AUTO_START_MS);
    QSignalSpy started(&controller, &MissionController::mission_start_requested);
    controller.set_selected_agent(QStringLiteral("uav_1_agent"));
    AgentStateData state = gcs::test::make_state(FlightState::HOVER, true, false);
    state.position_global = {25.0, 121.5, 50.0};
    controller.on_state_updated(QStringLiteral("uav_1_agent"), state);
    controller.addWaypoint(25.0001, 121.5);

    const MonoAudio audio = load_audio(QStringLiteral("patrol.wav"));
    for (const QString& keyword : spotter_.spot(audio.samples, audio.sample_rate))
    {
        controller.on_voice_keyword(keyword);
    }
    EXPECT_TRUE(controller.is_auto_start_pending());
    EXPECT_EQ(started.count(), 0);
    ASSERT_TRUE(started.wait(AUTO_START_MS * 10));
    EXPECT_EQ(started.takeFirst().at(0).value<MissionPlan>().waypoints.size(), 1);
}
