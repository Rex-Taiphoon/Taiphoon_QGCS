/**
 * @file test_keyword_spotter.cpp
 * @brief 語音口令辨識：以 Windows 中文語音合成（Microsoft Hanhan，zh-TW）錄製的口令驗證辨識與誤觸，
 *        並串接 MissionController 驗證「請執行任務 → 確認」的流程
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 *
 * test/data/voice/ 的音檔為 16 kHz、16 位元單聲道。未建置 sherpa-onnx（GCS_ENABLE_VOICE=OFF 或不支援的平台）時略過。
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

using namespace gcs;
using namespace gcs::voice;

namespace
{

/// 串流辨識每次送入的長度（秒），與桌面程式收音的區塊大小同級
constexpr double CHUNK_SECONDS = 0.1;

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
    // 未載入時一律沒有結果，不會當機
    const float silence[4] = {};
    EXPECT_TRUE(spotter.accept(silence, 4, 16000).isEmpty());
    EXPECT_TRUE(spotter.finish(16000).isEmpty());
}

TEST(WavFileTest, RejectsNonWav)
{
    MonoAudio audio;
    QString error;
    EXPECT_FALSE(read_wav_file(QStringLiteral(GCS_TEST_VOICE_DATA_DIR "/missing.wav"), &audio, &error));
    EXPECT_FALSE(error.isEmpty());
}

TEST_F(KeywordSpotterTest, RecognizesEachKeyword)
{
    const QList<QPair<QString, QString>> cases = {
        {QStringLiteral("start.wav"), KEYWORD_START_MISSION},
        {QStringLiteral("stop.wav"), KEYWORD_STOP_MISSION},
        {QStringLiteral("confirm.wav"), KEYWORD_CONFIRM},
        {QStringLiteral("cancel.wav"), KEYWORD_CANCEL},
    };
    for (const auto& item : cases)
    {
        const MonoAudio audio = load_audio(item.first);
        EXPECT_EQ(spotter_.spot(audio.samples, audio.sample_rate), QStringList{item.second})
            << item.first.toStdString();
    }
}

TEST_F(KeywordSpotterTest, OrdinarySpeechTriggersNothing)
{
    const MonoAudio audio = load_audio(QStringLiteral("negative.wav"));
    EXPECT_TRUE(spotter_.spot(audio.samples, audio.sample_rate).isEmpty());
}

TEST_F(KeywordSpotterTest, StreamingChunksGiveSameResult)
{
    const MonoAudio audio = load_audio(QStringLiteral("start.wav"));
    const int chunk = static_cast<int>(CHUNK_SECONDS * audio.sample_rate);
    QStringList found;
    spotter_.begin();
    for (int offset = 0; offset < audio.samples.size(); offset += chunk)
    {
        const int count = std::min(chunk, static_cast<int>(audio.samples.size()) - offset);
        found += spotter_.accept(audio.samples.constData() + offset, count, audio.sample_rate);
    }
    found += spotter_.finish(audio.sample_rate);
    EXPECT_EQ(found, QStringList{KEYWORD_START_MISSION});
}

TEST_F(KeywordSpotterTest, SpokenStartThenConfirmStartsMission)
{
    MissionController controller;
    QSignalSpy started(&controller, &MissionController::mission_start_requested);
    controller.set_selected_agent(QStringLiteral("uav_1_agent"));
    AgentStateData state = gcs::test::make_state(FlightState::HOVER, true, false);
    state.position_global = {25.0, 121.5, 50.0};
    controller.on_state_updated(QStringLiteral("uav_1_agent"), state);
    controller.addWaypoint(25.0001, 121.5);

    // 每次按住說話各送一段音訊，口令依序交給 MissionController
    const auto speak = [this, &controller](const QString& file)
    {
        const MonoAudio audio = load_audio(file);
        for (const QString& keyword : spotter_.spot(audio.samples, audio.sample_rate))
        {
            controller.on_voice_keyword(keyword);
        }
    };
    speak(QStringLiteral("start.wav"));
    EXPECT_TRUE(controller.is_confirm_pending());
    EXPECT_EQ(started.count(), 0);

    QTest::qWait(MissionController::VOICE_CONFIRM_MIN_DELAY_MS + 100);
    speak(QStringLiteral("confirm.wav"));
    ASSERT_EQ(started.count(), 1);
    EXPECT_EQ(started.takeFirst().at(0).value<MissionPlan>().waypoints.size(), 1);
}
