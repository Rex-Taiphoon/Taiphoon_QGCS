# ADR-0007：單一 GCS Docker 容器

**日期**：2026-07-30
**狀態**：已取代（ADR-0013，不再使用容器部署）

## 背景

GCS 需在 Docker 環境中運行，與 Taiphoon_Agent 容器共享 ROS2/DDS 網路。

## 決策

採用 **單一 Docker 容器 + multi-stage build**。

- Stage 1：編譯 C++ 後端（ROS2 + WebSocket） + 建置前端（Vite）
- Stage 2：精簡執行映像，內含後端執行檔 + 前端靜態檔案
- 透過 docker-compose 與 Agent 容器共享 ROS2 網路
- CycloneDDS 配置確保跨容器通訊

## 替代方案

| 方案 | 否決原因 |
|------|----------|
| 雙容器（後端+前端分離） | 增加部署複雜度，無實質收益 |
| GCS 不容器化 | 違反 Docker 開發原則，環境不一致 |

## 後果

- 單一 Dockerfile 維護前後端建置流程
- 前端靜態檔案由 C++ 後端 WebSocket server 同一 port 提供（或內嵌輕量 HTTP file server）
- 部署時僅需一個映像
