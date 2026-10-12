# ADR-0013：Qt 6.11 LTS 原生桌面程式

**日期**：2026-10-12
**狀態**：已接受
**取代**：ADR-0001、ADR-0005、ADR-0007、ADR-0010、ADR-0011
（原 ADR-0012 為 Taiphoon_GCS 的 GitHub CI／Release 流程，已自本專案移除；Qt 版 CI 於 M9 另立 ADR）

## 背景

Taiphoon_GCS 網頁版為 ROS2 節點 + WebSocket + React，以 Docker 容器部署，再由 Go 啟動器處理
Wi-Fi DDS 單播與 Windows WSL NAT 轉送。使用者必須安裝 Docker Desktop，且 ROS2 在 macOS
沒有官方 binary，無法發布原生 macOS 版本。

Taiphoon_QGCS 的目標是維持網頁版全部功能與 Agent 介面，讓使用者可直接安裝執行，
不需處理 ROS2、DDS 或容器環境。

## 決策

- 採 **Qt 6 + QML 全原生重寫**，不使用 QWebEngineView 內嵌既有網頁。
- **Qt 版本鎖定 6.11 LTS**，目前固定 **6.11.2**；所有開發者與 CI 使用相同版本，升版需另立 ADR 或更新本文件。
- 建置系統只用 CMake + Ninja + `CMakePresets.json`，第三方相依以 CPM／`FetchContent` 固定版本下載。
- Windows 使用 MSVC（`msvc2022_64` kit）；本機為 VS 2026 Build Tools（MSVC 14.50），與 msvc2022 版 Qt 二進位相容。
- macOS 產生 Universal Binary（x86_64 + arm64）。
- 部署使用 `qt_generate_deploy_qml_app_script`，打包使用 CPack：Windows 為 NSIS，macOS 為 DMG。
- 移除 React 前端、WebSocket Server、Docker 相關檔案與 Go 啟動器；首次設定流程移入程式內。
- 建置、部署與 CI 參考 QGroundControl 的做法（固定 Qt 版本、各平台獨立 workflow）。

## 替代方案

| 方案 | 否決原因 |
|------|----------|
| QWebEngineView 包裝既有 React 前端 | 仍需 WebSocket 與後端程序，體積大，未解決 ROS2 安裝問題 |
| 保留 Docker、只改善啟動器 | 仍需 Docker Desktop；macOS 無原生 ROS2 |
| Qt 6.12 | 非 LTS，支援期短；QGC 目前使用 6.11 |

## 後果

- 前端全部以 QML 重寫，網頁版元件只作為功能對等的參考（原始檔保留於 Taiphoon_GCS）。
- 使用者只需執行安裝程式；首次執行會出現作業系統防火牆／區域網路權限提示。
- Qt 以 LGPL 動態連結發布，需附授權聲明並保留替換能力。
