/**
 * @file voice_keywords.hpp
 * @brief 語音口令：辨識結果的口令名稱與 sherpa-onnx 關鍵詞定義
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 *
 * 關鍵詞格式為「拼音 token（聲母與帶聲調韻母，以空白分隔）@口令」，每行一種念法；
 * token 必須存在於模型的 tokens.txt。口令只有兩個字，容易被一般談話誤觸，
 * 因此四個口令的聲母與韻母刻意互不相同，誤觸則由收到口令後的 5 秒倒數讓操作人員取消。
 */
#pragma once

#include <QString>

namespace gcs::voice
{

/// 解鎖（ARM）
inline const QString KEYWORD_ARM = QStringLiteral("解鎖");
/// 起飛到預設高度
inline const QString KEYWORD_TAKEOFF = QStringLiteral("起飛");
/// 開始航點任務
inline const QString KEYWORD_PATROL = QStringLiteral("巡邏");
/// 降落（MODE_LAND）
inline const QString KEYWORD_LAND = QStringLiteral("降落");

/// 送給 sherpa-onnx 的關鍵詞定義（UTF-8）
inline constexpr const char* KEYWORD_DEFINITIONS = "j iě s uǒ @解鎖\n"
                                                   "q ǐ f ēi @起飛\n"
                                                   "x ún l uó @巡邏\n"
                                                   "j iàng l uò @降落\n";

}  // namespace gcs::voice
