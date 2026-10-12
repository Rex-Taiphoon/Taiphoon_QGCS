# ADR-0016：地圖採 Qt Location OSM plugin

**日期**：2026-10-12
**狀態**：已接受
**取代**：ADR-0004

## 背景

網頁版地圖實際上直接向 `tile.openstreetmap.org` 取得點陣圖磚，後端 MapModule 的 `/tiles/` 端點
前端並未使用，也沒有離線地圖。MapLibre Native Qt 的 QML plugin 依賴 Qt Location 私有 API，
必須隨 Qt 小版本自行編譯，與 ADR-0013 的固定版本策略衝突。

## 決策

- 第一版使用 Qt Location 內建的 `osm` plugin，以 `osm.mapping.custom.host` 指定點陣圖磚來源，
  並設定 `osm.useragent` 符合 OSM 圖磚使用政策；使用 plugin 內建磁碟快取。
- 無人機箭頭、名稱、軌跡折線等疊圖使用 `MapQuickItem`、`MapPolyline` 等公開 QML API。
- 地圖封裝在單一 `MapView.qml`，對外只公開必要屬性，日後替換為 MapLibre 或離線方案時不影響其他元件。
- 移除 MapModule（HTTP 圖磚代理）。

## 替代方案

| 方案 | 否決原因 |
|------|----------|
| MapLibre Native Qt | 依賴私有 API、建置負擔大；目前不需要向量圖磚，延後評估 |
| 自製 QQuickItem 圖磚算繪 | 需自行處理平移、縮放與手勢，第一版不值得 |

## 後果

- 功能與網頁版對等（線上 OSM 點陣地圖）；離線 `.mbtiles` 屬於新需求，另行規劃。
- 正式發布前需決定圖磚來源：OSM 官方圖磚不適合大量使用。
