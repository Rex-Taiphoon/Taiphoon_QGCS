/**
 * @file command_module.hpp
 * @brief 命令模組 — 將驗證通過的命令發布至 ROS2 topic
 *
 * 訂閱 EventBus 的 cmd_validated 事件，依目標 Agent 路由至：
 * - /{agent_namespace}/command（單機）
 * - /uav_all_agent/command（全體）
 * @author Taiphoon GCS Team
 * @date 2026-08-05
 */
#pragma once

#include "core/gcs_module.hpp"
#include "core/event_bus.hpp"
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

class CommandModule : public GcsModule {
public:
  CommandModule();

  bool init(EventBus& bus) override;
  void shutdown() override;
  bool healthCheck() override;
  std::string getName() const override { return "CommandModule"; }

  std::shared_ptr<rclcpp::Node> getNode() const { return node_; }

  /// 查詢指定 Agent 是否已有單機 publisher
  bool hasPublisherFor(const std::string& agentNs) const;

private:
  /// 確保指定 Agent 的 publisher 存在
  /// 呼叫端必須持有 mutex_
  void ensurePublisher(const std::string& agentNs);

  /// 處理 Agent 離線事件，清理單機 publisher
  void onAgentOffline(const std::any& data);

  /// 處理 cmd_validated 事件
  void onCmdValidated(const std::any& data);

  EventBus* bus_ = nullptr;
  std::shared_ptr<rclcpp::Node> node_;
  mutable std::mutex mutex_;
  bool initialized_ = false;

  /// Agent namespace → publisher
  std::unordered_map<std::string, rclcpp::Publisher<std_msgs::msg::String>::SharedPtr> publishers_;

  /// 全體廣播 publisher
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr allPub_;
};
