/**
 * @file module_manager.cpp
 * @brief ModuleManager 實作
 */
#include "core/module_manager.hpp"
#include <iostream>

bool ModuleManager::initAll() {
  for (auto& mod : modules_) {
    std::cout << "[ModuleManager] 初始化模組: " << mod->getName() << std::endl;
    if (!mod->init(*bus_)) {
      std::cerr << "[ModuleManager] 模組初始化失敗: " << mod->getName() << std::endl;
      return false;
    }
  }
  std::cout << "[ModuleManager] 全部模組初始化完成" << std::endl;
  return true;
}

void ModuleManager::shutdownAll() {
  // 反向關閉（後初始化的先關閉）
  for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) {
    std::cout << "[ModuleManager] 關閉模組: " << (*it)->getName() << std::endl;
    (*it)->shutdown();
  }
}

bool ModuleManager::healthCheckAll() {
  bool allHealthy = true;
  for (auto& mod : modules_) {
    if (!mod->healthCheck()) {
      std::cerr << "[ModuleManager] 模組異常: " << mod->getName() << std::endl;
      allHealthy = false;
    }
  }
  return allHealthy;
}
