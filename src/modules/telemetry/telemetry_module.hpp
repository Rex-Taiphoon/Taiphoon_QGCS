/**
 * @file telemetry_module.hpp
 * @brief 遙測模組 — 訂閱所有 Agent 對外 topic，聚合後透過 EventBus 推送
 */
#pragma once

#include "core/gcs_module.hpp"
#include "core/event_bus.hpp"
#include <rclcpp/rclcpp.hpp>
#include <taiphoon_agent/msg/agent_state.hpp>
#include <taiphoon_agent/msg/agent_attitude.hpp>
#include <taiphoon_agent/msg/agent_trajectory.hpp>
#include <taiphoon_agent/msg/ned_target.hpp>
#include <taiphoon_agent/msg/waypoint_sequence.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <nlohmann/json.hpp>

class TelemetryModule : public GcsModule {
public:
    /**
     * @brief 保留 WGS84 雙精度座標並將非有限值轉為 null
     * @param msg Agent 狀態
     * @param agentNs Agent 命名空間
     * @return WebSocket 狀態訊息
     */
    static nlohmann::json serialize_state(
        const taiphoon_agent::msg::AgentState& msg, const std::string& agentNs);

  TelemetryModule();

  bool init(EventBus& bus) override;
  void shutdown() override;
  bool healthCheck() override;
  std::string getName() const override { return "TelemetryModule"; }

  void subscribeAgent(const std::string& agentNs);
  void unsubscribeAgent(const std::string& agentNs);

  std::shared_ptr<rclcpp::Node> getNode() const { return node_; }

private:
  void ensureNode();

  struct AgentSubscriptions {
    rclcpp::Subscription<taiphoon_agent::msg::AgentState>::SharedPtr stateSub;
    rclcpp::Subscription<taiphoon_agent::msg::AgentAttitude>::SharedPtr attitudeSub;
    rclcpp::Subscription<taiphoon_agent::msg::NedTarget>::SharedPtr nedTargetSub;
    rclcpp::Subscription<taiphoon_agent::msg::WaypointSequence>::SharedPtr waypointsSub;
    rclcpp::Subscription<taiphoon_agent::msg::AgentTrajectory>::SharedPtr trajectorySub;
  };

  EventBus* bus_ = nullptr;
  std::shared_ptr<rclcpp::Node> node_;
  std::unordered_map<std::string, AgentSubscriptions> subscriptions_;
  std::mutex mutex_;
  bool initialized_ = false;
};
