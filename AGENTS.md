# Taiphoon_QGCS 開發規範 (AGENTS.md)

> 本文件是 AI 開發輔助工具在本專案作業的入口指引。
> 動工前先閱讀「文件地圖」中與任務相關的文件，程式碼風格請遵循 `CODE_CONSTRAINT.md`。

---

## 專案概述

Taiphoon_QGCS 是 Taiphoon_GCS（網頁版）的 Qt 原生版本，搭配 Taiphoon_Agent 使用。
目標是在維持網頁版所有功能與 Agent 介面的前提下，提供可直接安裝執行的桌面程式，
使用者不需安裝 Docker、ROS2 或處理 DDS 環境。

架構為 Qt 6.11 LTS + QML，C++17；內嵌 CycloneDDS，網路層與 ROS2 Humble + `rmw_cyclonedds_cpp` 相容，
但程式**不連結任何 ROS2 函式庫**。發布格式為 Windows NSIS 安裝程式與 macOS DMG。

主要職責：
- 狀態監控：訂閱所有 Agent topic，聚合後提供給 QML。
- 地圖顯示：Qt Location OSM 地圖與無人機、軌跡疊圖。
- 命令路由：接收 UI 命令，經 GuardModule 驗證後發布至 Agent。
- 機隊管理：透過 DDS 發現動態維護線上／離線機隊。

領域術語請參閱 `CONTEXT.md`，架構決策請參閱 `docs/adr/`，整體規劃與開發路線請參閱
`docs/qt-cross-platform-development.md`。網頁版原始碼保留於 `../Taiphoon_GCS/`，僅作功能對照。

---

## 儲存庫結構

```
Taiphoon_QGCS/
├── AGENTS.md                    # 本文件：作業入口
├── CHARACTORS.md                # 角色子代理定義
├── CODE_CONSTRAINT.md           # 程式碼風格規範（必讀）
├── CONTEXT.md                   # 領域詞彙表
├── README.md
├── CMakeLists.txt、CMakePresets.json
├── cmake/                       # CPM、MsgToIdl、Deploy
├── msg/                         # Agent 介面檔案同步副本與 interfaces.lock.json
├── idl/                         # 手寫 IDL（std_msgs、std_srvs、builtin_interfaces）
├── tools/                       # msg2idl.py 等建置工具
├── src/
│   ├── core/                    # GcsModule、ModuleManager、共用型別、IAgentLink、命令協定
│   ├── modules/                 # fleet / telemetry / guard / command / lifecycle / inspector
│   ├── link/                    # CycloneDDS ROS2 相容層
│   ├── app/                     # main、組裝根 gcs_wiring、UiBridge、models、設定
│   └── qml/                     # QML 檢視
├── test/                        # 單元測試（Google Test + QtTest，禁止混入主程式）
├── packaging/                   # NSIS、macOS 打包與授權聲明
├── docs/
│   ├── adr/                     # 架構決策記錄
│   └── agents/issue-tracker.md  # issue 流程
└── .scratch/                    # 功能規格與 issue 檔案
```

目錄依開發路線逐步建立；尚未建立的目錄以 `docs/qt-cross-platform-development.md` 第 5.6 節為準。

---

## 介面慣例

- Agent 端通訊介面的**唯一權威來源**為同級目錄 `../Taiphoon_Agent_Interfaces/`：
  - `taiphoon-agent-interface.md`：Node、Topic、自訂訊息、命令協定、命名空間與 QoS 規格。
  - `.msg` / `.srv` / `.action`：介面定義檔案。
- 開發或修改任何與 Agent 的通訊介面時，**必須**以該文件為依據，不得自行臆測欄位、Topic 名稱或命令格式。
- 編譯前**必須**將最新介面檔案同步至 `msg/`；介面更新後**必須**同步修正 IDL 產生流程與建置腳本
  （`CMakeLists.txt`、`cmake/`、`tools/msg2idl.py`）。
- 與 Agent 的資料鏈**相容 ROS2 Humble + `rmw_cyclonedds_cpp` 的 DDS 線上協定**（ADR-0014）：
  DDS 名稱依 `rt/`、`rq/`、`rr/` 前綴與 `<pkg>::msg::dds_::<Type>_` 型別命名；QoS 依介面文件第 8 節集中管理。
- 命名空間規則為 `/uav_{N}_agent/...` 與 `/uav_all_agent/command`，禁止硬編碼無人機數量。
- Agent 必須使用 `rmw_cyclonedds_cpp`；這是 shutdown service 互通的部署前提。
- UI 只能透過 `UiBridge` 與核心模組溝通；QML 與 `UiBridge` **禁止**直接呼叫 `IAgentLink` 或發布 DDS 資料。

---

## 架構與模組慣例

- 所有功能模組繼承 `QObject` 與 `GcsModule`，位於 `src/modules/<模組名>/`，新模組需在 CMake 註冊。
- 模組之間**禁止直接 include** 其他模組標頭；跨模組通訊一律使用 Qt signal／slot（ADR-0015），
  共用資料型別與介面放在 `src/core/`。
- 所有 `connect()` 集中在組裝根 `src/app/gcs_wiring.cpp`；其他檔案不得建立跨模組連線。
- 執行緒：模組位於核心執行緒，模組間使用 `Qt::DirectConnection`；DDS 執行緒與 UI 主執行緒進出核心一律使用 queued connection。
  禁止在 DDS 執行緒操作 QML 或其他執行緒的 `QObject`。
- `GuardModule` 是命令發布前的唯一安全閘門：`UiBridge` 只能連到 Guard 的命令請求 slot，
  只有 Guard 的 `command_validated` 可以連到 `CommandModule`。
- `CommandModule` 依目標路由至 `/{agent_namespace}/command` 或 `/uav_all_agent/command`。
- 機隊成員由 DDS 發現動態決定，禁止靜態寫死 Agent 命名空間或數量。
- 詳細架構決策參閱 `docs/adr/`，術語定義參閱 `CONTEXT.md`。

---

## 角色子代理

需要建立子代理時，可依 [`CHARACTORS.md`](./CHARACTORS.md) 的角色定義建立以下角色：

- 審查人員：Code review 與規範符合性審查。
- 測試人員：PX4 模擬器與 Taiphoon_Agent 互通、功能、流程與回歸測試。
- 建置與打包維護人員：CMake、第三方相依、安裝程式與 CI 維護。
- QML 介面工程師：QML 介面、`UiBridge` 與 models 開發。
- 核心與通訊工程師：核心模組、Guard、CycloneDDS 相容層開發。

完整角色定位、工作方式與邊界條件請參閱 [`CHARACTORS.md`](./CHARACTORS.md)。

**子代理開啟規則：**
- 在開發者沒有明確指示開啟子代理的前提下，**不得自行開啟子代理**。
- 若任務中認為有必要使用子代理，必須先向開發者說明原因、角色與用途，並取得同意後才能開啟。

---

## 邊界條件

**Always：**
- 執行 Git、刪除檔案或安全關鍵變更等重要決策前，必須先向使用者說明並取得同意。
- 動工前閱讀任務相關文件：`CODE_CONSTRAINT.md`、`CONTEXT.md`、相關 `docs/adr/`、Agent 介面文件。
- 所有變更僅限於 Taiphoon_QGCS 專案本身，除非任務明確要求。
- 所有中文註解、文件與回覆使用繁體中文，禁止中國用語。
- 程式碼風格遵循 `CODE_CONSTRAINT.md`，不在本文件重複描述。
- 命令發布前必須經過 `GuardModule` 驗證。
- 新增或修改跨模組的 signal／slot 或共用型別時，同步更新組裝根、相關測試與文件。
- 編譯或提交前，確認介面檔案已同步至 `msg/`。
- 安全關鍵變更必須附單元測試；Guard 轉移表需覆蓋每個狀態 × 命令組合。
- 單元測試程式碼一律集中在根目錄 `test/`，禁止放入 `src/` 主程式邏輯。
- Qt 版本與第三方相依版本固定，不得任意升級。
- `README.md` 應保持最新狀態。

**Ask first：**
- 若任務需要子代理協助，先說明原因與角色並取得開發者同意。
- 修改 GuardModule / CommandModule 等安全關鍵模組，尤其是 Arm、Takeoff、Disarm、RTL、Land。
- 修改一個功能時，若會影響其他功能的運行邏輯，須先告知風險並由使用者決定是否繼續。
- 修改模組邊界、組裝根的連線或執行緒配置。
- 新增、刪除或移動檔案／目錄。
- 變更與其他模組或 Taiphoon_Agent 的既有介面契約、QoS 或 DDS 命名。
- 變更 Qt、CycloneDDS 或其他第三方相依的版本。
- 執行 Git commit 時，必須先確認提交內容，提交訊息使用 `模組: 更改項目` 格式且內容簡短明確；
  修改提交歷史前也必須取得同意。目前不設定遠端，不執行 push。
- 修改安裝程式、簽章、公證或 CI 發布流程時，須先說明影響並取得使用者同意。

**Never：**
- 未經開發者同意自行開啟子代理。
- 禁止自行提交 Git；未經使用者明確同意不得執行 commit / push。
- 禁止在任何指令中使用 `--force` / `--hard` 等強制或破壞性指令。
- 嘗試繞過平台的安全機制或人工審核流程；遇到需要確認的操作時，一律交給人類開發者決定。
- 模組間直接 include，或在組裝根以外建立跨模組連線。
- 在 CommandModule（經 GuardModule 驗證）以外的地方發布 Agent command topic。
- QML 或 `UiBridge` 直接呼叫 `IAgentLink` 或發布 DDS 資料。
- 連結 rclcpp 或其他 ROS2 執行期函式庫。
- 除錯工具提供發布 topic 或呼叫 service 的功能。
- 註解或文件使用中國用語。
- 硬編碼 Agent 命名空間或無人機數量。
- 略過單元測試發布安全關鍵程式碼變更。
- 將單元測試程式碼放置在 `src/` 主程式邏輯中。
- 未參考 Agent 介面文件即修改通訊介面。
- 編譯或提交前未同步最新 `.msg` / `.srv` / `.action` 至 `msg/`。
- 介面檔案更新後未同步修正 IDL 產生流程與建置腳本。

---

## 建置與驗證

Windows 使用 MSVC（Qt `msvc2022_64` kit，本機為 VS 2026 Build Tools）；Qt 固定 6.11.2，
CMake 會拒絕其他版本。第三方相依（CycloneDDS 0.10.5、GoogleTest v1.15.2）於首次 configure 時下載。

```powershell
. .\tools\dev-env.ps1                  # 載入 MSVC 與 Qt；Qt 不在 D:\Qt 時先設定 QT_ROOT_DIR
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

其他 preset：`windows-release`、`macos-debug`、`macos-release`、`linux-debug`（需先設定 `QT_ROOT_DIR`）。
無畫面環境驗證主視窗可載入：設定 `QT_QPA_PLATFORM=offscreen` 後執行 `taiphoon_qgcs --smoke-test`。

`tools/dds_probe` 是 M1 互通驗證工具，不隨產品發布。它只能送出 Agent 無法解析的命令 `GCS_PROBE_NOOP`，
shutdown 必須加上 `--confirm`；正式命令一律經 GuardModule。

### 單元測試

- 單元測試程式碼一律集中在專案根目錄 `test/`，使用 Google Test，可搭配 QtTest 的 `QSignalSpy`。
- 禁止將單元測試程式碼放置在 `src/` 或其他主程式邏輯中；`src/` 內不得出現測試案例、測試框架或測試專用程式碼。
- 新增測試目標時，必須在 CMake 以 `gtest_discover_tests()` 註冊，並可透過 `ctest` 執行。

驗證要求：
- Guard 轉移表單元測試必須覆蓋每個 `FlightState` × `CmdType` 組合，以及 Arm／Takeoff 的 PX4 前置條件。
- 組裝測試必須證明未經 Guard 允許的請求不會發布命令。
- 修改 Agent 通訊介面時，需比對 `msg/` 與 `../Taiphoon_Agent_Interfaces/` 一致，並核對 QoS 表與介面文件第 8 節。
- 互通驗證需對 PX4 SITL + Taiphoon_Agent 執行，情境參考 Taiphoon_GCS 的 `.scratch/gcs-v1/test_e2e.py`。

---

## 文件地圖

| 文件 | 用途 | 何時讀取 |
|------|------|----------|
| `docs/qt-cross-platform-development.md` | 需求、架構、訊號流與開發路線 | 動工前、規劃新階段時 |
| `CHARACTORS.md` | 角色子代理定義 | 建立子代理或指派角色任務時 |
| `CODE_CONSTRAINT.md` | C++／QML 程式碼風格 | 每次撰寫或修改程式碼前 |
| `CONTEXT.md` | 領域詞彙與術語 | 動工前、使用專案術語時 |
| `docs/adr/` | 架構決策記錄（0013～0016 為 Qt 版主要決策） | 修改模組、通訊層或架構前 |
| `docs/agents/issue-tracker.md` | issue 流程規範 | 建立或更新 issue 時 |
| `../Taiphoon_Agent_Interfaces/taiphoon-agent-interface.md` | Agent 通訊介面契約 | 修改或驗證 Agent 通訊介面時 |
| `README.md` | 快速上手 | 不熟悉環境時 |
