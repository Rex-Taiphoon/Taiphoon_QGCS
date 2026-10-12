# ADR-0003：Guard 狀態機採用資料驅動轉移表

**日期**：2026-07-30
**狀態**：已接受

## 背景

GuardModule 需根據 Agent 的任務階段 `flight_mode` 驗證命令合法性。未來 flight_mode 枚舉可能增減
（Agent 端新增狀態），狀態轉移邏輯必須易於維護。

新版 `AgentState` 另提供已確認有效的 PX4 原生狀態：`armed`、`landed`、`px4_nav_state`
與 `px4_state_valid`。Arm 與 Takeoff 是安全關鍵命令，不能由 Agent 的任務階段或命令意圖推論；
例如 PX4 已處於 Offboard、未解鎖時，任務階段仍可能是 `IDLE`。

## 決策

採用 **資料驅動轉移表（Data-Driven Transition Table）**，而非 hard-coded if-else 或 switch。

- 轉移表為 `std::unordered_map<FlightState, std::unordered_set<CommandType>>`，記錄每個狀態下允許的命令
- 除 Arm 與 Takeoff 外，驗證時查表：`allowed_commands[agent.current_state].contains(command.type)`
- Arm 僅在 `px4_state_valid=true`、PX4 為 Offboard、已落地且未解鎖時允許
- Takeoff 僅在 `px4_state_valid=true`、PX4 為 Offboard、已落地且已解鎖時允許
- 全體模式對每一台線上 Agent 套用相同條件；任一台不符合即拒絕整個命令
- 新增狀態或命令時需修改轉移表；新增安全關鍵命令時，
  也需新增明確的 PX4 原生狀態規則
- 轉移表集中定義在單一檔案（如 `guard/transition_table.cpp`），附完整註解

## 替代方案

| 方案 | 否決原因 |
|------|----------|
| hard-coded if-else 鏈 | 新增狀態需改動多處，易漏改 |
| 外部設定檔（JSON/YAML） | 狀態轉移是安全關鍵邏輯，應在編譯期驗證；<br>外部檔案可能被誤改 |

## 後果

- 任務階段安全規則集中於轉移表，Arm／Takeoff 的 PX4 原生狀態規則集中於 GuardModule
- 單元測試可直接對轉移表做完整覆蓋（每個狀態 × 每個命令），
  並覆蓋 Arm／Takeoff 的 PX4 前置條件
- 轉移表與 AgentState 的 FlightState 枚舉耦合——Agent 端變更時需同步更新
