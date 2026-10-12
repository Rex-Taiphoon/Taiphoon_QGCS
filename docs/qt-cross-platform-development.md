# Taiphoon_QGCS 開發規劃：需求、架構與路線

**日期**：2026-10-12
**狀態**：已確認方向（決策見 ADR-0013～0016）
**來源**：由 Taiphoon_GCS（網頁版，提交 `1350831`）衍生；網頁版原始碼保留於 `../Taiphoon_GCS` 作為功能對照

---

## 1. 目標

在**維持網頁版所有功能與 Agent 介面**的前提下，提供可直接安裝、執行的原生地面控制站：

- 使用者不需安裝 Docker、ROS2、Python 或任何執行環境。
- Windows（NSIS 安裝程式）與 macOS（DMG、Universal Binary）皆支援；Linux 先作為開發與 CI 驗證平台。
- **Agent 端不做任何修改**，網路層維持與 ROS2 Humble + `rmw_cyclonedds_cpp` 相容。
- 保留安全機制：GuardModule 轉移表、Arm／Takeoff 的 PX4 原生狀態規則、防呆層級 L1／L2。

**非目標（第一版）**：行動版、影片串流、離線 `.mbtiles` 地圖、變更 Agent 介面契約。

## 2. 已確認決策

| 編號 | 決策 | ADR |
|---|---|---|
| D1 | Qt + QML 全原生重寫；移除 React、WebSocket、Docker、Go 啟動器 | 0013 |
| D2 | Qt 鎖定 **6.11 LTS（6.11.2）** | 0013 |
| D3 | 內嵌 CycloneDDS 0.10.x，網路層與 ROS2 Humble 相容，不連結 ROS2 函式庫 | 0014 |
| D4 | **以 Qt signal／slot 取代 EventBus**，事件改為帶型別結構，不再使用 JSON | 0015 |
| D5 | 地圖第一版用 Qt Location `osm` plugin，MapLibre 延後 | 0016 |
| D6 | 安全邏輯（轉移表、PX4 原生狀態規則）內容不變，只改介面與測試驅動方式 | 0003、0015 |
| D7 | 暫不設定 Git 遠端 | — |

## 3. 需求

### 3.1 功能需求（與網頁版對等）

| 編號 | 需求 | 網頁版來源 |
|---|---|---|
| F-01 | 動態機隊發現：每 3 秒掃描；超過 5 秒無狀態資料視為 stale；shutdown 成功後直到 topic 消失前不重新加入 | `FleetModule` |
| F-02 | 訂閱 `state`、`attitude`、`ned_target`、`waypoints`、`planning/trajectory` 並顯示 | `TelemetryModule` |
| F-03 | 所有命令經 Guard 驗證：轉移表 + Arm／Takeoff 依 PX4 原生狀態；全體命令要求每台線上 Agent 都允許；FORMATION_* 僅限全體 | `GuardModule`、ADR-0003 |
| F-04 | 命令路由至 `/{ns}/command` 或 `/uav_all_agent/command`；參數含 NaN／Inf 時拒絕；回報成功或失敗 | `CommandModule` |
| F-05 | 單機 Agent shutdown：只能選單一線上 Agent、二次確認、5 秒逾時、成功後立即從機隊隔離 | `AgentLifecycleModule` |
| F-06 | 版面：60 px 狀態列與 50 × 50 品牌圖示、四分頁飛行控制、可最小化的機群懸浮視窗、全幅地圖、姿態儀、提示訊息 | ADR-0009 |
| F-07 | 防呆層級：L1 兩步驟確認（Takeoff、RTL、Land、Disarm）、L2 滑桿確認（Arm）；按鈕依狀態停用 | `CommandPanel`、ADR-0006 |
| F-08 | 地圖：依航向旋轉的箭頭與名稱、軌跡折線（選中綠色、其他橘色）、自動跟隨、縮放與朝北按鍵在右下、OSM 來源標示在左下 | `MapView` |
| F-09 | `px4_state_valid` 尚未為 `true` 時顯示「等待飛控連線中...」；`Alt` 取 `-position_ned[2]`；Volt／Curr／Alt 分行顯示 | README |
| F-10 | 設定持久化：自動跟隨、命令逾時、起飛高度、移動距離、YAW、編隊參數 | `settingsStore`、`localStorage` |
| F-11 | 除錯工具：列出 DDS topic、即時檢視 Agent topic、檢視模組事件紀錄；**不得提供發布命令的功能** | `DebugTerminal`（重新設計） |
| F-12 | 結束 GCS 需確認，且不對 Agent 發送任何命令 | `gcs_shutdown_request` |
| F-13 | 首次設定：Agent 探索入口、網卡、Domain ID、participant index、SITL multicast；Wi-Fi 位址改變時提示並重建 DDS participant | Go 啟動器設定頁 |

### 3.2 非功能需求

| 編號 | 需求 |
|---|---|
| N-01 | 安裝後即可執行，不需另外安裝執行環境或手動編輯設定檔 |
| N-02 | 10 Hz × N 台的遙測不得阻塞 UI；UI 以約 20 Hz 合併刷新 |
| N-03 | 不得硬編碼 Agent 命名空間或無人機數量 |
| N-04 | 安全關鍵程式碼變更必須附單元測試；Guard 測試覆蓋 `FlightState` × `CmdType` 全組合 |
| N-05 | Agent 必須使用 `rmw_cyclonedds_cpp`（shutdown service 的部署前提） |
| N-06 | 第三方相依與 Qt 版本固定；發布時附 Qt（LGPL）、CycloneDDS（EPL-2.0）等授權聲明 |

## 4. 現有程式碼盤點

| 元件 | 處理方式 | 說明 |
|---|---|---|
| `core/event_bus.hpp` | 移除 | 改用 signal／slot（ADR-0015） |
| `core/gcs_module.hpp`、`module_manager` | 改寫 | 模組改為 `QObject`；管理器負責建立、移入核心執行緒、啟動與關閉 |
| `core/ws_protocol` | 改寫為 `core/command_protocol` | 保留命令驗證、命令列組裝、NaN／Inf 檢查；移除 JSON |
| `core/ws_server` | 移除 | 改由 `UiBridge` 提供給 QML |
| `modules/guard` | 介面改寫，規則不變 | 轉移表與 PX4 原生狀態規則逐字沿用；移除 mutex |
| `modules/telemetry` | 改寫 | 經 `IAgentLink` 訂閱 5 種 topic，輸出帶型別 signal |
| `modules/fleet` | 改寫 | 發現改用 `IAgentLink::list_state_topics()`；移除對 `telemetry_module.hpp` 的 include（原本違反模組隔離） |
| `modules/command` | 改寫 | 經 `IAgentLink` 發布 `std_msgs/String` |
| `modules/lifecycle` | 改寫 | 經 `IAgentLink` 呼叫 shutdown service；逾時改用 `QTimer` |
| `modules/map` | 移除 | 地圖由 Qt Location 處理（ADR-0016） |
| `modules/debug` | 重寫為 `modules/inspector` | 無 `ros2` CLI，改為內建唯讀工具 |
| `main.cpp` | 改寫 | 原本直接呼叫 `telemetryPtr->subscribeAgent()`，改由組裝根連線 |
| `test/*.cpp` | 改寫 | 改為 Google Test + QtTest，以 `FakeAgentLink` 驅動 |
| `frontend/`、Docker、Go 啟動器、舊 CI 與腳本 | 移除 | 原始檔保留於 `../Taiphoon_GCS` |
| `msg/` | 保留 | 內容與權威來源一致（僅換行字元不同）；`msg/CMakeLists.txt`、`msg/package.xml` 於 M1 改為 IDL 產生流程 |

## 5. 軟體架構

### 5.1 分層

```
┌─ app（QtGui／QtQml／QtLocation，主執行緒）──────────────────────┐
│ QML 檢視 ← UiBridge（QML singleton）、FleetModel、ToastModel     │
│ AppSettings（QSettings）、SiteConfig、gcs_wiring（組裝根）        │
└───────────────▲─────────────────────────────┬──────────────────┘
                │ queued signal               │ queued signal
┌─ gcs_core（QtCore，核心執行緒）──────────────▼──────────────────┐
│ Fleet、Telemetry、Guard、Command、Lifecycle、Inspector           │
│ 模組間：Qt::DirectConnection；共用型別與 IAgentLink 位於 core/   │
└───────────────▲─────────────────────────────┬──────────────────┘
                │ queued signal               │ 方法呼叫
┌─ gcs_link（QtCore + CycloneDDS，DDS 執行緒）─▼──────────────────┐
│ CycloneAgentLink：ROS2 命名、QoS 表、IDL 型別、服務請求標頭、     │
│ DCPSPublication 發現、XML 設定產生                                │
└──────────────────────────────────────────────────────────────────┘
          測試以 FakeAgentLink 取代 gcs_link
```

### 5.2 CMake 目標

| 目標 | 相依 | 內容 |
|---|---|---|
| `gcs_core`（靜態函式庫） | QtCore | `core/`、`modules/` |
| `gcs_link`（靜態函式庫） | QtCore、CycloneDDS、`gcs_core` | `link/`、產生的 IDL 型別 |
| `Taiphoon_QGCS`（執行檔） | QtGui、QtQml、QtQuick、QtLocation、QtPositioning、上述兩者 | `app/`、`qml/` |
| `test_*`（Google Test） | QtCore、QtTest、`gcs_core`、`test/fakes` | 不連結 `gcs_link` 即可執行 |

### 5.3 核心型別與介面（`src/core/`）

```cpp
// agent_types.hpp：取代 JSON 的事件資料
struct AgentStateData;      // 對應 AgentState 全部欄位
struct AgentAttitudeData;   // q[4]
struct NedTargetData;
struct WaypointSequenceData;
struct AgentTrajectoryData; // WGS84 control_points 等
struct CommandRequest;      // agent（uav_N_agent 或 all）、command、params
struct CommandAck;          // agent、command、is_ok、message

// i_agent_link.hpp：模組唯一可用的 Agent 通訊介面
class IAgentLink : public QObject
{
    Q_OBJECT
public:
    virtual void subscribe_agent(const QString& agent_ns) = 0;
    virtual void unsubscribe_agent(const QString& agent_ns) = 0;
    virtual QStringList list_remote_topics() const = 0;
    virtual bool publish_command(const QString& topic, const QString& command_line) = 0;
    virtual void request_shutdown(const QString& agent_ns, quint64 request_id) = 0;
signals:
    void state_received(const QString& agent_ns, const AgentStateData& state);
    void attitude_received(const QString& agent_ns, const AgentAttitudeData& attitude);
    // ned_target、waypoints、trajectory 同理
    void shutdown_replied(const QString& agent_ns, quint64 request_id, bool is_success,
                          const QString& message);
};
```

實際簽章於 M2 定案；上方用於說明責任劃分。

### 5.4 訊號流（取代 EventBus 主題）

```
IAgentLink.state_received ─▶ TelemetryModule ─▶ state_updated ─┬▶ GuardModule.on_state_updated
                                                              ├▶ FleetModule.on_state_updated（lastSeen）
                                                              └▶ UiBridge（queued）
FleetModule ─▶ agent_online／agent_offline ─▶ Telemetry、Guard、Command
FleetModule ─▶ fleet_changed ─▶ UiBridge

QML ─▶ UiBridge.sendCommand() ─▶（queued）GuardModule.request_command
GuardModule ─▶ command_validated ─（Direct）─▶ CommandModule.publish_validated ─▶ IAgentLink.publish_command
GuardModule／CommandModule ─▶ command_acked ─▶ UiBridge ─▶ 提示訊息

QML ─▶ UiBridge.shutdownAgent() ─▶ AgentLifecycleModule.request_shutdown ─▶ IAgentLink.request_shutdown
IAgentLink.shutdown_replied ─▶ Lifecycle ─▶ shutdown_accepted ─▶ FleetModule（立即隔離）
```

規則：

- `UiBridge` 與 QML 不得連到 `CommandModule` 或呼叫 `IAgentLink` 發布命令；這項規則以組裝測試驗證。
- 所有 `connect()` 只出現在 `src/app/gcs_wiring.cpp`。
- 按鈕啟用狀態由 `UiBridge` 依 Guard 轉移表推導（app 層可 include 轉移表），避免 QML 與 Guard 規則不一致；
  Guard 仍是唯一的實際閘門。

### 5.5 執行緒

| 執行緒 | 物件 | 備註 |
|---|---|---|
| 主執行緒 | QML、`UiBridge`、models | 合併 telemetry，以約 20 Hz 刷新 model |
| 核心執行緒 | 全部模組、模組內 `QTimer` | 單執行緒處理，移除模組內 mutex |
| CycloneDDS 執行緒 | listener 回呼 | 只做解碼與發出 signal，不碰其他 `QObject` |

### 5.6 目錄規劃

```
Taiphoon_QGCS/
├── CMakeLists.txt、CMakePresets.json
├── cmake/                 # CPM、MsgToIdl、Deploy
├── msg/                   # 權威介面同步副本、interfaces.lock.json
├── idl/                   # 手寫 IDL（std_msgs、std_srvs、builtin_interfaces）
├── tools/msg2idl.py
├── src/
│   ├── core/              # gcs_module、module_manager、agent_types、i_agent_link、command_protocol
│   ├── modules/           # fleet、telemetry、guard、command、lifecycle、inspector
│   ├── link/              # cyclone_agent_link、qos_table、ros_naming、service_header、dds_config
│   ├── app/               # main、gcs_wiring、ui_bridge、models/、app_settings、site_config
│   └── qml/               # Main、StatusBar、CommandPanel、ConfirmDialog、SlideToConfirm、FleetPanel、
│                          # MapView、AttitudeIndicator、Toasts、SettingsPanel、DebugInspector、FirstRunWizard
├── resources/             # 品牌圖示等
├── test/                  # Google Test、fakes/FakeAgentLink、互通測試腳本
├── packaging/             # nsis/、macos/（Info.plist、entitlements）、授權聲明
└── docs/
```

### 5.7 網頁元件 → QML 對應

| 網頁版 | QML | 備註 |
|---|---|---|
| `StatusBar.tsx` | `StatusBar.qml` | 60 px、可水平捲動摘要、燈號與設定入口保持可見 |
| `CommandPanel.tsx` | `CommandPanel.qml` | 四分頁：安全開關／模式控制／移動與 YAW／編隊命令 |
| `ConfirmDialog.tsx` | `ConfirmDialog.qml` | L1 |
| `SlideToConfirm.tsx` | `SlideToConfirm.qml` | L2 |
| `FleetPanel.tsx` | `FleetPanel.qml` | 圓角卡片、可最小化、窄視窗移位 |
| `MapView.tsx` | `MapView.qml` | Qt Location OSM |
| `AttitudeIndicator.tsx` | `AttitudeIndicator.qml` | `Canvas` 或 Shape |
| `ToastContainer.tsx` | `Toasts.qml` | 顯示 `command_acked` 結果 |
| `SettingsPanel.tsx` | `SettingsPanel.qml` | 進階設定、Agent shutdown、結束 GCS |
| `DebugTerminal.tsx` | `DebugInspector.qml` | 內建唯讀指令：`topic list`、`topic echo <topic>`、`events`；不提供 `topic pub` |
| Zustand stores | `UiBridge`、`FleetModel`、`ToastModel`、`AppSettings` | 設定改用 `QSettings` |

## 6. 開發路線

M1 是決定方案是否可行的關卡；M2 不需要網路，可與 M1 同時進行。

| 階段 | 內容 | 完成條件 |
|---|---|---|
| **M0 治理與骨架** ✅ | ADR、AGENTS／CONTEXT／CODE_CONSTRAINT 修訂；移除舊元件；本機 `git init`；`CMakePresets.json` 與空白 QML 視窗 | 文件經開發者確認；`cmake --preset windows-debug` 可建置執行 |
| **M1 DDS 相容 PoC** ✅ | 固定 CycloneDDS 0.10.5；`msg2idl`；`tools/dds_probe` 驗證 ADR-0014 的 5 個項目 | 對 Humble Agent（SITL）全部通過；任一項失敗則重新評估 |
| **M2 核心移植** ✅ | `gcs_core`、核心型別、`IAgentLink`、`FakeAgentLink`、模組改為 `QObject`、組裝根、測試改寫 | Guard 全組合測試與其他既有測試案例全部通過；組裝測試證明無法繞過 Guard |
| **M3 Link 層與無介面整合** ✅ | `CycloneAgentLink`、QoS 表測試、CDR 往返測試、服務請求標頭測試；以日誌驗證完整流程 | SITL 上發現 → 遙測 → 命令 → 回報 → shutdown 端到端通過 |
| **M4 QML 主介面** ✅ | `UiBridge`、models；F-01～F-07、F-09、F-10、F-12 | 逐項對照網頁版行為 |
| **M5 地圖** ✅ | `MapView.qml`、航向箭頭、軌跡、自動跟隨 | F-08 對等 |
| **M6 網路設定與除錯工具** ✅（Wi-Fi 位址切換待實機） | 首次設定精靈、Wi-Fi 位址變化處理、`DebugInspector` | F-11、F-13；乾淨 Windows 不編輯檔案即可連上現場 Agent |
| **M7 Windows 安裝程式（MVP）** 🟡 ZIP 完成，NSIS 待安裝工具 | CPack NSIS、windeployqt、MSVC runtime、防火牆說明 | 乾淨 Windows 安裝後可直接連線 Agent，可開始現場試用 |
| **M8 macOS** 🟡 設定完成，待 Mac 建置與簽章 | Universal Binary、`NSLocalNetworkUsageDescription`、簽章與公證 | 乾淨 macOS 可連線 |
| **M9 CI／Release** 🟡 workflow 完成，待遠端儲存庫 | windows、macos、linux workflow；Release 附件 | tag 後自動產生安裝檔 |

### 6.1 M1 互通驗證結果（2026-10-12，全部通過）

**環境**：Windows 11 原生 `dds_probe`（CycloneDDS 0.10.5）↔ Docker Desktop 容器
`taiphoon-agent:v1.3.0-rc.4-runtime-amd64`（ROS2 Humble、`rmw_cyclonedds_cpp`）+ `px4io/px4-sitl:latest`（SIH，
與 Agent 共用網路命名空間）。

| ADR-0014 項目 | 結果 | 證據 |
|---|---|---|
| CDR 網路格式 | ✅ | 單元測試確認 XCDR1 對齊與 service 請求標頭位置；容器內 `ros2 topic echo` 正確解碼 dds_probe 發布的 `AgentState`，全部欄位與送出值一致 |
| 1. 訂閱與解碼 | ✅ | `echo`：state、attitude 各 9.9 Hz；GPS 等於 PX4 home、`px4_state_valid=1`、四元數已正規化 |
| 2. 命令發布 | ✅ | 單機與全體 `GCS_PROBE_NOOP` 各配對 2 個訂閱者；Agent 日誌記錄 `Received ... command` 與 `Unknown command received`，無任何動作 |
| 3. DCPSPublication 發現與消失 | ✅ | 列出 Agent 全部 writer 與正確型別名稱；shutdown 後 `rt/uav_1_agent/state` 等 topic 消失 |
| 4. shutdown service | ✅ | Agent 回應 `success=1`、`關閉請求已接受；已排程終止容器`，以 client_guid 與序號比對成功；容器隨後結束 |
| 5. 型別資訊配對 | ✅ | `NO_TYPE_INFO` 型別與 rmw_cyclonedds 端點正常配對；Agent 端顯示 GCS reader 為 `_CREATED_BY_BARE_DDS_APP_`，QoS 與介面文件一致 |

**驗證中發現的事實**（M3 之後的實作需納入）：

- Agent 在收到 PX4 odometry 前**不發布** `/{ns}/state`；只有 Agent 容器、沒有飛控時，GCS 能發現 topic 但收不到狀態。
  機隊判斷「線上」不能只看 topic 是否存在（網頁版 FleetModule 以 5 秒 stale 判斷，需保留）。
- Windows 防火牆預設阻擋 GCS 程式的入站 UDP。M7 安裝程式需處理防火牆規則或引導使用者允許。
- 原生程式與 Docker Desktop 內的 Agent 互通需要特別設定（見下方），一般現場部署（Agent 在其他主機）不需要。

**Docker Desktop 測試環境設定**（Windows 防火牆不需修改）：

- Agent 容器：CycloneDDS 單播，`ExternalNetworkAddress` 設為主機區網 IP，發布 UDP 7410–7421，
  Peer 為 `localhost` 與 `192.168.65.254:7510`（`host.docker.internal` 與 GCS 的 participant index 50）。
- dds_probe：`--interface <主機區網 IP> --external-address 192.168.65.254 --no-multicast --peer <主機區網 IP>
  --participant-index 50`。容器回送封包經 Docker Desktop 轉送至主機 loopback，因此不受防火牆影響。

### 6.2 M2 核心移植結果（2026-10-12）

- `gcs_core`（QtCore）：核心型別、`IAgentLink`、`command_protocol`、五個模組；`gcs_wiring`：組裝根。
- 轉移表移至 `modules/guard/guard_rules.cpp`，規則逐字沿用網頁版；GuardModule 只負責狀態追蹤與錯誤訊息。
- 測試 82 項全數通過，包含：轉移表 `FlightState` × `CmdType` 全組合（規則層與模組層各一套）、
  網頁版所有 Guard 情境、組裝根整合測試（被 Guard 拒絕的命令一律不會到達 `IAgentLink`）。
- 移除：EventBus、WebSocket、`ws_protocol`、MapModule、DebugTerminalModule、舊主程式與對應測試。

與網頁版的行為差異（皆為刻意調整）：

| 項目 | 網頁版 | Qt 版 |
|---|---|---|
| 命令發布失敗 | 首次對某 Agent 發命令時才建立 publisher，尚未配對即發布，命令可能遺失但仍回報成功 | `IAgentLink::publish_command` 失敗（例如尚無訂閱者）時回報錯誤；M3 的 link 實作需在 Agent 上線時預先建立 writer |
| 離線後的殘留資料 | 取消訂閱後不再收到 | 佇列中殘留的資料也會被 TelemetryModule 丟棄，不會讓已離線 Agent 重新出現 |
| Fleet 名稱過濾 | 依 topic 正規表示式 | link 回報的名稱再經 `is_single_agent` 過濾，`fmu1_agent`、`uav_0_agent` 等一律忽略 |
| shutdown 逾時後的回應 | 依計時器清除 | 以請求識別碼比對，逾時後或屬於其他請求的回應一律忽略 |

### 6.3 M3～M9 結果（2026-10-12）

**M3 通訊層**
- `CycloneAgentLink`：DCPSPublication 探索、Agent 上線時預先建立命令 writer 與 shutdown 端點、
  發布前確認已有訂閱者、service 回應以 client 識別碼與序號比對、網路重建時保留已訂閱 Agent。
- `CoreRuntime`：核心執行緒、link 與模組的建立、重建與釋放（釋放前先把物件移回主執行緒）。
- `tools/gcs_headless`：對 Mac 上的兩台 Agent（方案 A）驗證探索、約 10 Hz 遙測，以及 Guard 依真實狀態
  拒絕 TAKEOFF（未解鎖）、MODE_RTL（閒置）、全體 ARM（非 Offboard）與單機編隊。
- 端到端飛行測試（方案 A：Windows GCS ↔ Mac 上的 SITL，`uav_2_agent`，2026-10-12，開發者同意後執行）：

  | 步驟 | 結果 |
  |---|---|
  | MODE_OFFBOARD | 成功；PX4 `nav_state` 4 → 14 |
  | ARM | 成功；任務階段 IDLE → ARMED |
  | TAKEOFF 3（間隔 22 秒後） | Guard 拒絕：PX4 已自動上鎖，「PX4 原生狀態不允許此命令」，未送出 |
  | ARM → 3 秒後 TAKEOFF 3 | 成功；TAKEOFF → HOVER，穩定於 3.00 m |
  | MODE_LAND | 成功；LAND → 落地 → 自動上鎖 → IDLE |

  過程中 Guard 依 Agent 實際狀態放行與拒絕，未發生錯誤放行。PX4 解鎖後若未及時起飛會自動上鎖，
  操作上 ARM 與 TAKEOFF 應連續執行。

**M4 介面、M5 地圖、M6 網路與除錯**
- `UiBridge`（只發出命令與關閉兩種請求）、`FleetModel`、`ToastModel`、`AppSettings`（QSettings）、
  `NetworkController`（站點設定、每 5 秒檢查本機位址並重建 DDS）、`DiagnosticsController`（唯讀）。
- QML：狀態列、飛行控制四分頁與 L1／L2、機群視窗、姿態儀、提示訊息、進階設定、網路設定（首次啟動精靈）、
  除錯工具、地圖（OSM、航向箭頭、名稱、軌跡、自動跟隨、縮放與朝北、來源標示）。
- 以無畫面模式連線 Mac 上的 Agent 截圖確認版面與資料。

**M7～M9 打包與 CI**
- `cmake/Packaging.cmake`：Qt 部署（windeployqt／macdeployqt）、CycloneDDS、MSVC 執行階段（app-local）、
  第三方授權聲明；CPack 產生 Windows ZIP（有 NSIS 時另產生安裝程式，並建立／移除防火牆規則）、
  macOS DMG、Linux TGZ。
- 已在乾淨 PATH 下執行安裝後的程式（煙霧測試通過）；ZIP 約 45 MB，展開約 116 MB。
- `.github/workflows/qgcs-build.yml`：三平台建置、測試、打包；`v*` 標籤建立草稿 Release。

單元測試共 116 項全數通過。

**與網頁版的差異（刻意調整）**

| 項目 | 網頁版 | Qt 版 |
|---|---|---|
| 全體模式按鈕可用狀態 | 只檢查每台 PX4 狀態有效 | 與 Guard 相同：每台都必須允許該命令 |
| L2 滑桿 | 點擊軌道任意位置即可開始拖曳 | 必須從滑塊開始拖曳，避免點近終點時誤觸 |
| 除錯終端機 | 在容器執行 `ros2` CLI | 內建唯讀指令（topic list／echo、agents、events），拒絕 pub 與 call |
| 結束 GCS | 停止容器並通知所有網頁 | 一般程式結束，不發送任何 Agent 命令 |
| 連線燈號 | WebSocket 連線狀態 | DDS 是否啟動，提示文字顯示網路設定 |
| 網路設定 | Go 啟動器的設定頁 | 程式內首次設定精靈與「網路設定」，保存後立即重建 DDS |

## 7. 測試策略

- 單元測試集中於根目錄 `test/`，使用 Google Test + QtTest（`QSignalSpy`），以 CTest 註冊。
- Guard：`FlightState` × `CmdType` 全組合、Arm／Takeoff 的 PX4 前置條件、全體命令任一台不符即拒絕。
- 組裝測試：未經 Guard 允許的請求不得讓 `FakeAgentLink` 發布命令。
- 新增：`msg2idl` 結果與 `msg/` 欄位一致、QoS 表與介面文件第 8 節一致、CDR 往返、服務請求標頭編解碼。
- 互通測試：對 PX4 SITL + Taiphoon_Agent 驗證訂閱、命令、發現與 shutdown，情境參考 Taiphoon_GCS 的 `.scratch/gcs-v1/test_e2e.py`。
- QML 互動（L1／L2）可視需要使用 Qt Quick Test。

## 8. 平台注意事項

- **Windows**：MSVC；首次執行出現防火牆提示；未簽章時 SmartScreen 會警告。
- **macOS**：Universal Binary 需以 `CMAKE_OSX_ARCHITECTURES="x86_64;arm64"` 建置所有相依；
  需 Apple Developer 帳號簽章與公證；`Info.plist` 需 `NSLocalNetworkUsageDescription`，
  使用 multicast 時另需 `com.apple.developer.networking.multicast` entitlement。
- **Linux**：第一版用於開發與 CI，發布格式待定。
- **互通測試環境**：Windows 上的 Agent SITL 在 Docker Desktop NAT 之後，原生 GCS 無法以 multicast 發現；
  需使用區網 Linux 主機／VM 或 host networking。

## 9. 開發環境

| 項目 | 狀態（2026-10-12） |
|---|---|
| Qt | 安裝 **6.11.2** 中（`msvc2022_64`、Qt Location、Qt Positioning）；6.12.0 已安裝但不使用 |
| 編譯器 | VS 2026 Build Tools（MSVC 14.50），與 msvc2022 版 Qt 相容 |
| CMake、Ninja | `D:\Qt\Tools\CMake_64`、`D:\Qt\Tools\Ninja` |
| Python | 3.14（`msg2idl.py` 使用） |
| NSIS | M7 再安裝 |

預定建置指令（在 VS 開發者命令提示字元中執行，M0 建立 Presets 後改為 `cmake --preset`）：

```powershell
D:\Qt\6.11.2\msvc2022_64\bin\qt-cmake.bat -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## 10. 待決事項

- [x] 正式發布使用的圖磚來源：先使用 OSM 官方圖磚（2026-10-12 決定），用量增加時再評估。
- [x] 端到端飛行命令測試：已完成（第 6.3 節）。
- [ ] 安裝 NSIS 後產生並驗證 Windows 安裝程式（含安裝、升級、移除與防火牆規則）。
- [x] 安裝程式自動建立防火牆入站規則：限本程式、UDP、只允許同一子網路來源（2026-10-12 決定）。
- [ ] macOS 實機建置、Local Network 權限與簽章公證。
- [ ] 實際執行 CI（目前僅能手動觸發，預設只建置 Windows）。
- [ ] Wi-Fi 位址切換時自動重建 DDS 的實機驗證。
- [ ] Apple Developer 帳號與 Windows 程式碼簽章憑證。
- [ ] Linux 發布格式。
- [x] Git 遠端：https://github.com/Rex-Taiphoon/Taiphoon_QGCS.git（尚未推送）。
- [x] 版本號起點：v1.0.0。
- [ ] 版本標籤、分支與 Release 標準（原 Taiphoon_GCS 標準已移除，M9 前重新制定）。

## 11. 參考資料

- Agent 介面：`../Taiphoon_Agent_Interfaces/taiphoon-agent-interface.md`
- 網頁版功能對照：`../Taiphoon_GCS/`（提交 `1350831`）
- QGC 開發者入門：<https://docs.qgroundcontrol.com/master/en/qgc-dev-guide/getting_started/index.html>
- QGC workflows：<https://github.com/mavlink/qgroundcontrol/tree/master/.github/workflows>
