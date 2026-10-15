# Agent 修改需求：將命令執行結果回報給 GCS（CommandAck）

> **狀態**：提案，尚未併入 `../Taiphoon_Agent_Interfaces/taiphoon-agent-interface.md`
> **提出方**：Taiphoon_QGCS
> **日期**：2026-10-15
> **影響**：介面契約變更（新增訊息與 Topic），需同步更新介面文件並升版至 1.10

---

## 1. 問題

介面文件第 6.1 節的命令協定是單向的 `std_msgs/String`，Agent 不回報任何結果，所以 GCS 只能確認
「命令已寫入 DDS」，無法知道：

1. Agent 是否收到並解析成功。目前 `AgentCommandHandler` 遇到 `UNKNOWN` 時只寫日誌就丟棄。
2. Agent 狀態機是否接受。例如 `basic_controller_node` 的 `result = controller_->arm()` 回傳值沒有對外輸出。
3. PX4 是否執行。`BasicController::vehicle_command_ack_cb` 已訂閱 `fmu/out/vehicle_command_ack_v1`，
   但只處理 command 176（Offboard）與 400（ARM 的 ACCEPTED）；ARM 被拒絕時結果直接丟棄。

實際案例：PX4 SITL 健康檢查未通過時，PX4 回報 `Arming denied: Resolve system health failures first`，
GCS 卻顯示「ARM 成功」。QGCS 已先在 GCS 端改成「已送出」，並觀察 `AgentState.armed` 來判斷是否真的解鎖
（`src/app/ui_bridge.cpp`），但這只是間接推測，無法提供拒絕原因，也只適用於 ARM／DISARM。

另外，`BasicController::arm()` 只要 `send_arm_command(true)` 送出就把 `flight_state_` 設為 `ARMED`，
不等待 PX4 ack；PX4 拒絕時 `AgentState.flight_mode` 可能短暫顯示 ARMED，但 `armed` 仍是 false。
建議一併檢視（見第 5 節）。

---

## 2. 提議的介面

### 2.1 新訊息 `taiphoon_agent/msg/CommandAck`

```
# Taiphoon Agent 命令執行結果
# 發布於 /{agent_namespace}/command_ack；全體命令由每台 Agent 各自回報

# --- 結果來源 ---
uint8 SOURCE_AGENT = 0          # Agent 解析或狀態機判斷
uint8 SOURCE_PX4 = 1            # PX4 VehicleCommandAck

# --- 結果代碼 ---
uint8 RESULT_ACCEPTED = 0       # 已接受（SOURCE_PX4 時代表飛控已執行）
uint8 RESULT_IN_PROGRESS = 1    # 執行中，稍後會再回報最終結果
uint8 RESULT_REJECTED = 2       # 被拒絕（狀態不允許、PX4 DENIED／TEMPORARILY_REJECTED）
uint8 RESULT_FAILED = 3         # 執行失敗（PX4 FAILED／CANCELLED、送出失敗）
uint8 RESULT_UNSUPPORTED = 4    # 無法解析或不支援（UNKNOWN 命令、PX4 UNSUPPORTED）
uint8 RESULT_TIMEOUT = 5        # Agent 送出 VehicleCommand 後未在時限內收到 PX4 ack

builtin_interfaces/Time stamp   # 回報時間
string command                  # 正規化後的命令關鍵字，如 "ARM"、"MOVE_FORWARD"；無法解析時為空字串
string command_line             # 收到的原始命令字串，原樣回傳
bool is_broadcast               # 是否由 /uav_all_agent/command 收到
uint8 source                    # SOURCE_*
uint8 result                    # RESULT_*
uint32 px4_command              # 對應的 PX4 VehicleCommand.command；非 PX4 來源時為 0
uint8 px4_result                # PX4 VehicleCommandAck.result 原始值；非 PX4 來源時為 0
string message                  # 人可讀的說明（英文 ASCII），例如拒絕原因
```

設計重點：

- `command` 使用正規化關鍵字（相容簡寫 `FORWARD` 回報為 `MOVE_FORWARD`），GCS 以「Agent namespace ＋ command」
  比對待回應的請求，與目前 GCS 的待回應表相同，不需要在命令字串加序號，**命令協定第 6 節格式不變**。
- 一個命令可以有多筆 ack：先回報 `SOURCE_AGENT`（已接受或拒絕），涉及 PX4 的命令再回報 `SOURCE_PX4`。
  GCS 以最後一筆非 `IN_PROGRESS` 的結果作為最終結果。
- `px4_result` 保留原始值，避免日後 PX4 新增代碼時資訊遺失。
- `message` 只放 ASCII，GCS 端負責翻譯與顯示。

### 2.2 新 Topic

加入介面文件第 4.2.1 節「狀態發布（Agent → GCS）」：

| Topic 模板 | 說明 | 型別 | 頻率 | QoS |
|------------|------|------|------|-----|
| `/{agent_namespace}/command_ack` | 命令執行結果 | `taiphoon_agent/CommandAck` | 事件觸發 | reliable (10) |

加入第 8 節 QoS 表：

| 介面類型 | Reliability | Durability | Depth |
|-----------|-------------|------------|-------|
| 命令結果 Topic (`/command_ack`) | reliable | volatile | 10 |

- 使用 volatile：GCS 晚加入時不應收到過去命令的結果。
- 只由 `basic_controller_node` 發布，確保每個命令只有一個發布者。編隊命令（`FORMATION_*`）若由
  `ego_swarm_planner` 判斷，第一階段可只回報 `SOURCE_AGENT` 的解析結果，第二階段再由 planner 補充。

### 2.3 PX4 結果對應

`px4_msgs/VehicleCommandAck.result`（數值以 Agent 使用的 px4_msgs 版本為準）：

| PX4 result | CommandAck.result |
|------------|-------------------|
| `VEHICLE_CMD_RESULT_ACCEPTED` | `RESULT_ACCEPTED` |
| `VEHICLE_CMD_RESULT_IN_PROGRESS` | `RESULT_IN_PROGRESS` |
| `VEHICLE_CMD_RESULT_TEMPORARILY_REJECTED`、`VEHICLE_CMD_RESULT_DENIED` | `RESULT_REJECTED` |
| `VEHICLE_CMD_RESULT_FAILED`、`VEHICLE_CMD_RESULT_CANCELLED` | `RESULT_FAILED` |
| `VEHICLE_CMD_RESULT_UNSUPPORTED` | `RESULT_UNSUPPORTED` |

需要對應的 PX4 命令：400（ARM／DISARM）、176（DO_SET_MODE：Offboard、Hold）、20（RTL）、21（LAND），
以及 TAKEOFF 流程中送出的 VehicleCommand。

### 2.4 關於拒絕原因文字

PX4 `VehicleCommandAck` **只有結果代碼，沒有文字**。`Arming denied: Resolve system health failures first`
這類訊息來自 MAVLink STATUSTEXT／events，預設的 uXRCE-DDS 主題清單沒有輸出。因此：

- 第一階段：`message` 填寫 Agent 自己能判斷的資訊，例如 `"PX4 denied arming (result=2)"`。
- 第二階段（選做，請 Agent 端評估）：若 Agent 端的 uXRCE-DDS 有輸出 `fmu/out/failsafe_flags` 或
  `vehicle_status` 的 `pre_flight_checks_pass`，可在 ARM 被拒絕時附上未通過的檢查項目名稱。

---

## 3. Agent 端修改點

| 檔案 | 修改 |
|------|------|
| `msg/CommandAck.msg` | 新增第 2.1 節訊息 |
| `CMakeLists.txt` | `rosidl_generate_interfaces` 加入 `msg/CommandAck.msg` |
| `src/controllers/AgentCommandHandler.cpp` | `UNKNOWN` 時發布 `RESULT_UNSUPPORTED`；需要能取得發布器或回呼 |
| `src/nodes/basic_controller_node.cpp` | 命令分派後依 `result` 發布 `SOURCE_AGENT` 的 ACCEPTED／REJECTED；建立 `command_ack` 發布器 |
| `src/controllers/BasicController.cpp` | `vehicle_command_ack_cb` 對所有已送出的命令轉成 `SOURCE_PX4` ack；記錄「已送出、等待 ack」的命令，逾時發布 `RESULT_TIMEOUT` |
| 單元測試 | 解析失敗、狀態機拒絕、PX4 各結果代碼對應、逾時、全體命令每台各回報一筆 |

注意事項：

- `vehicle_command_ack_cb` 在 PX4 訂閱回呼中執行，發布 ack 前應以 `from_external`／`target_system`
  過濾非本機送出的命令，避免把其他來源（例如 QGroundControl）的 ack 誤報給 GCS。
- PX4 ack 可能比 Agent 預期的晚到或重複，請以 PX4 command id 對應「最近一次送出」的 GCS 命令。

---

## 4. 介面文件需同步修改的章節

1. 版本與更新日期：1.9 → 1.10。
2. 第 3 節：新增 `3.x CommandAck`，內容同第 2.1 節。
3. 第 4.2.1 節：新增 `/{agent_namespace}/command_ack`。
4. 第 6.1 節：補充「命令結果由 `/{agent_namespace}/command_ack` 回報；全體命令由每台 Agent 各自回報」。
5. 第 8 節：QoS 表新增 `/command_ack`。
6. 介面目錄新增 `CommandAck.msg`。

---

## 5. 建議一併檢視

- `BasicController::arm()` 在送出後立即設為 `FlightState::ARMED`。建議改為收到 PX4 ACCEPTED（或
  `vehicle_status` 顯示已解鎖）才轉移狀態，被拒絕時維持 `IDLE`，避免 `flight_mode` 與 `armed` 不一致；
  GCS 的 Guard 也是依 `flight_mode` 判斷可用命令。

---

## 6. GCS（Taiphoon_QGCS）端後續配合

介面合併後，QGCS 端需要：

1. 執行 `tools/msg2idl.py` 同步 `msg/` 並更新 `interfaces.lock.json`。
2. `IAgentLink`／CycloneDDS 相容層訂閱 `rt/<ns>/command_ack`（型別 `taiphoon_agent::msg::dds_::CommandAck_`），QoS 依第 8 節。
3. 新增共用型別與訊號，由組裝根連到 `UiBridge`：收到 `SOURCE_PX4` 的結果時，以飛控結果取代目前「已送出」與解鎖狀態推測的提示。
4. Agent 未升級（沒有 `command_ack` 主題）時，保留目前觀察 `AgentState.armed` 的做法作為後備。
