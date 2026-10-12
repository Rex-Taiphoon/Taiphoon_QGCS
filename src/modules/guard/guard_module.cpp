/**
 * @file guard_module.cpp
 * @brief 安全防呆模組實作 — 資料驅動轉移表
 *
 * 大多數安全邏輯集中於 initTransitionTable()；Arm／Takeoff 以 PX4 原生狀態驗證。
 * 新增狀態或命令時需同步檢視轉移表與 PX4 原生狀態規則。
 * @author Taiphoon GCS Team
 * @date 2026-08-05
 */
#include "modules/guard/guard_module.hpp"
#include "core/ws_protocol.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <algorithm>
#include <exception>

using json = nlohmann::json;

// ============================================================================
// 輔助：命令字串 → CmdType
// ============================================================================
CmdType GuardModule::parseCommand(const std::string& cmdStr) {
  // 轉大寫比對
  std::string upper = cmdStr;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

  if (upper == "ARM")                return CmdType::ARM;
  if (upper == "DISARM")             return CmdType::DISARM;
  if (upper == "TAKEOFF")            return CmdType::TAKEOFF;
  if (upper == "MOVE_FORWARD" || upper == "FORWARD")  return CmdType::MOVE_FORWARD;
  if (upper == "MOVE_BACKWARD" || upper == "BACKWARD") return CmdType::MOVE_BACKWARD;
  if (upper == "MOVE_LEFT" || upper == "LEFT")         return CmdType::MOVE_LEFT;
  if (upper == "MOVE_RIGHT" || upper == "RIGHT")       return CmdType::MOVE_RIGHT;
  if (upper == "MOVE_UP")            return CmdType::MOVE_UP;
  if (upper == "MOVE_DOWN")          return CmdType::MOVE_DOWN;
  if (upper == "YAW")                return CmdType::YAW;
  if (upper == "MOVE_POINT")         return CmdType::MOVE_POINT;
  if (upper == "HOLD")               return CmdType::HOLD;
  if (upper == "MODE_RTL")           return CmdType::MODE_RTL;
  if (upper == "MODE_LAND")          return CmdType::MODE_LAND;
  if (upper == "MODE_OFFBOARD")      return CmdType::MODE_OFFBOARD;
  if (upper == "FORMATION_CIRCLE")   return CmdType::FORMATION_CIRCLE;
  if (upper == "FORMATION_HORI_LINEAR") return CmdType::FORMATION_HORI_LINEAR;
  if (upper == "FORMATION_VERT_LINEAR") return CmdType::FORMATION_VERT_LINEAR;
  if (upper == "FORMATION_CANCEL")   return CmdType::FORMATION_CANCEL;
  return CmdType::UNKNOWN;
}

// ============================================================================
// 建構 / 生命週期
// ============================================================================
GuardModule::GuardModule() {
  initTransitionTable();
}

bool GuardModule::init(EventBus& bus) {
  bus_ = &bus;

  // 訂閱 state_update 事件（來自 TelemetryModule）
  bus_->subscribe("state_update", [this](const std::string&, const std::any& data) {
    onStateUpdate("", data);
  });

  // 訂閱機隊上下線事件（來自 FleetModule）
  bus_->subscribe("agent_online", [this](const std::string&, const std::any& data) {
    onAgentOnline(data);
  });
  bus_->subscribe("agent_offline", [this](const std::string&, const std::any& data) {
    onAgentOffline(data);
  });

  // 訂閱 cmd_request 事件（來自前端，經 EventBus）
  bus_->subscribe("cmd_request", [this](const std::string&, const std::any& data) {
    onCmdRequest(data);
  });

  initialized_ = true;
  std::cout << "[GuardModule] 初始化完成，轉移表已載入" << std::endl;
  return true;
}

void GuardModule::shutdown() {
  initialized_ = false;
}

bool GuardModule::healthCheck() {
  return initialized_;
}

// ============================================================================
// 查詢命令是否允許
// ============================================================================
bool GuardModule::isCommandAllowed(FlightState state, CmdType cmd) const {
  // 編隊命令不經 Guard 驗證（由 CommandModule 廣播至 /uav_all_agent/command）
  if (cmd == CmdType::FORMATION_CIRCLE ||
      cmd == CmdType::FORMATION_HORI_LINEAR ||
      cmd == CmdType::FORMATION_VERT_LINEAR ||
      cmd == CmdType::FORMATION_CANCEL) {
    return true;
  }

  auto it = transitionTable_.find(state);
  if (it == transitionTable_.end()) return false;
  return it->second.find(cmd) != it->second.end();
}

bool GuardModule::isCommandAllowedForAllLocked(CmdType cmd, std::string* deniedAgent) const {
  // 編隊命令不經 Guard 驗證（由 CommandModule 廣播至 /uav_all_agent/command）
  if (cmd == CmdType::FORMATION_CIRCLE ||
      cmd == CmdType::FORMATION_HORI_LINEAR ||
      cmd == CmdType::FORMATION_VERT_LINEAR ||
      cmd == CmdType::FORMATION_CANCEL) {
    return true;
  }

  if (onlineAgents_.empty()) {
    return false;
  }

  for (const auto& agent : onlineAgents_) {
    if (!hasValidPx4StateLocked(agent)) {
      if (deniedAgent != nullptr) {
        *deniedAgent = agent;
      }
      return false;
    }
    if (!isCommandAllowedForAgentLocked(agent, cmd)) {
      if (deniedAgent != nullptr) {
        *deniedAgent = agent;
      }
      return false;
    }
  }
  return true;
}

FlightState GuardModule::getAgentState(const std::string& agentNs) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto it = agentStates_.find(agentNs);
  return (it != agentStates_.end()) ? it->second : FlightState::IDLE;
}

bool GuardModule::isAgentOnlineLocked(const std::string& agentNs) const {
  return onlineAgents_.find(agentNs) != onlineAgents_.end();
}

bool GuardModule::hasValidPx4StateLocked(const std::string& agentNs) const {
  return validPx4StateAgents_.find(agentNs) != validPx4StateAgents_.end();
}

bool GuardModule::hasLandedPx4OffboardStateLocked(const std::string& agentNs) const {
  if (!hasValidPx4StateLocked(agentNs)) {
    return false;
  }

  const auto stateIt = agentPx4States_.find(agentNs);
  if (stateIt == agentPx4States_.end()) {
    return false;
  }

  const AgentPx4State& px4State = stateIt->second;
  return px4State.is_landed && px4State.nav_state == PX4_NAV_STATE_OFFBOARD;
}

bool GuardModule::isArmAllowedByPx4Locked(const std::string& agentNs) const {
  if (!hasLandedPx4OffboardStateLocked(agentNs)) {
    return false;
  }

  const auto stateIt = agentPx4States_.find(agentNs);
  return stateIt != agentPx4States_.end() && !stateIt->second.is_armed;
}

bool GuardModule::isTakeoffAllowedByPx4Locked(const std::string& agentNs) const {
  if (!hasLandedPx4OffboardStateLocked(agentNs)) {
    return false;
  }

  const auto stateIt = agentPx4States_.find(agentNs);
  return stateIt != agentPx4States_.end() && stateIt->second.is_armed;
}

bool GuardModule::isCommandAllowedForAgentLocked(const std::string& agentNs, CmdType cmd) const {
  if (!hasValidPx4StateLocked(agentNs)) {
    return false;
  }

  if (cmd == CmdType::ARM) {
    return isArmAllowedByPx4Locked(agentNs);
  }
  if (cmd == CmdType::TAKEOFF) {
    return isTakeoffAllowedByPx4Locked(agentNs);
  }

  const auto stateIt = agentStates_.find(agentNs);
  const FlightState state = stateIt != agentStates_.end() ? stateIt->second : FlightState::IDLE;
  return isCommandAllowed(state, cmd);
}

// ============================================================================
// 初始化轉移表（安全核心邏輯集中處）
// ============================================================================
void GuardModule::initTransitionTable() {
  using FS = FlightState;
  using CT = CmdType;

  // =========================================================================
  // 狀態轉移表
  //
  // 僅記錄「允許」的 (FlightState, CmdType) 組合。
  // 所有未列出的組合預設為拒絕。
  //
  // 新增狀態或命令時，僅需在此處增減對映關係。
  // =========================================================================

  // IDLE：可要求 Agent 切換至 Offboard 模式
  transitionTable_[FS::IDLE] = {
    CT::MODE_OFFBOARD
  };

  // OFFBOARD_INIT：等待 Offboard 啟用，可再次送出切換命令
  transitionTable_[FS::OFFBOARD_INIT] = {
    CT::MODE_OFFBOARD
  };

  // Arm 與 Takeoff 由已確認的 PX4 原生狀態驗證，不以任務階段推論。

  // ARMED：已解鎖，可 Disarm
  transitionTable_[FS::ARMED] = {
    CT::DISARM
  };

  // TAKEOFF：Agent 起飛後未轉 HOVER 時仍允許六向移動
  transitionTable_[FS::TAKEOFF] = {
    CT::MOVE_FORWARD, CT::MOVE_BACKWARD,
    CT::MOVE_LEFT, CT::MOVE_RIGHT,
    CT::MOVE_UP, CT::MOVE_DOWN
  };

  // HOVER：懸停中，可移動、返航、降落、上鎖
  transitionTable_[FS::HOVER] = {
    CT::MOVE_FORWARD, CT::MOVE_BACKWARD,
    CT::MOVE_LEFT, CT::MOVE_RIGHT,
    CT::MOVE_UP, CT::MOVE_DOWN,
    CT::YAW, CT::MOVE_POINT, CT::HOLD,
    CT::MODE_RTL, CT::MODE_LAND,
    CT::DISARM
  };

  // MOVING：移動中，可繼續移動、懸停、返航、降落
  transitionTable_[FS::MOVING] = {
    CT::MOVE_FORWARD, CT::MOVE_BACKWARD,
    CT::MOVE_LEFT, CT::MOVE_RIGHT,
    CT::MOVE_UP, CT::MOVE_DOWN,
    CT::YAW, CT::MOVE_POINT, CT::HOLD,
    CT::MODE_RTL, CT::MODE_LAND
  };

  // ERROR：錯誤狀態，僅允許 Disarm 緊急上鎖
  transitionTable_[FS::ERROR] = {
    CT::DISARM
  };

  // 以下狀態不接受外部命令（僅 RTL、LAND、POST_RTL）
  // 不需列舉 — 空集合即拒絕全部
}

// ============================================================================
// 處理遙測狀態更新
// ============================================================================
void GuardModule::onStateUpdate(const std::string&, const std::any& data) {
  try {
    auto jsonStr = std::any_cast<std::string>(data);
    auto j = json::parse(jsonStr);
    std::string agent = j["agent"];
    FlightState state = static_cast<FlightState>(j["state"]["flight_mode"].get<uint8_t>());
    bool isOnline = j["state"].value("online", true);
    bool isPx4StateValid = j["state"].value("px4_state_valid", false);
    AgentPx4State px4State;
    px4State.is_armed = j["state"].value("armed", false);
    px4State.is_landed = j["state"].value("landed", true);
    px4State.nav_state = j["state"].value("px4_nav_state", 0);

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    agentStates_[agent] = state;
    if (isOnline) {
      onlineAgents_.insert(agent);
      if (isPx4StateValid) {
        validPx4StateAgents_.insert(agent);
        agentPx4States_[agent] = px4State;
      } else {
        validPx4StateAgents_.erase(agent);
        agentPx4States_.erase(agent);
      }
    } else {
      onlineAgents_.erase(agent);
      agentStates_.erase(agent);
      validPx4StateAgents_.erase(agent);
      agentPx4States_.erase(agent);
    }
  } catch (const std::exception& e) {
    std::cerr << "[GuardModule] state_update 解析失敗: " << e.what() << std::endl;
  }
}

// ============================================================================
// 處理 Agent 上下線
// ============================================================================
void GuardModule::onAgentOnline(const std::any& data) {
  try {
    auto agent = std::any_cast<std::string>(data);
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    onlineAgents_.insert(agent);
  } catch (const std::exception& e) {
    std::cerr << "[GuardModule] agent_online 處理失敗: " << e.what() << std::endl;
  }
}

void GuardModule::onAgentOffline(const std::any& data) {
  try {
    auto agent = std::any_cast<std::string>(data);
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    onlineAgents_.erase(agent);
    agentStates_.erase(agent);
    validPx4StateAgents_.erase(agent);
    agentPx4States_.erase(agent);
  } catch (const std::exception& e) {
    std::cerr << "[GuardModule] agent_offline 處理失敗: " << e.what() << std::endl;
  }
}

// ============================================================================
// 處理 cmd_request（驗證 → 轉發或拒絕）
// ============================================================================
void GuardModule::onCmdRequest(const std::any& data) {
  try {
    auto jsonStr = std::any_cast<std::string>(data);
    ws_protocol::CmdRequest request;
    std::string protocolError;
    if (!ws_protocol::parseCmdRequest(jsonStr, request, &protocolError)) {
      const std::string agent = request.agent.empty() ? "unknown" : request.agent;
      const std::string command = request.command.empty() ? "unknown" : request.command;
      std::cerr << "[GuardModule] cmd_request 格式錯誤: " << protocolError << std::endl;
      rejectCommand(agent, command, "命令格式錯誤: " + protocolError);
      return;
    }

    CmdType cmd = parseCommand(request.command);
    if (cmd == CmdType::UNKNOWN) {
      rejectCommand(request.agent, request.command, "不支援的命令");
      return;
    }

    if (ws_protocol::isFormationCommand(request.command) && request.agent != "all") {
      rejectCommand(request.agent, request.command, "FORMATION_* 僅允許全體模式");
      return;
    }

    std::string guardError;
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      if (publishValidatedLocked(request.agent, cmd, jsonStr, &guardError)) {
        return;
      }
    }
    rejectCommand(request.agent, request.command, guardError);
  } catch (const std::exception& e) {
    std::cerr << "[GuardModule] cmd_request 處理異常: " << e.what() << std::endl;
  }
}

bool GuardModule::publishValidatedLocked(const std::string& agentNs, CmdType cmd,
                                          const std::string& rawJson, std::string* error) {
  if (bus_ == nullptr) {
    if (error != nullptr) {
      *error = "GuardModule 未初始化";
    }
    return false;
  }

  // Guard 的原子範圍涵蓋檢查與 EventBus 送出 cmd_validated。
  // CommandModule 隨後進行的 ROS2 publish 不在這把鎖內，因此無法與 Guard 驗證
  // 合併為單一原子操作。
  if (agentNs == "all") {
    std::string deniedAgent;
    if (!isCommandAllowedForAllLocked(cmd, &deniedAgent)) {
      if (error != nullptr) {
        if (deniedAgent.empty()) {
          *error = "沒有線上 Agent 可執行此命令";
        } else if (!hasValidPx4StateLocked(deniedAgent)) {
          *error = "Agent " + deniedAgent + " 的 PX4 狀態尚未有效，拒絕執行命令";
        } else if (cmd == CmdType::ARM || cmd == CmdType::TAKEOFF) {
          *error = "Agent " + deniedAgent + " 的 PX4 原生狀態不允許此命令";
        } else {
          *error = "Agent " + deniedAgent + " 目前狀態不允許此命令";
        }
      }
      return false;
    }
    bus_->publish("cmd_validated", rawJson);
    return true;
  }

  if (!isAgentOnlineLocked(agentNs)) {
    if (error != nullptr) {
      *error = "Agent 不在線上或尚未發現";
    }
    return false;
  }

  if (!hasValidPx4StateLocked(agentNs)) {
    if (error != nullptr) {
      *error = "PX4 狀態尚未有效，拒絕執行命令";
    }
    return false;
  }

  if (!isCommandAllowedForAgentLocked(agentNs, cmd)) {
    if (error != nullptr) {
      *error = (cmd == CmdType::ARM || cmd == CmdType::TAKEOFF)
        ? "PX4 原生狀態不允許此命令"
        : "目前狀態不允許此命令";
    }
    return false;
  }

  bus_->publish("cmd_validated", rawJson);
  return true;
}

void GuardModule::rejectCommand(const std::string& agent, const std::string& command,
                                const std::string& message) const {
  std::cout << "[GuardModule] 命令拒絕: agent=" << agent
            << " cmd=" << command << " reason=" << message << std::endl;
  if (bus_ != nullptr) {
    bus_->publish("cmd_ack", ws_protocol::makeCmdAck(agent, command, "error", message));
  }
}
