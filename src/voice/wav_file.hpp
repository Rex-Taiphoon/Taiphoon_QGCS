/**
 * @file wav_file.hpp
 * @brief 讀取 16 位元 PCM WAV 檔，供語音口令測試與無介面工具使用
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 */
#pragma once

#include <QString>
#include <QVector>

namespace gcs::voice
{

/// 單聲道浮點音訊
struct MonoAudio
{
    QVector<float> samples;
    int sample_rate = 0;
};

/**
 * @brief 讀取 16 位元 PCM WAV；多聲道時取平均成單聲道
 * @param path 檔案路徑
 * @param audio 輸出
 * @param error 失敗原因（可為 nullptr）
 * @return 是否成功
 */
bool read_wav_file(const QString& path, MonoAudio* audio, QString* error);

}  // namespace gcs::voice
