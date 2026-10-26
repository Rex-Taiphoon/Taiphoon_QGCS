/**
 * @file voice_keywords.hpp
 * @brief 語音口令：辨識結果的口令名稱與 sherpa-onnx 關鍵詞定義
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 *
 * 關鍵詞格式為「拼音 token（聲母與帶聲調韻母，以空白分隔）@口令」，每行一種念法；
 * token 必須存在於模型的 tokens.txt。同一口令可列多種念法，台灣口語常把捲舌音說成不捲舌，
 * 因此「執」「止」另列 z 開頭的念法。
 */
#pragma once

#include <QString>

namespace gcs::voice
{

/// 開始航點任務（需再確認）
inline const QString KEYWORD_START_MISSION = QStringLiteral("請執行任務");
/// 停止航點任務並懸停
inline const QString KEYWORD_STOP_MISSION = QStringLiteral("停止任務");
/// 確認待執行的任務
inline const QString KEYWORD_CONFIRM = QStringLiteral("確認");
/// 取消待執行的任務
inline const QString KEYWORD_CANCEL = QStringLiteral("取消");

/// 送給 sherpa-onnx 的關鍵詞定義（UTF-8）
inline constexpr const char* KEYWORD_DEFINITIONS = "q ǐng zh í x íng r èn w ù @請執行任務\n"
                                                   "q ǐng z í x íng r èn w ù @請執行任務\n"
                                                   "t íng zh ǐ r èn w ù @停止任務\n"
                                                   "t íng z ǐ r èn w ù @停止任務\n"
                                                   "q uè r èn @確認\n"
                                                   "q ǔ x iāo @取消\n";

}  // namespace gcs::voice
