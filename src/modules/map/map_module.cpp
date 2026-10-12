/**
 * @file map_module.cpp
 * @brief 地圖模組實作
 */
#include "modules/map/map_module.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

MapModule::MapModule() : cacheDir_("/tmp/taiphoon_gcs_tiles") {}

bool MapModule::init(EventBus& bus) {
  bus_ = &bus;
  fs::create_directories(cacheDir_);
  initialized_ = true;
  std::cout << "[MapModule] 初始化完成，快取目錄: " << cacheDir_ << std::endl;
  return true;
}

void MapModule::shutdown() { initialized_ = false; }
bool MapModule::healthCheck() { return initialized_; }

std::string MapModule::getTile(int z, int x, int y) {
  // 檢查本地快取
  std::string cachePath = cacheDir_ + "/" +
    std::to_string(z) + "_" + std::to_string(x) + "_" + std::to_string(y) + ".png";

  std::ifstream cacheFile(cachePath, std::ios::binary);
  if (cacheFile.is_open()) {
    std::ostringstream ss;
    ss << cacheFile.rdbuf();
    return ss.str();
  }

  // 無快取 → 線上代理
  std::string data = fetchFromOSM(z, x, y);
  if (!data.empty()) {
    // 寫入快取
    std::ofstream out(cachePath, std::ios::binary);
    out.write(data.data(), data.size());
  }
  return data;
}

std::string MapModule::fetchFromOSM(int z, int x, int y) {
  // 使用 curl 下載 OSM 圖磚
  std::string url = "https://tile.openstreetmap.org/" +
    std::to_string(z) + "/" + std::to_string(x) + "/" + std::to_string(y) + ".png";
  std::string cmd = "curl -s --max-time 5 " + url + " 2>/dev/null";
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) return "";
  std::ostringstream ss;
  char buffer[4096];
  while (fread(buffer, 1, sizeof(buffer), pipe) > 0) {
    ss.write(buffer, sizeof(buffer));
  }
  pclose(pipe);
  return ss.str();
}
