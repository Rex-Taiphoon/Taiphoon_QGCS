/**
 * @file fleet_module.cpp
 * @brief 機隊管理模組實作
 *
 * 定期掃描 ROS2 網路中的 Agent state topic，
 * 偵測 Agent 上下線並通知 TelemetryModule 建立/取消訂閱。
 */
#include "modules/fleet/fleet_module.hpp"
#include "modules/telemetry/telemetry_module.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <regex>

using json = nlohmann::json;

// ============================================================================
// 建構
// ============================================================================
FleetModule::FleetModule() = default;

// ============================================================================
// 初始化
// ============================================================================
bool FleetModule::init(EventBus& bus) {
  bus_ = &bus;

  // 建立 ROS2 節點（使用與 TelemetryModule 不同的節點名稱）
  node_ = std::make_shared<rclcpp::Node>("gcs_fleet_node");

  // 以實際資料流更新最後可見時間，作為 DDS 發現快取的補充驗證
  bus_->subscribe("state_update", [this](const std::string&, const std::any& data) {
    onStateUpdate(data);
  });
  bus_->subscribe("agent_shutdown_accepted", [this](const std::string&, const std::any& data) {
    onAgentShutdownAccepted(data);
  });

  // 啟動定時掃描
  scanTimer_ = node_->create_wall_timer(
    std::chrono::duration<double>(SCAN_INTERVAL_S),
    std::bind(&FleetModule::scanTimerCallback, this));

  initialized_ = true;
  std::cout << "[FleetModule] 初始化完成，掃描週期: " << SCAN_INTERVAL_S << " 秒" << std::endl;
  return true;
}

// ============================================================================
// 關閉
// ============================================================================
void FleetModule::shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  initialized_ = false;
  onlineAgents_.clear();
  lastSeen_.clear();
  shutdownSuppressedAgents_.clear();
  scanTimer_.reset();
  node_.reset();
  std::cout << "[FleetModule] 已關閉" << std::endl;
}

// ============================================================================
// 健康檢查
// ============================================================================
bool FleetModule::healthCheck() {
  return initialized_ && node_ != nullptr;
}

// ============================================================================
// 取得線上 Agent 清單
// ============================================================================
std::set<std::string> FleetModule::getOnlineAgents() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return onlineAgents_;
}

// ============================================================================
// 從 topic 名稱提取 agent namespace
// ============================================================================
std::string FleetModule::extractAgentNs(const std::string& topicName) {
  // 格式：/uav_{N}_agent/state → 提取 "uav_{N}_agent"。
  // N 依介面規格採用 1-based 索引，且只辨識完整的 state topic。
  std::regex re("^/?(uav_[1-9][0-9]*_agent)/state$");
  std::smatch match;
  if (std::regex_search(topicName, match, re)) {
    return match[1].str();
  }
  return "";
}

// ============================================================================
// 定時掃描回呼
// ============================================================================
void FleetModule::scanTimerCallback() {
  if (!node_) return;

  // 取得 ROS2 網路上所有 topic
  auto topicMap = node_->get_topic_names_and_types();

  // 過濾出 Agent state topic
  std::set<std::string> currentAgents;
  for (const auto& [topicName, types] : topicMap) {
    std::string agentNs = extractAgentNs(topicName);
    if (!agentNs.empty()) {
      currentAgents.insert(agentNs);
    }
  }

  std::set<std::string> added;
  std::set<std::string> removed;
  std::set<std::string> verifiedOnline;
  std::set<std::string> staleAgents;
  const auto now = std::chrono::steady_clock::now();

  {
    std::lock_guard<std::mutex> lock(mutex_);

    // Shutdown 成功後，DDS graph 可能在短時間內仍保留舊 topic；
    // 直到 topic 消失前，避免將已關閉的 Agent 重新加入機隊。
    for (auto it = shutdownSuppressedAgents_.begin(); it != shutdownSuppressedAgents_.end();) {
      if (currentAgents.find(*it) == currentAgents.end()) {
        it = shutdownSuppressedAgents_.erase(it);
      } else {
        ++it;
      }
    }

    // 找出新增的 Agent（topic 首次被 DDS 發現）
    for (const auto& agent : currentAgents) {
      if (shutdownSuppressedAgents_.find(agent) == shutdownSuppressedAgents_.end()
          && onlineAgents_.find(agent) == onlineAgents_.end()) {
        added.insert(agent);
      }
    }

    // 找出 topic 已從 DDS 網路消失的 Agent
    for (const auto& agent : onlineAgents_) {
      if (currentAgents.find(agent) == currentAgents.end()) {
        removed.insert(agent);
      }
    }

    // 更新訂閱清單：新增者以發現時間作為寬限期起點
    for (const auto& agent : added) {
      onlineAgents_.insert(agent);
      lastSeen_[agent] = now;
    }
    for (const auto& agent : removed) {
      onlineAgents_.erase(agent);
      lastSeen_.erase(agent);
    }

    // 顯示清單以實際資料流為準：超過 STALE_TIMEOUT 未收到狀態即不顯示
    for (const auto& agent : onlineAgents_) {
      const auto seen = lastSeen_.find(agent);
      if (seen != lastSeen_.end() && now - seen->second <= STALE_TIMEOUT) {
        verifiedOnline.insert(agent);
      } else {
        staleAgents.insert(agent);
      }
    }
  }

  // 發布上線事件（通知 TelemetryModule 建立訂閱）
  for (const auto& agent : added) {
    std::cout << "[FleetModule] Agent 上線: " << agent << std::endl;
    bus_->publish("agent_online", agent);
  }

  // 發布離線事件（topic 已消失才通知取消訂閱，資料短暫停滯保留訂閱以利復原）
  for (const auto& agent : removed) {
    std::cout << "[FleetModule] Agent 離線: " << agent << std::endl;
    bus_->publish("agent_offline", agent);
  }

  // 發布機隊狀態更新（給前端）；online 只含仍有資料流的 Agent，
  // offline 含資料停滯與 topic 消失的 Agent，前端據此清除殘留顯示
  json fleetUpdate;
  fleetUpdate["type"] = "fleet_update";
  fleetUpdate["online"] = json::array();
  fleetUpdate["offline"] = json::array();
  for (const auto& a : verifiedOnline) {
    fleetUpdate["online"].push_back(a);
  }
  for (const auto& a : staleAgents) {
    fleetUpdate["offline"].push_back(a);
  }
  for (const auto& a : removed) {
    fleetUpdate["offline"].push_back(a);
  }
  bus_->publish("fleet_update", fleetUpdate.dump());
}

// ============================================================================
// 狀態資料流追蹤
// ============================================================================
void FleetModule::onStateUpdate(const std::any& data) {
  try {
    const auto rawJson = std::any_cast<std::string>(data);
    const json parsed = json::parse(rawJson);
    const std::string agent = parsed.value("agent", "");
    if (agent.empty()) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    lastSeen_[agent] = std::chrono::steady_clock::now();
  } catch (const std::exception& e) {
    std::cerr << "[FleetModule] state_update 解析失敗: " << e.what() << std::endl;
  }
}

void FleetModule::onAgentShutdownAccepted(const std::any& data) {
  try {
    const std::string agent = std::any_cast<std::string>(data);
    bool wasOnline = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      shutdownSuppressedAgents_.insert(agent);
      wasOnline = onlineAgents_.erase(agent) > 0;
      lastSeen_.erase(agent);
    }

    // 即使 Fleet 下一次掃描尚未執行，也要立即讓其他模組停止使用目標 Agent。
    if (bus_ != nullptr) {
      bus_->publish("agent_offline", agent);

      json fleetUpdate;
      fleetUpdate["type"] = "fleet_update";
      fleetUpdate["online"] = json::array();
      fleetUpdate["offline"] = json::array();
      fleetUpdate["offline"].push_back(agent);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& onlineAgent : onlineAgents_) {
          fleetUpdate["online"].push_back(onlineAgent);
        }
      }
      bus_->publish("fleet_update", fleetUpdate.dump());
    }

    std::cout << "[FleetModule] Shutdown Agent 已隔離: " << agent
              << (wasOnline ? "（原為線上）" : "（原本不在線上）") << std::endl;
  } catch (const std::exception& e) {
    std::cerr << "[FleetModule] agent_shutdown_accepted 處理失敗: " << e.what() << std::endl;
  }
}

// ============================================================================
// 手動觸發掃描
// ============================================================================
void FleetModule::scanNow() {
  scanTimerCallback();
}
