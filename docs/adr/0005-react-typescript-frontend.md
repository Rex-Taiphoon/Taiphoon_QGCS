# ADR-0005：React + TypeScript 前端

**日期**：2026-07-30
**狀態**：已取代（ADR-0013，改用 QML）

## 背景

前端需整合 WebSocket 即時資料流、MapLibre GL 地圖、多面板儀表板佈局。需選擇前端框架。

## 決策

採用 **React 18+ + TypeScript**。

- 地圖整合：react-map-gl（MapLibre GL 原生封裝）
- 狀態管理：Zustand（輕量、適合 WebSocket 高頻更新）
- 建置工具：Vite
- UI 元件庫：不強制，推薦輕量方案（如 Tailwind CSS + Headless UI）

## 替代方案

| 方案 | 否決原因 |
|------|----------|
| Vue 3 | MapLibre 封裝不如 react-map-gl 成熟 |
| Svelte | 生態較小，MapLibre 整合需全手動 |

## 後果

- 靜態檔案由 Vite 建置，部署時掛載至 C++ 後端 Docker 容器
- TypeScript 強型別確保 WebSocket 訊息格式與後端一致
