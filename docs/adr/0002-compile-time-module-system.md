# ADR-0002：編譯時模組系統

**日期**：2026-07-30
**狀態**：部分取代（編譯時模組註冊沿用；EventBus 改由 ADR-0015 取代）

## 背景

C++ 後端需要模組化架構，參考 QGroundControl 的 QGCTool 設計模式。需決定模組載入方式。

## 決策

採用 **編譯時模組註冊**，不使用執行時動態載入。

- 定義 `GcsModule` 抽象基底類別，提供 `init()` / `shutdown()` / `healthCheck()` 介面
- 各功能模組繼承 `GcsModule`，在 CMakeLists.txt 中可選編譯
- 模組間透過核心事件匯流排（EventBus）解耦，不直接互相依賴
- 啟動時由 ModuleManager 依相依順序初始化所有已編譯模組

## 替代方案

| 方案 | 否決原因 |
|------|----------|
| dlopen 動態載入 | ABI 相容性風險、除錯困難，GCS 無 hot-swap 需求 |
| 純 namespace 隔離 | 無強制介面約束，團隊協作一致性不足 |

## 後果

- 新增/移除模組需重新編譯，但部署仍為單一容器
- GcsModule 介面為所有模組的唯一合約，新模組必須實作
- EventBus 為系統內唯一跨模組通訊通道
