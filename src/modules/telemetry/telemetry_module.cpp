/**
 * @file telemetry_module.cpp
 * @brief 遙測模組實作
 * @author Taiphoon GCS Team
 * @date 2026-08-05
 */
#include "modules/telemetry/telemetry_module.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <iostream>

using json = nlohmann::json;

namespace {

/// 將有限浮點值轉為 JSON 數字，NaN / Inf 轉為 null
json finiteNumberOrNull(double value) {
  if (std::isfinite(value)) {
    return value;
  }
  return nullptr;
}

}  // namespace

// ============================================================================
// 輔助函式：ROS2 訊息轉 JSON
// ============================================================================
nlohmann::json TelemetryModule::serialize_state(
    const taiphoon_agent::msg::AgentState& msg, const std::string& agentNs)
{
  return {
    {"type", "state_update"},
    {"agent", agentNs},
    {"state", {
      {"stamp", {
        {"sec", msg.stamp.sec},
        {"nanosec", msg.stamp.nanosec}
      }},
      {"position_ned", {msg.position_ned[0], msg.position_ned[1], msg.position_ned[2]}},
      {"position_global", {
        finiteNumberOrNull(msg.position_global[0]),
        finiteNumberOrNull(msg.position_global[1]),
        finiteNumberOrNull(msg.position_global[2])
      }},
      {"flight_mode", msg.flight_mode},
      {"battery_remaining", msg.battery_remaining},
      {"battery_voltage_v", finiteNumberOrNull(msg.battery_voltage_v)},
      {"battery_current_a", finiteNumberOrNull(msg.battery_current_a)},
      {"online", msg.online},
      {"armed", msg.armed},
      {"landed", msg.landed},
      {"px4_nav_state", msg.px4_nav_state},
      {"px4_state_valid", msg.px4_state_valid}
    }}
  };
}

static json attitudeToJson(const taiphoon_agent::msg::AgentAttitude& msg, const std::string& agentNs) {
  return {
    {"type", "attitude_update"},
    {"agent", agentNs},
    {"attitude", {
      {"q", {msg.q[0], msg.q[1], msg.q[2], msg.q[3]}}
    }}
  };
}

static json nedTargetToJson(const taiphoon_agent::msg::NedTarget& msg, const std::string& agentNs) {
  return {
    {"type", "ned_target"},
    {"agent", agentNs},
    {"target", {
      {"x", msg.x},
      {"y", msg.y},
      {"z", msg.z},
      {"yaw", msg.yaw}
    }}
  };
}

static json waypointsToJson(const taiphoon_agent::msg::WaypointSequence& msg, const std::string& agentNs) {
  json wps = json::array();
  for (const auto& wp : msg.waypoints) {
    wps.push_back({
      {"position_ned", {wp.position_ned[0], wp.position_ned[1], wp.position_ned[2]}},
      {"arrival_time", {
        {"sec", wp.arrival_time.sec},
        {"nanosec", wp.arrival_time.nanosec}
      }}
    });
  }
  return {
    {"type", "waypoints"},
    {"agent", agentNs},
    {"waypoints", wps}
  };
}

static json trajectoryToJson(const taiphoon_agent::msg::AgentTrajectory& msg,
                             const std::string& agentNs) {
  json controlPoints = json::array();
  for (const double value : msg.control_points) {
    controlPoints.push_back(value);
  }
  return {
    {"type", "trajectory_update"},
    {"agent", agentNs},
    {"trajectory", {
      {"stamp", {
        {"sec", msg.stamp.sec},
        {"nanosec", msg.stamp.nanosec}
      }},
      {"start_time", {
        {"sec", msg.start_time.sec},
        {"nanosec", msg.start_time.nanosec}
      }},
      {"trajectory_id", msg.trajectory_id},
      {"agent_index", msg.agent_index},
      {"coordinate_frame", msg.coordinate_frame},
      {"frame_revision", msg.frame_revision},
      {"order", msg.order},
      {"knot_interval_s", msg.knot_interval_s},
      {"control_points", controlPoints},
      {"duration_s", msg.duration_s},
      {"terminal_hold", msg.terminal_hold},
      {"valid", msg.valid}
    }}
  };
}

// ============================================================================
// 建構 / 生命週期
// ============================================================================
TelemetryModule::TelemetryModule() = default;

bool TelemetryModule::init(EventBus& bus) {
  bus_ = &bus;
  initialized_ = true;
  ensureNode();
  std::cout << "[TelemetryModule] 初始化完成" << std::endl;
  return true;
}

void TelemetryModule::shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  subscriptions_.clear();
  node_.reset();
}

bool TelemetryModule::healthCheck() {
  return initialized_;
}

void TelemetryModule::ensureNode() {
  if (!node_) {
    node_ = std::make_shared<rclcpp::Node>("gcs_telemetry_node");
  }
}

// ============================================================================
// 訂閱 / 取消訂閱
// ============================================================================
void TelemetryModule::subscribeAgent(const std::string& agentNs) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (subscriptions_.find(agentNs) != subscriptions_.end()) return;
  ensureNode();

  auto* bus = bus_;
  std::string ns = agentNs;
  auto node = node_;
  AgentSubscriptions subs;

  std::string stateT = "/" + agentNs + "/state";
  std::string attT = "/" + agentNs + "/attitude";
  std::string nedT = "/" + agentNs + "/ned_target";
  std::string wpT = "/" + agentNs + "/waypoints";
  std::string trajectoryT = "/" + agentNs + "/planning/trajectory";

  // AgentState
  subs.stateSub = node_->create_subscription<taiphoon_agent::msg::AgentState>(
    stateT, 10,
    [bus, node, ns](taiphoon_agent::msg::AgentState::ConstSharedPtr msg) {
      if (msg == nullptr) {
        RCLCPP_WARN(node->get_logger(), "[TelemetryModule] %s state 收到空指標", ns.c_str());
        return;
      }
      bus->publish("state_update", serialize_state(*msg, ns).dump());
    });

  // AgentAttitude
  subs.attitudeSub = node_->create_subscription<taiphoon_agent::msg::AgentAttitude>(
    attT, 10,
    [bus, node, ns](taiphoon_agent::msg::AgentAttitude::ConstSharedPtr msg) {
      if (msg == nullptr) {
        RCLCPP_WARN(node->get_logger(), "[TelemetryModule] %s attitude 收到空指標", ns.c_str());
        return;
      }
      bus->publish("attitude_update", attitudeToJson(*msg, ns).dump());
    });

  // NedTarget
  subs.nedTargetSub = node_->create_subscription<taiphoon_agent::msg::NedTarget>(
    nedT, 10,
    [bus, node, ns](taiphoon_agent::msg::NedTarget::ConstSharedPtr msg) {
      if (msg == nullptr) {
        RCLCPP_WARN(node->get_logger(), "[TelemetryModule] %s ned_target 收到空指標", ns.c_str());
        return;
      }
      bus->publish("ned_target", nedTargetToJson(*msg, ns).dump());
    });

  // WaypointSequence
  subs.waypointsSub = node_->create_subscription<taiphoon_agent::msg::WaypointSequence>(
    wpT, 10,
    [bus, node, ns](taiphoon_agent::msg::WaypointSequence::ConstSharedPtr msg) {
      if (msg == nullptr) {
        RCLCPP_WARN(node->get_logger(), "[TelemetryModule] %s waypoints 收到空指標", ns.c_str());
        return;
      }
      bus->publish("waypoints", waypointsToJson(*msg, ns).dump());
    });

  // EGO-Swarm 未來軌跡（WGS84 控制點，供前端以折線繪製）
  subs.trajectorySub = node_->create_subscription<taiphoon_agent::msg::AgentTrajectory>(
    trajectoryT, 10,
    [bus, node, ns](taiphoon_agent::msg::AgentTrajectory::ConstSharedPtr msg) {
      if (msg == nullptr) {
        RCLCPP_WARN(node->get_logger(), "[TelemetryModule] %s trajectory 收到空指標", ns.c_str());
        return;
      }
      bus->publish("trajectory_update", trajectoryToJson(*msg, ns).dump());
    });

  subscriptions_[agentNs] = std::move(subs);
  std::cout << "[TelemetryModule] 已訂閱 Agent: " << agentNs << std::endl;
}

void TelemetryModule::unsubscribeAgent(const std::string& agentNs) {
  std::lock_guard<std::mutex> lock(mutex_);
  subscriptions_.erase(agentNs);
  std::cout << "[TelemetryModule] 已取消訂閱 Agent: " << agentNs << std::endl;
}
