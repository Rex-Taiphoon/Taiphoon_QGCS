# ADR-0015：以 Qt signal／slot 取代 EventBus

**日期**：2026-10-12
**狀態**：已接受
**取代**：ADR-0002 中「模組間透過 EventBus 通訊」的部分、ADR-0008

## 背景

網頁版以字串主題的 EventBus 串接模組，事件內容是 WebSocket JSON 字串；JSON 的存在是為了與瀏覽器通訊。
Qt 原生版沒有瀏覽器端，Qt 內建的 signal／slot 已提供型別安全的發布／訂閱與跨執行緒傳遞。

## 決策

- **移除 EventBus 與 JSON 事件格式**。模組繼承 `QObject` 與 `GcsModule`，以帶型別的 signal／slot 溝通。
- 事件資料改為 `src/core/` 下的 C++ 結構（例如 `AgentStateData`、`CommandRequest`、`CommandAck`、
  `FleetSnapshot`），以 `Q_DECLARE_METATYPE` 註冊，可跨執行緒以 queued connection 傳遞。
- **模組間仍禁止互相 include**。模組只依賴 `core/` 的型別與介面；跨模組與 UI ↔ 模組的 `connect()`
  集中在組裝根：`src/app/gcs_wiring.cpp`（模組之間）與 `src/app/ui_wiring.cpp`（UiBridge、除錯工具與模組之間）。
  模組可在 `init()` 內連接自己被注入的 `IAgentLink`，因為那不是跨模組連線。
- **執行緒模型**：
  - 所有功能模組位於同一個「核心執行緒」（`QThread`），模組之間的連線一律指定 `Qt::DirectConnection`。
  - DDS 回呼位於 CycloneDDS 執行緒，由 `IAgentLink` 發出 signal，以 queued connection 進入核心執行緒。
  - QML 與 `UiBridge` 位於主執行緒，與核心執行緒之間一律使用 queued connection。
  - 因模組只在單一執行緒執行，原本的 mutex 可移除；狀態更新與命令驗證依事件順序處理，不會交錯。
- **Guard 安全閘門**：
  - `UiBridge` 只能連到 `GuardModule::request_command()`；只有 Guard 的 `command_validated` signal
    可連到 `CommandModule::publish_validated()`，且使用 `Qt::DirectConnection`，驗證與發布在同一次呼叫內完成。
  - 以單元測試驗證組裝結果：未經 Guard 允許的請求不得讓 `FakeAgentLink` 發布任何命令。
- 計時改用核心執行緒的 `QTimer`（Fleet 掃描、shutdown 逾時）；需要判斷時間的邏輯以可注入時間點的
  公開方法提供，供單元測試使用。
- 日誌使用 Qt logging category（`Q_LOGGING_CATEGORY`、`qCInfo` 等）。
- 原網頁版的事件對應：

  | EventBus 主題 | signal／slot |
  |---|---|
  | `state_update`、`attitude_update`、`trajectory_update` 等 | `IAgentLink` → `TelemetryModule` → 對外 signal |
  | `agent_online`／`agent_offline`、`fleet_update` | `FleetModule::agent_online` 等 signal |
  | `cmd_request` → `cmd_validated` → `cmd_ack` | `GuardModule::request_command` → `command_validated` → `CommandModule` → `command_acked` |
  | `agent_shutdown_request`／`agent_shutdown_accepted` | `AgentLifecycleModule::request_shutdown` → `shutdown_accepted` |
  | `gcs_shutdown_request` | 一般程式結束流程，不發送任何 Agent 命令 |

- 除錯工具（`DiagnosticsController`）位於 app 層而非模組：它需要讀取 link 的 DDS 探索清單，
  且只接收事件、不送出任何請求。

## 替代方案

| 方案 | 否決原因 |
|------|----------|
| 保留 EventBus + JSON | JSON 只為瀏覽器通訊存在；字串主題無編譯期檢查 |
| 核心完全不依賴 Qt（純 C++ + 自製回呼） | 需重寫一套跨執行緒佇列與計時器，與 Qt 重複 |

## 後果

- `gcs_core` 依賴 QtCore（不依賴 QtGui／QML），單元測試可在無畫面環境執行。
- Guard、Command、Fleet、Lifecycle、Telemetry 的介面改變，屬安全關鍵變更：
  轉移表內容不變，但測試需改寫為以 signal／slot 驅動，並維持 `FlightState` × `CmdType` 全組合覆蓋。
- 單元測試沿用 Google Test，搭配 QtTest 的 `QSignalSpy`；測試主程式需建立 `QCoreApplication`。
