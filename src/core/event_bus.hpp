/**
 * @file event_bus.hpp
 * @brief 事件匯流排 — 模組間唯一的跨模組通訊通道
 *
 * 採 publish/subscribe 模式。模組可發布事件至指定主題，
 * 其他模組可訂閱感興趣的主題。使用 type-erased 回呼 + mutex 保證執行緒安全。
 */
#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <any>
#include <memory>
#include <exception>
#include <iostream>

/// 事件回呼型別（接收 any 封裝的任意事件資料）
using EventCallback = std::function<void(const std::string& topic, const std::any& data)>;

/**
 * @class EventBus
 * @brief 執行緒安全的事件匯流排
 *
 * 模組透過 publish() 發布事件，透過 subscribe() 訂閱事件。
 * publish() 為同步呼叫：所有訂閱者的回呼會在 publish() 執行期間被呼叫。
 * 適合低延遲場景，但訂閱者回呼應避免長時間阻塞。
 */
class EventBus {
public:
  EventBus() = default;
  ~EventBus() = default;

  /// 禁止複製
  EventBus(const EventBus&) = delete;
  EventBus& operator=(const EventBus&) = delete;

  /**
   * @brief 發布事件至指定主題
   * @param topic 主題名稱（例如 "state_update"、"fleet_update"）
   * @param data 事件資料（以 std::any 封裝）
   */
  void publish(const std::string& topic, const std::any& data) {
    // 先複製訂閱者清單再釋放鎖，避免回呼內巢狀 publish() 造成重複鎖定
    std::vector<EventCallback> callbacks;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = subscribers_.find(topic);
      if (it != subscribers_.end()) {
        callbacks = it->second;
      }
    }
    for (auto& cb : callbacks) {
      try {
        cb(topic, data);
      } catch (const std::exception& e) {
        std::cerr << "[EventBus] 訂閱者例外，topic=" << topic
                  << ": " << e.what() << std::endl;
      } catch (...) {
        std::cerr << "[EventBus] 訂閱者未知例外，topic=" << topic << std::endl;
      }
    }
  }

  /**
   * @brief 訂閱指定主題的事件
   * @param topic 主題名稱
   * @param callback 事件發生時的回呼函式
   */
  void subscribe(const std::string& topic, EventCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_[topic].push_back(std::move(callback));
  }

private:
  std::mutex mutex_;
  std::unordered_map<std::string, std::vector<EventCallback>> subscribers_;
};
