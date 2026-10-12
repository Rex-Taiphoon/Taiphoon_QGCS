# ADR-0004：MapLibre GL + 自建圖磚伺服器

**日期**：2026-07-30
**狀態**：已取代（ADR-0016）

## 背景

GCS 需支援線上與離線地圖，顯示無人機位置與軌跡。需選擇前端地圖函式庫與圖磚來源策略。

## 決策

採用 **MapLibre GL + C++ 後端內嵌圖磚 HTTP 端點**。

- 前端使用 MapLibre GL JS，原生支援 `.mbtiles` 向量圖磚與 3D 視角
- C++ 後端 MapModule 內嵌輕量 tile HTTP endpoint：
  - 線上模式：代理 OSM 或自選圖磚來源，並將熱區圖磚快取至 `.mbtiles`
  - 離線模式：直接讀取預先部署的 `.mbtiles` 檔案
- 前端透過標準 Z/X/Y tile URL 向後端請求圖磚

## 替代方案

| 方案 | 否決原因 |
|------|----------|
| Leaflet | 離線支援需大量自訂，無原生 `.mbtiles` |
| CesiumJS | 3D 地球過重，無人機操作區域通常小於 100km² |
| 純前端離線 (Service Worker) | 快取策略複雜，不如後端統一管理可靠 |

## 後果

- MapModule 需實作 tile HTTP endpoint 與 `.mbtiles` 讀寫
- 離線使用前需預先下載操作區域圖磚
- MapLibre GL 使用 WebGL，對低效能機器需注意效能
