/**
 * @file gcs_module.hpp
 * @brief GcsModule 抽象基底類別 — 所有功能模組必須繼承此類別
 *
 * 提供 init() / shutdown() / healthCheck() 三個生命週期方法。
 * 模組之間嚴禁直接耦合，所有跨模組通訊必須透過 EventBus。
 */
#pragma once

#include <string>

// 前向宣告
class EventBus;

/**
 * @class GcsModule
 * @brief 地面控制站功能模組的抽象基底類別
 *
 * 使用方式：
 * 1. 繼承此類別，實作三個純虛擬方法
 * 2. 在 ModuleManager 中註冊模組實例
 * 3. ModuleManager 依相依順序呼叫 init() → 運行 → shutdown()
 */
class GcsModule {
public:
  virtual ~GcsModule() = default;

  /**
   * @brief 模組初始化
   * @param bus 事件匯流排參考，模組可在此訂閱或發布事件
   * @return 初始化是否成功
   */
  virtual bool init(EventBus& bus) = 0;

  /**
   * @brief 模組關閉，釋放資源
   */
  virtual void shutdown() = 0;

  /**
   * @brief 回報模組健康狀態
   * @return 模組是否正常運行
   */
  virtual bool healthCheck() = 0;

  /**
   * @brief 取得模組名稱（用於日誌與除錯）
   */
  virtual std::string getName() const = 0;
};
