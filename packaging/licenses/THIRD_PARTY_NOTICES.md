# Taiphoon_QGCS 第三方元件授權聲明

Taiphoon_QGCS 以 MIT 授權發布，並包含下列第三方元件。各元件的完整授權條文隨安裝檔附於
`licenses/` 目錄（若安裝檔未附，請至各元件官方網站取得）。

| 元件 | 版本 | 授權 | 使用方式 | 來源 |
|---|---|---|---|---|
| Qt | 6.11.2 | LGPL-3.0（部分模組另有 GPL／商業選項） | 動態連結；使用者可替換 Qt 函式庫 | https://www.qt.io/ |
| Eclipse Cyclone DDS | 0.10.5 | EPL-2.0 或 EDL-1.0（BSD-3-Clause） | 動態連結（ddsc） | https://github.com/eclipse-cyclonedds/cyclonedds |
| Microsoft Visual C++ 執行階段（僅 Windows） | 14.50 | Microsoft 軟體授權條款（可再散布檔案） | 隨程式安裝 | https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files |
| B612／B612 Mono 字型 | 1.008 | SIL Open Font License 1.1 | 內嵌於程式資源，未修改字型檔 | https://github.com/polarsys/b612 |
| Noto Sans TC 字型（可變字重） | 2.004 | SIL Open Font License 1.1 | 內嵌於程式資源作為中文字形，未修改字型檔 | https://github.com/notofonts/noto-cjk |
| OpenStreetMap 圖磚 | — | 地圖資料 ODbL；圖磚使用須遵守 OSM 圖磚使用政策 | 執行時線上取得，畫面左下角保留來源標示 | https://www.openstreetmap.org/copyright |
| FFmpeg（Qt Multimedia 附帶） | 7.1.5 | LGPL-2.1-or-later | Qt 部署腳本隨 Qt Multimedia 部署的動態函式庫（avcodec、avformat、avutil、swresample、swscale），可替換 | https://ffmpeg.org/ |
| sherpa-onnx（C API） | 1.13.8 | Apache-2.0 | 官方預編譯動態函式庫，執行期載入（語音口令；iOS 版不含） | https://github.com/k2-fsa/sherpa-onnx |
| ONNX Runtime | 1.28.2 | MIT | 隨 sherpa-onnx 預編譯檔附帶的動態函式庫 | https://github.com/microsoft/onnxruntime |
| 中文關鍵詞模型 sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01 | 2024-01-01 | Apache-2.0（模型說明）；訓練資料 WenetSpeech 已取得使用許可 | 未修改的模型檔（encoder／decoder／joiner、tokens.txt） | https://github.com/k2-fsa/sherpa-onnx/releases/tag/kws-models |

## Qt（LGPL-3.0）

本程式以動態連結方式使用 Qt。依 LGPL-3.0，使用者可以相容版本的 Qt 函式庫替換安裝目錄中的
Qt 動態函式庫。Qt 原始碼可自 https://download.qt.io/ 取得。

Qt Multimedia（語音口令收音）使用 Qt 提供的 LGPL 版 FFmpeg 動態函式庫（依平台由 Qt 部署腳本附上），
使用者同樣可以相容版本替換；FFmpeg 原始碼可自 https://ffmpeg.org/download.html 取得。

iOS／iPadOS 版因平台限制，Qt 與 Cyclone DDS 皆以靜態方式連結。本程式完整原始碼與建置腳本公開於
https://github.com/Rex-Taiphoon/Taiphoon_QGCS ，使用者可以修改後的 Qt 重新建置並安裝。

## 發布前待確認事項

- 正式發布前需由法務確認 Qt 授權模式（LGPL 或商業授權），iOS／iPadOS 靜態連結的情況尤其需要確認。
- 目前使用 OSM 官方圖磚（ADR-0016）；需遵守 OSM 圖磚使用政策，用量增加時再評估圖磚來源。
