# ADR-0011：跨平台原生啟動器與 Wi-Fi DDS 單播

**日期**：2026-10-02
**狀態**：已取代（ADR-0013、ADR-0014；DDS 單播參數沿用）

## 背景

使用者需要 macOS 與 Windows 一行啟動 GCS，現場設備透過 Wi-Fi 連線。
原有部署依賴開發映像、原始碼掛載、固定 Mac IP 與額外設定 host networking。
本次先驗證 Windows → WSL → Mac 四台 Agent 的 topic 與實際狀態，再開發啟動器。

## 決策

- 使用 Go 標準函式庫建置 `taiphoon_gcs` 原生執行檔，使用者不必安裝 Go。
  Go 原始碼使用 `gofmt`，後端與前端仍遵循既有 C++／TypeScript 規範。
- `start` 為前景程序，持有轉送資源。Ctrl+C 或 `stop` 停止它管理的容器。
  容器以 `--rm` 建立，停止後自動刪除；start 遇到同名的已停止舊容器時先移除再建立。
  不刪除其他容器、映像或 volume，不修改防火牆、WSL 設定或系統網路。
- 首次設定 Agent 探索入口、Docker 映像與站點參數，之後保存為 JSON。
  Wi-Fi 位址每次啟動偵測；位址改變時停止目前 GCS，要求重新啟動更新 DDS locator。
- 首次與後續均為 `taiphoon_gcs start`。首次開啟本機設定頁，讓使用者輸入現場 Agent 入口，
  保存後同一分頁轉入 GCS；之後不再要求 CLI 參數。
- 正式 ZIP 包含啟動器與 `.taiphoon_gcs` 下的預編譯映像及版本描述。首次自動以 stdin
  載入映像，後續重用快取；不生成或執行 SHA256 校驗。套件損毀時由 Docker 回報格式錯誤。
- 同機預設 DDS 埠衝突時，檢查 Docker 映射與主機監聽埠，自動選擇可用固定索引並保存。
- DDS 使用 CycloneDDS 單播、`ParticipantIndex=auto`、`MaxAutoParticipantIndex=20`、
  `ManySocketsMode=false`。GCS 公告主機的設備網路 IPv4，明確映射所有單播接收埠。
  這是 GCS 程序的 participant 容量，與 Agent 數量無關。
- 同機 Agent 占用低位 DDS 埠時，可用 `--participant-index` 指定固定 GCS 程序索引，
  XML、Docker 映射與 Windows 轉送皆使用該索引的兩個埠。domain 0、索引 100 使用
  7610／7611；索引保存於站點設定並納入容器名稱，不能在同容器額外執行共用索引的 ROS2 CLI。
- Docker Desktop 與 Linux 使用 bridge + port publishing。Windows WSL NAT Docker
  另由原生啟動器接收 RTPS，再以 Windows–WSL 路由的來源位址送到 WSL 發布埠。
  轉送不綁定 Wi-Fi 來源位址；本次實測已證明這是 WSL 接收成功的必要條件。
- Windows WSL 提供 HTTP reverse proxy，支援 WebSocket upgrade，統一使用 localhost URL。
- 預設網頁只提供給本機。`--share` 另外在設備網路位址提供完整 GCS，僅用於可信任網路。
- SITL 使用 `Taiphoon_Net` 及 multicast，不公告實體 Wi-Fi 位址。
- `Dockerfile.release` 分離前端、後端建置與執行環境。原有 Dockerfile／Compose 保留開發用途。
- 保留 ROS2 與 Agent 的既有介面、QoS、Guard、Command 與 EventBus 流程。
  啟動器不發布 ROS2 控制命令，也不把 DDS discovery peer 當作 Agent 同儕中繼。

## 限制與驗收

- Docker 與原生啟動器需首次安裝；首次下載映像的時間取決於網路。
- 支援 Linux containers；WSL NAT 經本次 Wi-Fi 實測，WSL mirrored networking 不使用此轉送模式。
- 基地台需允許設備互通，關閉 client isolation；Agent 需相同 ROS_DOMAIN_ID 與可路由 locator。
- 沒有 discovery peer 的單播系統無法憑空找到彼此，首次需設定入口或讓 Agent 主動探索 GCS。
- 已驗證 Windows amd64／WSL 接收四台 Agent。macOS amd64／arm64 與 Linux 原生執行檔
  可交叉編譯，但編譯成功不等於實機 Wi-Fi 驗收成功。
- 完整套件建置由人工啟動工作流程；映像隨 ZIP 發布，不需 registry。
- 依 `docs/release-standard.md` 發布產品版本標籤與各 OS／CPU 的單一完整 ZIP。
  目前本機產物含 amd64 映像，Windows／macOS ARM 使用 Docker Desktop 模擬；
  未有原生 arm64 映像時不發布 Linux arm64 完整套件。ARM 與 Mac 同機 DDS 回程仍待實機驗收。

## 替代方案

直接安裝 ROS2 增加各平台相依套件負擔；只包裝 Compose 無法自動選擇網卡或處理 WSL 回程。
新增 DDS router／修改 Agent middleware 會擴大架構與通訊契約，本次不需要採用。

## 2026-10-03 更新

目前 Windows 開發使用 Docker Desktop，從 PowerShell 操作，不再使用 WSL 發行版的 dockerd。
啟動器支援系統與使用者安裝目錄，Windows 一律使用 Windows Docker Desktop。
舊 WSL 設定會於 start 自動遷移並保留站點參數，不沿用跨 engine 的映像快取與容器識別。
已移除 WSL Docker 呼叫路徑；--wsl 指定發行版會被拒絕，不再提供舊後端相容方式。
前述 WSL 四台 Agent 驗證屬於當時環境，不作為目前 Docker Desktop 的驗證結論。
