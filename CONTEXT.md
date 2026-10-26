# Taiphoon_QGCS

地面控制站 (Ground Control Station) 的 Qt 原生版本，搭配 Taiphoon_Agent 使用，提供桌面介面進行無人機監控與控制。

## 語言

**地面控制站 (Ground Control Station, GCS)**：
透過 DDS 網路與 Taiphoon_Agent 通訊的桌面應用程式。功能涵蓋狀態監控、地圖顯示、命令發送與編隊控制。以 Qt 6 + QML 實作，內嵌 CycloneDDS，安裝後即可執行。
_Avoid_：地面站、地面控制系統、command center

**ROS2 線上相容 (ROS2 Wire Compatibility)**：
GCS 不連結 ROS2 函式庫，但在 DDS 網路上產生與 ROS2 Humble + `rmw_cyclonedds_cpp` 相同的 topic 名稱、型別名稱、QoS 與 service 格式，使 Agent 視其為一般 ROS2 端點。
_Avoid_：ROS2 節點（GCS 不是 rclcpp 節點）、ROS bridge

**Agent 連結 (Agent Link)**：
核心模組與 Agent 通訊的唯一介面 `IAgentLink`。正式實作為 `CycloneAgentLink`，測試使用 `FakeAgentLink`。
_Avoid_：通訊模組、DDS wrapper

**狀態聚合 (State Aggregation)**：
TelemetryModule 訂閱所有 Agent 的 `/uav_{N}_agent/state`、`/attitude` 等 topic，將分散的 Agent 狀態統整為單一機隊視圖提供給 UI。
_Avoid_：telemetry collector、狀態收集器

**命令轉發 (Command Relay)**：
CommandModule 將 Guard 驗證通過的命令，依目標（單機或全機隊）路由至 `/uav_{N}_agent/command` 或 `/uav_all_agent/command`。只做路由與格式驗證，不修改命令語意。
_Avoid_：命令代理、command proxy

**防呆 (Guard)**：
命令發送前的安全檢查機制。確認當前狀態允許該命令（例如 Takeoff 前 PX4 須為 Offboard、已落地且已解鎖），阻擋邏輯衝突的操作。
_Avoid_：safety check、驗證（防呆是特定概念，不等同一般驗證）

**組裝根 (Composition Root)**：
`src/app/gcs_wiring.cpp`，唯一建立模組間 signal／slot 連線的位置，取代網頁版的 EventBus。
_Avoid_：事件匯流排、message bus

**介面橋接 (UiBridge)**：
QML 與核心模組之間的唯一通道。將核心 signal 轉為 QML 可用的屬性與 model，並把操作送往 Guard 或 Lifecycle，不得直接發布 DDS 資料。
_Avoid_：WebSocket 橋接、controller

**核心執行緒 (Core Thread)**：
所有功能模組所在的 `QThread`。模組在此依事件順序單執行緒處理，與 UI 主執行緒、DDS 執行緒以 queued connection 溝通。
_Avoid_：背景執行緒、工作執行緒（未特指時）

**機隊 (Fleet)**：
GCS 目前管理的所有無人機集合。對應 DDS 網路上所有 `rt/uav_*_agent/state` 的發布者，成員由 DDS 發現動態決定。
_Avoid_：swarm、叢集

**狀態轉移表 (Transition Table)**：
GuardModule 的核心資料結構，以查表方式驗證「當前 FlightState 是否允許執行指定 CommandType」。資料驅動設計，新增狀態或命令僅需修改此表。
_Avoid_：狀態機、state machine（狀態轉移表是實作方式，狀態機是概念）

**防呆層級 (Guard Level)**：
UI 關鍵指令的互動防護分級。L1 為兩步驟確認對話框（Takeoff、RTL、Land、Disarm），L2 為長按滑桿確認（Arm）。不設 L3。
_Avoid_：safety level（防呆是 UI 互動概念，safety 是後端驗證概念）

**站點設定 (Site Config)**：
連線現場 Agent 所需的網路參數：Agent 探索入口、網卡、Domain ID、participant index 與是否使用 multicast。由首次設定精靈建立並保存，用於產生 CycloneDDS 設定。
_Avoid_：設定檔（未特指時）、config

**地圖顯示位置 (Display Origin)**：
把 Agent 回報的 WGS84 位置平移到實際場地的顯示設定，用於 UWB 定位原點固定在別處的情況。只影響地圖、軌跡與機群顯示；命令、Guard 與除錯工具一律使用回報座標。
_Avoid_：GPS 偏移、修改 GPS（實際位置沒有改變）
