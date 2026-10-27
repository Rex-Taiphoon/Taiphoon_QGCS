# ADR-0018：單機航點任務與語音口令

**日期**：2026-10-26
**狀態**：已接受
**相關**：ADR-0003（Guard 狀態轉移表）、ADR-0015（signal／slot 模組組裝）、ADR-0016（Qt Location 地圖）

## 背景

開發者要求：在地面站預先設定每台無人機的航點，懸停中的單機說出「請執行任務」後自動依序飛完。
已定案的需求為：

- 每台無人機各自一組航點；只支援單機控制，機群模式不支援。
- 不包含解鎖與起飛，無人機必須在懸停狀態才能開始。
- 每個航點停留 3 秒，最後一個航點到達後懸停。
- 離線語音辨識（sherpa-onnx 關鍵詞偵測）、按住說話、辨識後必須再確認。

Agent 命令協定沒有航點清單命令，只有單點的 `MOVE_POINT x y z yaw`；`/waypoints` 主題已標示為舊介面，
不能作為控制來源。查閱 Agent 原始碼（`ego_swarm_planner_node.cpp`）得知：

- `MOVE_POINT` 的座標是 planner-local NED，原點是 planner 啟動後第一筆有效狀態的 WGS84 位置，該原點不對外發布；
  它與 `AgentState.position_ned`（PX4 local）之間只差一個啟動時決定的固定平移。
- 單機相對移動（`MOVE_FORWARD` 等）以已保存的目標為基準累加，不能用來「停在目前位置」。

## 決策

### 任務執行（核心）

- 新增 `MissionModule`（`src/modules/mission/`）在核心執行緒依序送出 `MOVE_POINT`，**每一段都經 `GuardModule`**；
  組裝根新增 `MissionModule.command_requested → GuardModule.request_command`，不直接連到 `CommandModule`。
- 航點以 Agent 回報的 WGS84 保存；送出時以「目前 `position_ned` + 目前位置到航點的北／東位移」換算，
  高度以相對高度（`-position_ned[2]`）表示，機頭維持任務開始時的方向。
  此換算假設 planner 原點與 PX4 local 原點相同（Agent 在起飛點啟動時成立）。
- 到達判斷：與預期位置相距 1 公尺內並持續 1 秒，或「已離開該段起點 2 公尺以上且位置穩定 1.5 秒」。
  只用連續、有效的觀測：狀態超過 1 秒未更新時暫停判斷與下一段命令（SITL 實測 Agent 停止發布後，
  凍結的位置曾被當成已停止而誤判到達），恢復後重新計時；相鄰兩筆位置換算速度超過 10 m/s
  （Agent 預設最高 2.5 m/s）視為定位跳動，重新計時且跳動距離不算離開起點。
  AgentState 沒有 GPS 品質與 EKF 重設資訊，緩慢飄移無法與實際移動區分。
  SITL 實測 EGO-Swarm 執行 `MOVE_POINT` 時 `flight_mode` 維持 HOVER、不會轉為 MOVING，不能只靠任務階段。
- 開始條件：單機、懸停、PX4 狀態有效、有姿態資料；航點 1～50 個，高度 1～120 公尺，與目前位置的水平距離不超過 1 公里
  （防止顯示原點設錯時把航點放到極遠處）。
- 中止條件：其他成功送出的命令（含全體命令，視同操作人員介入）、狀態離開懸停／移動、Agent 離線、單段逾時。
  離線以機隊顯示清單為準：topic 消失，或資料停滯超過 5 秒（topic 仍在、不會發出 `agent_offline`）都算。
  中止時不自動送出任何命令。
- 操作人員停止任務時送出 **`MOVE_POINT` 目前位置**：SITL 實測 `HOLD` 只鎖住 BasicController 的 setpoint，
  planner 仍持續發布原軌跡，無人機會繼續飛往原目標。

### 介面

- 新增 `MissionController`（主執行緒）：各機航點編輯、開始前確認、任務進度與語音口令處理。
  它只對 `MissionModule` 送出「開始」與「停止」兩種任務請求（`ui_wiring.cpp` 的 `wire_mission`），不直接產生 Agent 命令。
- 開始任務一律經確認：按鈕或「請執行任務」先暫存，10 秒內按 EXEC 或說「確認」才開始；
  語音「確認」必須在提示出現 0.8 秒後才接受，同一句話連說兩個口令不算確認。
- 地圖點選的位置先依「地圖顯示位置」反向換算回 Agent 回報座標再保存。
- 航點不保存，關閉程式即清除，避免隔天被語音誤觸舊航線；該機離線時航點、任務狀態與待確認的任務也一併清除。

### 語音

- 辨識使用 sherpa-onnx 1.13.8 的 C API 與中文關鍵詞模型 `sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01`
  （fp32、epoch-12），口令與念法定義在 `src/voice/voice_keywords.hpp`；「執」「止」另列不捲舌的念法。
- sherpa-onnx 以 `QLibrary` 在執行期載入（`gcs_voice` 只用標頭中的型別），缺少檔案時語音停用，主程式照常啟動。
  建置時由 `cmake/Voice.cmake` 下載固定版本與雜湊的預編譯函式庫與模型，放在建置目錄的 `bin/voice/`。
- 收音使用 Qt Multimedia，只在按住 F2 或畫面 PTT 按鈕時開啟麥克風；未安裝 Qt Multimedia 時語音停用。
  不用空白鍵，避免觸發目前焦點所在的按鈕。

## 替代方案

| 方案 | 否決原因 |
|------|----------|
| 請 Agent 新增航點清單或 WGS84 目標命令 | 需變更 Agent 介面契約，不在本專案範圍；列為後續改善 |
| 以相對移動命令逐段飛行 | Agent 以保存目標與 target yaw 累加，誤差會累積且無法停在目前位置 |
| 停止任務送 `HOLD` | SITL 實測無人機仍飛往原目標 |
| 雲端語音辨識 | 外場常無網路，且延遲與隱私不適合飛行操作 |
| Windows／Apple 系統語音辨識 | 需維護兩到三套程式，iOS 以外平台行為不一 |
| 常駐聆聽 | 旁人交談或無線電聲音容易誤觸；按住說話較安全 |

## 後果

- 航點的實際位置依賴「planner 原點 ≈ PX4 local 原點」：Agent 若在空中重新啟動，航點會整體偏移；
  到達判斷仍可依位置穩定完成，但飛到的位置會差一個固定量。根本解法需 Agent 發布 planner 原點或支援 WGS84 目標命令。
- 2026-10-26 開發者同意：安裝程式附上語音元件與模型（Windows／Linux 放 `bin/voice/`；macOS 的動態函式庫放 bundle 的
  `Contents/Frameworks`、模型放 `Contents/Resources/voice/model`），CI 的 Qt 模組加入 `qtmultimedia`。
  只有包含 sherpa-onnx 的平台才連結 Qt Multimedia，Qt 部署腳本會一併部署其函式庫與外掛；Windows 版因此多出
  Qt 提供的 LGPL 版 FFmpeg（約 18 MB），語音元件與模型約 32 MB。Linux 執行需要 `libpulse0`。
- 收音類別 `VoiceController` 放在獨立的靜態函式庫 `gcs_voice_capture`：Taiphoon_QGCS 執行檔目標的 autogen
  步驟在 Ninja 下沒有產生 moc 的規則，執行檔內新增的 QObject 標頭不會被 moc 處理。
- 授權：sherpa-onnx 為 Apache-2.0、ONNX Runtime 為 MIT，條文隨安裝檔附於 `licenses/`；
  中文關鍵詞模型的模型說明標示 Apache-2.0，訓練資料 WenetSpeech 已由開發者取得使用許可。
- 口令辨識以 Windows 中文語音合成（Microsoft Hanhan）錄製的音檔做自動測試，並以 SITL 驗證
  「請執行任務 → 確認 → 三個航點 → 懸停」與「停止任務」；真人口音與現場噪音仍需實地測試。
