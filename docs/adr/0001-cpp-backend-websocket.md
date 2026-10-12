# ADR-0001：C++ 後端內嵌 WebSocket Server

**日期**：2026-07-30
**狀態**：已取代（Qt 原生版改用 ADR-0013、ADR-0015）

## 背景

Taiphoon_GCS 需要同時作為 ROS2 節點與 Web 前端伺服器。後端必須：
- 原生訂閱/發布 ROS2 topic（與 Taiphoon_Agent 通訊）
- 將狀態推送給瀏覽器前端
- 接收前端命令並轉發至 ROS2

## 決策

採用 **C++ 後端內嵌 WebSocket Server**，不使用 rosbridge 或額外橋接程序。

- C++ 後端作為 ROS2 節點，直接使用 rclcpp
- 內嵌 WebSocket Server（uWebSockets 或 Boost.Beast）處理前端通訊
- 前端僅為展示與互動層，所有業務邏輯在後端

## 替代方案

| 方案 | 否決原因 |
|------|----------|
| rosbridge (rosbridge_suite) | JSON 協定開銷大，10Hz × N 台高頻資料不合適 |
| Node.js/Python 橋接程序 | 增加部署複雜度，與 Taiphoon_Agent 全 C++ 棧不一致 |

## 後果

- 後端開發人員需同時熟悉 ROS2 C++ 與 WebSocket 程式設計
- 部署單元為單一 Docker 容器（含 C++ 後端 + 前端靜態檔案）
- WebSocket 協定需自訂訊息格式（JSON 或 binary）
