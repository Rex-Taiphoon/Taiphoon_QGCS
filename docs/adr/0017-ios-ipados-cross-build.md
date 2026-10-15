# ADR-0017：iOS／iPadOS 交叉編譯與未簽章測試版

**日期**：2026-10-15
**狀態**：已接受
**相關**：ADR-0013（Qt 原生桌面程式）、ADR-0014（CycloneDDS ROS2 線上相容）

## 背景

規劃文件將行動版列為第一版的非目標。開發者要求在 GitHub Actions 上一併建置 iOS 與 iPadOS 版本，
並發布到 Releases，供平板現場測試。目前沒有 Apple Developer 帳號，也沒有 multicast entitlement。

交叉編譯有兩個建置上的限制：

- Qt for iOS 只支援 Xcode generator，且 Qt 為靜態函式庫。
- CycloneDDS 交叉編譯時不編 `idlc`，`idlc_generate` 改從 PATH 尋找主機版 `idlc`。

## 決策

- iOS 與 iPadOS 使用**同一個 App**（`TARGETED_DEVICE_FAMILY` 為 `1,2`），以 `ios-release` preset
  在 macOS 上交叉編譯。
- 主機版 `idlc` 由 `tools/build-host-idlc.sh` 建置，CycloneDDS 版本取自 `cmake/Dependencies.cmake`；
  交叉編譯時 `BUILD_IDLC` 關閉，iOS 上 `ddsc` 改為靜態連結。
- App 設定使用 `packaging/ios/Info.plist.in`：只支援橫向、要求全螢幕，並附區域網路授權說明。
- 取得 Apple Developer 帳號前只發布**未簽章 IPA**，標示為測試用；使用者需以自己的 Apple ID 重新簽章後安裝。
- 未取得 multicast entitlement 前，iOS 只能以單播連線（關閉 multicast，輸入 Agent 位址）。
- 授權聲明以檔案放入 App bundle 的 `licenses/`，不使用 install／CPack。
- 桌面三平台的決策（ADR-0013）不變；行動版介面與觸控操作不在本 ADR 範圍。

## 替代方案

| 方案 | 否決原因 |
|------|----------|
| 等取得 Apple Developer 帳號再建置 | 無法提早發現交叉編譯與 CycloneDDS 的相容問題 |
| 以 ExternalProject 在同一次 configure 內建置主機版 idlc | 需在交叉編譯環境中另建主機工具鏈，比獨立腳本複雜 |
| iPhone 與 iPad 分成兩個 App | 程式完全相同，只增加維護成本 |

## 後果

- 未簽章 IPA 無法直接安裝；免費 Apple ID 簽章的 App 7 天後失效，只適合測試。
- App 切到背景時 iOS 會暫停程式，DDS 連線隨之中斷，現場操作需維持 App 在前景。
- 介面沿用桌面版面，iPad 橫向可用，iPhone 畫面過小只供檢視。
- iOS 上 Qt 為靜態連結；本專案原始碼與建置腳本公開，使用者可自行以修改後的 Qt 重新建置。
  正式上架前仍需法務確認 Qt 授權模式。
- 正式發布前需補上：App 圖示（asset catalog）、Apple 簽章、multicast entitlement 申請與實機驗證。
