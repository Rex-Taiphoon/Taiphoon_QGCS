/**
 * @file map_module.hpp
 * @brief 地圖模組 — 圖磚代理與快取服務
 *
 * 線上模式：代理 OSM 圖磚並快取至本地。
 * 離線模式：讀取本地 .mbtiles 快取。
 */
#pragma once

#include "core/gcs_module.hpp"
#include "core/event_bus.hpp"
#include <string>
#include <memory>

class MapModule : public GcsModule {
public:
  MapModule();

  bool init(EventBus& bus) override;
  void shutdown() override;
  bool healthCheck() override;
  std::string getName() const override { return "MapModule"; }

  /// 取得圖磚資料（Z/X/Y），若無快取則嘗試線上代理
  std::string getTile(int z, int x, int y);

  /// 設定快取目錄
  void setCacheDir(const std::string& dir) { cacheDir_ = dir; }

private:
  /// 從 OSM 下載圖磚
  std::string fetchFromOSM(int z, int x, int y);

  EventBus* bus_ = nullptr;
  std::string cacheDir_;
  bool initialized_ = false;
};
