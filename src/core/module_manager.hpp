/**
 * @file module_manager.hpp
 * @brief 模組管理器 — 管理所有 GcsModule 的生命週期
 *
 * 依註冊順序初始化模組，反向順序關閉。
 * 在初始化前建立 EventBus 並傳遞給所有模組。
 */
#pragma once

#include "gcs_module.hpp"
#include "event_bus.hpp"
#include <memory>
#include <vector>
#include <string>

/**
 * @class ModuleManager
 * @brief 模組生命週期管理器
 */
class ModuleManager {
public:
  ModuleManager() : bus_(std::make_shared<EventBus>()) {}

  /**
   * @brief 註冊模組（在 initAll 之前呼叫）
   * @param module 模組實例（unique_ptr，所有權轉移）
   */
  void registerModule(std::unique_ptr<GcsModule> module) {
    modules_.push_back(std::move(module));
  }

  /**
   * @brief 依序初始化所有已註冊模組
   * @return 是否全部初始化成功
   */
  bool initAll();

  /**
   * @brief 反向關閉所有模組
   */
  void shutdownAll();

  /**
   * @brief 檢查所有模組健康狀態
   * @return 是否全部健康
   */
  bool healthCheckAll();

  /**
   * @brief 取得事件匯流排（供 main 或其他外部使用）
   */
  std::shared_ptr<EventBus> getEventBus() { return bus_; }

private:
  std::shared_ptr<EventBus> bus_;
  std::vector<std::unique_ptr<GcsModule>> modules_;
};
