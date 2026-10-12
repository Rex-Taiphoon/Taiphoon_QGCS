# Taiphoon_QGCS — Qt 原生地面控制站

搭配 Taiphoon_Agent 使用的桌面地面控制站，是 [Taiphoon_GCS](../Taiphoon_GCS)（網頁版）的 Qt 原生版本。
目標是維持網頁版所有功能與 Agent 介面，並讓使用者**直接安裝即可執行**，不需安裝 Docker、ROS2 或處理 DDS 環境。

> **目前狀態：開發中（M0 治理與骨架）。** 尚無可執行程式；需求、架構與開發路線見
> [開發規劃](docs/qt-cross-platform-development.md)。

## 平台與發布格式（規劃）

| 平台 | 格式 | 備註 |
|------|------|------|
| Windows 10／11 x64 | NSIS 安裝程式 | 首次執行需允許防火牆 |
| macOS | DMG（Universal Binary） | 首次執行需允許區域網路存取 |
| Linux | 開發與 CI 驗證 | 發布格式待定 |

## 與 Agent 的相容性

- 程式內嵌 CycloneDDS，網路層與 **ROS2 Humble + `rmw_cyclonedds_cpp`** 相容，Agent 端不需修改。
- Agent 必須使用 `rmw_cyclonedds_cpp`；使用 Fast DDS 時 shutdown service 無法互通。
- 介面以 `../Taiphoon_Agent_Interfaces/` 為唯一權威來源，同步副本位於 `msg/`。

## 架構

```
┌──────────────────────────────────────────────┐
│ QML：狀態列｜飛行控制｜地圖｜機群｜設定      │  主執行緒
│ UiBridge、models                              │
└──────────────────────┬───────────────────────┘
                       │ Qt signal／slot（queued）
┌──────────────────────▼───────────────────────┐
│ Fleet｜Telemetry｜Guard｜Command｜Lifecycle   │  核心執行緒
│ 模組間連線集中於組裝根 gcs_wiring             │
└──────────────────────┬───────────────────────┘
                       │ IAgentLink
┌──────────────────────▼───────────────────────┐
│ CycloneAgentLink（ROS2 線上相容層）           │  CycloneDDS
└──────────────────────┬───────────────────────┘
                       │ DDS／RTPS
┌──────────────────────▼───────────────────────┐
│ Taiphoon_Agent (uav_{N}_agent)                │
└──────────────────────────────────────────────┘
```

## 模組

| 模組 | 功能 |
|------|------|
| FleetModule | DDS 發現、機隊線上／離線管理 |
| TelemetryModule | 訂閱所有 Agent topic，聚合後提供給 UI |
| GuardModule | 資料驅動狀態轉移表，命令發布前的唯一安全閘門 |
| CommandModule | 將驗證通過的命令發布至 Agent command topic |
| AgentLifecycleModule | 呼叫單機 Agent shutdown service，成功後通知機隊隔離 |
| InspectorModule | 唯讀除錯工具：DDS topic 清單、即時資料、事件紀錄 |

## Agent 遠端關閉

沿用網頁版行為：只能選取單一線上 Agent 並需二次確認，直接呼叫 `/{agent_namespace}/shutdown`，**不要求授權碼**。
此操作不會先要求 RTL 或降落，只能在確認目標已安全落地後執行；成功後 GCS 立即將目標從機隊與單機命令路由隔離，
`/uav_all_agent/command` 仍供其他線上 Agent 接收全體命令。

## 開發環境

- Qt **6.11.2**（`msvc2022_64`、Qt Location、Qt Positioning），版本固定
- Windows：VS Build Tools（MSVC），CMake + Ninja（Qt 安裝目錄 `Tools` 內附）
- Python 3（IDL 產生腳本）

建置指令見 [AGENTS.md](AGENTS.md#建置與驗證)，M0 完成後改用 CMake Presets。

## 文件

- [開發規劃：需求、架構與路線](docs/qt-cross-platform-development.md)
- [領域詞彙表](CONTEXT.md)
- [架構決策記錄](docs/adr/)（Qt 版主要決策：ADR-0013～0016）
- [開發規範 (AGENTS.md)](AGENTS.md)
- [程式碼規範 (CODE_CONSTRAINT.md)](CODE_CONSTRAINT.md)

## 授權

MIT。發布套件另附 Qt（LGPL）、CycloneDDS（EPL-2.0／EDL-1.0）等第三方授權聲明。
