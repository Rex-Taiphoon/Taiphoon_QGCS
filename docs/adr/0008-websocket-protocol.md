# ADR-0008：WebSocket JSON 全量推送協定

**日期**：2026-08-06
**狀態**：已取代（ADR-0015；Guard 與 PX4 狀態欄位規則仍有效）

## 背景

C++ 後端需透過 WebSocket 與前端雙向通訊。需定義訊息格式與推送策略。

## 決策

採用 **JSON 格式 + 型別欄位分派 + 全量推送**。

- 每筆 ROS2 AgentState/AgentAttitude 收到後立即轉發前端，不做節流
- `position_global` 保留 ROS2 的 `float64` 精度；非有限值轉為 `null`，不得降為 `float32`。
- 前端可傳送 `{ "type": "gcs_shutdown_request", "confirmed": true }`；
  後端僅接受布林 `true`，透過核心 EventBus 的 `gcs_shutdown_request` 觸發正常退出。
  關閉前廣播 `{ "type": "gcs_shutdown_notice" }` 至所有已連線前端，前端停止自動重連。
  通知表示請求已接受，不表示容器停止已驗證。主程式預留 500 毫秒供非同步通知傳送，
  再停止 WebSocket、模組與 ROS2。此請求不發送 Agent 命令；GCS 退出後容器停止；
  啟動器以 `--rm` 建立容器，停止後自動刪除。
  容器 entrypoint 與完整套件啟動器直接以 GCS 執行檔作為 PID 1，
  不透過 `ros2 run` 包裝程序；缺少工作空間時以錯誤碼退出，不留在互動 shell。
- 訊息結構：`{ "type": "<msg_type>", ...payload }`
- 後端→前端：state_update、attitude_update、fleet_update、ned_target、waypoints、
  trajectory_update、cmd_ack、ros2_exec_response
- 前端→後端：cmd_request、subscribe、agent_shutdown_request、ros2_exec_request
- 命令請求採非同步 ack 模式：前端發 cmd_request → 後端經 GuardModule 驗證 → 發布 ROS2 → 回應 cmd_ack
- `state_update.state` 應原樣包含 Agent 的 `armed`、`landed`、`px4_nav_state` 與
  `px4_state_valid`；GCS 不得由使用者命令推論飛行狀態。
- `agent_shutdown_request` 僅允許單一 `uav_{N}_agent`，格式為
  `{ "type": "agent_shutdown_request", "agent": "uav_{N}_agent" }`。
  後端以 ROS2 `std_srvs/srv/SetBool` 呼叫
  `/{agent}/shutdown`（`data: true`），並以 `cmd_ack`、`command: "AGENT_SHUTDOWN"`
  非同步回覆結果。此功能不要求授權碼，操作人員需自行確認飛行狀態。
- `trajectory_update` 承載 EGO-Swarm `AgentTrajectory` 的 WGS84 控制點，
  前端以折線繪製於地圖。
- `ros2_exec_request` 格式為
  `{ "type": "ros2_exec_request", "request_id": "...", "command": "ros2 ..." }`，
  僅允許以 `ros2` 開頭的指令；後端在容器內以 timeout 執行，並以
  `ros2_exec_response` 回傳合併輸出與執行狀態。

## 替代方案

| 方案 | 否決原因 |
|------|----------|
| 批次節流推送 | Agent 上限 10Hz，全量無效能問題 |
| 增量推送 | 實作複雜，除錯困難，收益有限 |
| 二進位協定 (MsgPack/Protobuf) | JSON 在 10Hz × 5 台內頻寬可控，人類可讀性優先 |

## 後果

- 前端需處理高頻更新（React 狀態更新需注意 render 效能）
- JSON.stringify/parse 為訊息邊界，除錯時可直接閱讀
