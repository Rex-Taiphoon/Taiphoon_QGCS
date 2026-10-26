/**
 * @file wav_file.cpp
 * @brief WAV 檔讀取實作
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 */
#include "voice/wav_file.hpp"

#include <QFile>
#include <QtEndian>

namespace gcs::voice
{

namespace
{

/// RIFF 標頭：「RIFF」、檔案長度、「WAVE」
constexpr int RIFF_HEADER_SIZE = 12;
/// 「WAVE」在檔案中的位置
constexpr int WAVE_TAG_OFFSET = 8;
constexpr int TAG_SIZE = 4;
/// 每個區塊的標頭：識別碼 4 位元組 + 長度 4 位元組
constexpr int CHUNK_HEADER_SIZE = 8;
/// fmt 區塊至少 16 位元組
constexpr int MIN_FMT_SIZE = 16;
constexpr quint16 PCM_FORMAT = 1;
constexpr quint16 PCM_16_BITS = 16;
constexpr int BYTES_PER_16_BIT_SAMPLE = 2;
/// 16 位元整數轉為 -1～1 的比例
constexpr float INT16_SCALE = 32768.0F;

/**
 * @brief 設定錯誤訊息並回傳 false
 * @param error 輸出（可為 nullptr）
 * @param message 訊息
 * @return 一律為 false
 */
bool fail(QString* error, const QString& message)
{
    if (error != nullptr)
    {
        *error = message;
    }
    return false;
}

}  // namespace

bool read_wav_file(const QString& path, MonoAudio* audio, QString* error)
{
    if (audio == nullptr)
    {
        return fail(error, QStringLiteral("未提供輸出"));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return fail(error, QStringLiteral("無法開啟 %1：%2").arg(path, file.errorString()));
    }
    const QByteArray data = file.readAll();
    if (data.size() < RIFF_HEADER_SIZE || !data.startsWith("RIFF") || data.mid(WAVE_TAG_OFFSET, TAG_SIZE) != "WAVE")
    {
        return fail(error, QStringLiteral("不是 WAV 檔：%1").arg(path));
    }

    quint16 channels = 0;
    quint16 bits = 0;
    quint32 sample_rate = 0;
    qsizetype offset = RIFF_HEADER_SIZE;
    while (offset + CHUNK_HEADER_SIZE <= data.size())
    {
        const QByteArray id = data.mid(offset, TAG_SIZE);
        const auto size = static_cast<qsizetype>(qFromLittleEndian<quint32>(data.constData() + offset + TAG_SIZE));
        const qsizetype body = offset + CHUNK_HEADER_SIZE;
        if (body + size > data.size())
        {
            return fail(error, QStringLiteral("WAV 區塊長度錯誤：%1").arg(path));
        }
        if (id == "fmt " && size >= MIN_FMT_SIZE)
        {
            const char* fmt = data.constData() + body;
            if (qFromLittleEndian<quint16>(fmt) != PCM_FORMAT)
            {
                return fail(error, QStringLiteral("只支援 PCM WAV：%1").arg(path));
            }
            channels = qFromLittleEndian<quint16>(fmt + 2);
            sample_rate = qFromLittleEndian<quint32>(fmt + 4);
            bits = qFromLittleEndian<quint16>(fmt + 14);
        }
        else if (id == "data")
        {
            if (bits != PCM_16_BITS || channels == 0 || sample_rate == 0)
            {
                return fail(error, QStringLiteral("只支援 16 位元 PCM WAV：%1").arg(path));
            }
            const qsizetype frames = size / (BYTES_PER_16_BIT_SAMPLE * channels);
            audio->samples.resize(frames);
            const char* pcm = data.constData() + body;
            for (qsizetype frame = 0; frame < frames; ++frame)
            {
                float sum = 0.0F;
                for (quint16 channel = 0; channel < channels; ++channel)
                {
                    const qsizetype at = (frame * channels + channel) * BYTES_PER_16_BIT_SAMPLE;
                    sum += static_cast<float>(qFromLittleEndian<qint16>(pcm + at)) / INT16_SCALE;
                }
                audio->samples[frame] = sum / channels;
            }
            audio->sample_rate = static_cast<int>(sample_rate);
            return true;
        }
        // 區塊長度為奇數時有一個填充位元組
        offset = body + size + (size % 2);
    }
    return fail(error, QStringLiteral("WAV 檔沒有音訊資料：%1").arg(path));
}

}  // namespace gcs::voice
