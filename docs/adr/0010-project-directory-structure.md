# ADR-0010：專案目錄結構

**日期**：2026-07-30
**狀態**：已取代（ADR-0013，目錄見 docs/qt-cross-platform-development.md）

## 背景

需定義 Taiphoon_GCS 的完整目錄結構，涵蓋 C++ 後端、React 前端、Docker 配置與文件。

## 決策

```
Taiphoon_GCS/
├── CMakeLists.txt
├── Dockerfile
├── docker-compose.yml
├── src/
│   ├── core/                   # GcsModule, ModuleManager, EventBus, WsServer
│   ├── modules/
│   │   ├── telemetry/          # 訂閱所有 Agent topic，聚合推送
│   │   ├── command/            # 命令發布至 ROS2
│   │   ├── guard/              # 狀態機 + 資料驅動轉移表
│   │   ├── formation/          # 編隊命令處理
│   │   ├── fleet/              # 同儕發現、機隊管理
│   │   └── map/                # 圖磚 HTTP endpoint + .mbtiles
│   └── main.cpp
├── frontend/                   # React + TypeScript (Vite)
│   └── src/
│       ├── components/
│       ├── hooks/
│       ├── stores/
│       └── types/
├── docs/adr/
├── CONTEXT.md
└── AGENTS.md
```

## 後果

- 模組間嚴格隔離，僅透過 EventBus 通訊
- 前端靜態檔案由 Vite 建置，輸出至後端 serving 目錄
