# Taiphoon_QGCS — Qt 原生地面控制站

搭配 Taiphoon_Agent 使用的桌面地面控制站，是 [Taiphoon_GCS](../Taiphoon_GCS)（網頁版）的 Qt 原生版本。
目標是維持網頁版所有功能與 Agent 介面，並讓使用者**直接安裝即可執行**，不需安裝 Docker、ROS2 或處理 DDS 環境。

> **目前狀態：功能開發完成，待現場驗收。** 介面、地圖、網路設定與除錯工具均已完成，
> 並已連線 ROS2 Humble Agent 驗證；Windows 可攜式 ZIP 可用，NSIS 安裝程式、macOS 與 CI 尚待驗證。
> 需求、架構與開發路線見 [開發規劃](docs/qt-cross-platform-development.md)。

## 平台與發布格式

| 平台 | 格式 | 狀態 |
|------|------|------|
| Windows 10／11 x64 | 可攜式 ZIP；NSIS 安裝程式 | ZIP 可用；安裝程式需建置環境安裝 NSIS |
| macOS | DMG（Universal Binary） | 設定完成，待 Mac 建置與簽章 |
| Linux | TGZ | 開發與 CI 驗證用 |

## 使用方式

1. 解壓縮 ZIP（或執行安裝程式），開啟 `bin/Taiphoon_QGCS.exe`。
2. 首次啟動會出現「網路設定」：填入 Agent 所在電腦的區網 IP（多台以換行分隔）。
   同一台電腦或允許 multicast 的區網模擬時可留空。之後可在「進階設定 → 網路設定」修改，保存後立即重新連線。
3. Windows 首次執行會詢問防火牆，需允許；未允許時收不到 Agent 資料。
   使用安裝程式時會自動建立規則（只允許同一子網路的 UDP；Agent 需經路由器或 VPN 連線時需自行放寬）。
4. 右上機群表選取單機或「全部」後，以左上紙飛機按鈕開啟飛行控制。
   按鈕是否可用與 GCS 的防呆規則一致；Arm 需拖曳滑桿，Takeoff、RTL、Land、Disarm 與編隊需確認。
   按鈕顏色只表示風險：白色為一般命令、琥珀色需確認、紅色為 Arm／Disarm；面板底部常駐顯示解鎖狀態。
   單機移動的前後左右以機頭方向為準，不是東南西北。
5. 「進階設定」可調整自動跟隨與命令逾時、關閉單一 Agent、開啟唯讀除錯工具或結束 GCS。
   結束 GCS 不會對無人機發送任何命令。

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
│ 連線集中於組裝根 gcs_wiring／ui_wiring        │
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
| DiagnosticsController（app） | 唯讀除錯工具：DDS topic 清單、最新資料、事件紀錄；不提供發布 |

## Agent 遠端關閉

沿用網頁版行為：只能選取單一線上 Agent 並需二次確認，直接呼叫 `/{agent_namespace}/shutdown`，**不要求授權碼**。
此操作不會先要求 RTL 或降落，只能在確認目標已安全落地後執行；成功後 GCS 立即將目標從機隊與單機命令路由隔離，
`/uav_all_agent/command` 仍供其他線上 Agent 接收全體命令。

## 開發環境

- Qt **6.11.2**（`msvc2022_64`、Qt Location、Qt Positioning），版本固定
- Windows：VS Build Tools（MSVC），CMake + Ninja（Qt 安裝目錄 `Tools` 內附）
- Python 3（IDL 產生腳本）

```powershell
. .\tools\dev-env.ps1
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

打包：`cmake --preset windows-release`、`cmake --build --preset windows-release` 後，於 `build/windows-release`
執行 `cpack -C Release`。詳見 [AGENTS.md](AGENTS.md#建置與驗證)。

開發用工具（不隨產品發布）：`tools/dds_probe`（DDS 互通驗證）、`tools/gcs_headless`（無介面整合驗證）。

### 連線本機 Docker Desktop 內的 Agent

Agent 位於 Docker Desktop NAT 之後，需使用單播並讓 GCS 對外公告 `host.docker.internal`（192.168.65.254），
容器回送封包才會經 Docker Desktop 轉送到主機。Agent 端設定見
[開發規劃](docs/qt-cross-platform-development.md) 6.1 節
（`ExternalNetworkAddress` 設為主機區網 IP、發布 UDP 7410 起的埠、Peer 加上 `192.168.65.254:7510`）。
GCS 以命令列參數啟動（只在本次有效，不寫入設定檔）：

```powershell
.\build\windows-debug\bin\Taiphoon_QGCS.exe --interface <主機區網 IP> --external-address 192.168.65.254 `
    --no-multicast --peer <主機區網 IP> --participant-index 50
```

## 文件

- [開發規劃：需求、架構與路線](docs/qt-cross-platform-development.md)
- [領域詞彙表](CONTEXT.md)
- [架構決策記錄](docs/adr/)（Qt 版主要決策：ADR-0013～0016）
- [開發規範 (AGENTS.md)](AGENTS.md)
- [程式碼規範 (CODE_CONSTRAINT.md)](CODE_CONSTRAINT.md)

## 授權

MIT。發布套件另附 Qt（LGPL）、CycloneDDS（EPL-2.0／EDL-1.0）、B612 字型（OFL-1.1）等第三方授權聲明。
