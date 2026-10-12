#pragma once

#include "core/gcs_module.hpp"
#include "core/event_bus.hpp"
#include <rclcpp/rclcpp.hpp>
#include <memory>
#include <string>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <chrono>

class FleetModule : public GcsModule {
public:
  FleetModule();

  bool init(EventBus& bus) override;
  void shutdown() override;
  bool healthCheck() override;
  std::string getName() const override { return "FleetModule"; }

  /// 手動觸發掃描
  void scanNow();

  /// 取得當前線上 Agent 清單
  std::set<std::string> getOnlineAgents() const;

  /// 取得 ROS2 節點（供 main 加入 executor）
  std::shared_ptr<rclcpp::Node> getNode() const { return node_; }

private:
  void scanTimerCallback();
  void onStateUpdate(const std::any& data);
  void onAgentShutdownAccepted(const std::any& data);
  static std::string extractAgentNs(const std::string& topicName);

  EventBus* bus_ = nullptr;
  std::shared_ptr<rclcpp::Node> node_;
  rclcpp::TimerBase::SharedPtr scanTimer_;
  std::set<std::string> onlineAgents_;
  /// Agent namespace → 最後一次收到 state_update 的時間
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastSeen_;
  /// 已接受 shutdown 的 Agent；直到 topic 消失前不重新加入機隊
  std::unordered_set<std::string> shutdownSuppressedAgents_;
  mutable std::mutex mutex_;
  bool initialized_ = false;
  static constexpr double SCAN_INTERVAL_S = 3.0;
  /// 超過此時間未收到狀態資料流即視為離線（DDS 發現快取可能殘留）
  static constexpr std::chrono::seconds STALE_TIMEOUT{5};
};
