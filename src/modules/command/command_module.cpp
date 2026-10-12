/**
 * @file command_module.cpp
 * @brief 命令模組實作 — 將驗證通過的命令發布至 ROS2 topic
 * @author Taiphoon GCS Team
 * @date 2026-08-05
 */
#include "modules/command/command_module.hpp"
#include "core/ws_protocol.hpp"
#include <iostream>
#include <stdexcept>
#include <exception>

namespace {

std::string safeAgent(const ws_protocol::CmdRequest& request) {
  return request.agent.empty() ? "unknown" : request.agent;
}

std::string safeCommand(const ws_protocol::CmdRequest& request) {
  return request.command.empty() ? "unknown" : request.command;
}

void publishErrorAck(EventBus* bus, const ws_protocol::CmdRequest& request,
                     const std::string& message) {
  if (bus == nullptr) {
    return;
  }
  bus->publish("cmd_ack", ws_protocol::makeCmdAck(
    safeAgent(request), safeCommand(request), "error", message));
}

}  // namespace

CommandModule::CommandModule() = default;

bool CommandModule::init(EventBus& bus) {
  bus_ = &bus;
  node_ = std::make_shared<rclcpp::Node>("gcs_command_node");

  // 建立全體廣播 publisher
  allPub_ = node_->create_publisher<std_msgs::msg::String>("/uav_all_agent/command", 10);

  // 訂閱 cmd_validated 事件
  bus_->subscribe("cmd_validated", [this](const std::string&, const std::any& data) {
    onCmdValidated(data);
  });

  // 訂閱離線事件，避免殘留不會再使用的單機 publisher
  bus_->subscribe("agent_offline", [this](const std::string&, const std::any& data) {
    onAgentOffline(data);
  });

  initialized_ = true;
  std::cout << "[CommandModule] 初始化完成" << std::endl;
  return true;
}

void CommandModule::shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  initialized_ = false;
  publishers_.clear();
  allPub_.reset();
  node_.reset();
}

bool CommandModule::healthCheck() {
  std::lock_guard<std::mutex> lock(mutex_);
  return initialized_ && node_ != nullptr;
}

bool CommandModule::hasPublisherFor(const std::string& agentNs) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return publishers_.find(agentNs) != publishers_.end();
}

void CommandModule::ensurePublisher(const std::string& agentNs) {
  if (node_ == nullptr) {
    throw std::runtime_error("CommandModule 節點尚未初始化");
  }
  if (publishers_.find(agentNs) != publishers_.end()) {
    return;
  }

  const std::string topic = "/" + agentNs + "/command";
  publishers_[agentNs] = node_->create_publisher<std_msgs::msg::String>(topic, 10);
  std::cout << "[CommandModule] 建立 publisher: " << topic << std::endl;
}

void CommandModule::onAgentOffline(const std::any& data) {
  try {
    const std::string agent = std::any_cast<std::string>(data);
    std::lock_guard<std::mutex> lock(mutex_);
    const auto erased = publishers_.erase(agent);
    if (erased > 0) {
      std::cout << "[CommandModule] 移除 publisher: " << agent << std::endl;
    }
  } catch (const std::exception& e) {
    std::cerr << "[CommandModule] agent_offline 處理失敗: " << e.what() << std::endl;
  }
}

void CommandModule::onCmdValidated(const std::any& data) {
  ws_protocol::CmdRequest request;
  try {
    auto jsonStr = std::any_cast<std::string>(data);
    std::string protocolError;
    if (!ws_protocol::parseCmdRequest(jsonStr, request, &protocolError)) {
      std::cerr << "[CommandModule] cmd_validated 格式錯誤: " << protocolError << std::endl;
      publishErrorAck(bus_, request, "命令格式錯誤: " + protocolError);
      return;
    }

    // 控制指令參數不得包含 NaN / Inf
    if (ws_protocol::hasNonFiniteParam(request)) {
      std::cerr << "[CommandModule] 命令參數含 NaN / Inf: " << request.command << std::endl;
      publishErrorAck(bus_, request, "命令參數包含 NaN / Inf");
      return;
    }

    const std::string cmdStr = ws_protocol::makeCommandLine(request);
    auto msg = std_msgs::msg::String();
    msg.data = cmdStr;

    std::lock_guard<std::mutex> lock(mutex_);
    if (request.agent == "all") {
      if (allPub_ == nullptr) {
        throw std::runtime_error("全體 publisher 尚未初始化");
      }
      allPub_->publish(msg);
      std::cout << "[CommandModule] 廣播命令: " << cmdStr << std::endl;
    } else {
      ensurePublisher(request.agent);
      auto it = publishers_.find(request.agent);
      if (it == publishers_.end()) {
        throw std::runtime_error("單機 publisher 建立失敗");
      }
      it->second->publish(msg);
      std::cout << "[CommandModule] 發布命令至 " << request.agent << ": " << cmdStr << std::endl;
    }

    // 回傳成功回執
    bus_->publish("cmd_ack", ws_protocol::makeCmdAck(
      request.agent, request.command, "ok"));
  } catch (const std::exception& e) {
    std::cerr << "[CommandModule] 處理異常: " << e.what() << std::endl;
    publishErrorAck(bus_, request, "CommandModule 處理失敗: " + std::string(e.what()));
  }
}
