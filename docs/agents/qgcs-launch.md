# Taiphoon_QGCS 啟動標準（供 Taiphoon_Agent 驗證使用）

> **讀者**：在 `../Taiphoon_Agent/` 作業、需要以 Taiphoon_QGCS 驗證 Agent 操作功能的開發者或 AI 開發輔助工具。
> **效力**：本文件列出的命令列參數、日誌格式與結束碼是 Taiphoon_QGCS 對外的**穩定啟動介面**，
> 未來版本必須維持相容（見 `AGENTS.md`「啟動介面相容性」）。文件不寫死版本號，各版本皆適用。

---

## 1. 驗證方式總覽

| 方式 | 執行檔 | 用途 | 可送命令 |
|---|---|---|---|
| 桌面版 | `Taiphoon_QGCS` | 人工操作、畫面驗證、截圖 | 由操作人員在介面上操作 |
| 無介面版 | `gcs_headless` | 自動化驗證：機隊、遙測、命令回報輸出至 stdout | 加 `--allow-commands` 後以 `--command` 送出，**一律經 GuardModule** |

兩者使用相同的核心（機隊、遙測、Guard、Command 模組），連線參數相同。
`gcs_headless` 是開發工具，只存在於原始碼建置，不隨安裝程式或 ZIP 發布。

---

## 2. 取得執行檔

依下列順序尋找，找到第一個存在的即可使用：

| 優先 | 來源 | Windows | macOS |
|---|---|---|---|
| 1 | 原始碼建置（Release） | `../Taiphoon_QGCS/build/windows-release/bin/Taiphoon_QGCS.exe` | `../Taiphoon_QGCS/build/macos-release/bin/Taiphoon_QGCS.app/Contents/MacOS/Taiphoon_QGCS` |
| 2 | 原始碼建置（Debug） | `../Taiphoon_QGCS/build/windows-debug/bin/Taiphoon_QGCS.exe` | `../Taiphoon_QGCS/build/macos-debug/bin/Taiphoon_QGCS.app/Contents/MacOS/Taiphoon_QGCS` |
| 3 | 安裝程式 | `C:\Program Files\Taiphoon_QGCS\bin\Taiphoon_QGCS.exe` | `/Applications/Taiphoon_QGCS.app/Contents/MacOS/Taiphoon_QGCS` |
| 4 | ZIP 解壓 | `<解壓目錄>\Taiphoon_QGCS-v<版本>-Windows-AMD64\bin\Taiphoon_QGCS.exe` | — |

`gcs_headless` 只有第 1、2 項，與 `Taiphoon_QGCS` 位於同一個 `bin/` 目錄
（macOS 位於 `bin/gcs_headless`，不在 `.app` 內）。

尚未建置時，於 `../Taiphoon_QGCS/` 執行（Windows PowerShell）：

```powershell
. .\tools\dev-env.ps1                      # 載入 MSVC 與 Qt；Qt 不在 D:\Qt 時先設定 QT_ROOT_DIR
cmake --preset windows-release
cmake --build --preset windows-release
```

macOS 改用 `macos-release` preset（需先設定 `QT_ROOT_DIR`）。建置細節以 `AGENTS.md`「建置與驗證」為準。

Windows 原始碼建置的執行檔需要 Qt DLL：請在已執行 `tools/dev-env.ps1` 的 PowerShell 中啟動，
或改用安裝程式／ZIP 版（已附 Qt 與 VC 執行期）。

---

## 3. 前置條件：Agent 模擬環境

1. 以 Taiphoon_Agent 啟動器在**同一台電腦**的 Docker Desktop 啟動 sim 模式（1～4 組）。
   每台 Agent 會公告主機區網 IPv4，並使用專屬單播埠段（domain 0：7410～7505），
   主動把 discovery 送往 `192.168.65.254:7510`（Taiphoon_QGCS 固定 participant index 50 的 discovery 埠）。
2. 啟動器成功後會印出 QGCS 啟動參數，**以啟動器印出的內容為準**；本文件第 4 節為相同格式的說明。
3. 確認 PX4 SITL 已連上 Agent。沒有 PX4 時 Agent 不會發布 `state`，QGCS 雖能發現 Agent，
   畫面與 `gcs_headless` 都會顯示 0 台線上。

取得主機區網 IPv4（下文以 `<主機IP>` 表示）：

```powershell
# Windows：具有預設閘道的網卡位址
(Get-NetIPConfiguration | Where-Object { $_.IPv4DefaultGateway -and $_.NetAdapter.Status -eq 'Up' } |
    Select-Object -First 1).IPv4Address.IPAddress
```

```bash
# macOS：Wi-Fi 通常為 en0，有線網路依實際網卡調整
ipconfig getifaddr en0
```

---

## 4. 啟動指令

### 4.1 連線參數（桌面版與無介面版共用）

| 參數 | 值 | 說明 |
|---|---|---|
| `--interface` | `<主機IP>` | 使用的網卡（IPv4 或網卡名稱） |
| `--external-address` | `192.168.65.254` | Docker Desktop 的 `host.docker.internal`；容器回送封包經此轉送到主機 |
| `--no-multicast` | — | Docker Desktop 主機收不到容器 multicast，改用單播 |
| `--peer` | `<主機IP>` | Agent 探索入口；不含 port 即可涵蓋 4 台 Agent 的埠段，可重複 |
| `--participant-index` | `50` | 固定 index，對應 Agent 主動送 discovery 的 7510 埠 |
| `--domain` | `0` | ROS_DOMAIN_ID，需與 Agent 相同；預設 0 |

命令列網路參數只在本次執行有效，不會寫入使用者設定，也不會顯示首次設定精靈。

### 4.2 桌面版

```powershell
# Windows
& "<Taiphoon_QGCS.exe 路徑>" --interface <主機IP> --external-address 192.168.65.254 `
    --no-multicast --peer <主機IP> --participant-index 50
```

```bash
# macOS
"<Taiphoon_QGCS 執行檔路徑>" --interface <主機IP> --external-address 192.168.65.254 \
    --no-multicast --peer <主機IP> --participant-index 50
```

開發驗證用參數（可與上方連線參數併用）：

| 參數 | 說明 |
|---|---|
| `--screenshot <檔案.png>` | 延遲後將主視窗存成 PNG 並結束；成功結束碼 0，失敗 1 |
| `--screenshot-delay <毫秒>` | 搭配 `--screenshot`，預設 12000；連線 4 台建議 20000 以上 |
| `--select <all 或 uav_N_agent>` | 機隊第一次掃描後自動選取目標 |
| `--open-command-panel` | 啟動時展開飛行控制面板 |
| `--smoke-test` | 載入主視窗後立即結束；成功結束碼 0（無畫面環境另設 `QT_QPA_PLATFORM=offscreen`） |

Windows 版是視窗程式，`--help`、`--version` 會跳出對話框而非輸出到終端機，自動化流程不要使用。

### 4.3 無介面版 `gcs_headless`

只觀察（不送命令）：

```powershell
& "<bin 目錄>\gcs_headless.exe" --interface <主機IP> --external-address 192.168.65.254 `
    --no-multicast --peer <主機IP> --participant-index 50 --duration 30
```

送出命令（**必須**加 `--allow-commands`，否則程式拒絕執行並以結束碼 1 結束）：

```powershell
& "<bin 目錄>\gcs_headless.exe" --interface <主機IP> --external-address 192.168.65.254 `
    --no-multicast --peer <主機IP> --participant-index 50 --duration 60 --warmup 8 `
    --command uav_1_agent:ARM --command uav_1_agent:TAKEOFF:3 --command uav_1_agent:MODE_LAND `
    --allow-commands
```

| 參數 | 說明 |
|---|---|
| `--duration <秒>` | 執行秒數後結束，預設 20 |
| `--warmup <秒>` | 第一個命令前的等待時間，預設 8，讓機隊與 Guard 收到狀態 |
| `--command <agent>:<命令>[:p1,p2,...]` | 可重複，依序每 3 秒送出一個；`<agent>` 為 `uav_N_agent` 或 `all` |
| `--allow-commands` | 確認允許送出 `--command`／`--shutdown` |
| `--shutdown <uav_N_agent>` | 最後呼叫該 Agent 的 shutdown service；會終止 Agent 程序，非必要不要使用 |

命令名稱與參數以 `../Taiphoon_Agent_Interfaces/taiphoon-agent-interface.md` 的命令協定為準；
Guard 依目前飛行狀態拒絕不允許的命令時，會輸出失敗的 `[ack]`。編隊命令（`FORMATION_*`）只接受 `all`。

### 4.4 同時開啟桌面版與無介面版

同一台電腦上兩個程序不能使用相同的 participant index。桌面版維持 `--participant-index 50`，
`gcs_headless` 改用 `--participant-index 51`；Agent 不會主動送 discovery 到 51，
但 QGCS 會自行探測 `--peer` 的 4 段埠，仍可發現全部 Agent。

---

## 5. 判斷結果

### 5.1 桌面版日誌

日誌檔每天一個，`info` 以上等級都會寫入：

- Windows：`%LOCALAPPDATA%\Taiphoon\Taiphoon_QGCS\logs\Taiphoon_QGCS_<yyyyMMdd>.log`
- macOS：`~/Library/Application Support/Taiphoon/Taiphoon_QGCS/logs/Taiphoon_QGCS_<yyyyMMdd>.log`

| 日誌片段 | 意義 |
|---|---|
| `DDS 已啟動：` | DDS 建立成功 |
| `DDS 啟動失敗：`、`DDS 未能啟動：` | 參數或網路錯誤，介面仍會開啟 |
| `Agent 上線： "uav_N_agent"` | 發現該 Agent 的 topic |
| `Agent 離線： "uav_N_agent"` | 該 Agent 的 topic 消失 |
| `畫面擷取 已保存：` | `--screenshot` 成功 |

日誌的「上線」以 DDS 發現為準；畫面上的「N 台線上」另外要求持續收到 `state`。

### 5.2 `gcs_headless` 輸出

每行以 `HH:mm:ss.zzz` 開頭，輸出到 stdout（UTF-8）：

| 輸出 | 意義 |
|---|---|
| `[fleet] 上線 uav_N_agent`／`[fleet] 離線 uav_N_agent` | 機隊變化 |
| `[fleet] online=... offline=...` | 每次掃描的線上／離線清單 |
| `[state] uav_N_agent n=... mode=... armed=... landed=... alt=...` | 每 2 秒一次的遙測摘要 |
| `[request] uav_N_agent COMMAND` | 已交給 GuardModule 的命令 |
| `[ack] uav_N_agent COMMAND 成功/失敗 <訊息>` | Guard 拒絕或 Agent 回報結果 |
| `結束：收到 N 台 Agent 的狀態，失敗回報 M 筆` | 最後一行摘要 |

結束碼：至少收到 1 台 Agent 的狀態為 0，否則為 1（核心啟動失敗、命令格式錯誤、未加 `--allow-commands` 也是 1）。
失敗的 `[ack]` 不影響結束碼，請以輸出內容判斷命令是否成功。

---

## 6. 排查

| 現象 | 原因與處理 |
|---|---|
| 0 台，日誌沒有任何「上線」 | 檢查 `<主機IP>` 是否正確、Agent 容器是否執行、`--external-address 192.168.65.254` 是否遺漏 |
| 日誌有「上線」但畫面 0 台 | PX4 SITL 未連上 Agent，`state` 沒有資料 |
| 只看到部分 Agent | 確認 `--peer` 不含 port；QGCS 版本需包含 2026-10-16 的探測範圍修正（ADR-0011） |
| 修改前後比對結果相同 | CycloneDDS 會記住曾連線的對端位址並持續送 discovery；每輪比對前重啟 Agent 容器 |
| `participant index` 啟動失敗 | 同機已有程序使用相同 index；改用其他 index（例如 51） |
| Windows 防火牆詢問 | 允許 Taiphoon_QGCS／gcs_headless 的 UDP 連線（固定 index 50 使用 7510／7511） |

Linux Docker Engine 的模擬（Agent 接入 `Taiphoon_Net`、不映射主機埠）尚未以本文件的方式驗證。

---

## 7. 安全邊界

- 命令只能經 `gcs_headless --command ... --allow-commands` 或桌面版介面送出，兩者都經 GuardModule；
  不要以 `ros2 topic pub` 或其他工具繞過 Guard 發布 `/uav_N_agent/command`。
- `tools/dds_probe` 是 M1 互通驗證工具，不用於操作驗證。
- 本文件只適用於模擬環境；實機驗證須由人類開發者確認後執行。
