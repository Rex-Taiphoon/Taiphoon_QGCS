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

**航點任務 (Waypoint Mission)**：
單機依序飛往預先設定的航點，每點停留 3 秒，最後一點懸停。MissionModule 把整組航點上傳給 Agent（`mission/plan`），經 Guard 送出 `MISSION_START` 後由 Agent 執行，進度以 Agent 回報的 `mission/status` 為準；QGCS 斷線時 Agent 繼續執行。只能在懸停時開始，且必須確認。
_Avoid_：mission（PX4 原生的 Mission 模式，QGCS 不使用）、航線規劃

**任務狀態不明 (Mission Lost)**：
航點任務開始後，Agent 離線、資料停滯或超過 5 秒沒有任務狀態時 QGCS 的任務階段（LOST）。QGCS 不送出任何命令，Agent 仍繼續執行；恢復收到狀態後繼續追蹤。
_Avoid_：任務中止（中止是 Agent 回報的 ABORTED，任務已結束）

**重新啟動飛控 (Reboot Flight Controller)**：
`REBOOT_FC` 命令，請 Agent 重新啟動 PX4 飛控，只能指定單機。一般重新啟動需飛控狀態有效、未解鎖且已落地；飛控狀態不明時只能強制重新啟動（`REBOOT_FC 1`，滑桿確認），且最後一次飛控狀態有效時、以及 Agent 保留的最後解鎖與落地狀態都不能顯示可能在空中。
_Avoid_：關閉 Agent、shutdown（shutdown service 是結束 Agent 程序，不影響飛控）

**Agent 事件 (Agent Event)**：
Agent 在錯誤、警告或重要資訊發生時發布一次的 `events` 訊息，例如 PX4 拒絕解鎖與原因、PX4 資料中斷。與週期發布的 Agent 健康狀態（`health`：各 PX4 資料的經過時間、preflight 與 failsafe 狀態）互補；介面上錯誤與警告彈出提示，一般資訊只寫入日誌。
_Avoid_：命令回報（命令結果仍以 command ack 為準）、日誌

**語音口令 (Voice Command)**：
按住說話（F2 或 PTT 按鈕）時以離線關鍵詞偵測辨識的固定口令：請執行任務、確認、取消、停止任務。口令只觸發航點任務的開始、確認、取消與停止，不直接成為 Agent 命令。
_Avoid_：語音辨識（泛指時）、語音控制（不能用語音下任意命令）
