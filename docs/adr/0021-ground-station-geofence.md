# ADR-0021：地面站設定地理圍籬與規劃狀態顯示

**日期**：2026-11-06
**狀態**：已接受（介面已併入 `Taiphoon_Agent_Interfaces` 介面文件 3.7、3.14～3.16、4.4）
**相關**：ADR-0003（Guard 狀態轉移表）、ADR-0014（CycloneDDS 線上相容）、ADR-0015（signal／slot 模組組裝）、
ADR-0020（Agent 執行航點任務）

## 背景

Taiphoon_Agent 移除了部署參數中的地理圍籬，改由地面站在執行期設定（Agent `docs/qgcs-adaptation-geofence.md`）。
沒有 GCS 設定時 Agent 不啟用圍籬，舊版 QGCS 可以連線但沒有場域保護。同時 `PlannerStatus` 新增 `STATE_*` 常數，
`reason` 的分類（尚未就緒、正常懸停、等待同儕、到位煞停、避碰煞停）由介面文件 3.7 定義。

開發者決定（2026-11-06）：

- 指令面板新增 FENCE 分頁；地圖上點選新增頂點（加在最後一點之後）、拖曳移動、右鍵刪除，自交時邊線變紅且不能套用。
- 生效中的圍籬琥珀色實線加淡填色，margin 以內縮虛線表示；編輯中的草稿為青色；各機版本不一致時畫多數機的版本並警示。
- 地圖顯示以各機 `GeofenceStatus` 為準，不使用本地保存的內容。
- 設定與清除經 GuardModule 驗證後才發布；Guard 只預檢航點上傳，`MOVE_POINT` 不預檢
  （座標為 planner-local NED，原點不對外發布，且介面不會送出 `MOVE_POINT`）。
- 規劃狀態在選取單機時於機群表下方顯示一行中文，依 `STATE_*` 分大類，未知 `reason` 照原文顯示，不彈出提示；
  到位煞停只寫日誌，避碰煞停用警示色。
- `gcs_headless` 也能設定圍籬。

## 決策

### 介面

| Topic | 型別 | 方向 | QoS |
|---|---|---|---|
| `/uav_all_agent/geofence` | `Geofence`（含 `GeofenceVertex` 序列） | GCS → 全體 Agent | reliable、transient_local、depth 1 |
| `/uav_N_agent/geofence/status` | `GeofenceStatus` | Agent → GCS | reliable、transient_local、depth 1 |
| `/uav_N_agent/planning/status` | `PlannerStatus` | Agent → GCS | reliable、volatile、depth 10 |

`CycloneAgentLink` 保存最後一次發布的圍籬，網路重新啟動重建 writer 後重新寫入，晚加入或重新啟動的 Agent 仍能取得。

### 資料流

```
GeofenceController（UI）→ GeofenceModule.request_set／request_clear（指定 fence_id）
  → Guard.request_geofence（版本號、動作、頂點數、座標、自交、面積、margin）
  → Guard.geofence_validated → CommandModule.publish_geofence → IAgentLink.publish_geofence
Guard／CommandModule.geofence_acked → GeofenceModule → GeofenceController（失敗彈出提示）
Telemetry.geofence_status_updated → Guard（航點預檢）、GeofenceModule（各機狀況與顯示用圍籬）
Telemetry.planner_status_updated → UiBridge（機群表 PLAN 列）
```

- `fence_id` 每次設定或清除都遞增，從各機 `fence_id` 與 `last_request_id` 的最大值加 1 接續；被 Guard 拒絕的版本號不重複使用。
- GeofenceModule 依最近一次送出的設定整理各機狀況：生效、等待 Home、等待回報、未回報（5 秒）、被拒、版本不一致、無圍籬，
  並以多數機目前的版本作為顯示用圍籬（票數相同時取較新的版本）。各機版本不一致時不自動重送，由操作人員重新套用。
- 幾何計算（`src/core/geofence_geometry`）以第一個頂點為原點做局部平面近似，場域數十到數千公尺時誤差遠小於 margin；
  最終仍以 Agent 判定為準。Agent 拒絕面積過小的門檻未公開，QGCS 只排除面積小於 1 m² 的退化多邊形。
- 清除圍籬會失去場域保護，Agent 不另外確認，QGCS 要求 10 秒內再按一次確認。

### 規劃狀態

`ui::planner_status_view` 先以 `state` 分大類，再以 `reason` 開頭細分；到位煞停
（`Emergency brake: target reached while moving`）排在一般緊急煞停之前比對。超過 3 秒沒有規劃狀態時顯示「—」。
說明改變時寫入日誌（附 Agent 原文），不彈出提示。

## 影響

- 新增 `GeofenceModule`（`src/modules/geofence/`），`CoreModules` 與 `wire_core_modules` 增加圍籬模組，所有組裝點同步更新。
- 事件 403 `PLANNER_GEOFENCE_TARGET_CANCELLED` 與任務中止原因 15 `REASON_GEOFENCE_UPDATE` 依適配文件第 3 節中文化。
- `gcs_headless` 新增 `--geofence`、`--geofence-relative`、`--geofence-margin`、`--geofence-clear` 與 `[geofence]` 輸出
  （只新增，見 `docs/agents/qgcs-launch.md`）。
- `V_SHAPE_SPACING` 下限維持 2 m；Agent 預設改為 1.5 m 時再同步調整。
- 全體移動與編隊的圍籬預檢暫不實作（槽位由 Agent 分配），以 Agent 的拒絕事件為準。
