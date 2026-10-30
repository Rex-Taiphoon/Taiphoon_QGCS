# ADR-0020：航點任務改由 Agent 執行、錯誤回報與重新啟動飛控

**日期**：2026-10-30
**狀態**：已接受（介面已併入 `Taiphoon_Agent_Interfaces` 介面文件 2.0）
**相關**：ADR-0003（Guard 狀態轉移表）、ADR-0014（CycloneDDS 線上相容）、ADR-0015（signal／slot 模組組裝）、
ADR-0018（單機航點任務與語音口令；本 ADR 取代其「任務執行」一節）

## 背景

ADR-0018 的航點任務由 QGCS 逐段送出 `MOVE_POINT`，實際使用發現：

- 航點位置依賴「planner 原點 ≈ PX4 local 原點」，Agent 在空中重新啟動時航點會整體偏移。
- 到達判斷只能從 `AgentState` 位置推測，定位跳動、資料停滯都要另外防範。
- QGCS 斷線或關閉時任務就停在半途，無人機停在最後一段的目標。

2026-10-30 試飛時無人機無法解鎖，QGCS 也無法判斷飛控狀態：Agent 不回報 PX4 拒絕原因與連線健康狀態，
`px4_state_valid` 為 false 時操作人員沒有任何處理方式。

開發者決定（2026-10-30）：

- 航點座標交給 Agent，由 Agent 執行任務；QGCS 不再保留逐段 `MOVE_POINT` 的執行器。
- 不考慮與舊版 Agent 的相容，QGCS 與 Agent 一起升級。
- QGCS 斷線時任務照常繼續。
- 任務中送出 `HOLD` 視為中止任務並停在目前位置（Agent 端行為）。
- Agent 要把錯誤訊息回報給 QGCS，並新增重新啟動飛控命令；飛控狀態不明時允許強制重新啟動，以滑桿確認。

## 決策

### 介面

Agent 端的修改規劃由 QGCS 提出，2026-10-30 併入介面文件 2.0（第 3.9～3.13、4.3、6.2 節），
訊息檔已同步至 `msg/msg/`。

| Topic | 型別 | 方向 | QoS |
|---|---|---|---|
| `/uav_N_agent/mission/plan` | `WaypointMission`（含 `MissionWaypoint` 序列） | GCS → Agent | reliable、volatile、depth 10 |
| `/uav_N_agent/mission/status` | `WaypointMissionStatus` | Agent → GCS | reliable、transient_local、depth 1 |
| `/uav_N_agent/events` | `AgentEvent` | Agent → GCS | reliable、volatile、depth 20 |
| `/uav_N_agent/health` | `AgentHealth` | Agent → GCS | reliable、volatile、depth 1 |

| 命令 | 參數 | 說明 |
|---|---|---|
| `MISSION_START <mission_id>` | 1～4294967295 的整數 | 開始已載入的任務 |
| `MISSION_STOP` | 無 | 停止任務並停在目前位置 |
| `REBOOT_FC [0\|1]` | 省略或 0 為一般、1 為強制 | 重新啟動飛控（PX4），不是關閉 Agent |

- 三個命令都只能指定單一 Agent，命令協定驗證時拒絕 `all`。
- 整數參數一律以十進位整數輸出（例如 `MISSION_START 4000000000`），不使用指數表示。
- `mission/plan` 使用 volatile：Agent 重新啟動後不會重新載入舊任務。沒有訂閱者時寫入會直接遺失，
  所以 `CycloneAgentLink` 在沒有配對的 reader 時直接回報失敗（舊版 Agent 也是這個結果）。

### 任務流程（MissionModule）

1. `MissionController` 仍只送出開始與停止兩種請求（ADR-0018 的介面與語音流程不變）。
2. `MissionModule` 產生非 0、且與上一個任務不同的隨機 `mission_id`，組成任務：高度基準為 PX4 local
   （`altitude_frame=1`，相對高度）、每點停留 3 秒、`yaw_deg` 為 NaN（機頭由 Agent 決定）。
3. 上傳經 Guard：`MissionModule.mission_upload_requested → GuardModule.request_mission_upload`，
   通過後 `GuardModule.mission_upload_validated → CommandModule.publish_mission_plan → IAgentLink`。
   上傳結果走獨立的 `mission_upload_acked`，不會出現在一般命令的「已送出」提示。
4. 收到同一 `mission_id` 的 `LOADED` 後送出 `MISSION_START <id>`（經 Guard 再檢查一次懸停），
   之後等待 `RUNNING`。兩個階段各等 3 秒，逾時即判定開始失敗。
5. 開始後的進度**只依 `mission/status`**：`RUNNING` → MOVING、`DWELLING` → DWELLING，
   `COMPLETED`、`ABORTED`、`REJECTED` 依 Agent 回報的原因代碼顯示中文說明。
   QGCS 不再訂閱位置判斷到達，也不自行決定中止；中止條件（操作人員介入、PX4 狀態失效、單段逾時等）由 Agent 判斷。

任務階段新增 STARTING（上傳到開始之間）與 LOST：

- Agent 離線、狀態停滯，或超過 5 秒沒有收到任務狀態時轉為 LOST，不送出任何命令；Agent 會繼續執行任務，
  恢復收到狀態後繼續追蹤。
- 收到未追蹤任務的 `RUNNING`／`DWELLING`（QGCS 重新啟動、晚加入、其他地面站開始）時接手追蹤；
  `mission/status` 為 transient_local，訂閱後即可收到最後一筆。
- 追蹤中 Agent 回報 `IDLE` 視為任務結束（Agent 可能已重新啟動）。
- 停止任務送 `MISSION_STOP`，經 Guard；開始中按停止時本機先標示中止，同時送出 `MISSION_STOP`。

### Guard

- 上傳檢查（`check_mission_upload`）：PX4 狀態有效、目前允許 `MISSION_START`（懸停）、`mission_id` 非 0、
  高度基準為 PX4 local、航點 1～50 個、GPS 有效；每個航點的經緯度與高度為有限值、高度 1～120 公尺、
  停留 0～60 秒、`yaw_deg` 為 NaN、與目前位置的水平距離不超過 1 公里。
- 狀態轉移表：HOVER 允許 `MISSION_START`；`MISSION_STOP` 在每個狀態都允許，PX4 狀態失效時也允許，
  確保操作人員隨時能停止任務。
- `REBOOT_FC` 不放進狀態轉移表，由 `check_reboot` 判斷：
  - 一般重新啟動：PX4 狀態有效、未解鎖且已落地。
  - 強制重新啟動（`REBOOT_FC 1`）：**只在 PX4 狀態不明時**允許（沒有 AgentState，或 `px4_state_valid=false`）；
    PX4 狀態有效時拒絕並要求使用一般重新啟動（Agent 端在 PX4 狀態有效時 force 與一般相同，QGCS 不送出以免混淆）。
  - 強制重新啟動需已收到 `health`，且下列任一顯示可能在空中就拒絕（在空中重新啟動飛控會讓無人機失去動力）：
    - QGCS 記住的「最後一次 PX4 狀態有效時」的 AgentState 為已解鎖或未落地。Guard 與 UiBridge 各自保存，
      Agent 離線後仍保留，GCS 關閉時清除；從未有效時不限制。
    - `health.px4_arming_state` 為 ARMED（Agent 在 PX4 逾時後保留最後收到的值）。
    - AgentState 保留的 `armed` 為 true，或 `landed` 為 false 且 `health.px4_land_detected_age_s` ≥ 0
      （從未收到時兩者都是 false，需以 health 區分）。
  - PX4 狀態不明時 Agent 的 `flight_mode` 一律是 ERROR，無法判斷是否在空中，因此不採用（原先的設計）。
  - 斷線後遙控器仍可能解鎖，最終保護是 PX4 解鎖時拒絕重新啟動（PX4 Commander 的重新啟動分支檢查 `!isArmed()`）。
- Agent 收到第一筆 odometry 前不發布 AgentState，只發布 `health`：`FleetModule` 以 AgentState 或 `health`
  任一資料流判斷在線，Guard 對這種 Agent 只接受強制重新啟動，其餘命令仍因 PX4 狀態不明而拒絕。

### 錯誤事件與健康狀態

- `TelemetryModule` 只轉發已訂閱 Agent 的 `events` 與 `health`（`agent_event_received`、`health_updated`），
  `gcs_headless` 輸出 `[event]` 與 `[health]` 行。
- 事件提示：錯誤紅色、警告黃色，標題為「UAV_N＋中文事件名稱」，內容附相關命令、Agent 的說明與 PX4 結果碼；
  一般資訊只寫入日誌。同一 Agent、代碼與命令在 10 秒內重複時只提示一次。
- 預檢未通過（303）不去抖動，正常降落自動上鎖的瞬間可能出現一筆：收到後先等 `health`
  （2.5 秒，約兩筆），預檢恢復就只寫日誌，仍未通過或沒有收到 `health` 才提示（介面文件事件代碼表）。
- 健康狀態：機群面板選取單機的明細區顯示 PX4 LINK（正常／中斷 N 秒／未連線）與 PREFLIGHT（PASS／FAIL），
  機群表該列在 PX4 資料中斷或預檢未通過時於機號旁加警示標記；ARM 滑桿下方以具體原因取代「等待飛控」。
  VehicleStatus 超過 5 秒未更新視為中斷，與 Agent 判斷 `px4_state_valid` 的時限相同。
- SAFETY 頁：PX4 狀態有效時「REBOOT FC 重新啟動飛控」為一般列，按 EXEC 確認（L1）；
  狀態不明時改為紅色滑桿「滑動以強制重新啟動飛控」（L2），下方警告「飛控狀態不明，無法確認是否在地面」，
  被上述安全條件擋下時顯示原因。
- MISSION 頁：開始中顯示「上傳中」；任務狀態不明時顯示「狀態不明，Agent 繼續執行」，STOP 仍可按。

## 替代方案

| 方案 | 否決原因 |
|---|---|
| 保留逐段 `MOVE_POINT` 作為舊版 Agent 的退路 | 開發者決定不保留；兩套執行器要各自維護與測試，行為也不一致 |
| `mission/plan` 使用 transient_local | Agent 重新啟動後可能重新載入舊任務；改以 volatile 加 `LOADED` 確認 |
| QGCS 斷線時中止任務 | 開發者要求繼續執行；安全中止由 Agent 端負責 |
| 上傳即開始，不另送 `MISSION_START` | 無法在開始前確認 Agent 收到完整航點，也無法讓 Guard 在開始時再檢查一次懸停 |
| 強制重新啟動不做任何空中判斷 | PX4 狀態不明但最後已知在空中時，誤送會讓無人機墜落 |
| 以 Agent 的 `flight_mode` 判斷是否在空中 | PX4 狀態不明時 `flight_mode` 一律是 ERROR，限制形同虛設 |
| 只依 303 事件提示預檢未通過 | 正常降落上鎖時會出現一筆短暫的 303，每次降落都誤報 |
| 航點高度使用 WGS84 海拔 | 維持 ADR-0018 的相對高度，操作人員輸入習慣不變；`altitude_frame` 保留日後擴充 |

## 後果

- QGCS 必須搭配支援航點任務的 Agent；舊版 Agent 上傳時回報「Agent 沒有訂閱航點任務」，任務無法開始。
- Agent 端另有 QGCS 不檢查的條件：開始任務需 PX4 為 Offboard，`REBOOT_FC` 送出後 15 秒冷卻；
  不符時由 Agent 以 `REJECTED` 或事件 102 回報。
- planner 原點的換算移到 Agent 端（ADR-0018 後果第一點），QGCS 端不再有這個假設。
- ADR-0018 的「任務執行（核心）」一節由本 ADR 取代；介面、語音與航點編輯的決策不變。
- 最後已知在空中（已解鎖或未落地）而 PX4 狀態不明時無法強制重新啟動；需先確認無人機確實在地面，
  再由 Agent 端處理（例如重新啟動 Agent），或重新啟動 QGCS 清除記憶後再送出。
- 已與 PX4 SITL 及新版 Agent 互通驗證上傳、執行、斷線繼續、停止與 Guard 拒絕；SITL 對 `REBOOT_FC`
  一律回 DENIED（POSIX 版 PX4 沒有重新啟動功能），實際重新啟動與「解鎖時 PX4 拒絕」只能在實機驗證。
