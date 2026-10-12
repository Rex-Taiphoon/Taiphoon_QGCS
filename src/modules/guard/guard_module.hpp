/**
 * @file guard_module.hpp
 * @brief 安全防呆模組 — 命令發送前驗證狀態機合法性
 *
 * 以資料驅動轉移表維護每台 Agent 的 FlightState → 允許命令對映。
 * 攔截前端 cmd_request，驗證通過才轉發 cmd_validated 事件。
 * @author Taiphoon GCS Team
 * @date 2026-08-05
 */
#pragma once

#include "core/gcs_module.hpp"
#include "core/event_bus.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

/// 命令型別列舉（與 Agent 命令協定對應）
enum class CmdType {
  ARM, DISARM, TAKEOFF,
  MOVE_FORWARD, MOVE_BACKWARD, MOVE_LEFT, MOVE_RIGHT, MOVE_UP, MOVE_DOWN,
  YAW, MOVE_POINT, HOLD,
  MODE_RTL, MODE_LAND, MODE_OFFBOARD,
  FORMATION_CIRCLE, FORMATION_HORI_LINEAR, FORMATION_VERT_LINEAR, FORMATION_CANCEL,
  UNKNOWN
};

/// 飛行狀態枚舉（與 AgentState.flight_mode 對齊）
enum class FlightState : uint8_t {
  IDLE = 0, OFFBOARD_INIT = 1, OFFBOARD_ACTIVE = 2,
  ARMED = 3, TAKEOFF = 4, HOVER = 5,
  RTL = 6, LAND = 7, MOVING = 8, POST_RTL = 9, ERROR = 10
};

class GuardModule : public GcsModule {
public:
  GuardModule();

  bool init(EventBus& bus) override;
  void shutdown() override;
  bool healthCheck() override;
  std::string getName() const override { return "GuardModule"; }

  /// 查詢指定狀態下是否允許指定命令
  bool isCommandAllowed(FlightState state, CmdType cmd) const;

  /// 命令字串轉 CmdType
  static CmdType parseCommand(const std::string& cmdStr);

  /// 取得指定 Agent 的當前狀態
  FlightState getAgentState(const std::string& agentNs) const;

private:
  /**
   * @brief 從 AgentState 保留的 PX4 原生安全事實
   *
   * 僅在 px4_state_valid=true 時寫入；Arm 與 Takeoff 不以任務階段推論。
   */
  struct AgentPx4State {
    bool is_armed = false;
    bool is_landed = true;
    uint8_t nav_state = 0;
  };

  /// PX4 VehicleStatus::NAVIGATION_STATE_OFFBOARD 的介面版本常數
  static constexpr uint8_t PX4_NAV_STATE_OFFBOARD = 14;

  /// 初始化轉移表
  void initTransitionTable();

  /// 處理 state_update 事件（追蹤 Agent 狀態）
  void onStateUpdate(const std::string& agentNs, const std::any& data);

  /// 處理 cmd_request 事件（驗證命令）
  void onCmdRequest(const std::any& data);

  /// 處理 Agent 上線事件
  void onAgentOnline(const std::any& data);

  /// 處理 Agent 離線事件
  void onAgentOffline(const std::any& data);

  /// 查詢 Agent 是否已發現且在線（呼叫端必須持有 mutex_）
  bool isAgentOnlineLocked(const std::string& agentNs) const;

  /// 查詢 Agent 的 PX4 狀態是否已由最新版 AgentState 確認有效（呼叫端必須持有 mutex_）
  bool hasValidPx4StateLocked(const std::string& agentNs) const;

  /// 查詢 Agent 是否處於已確認的 PX4 Offboard 且已落地狀態（呼叫端必須持有 mutex_）
  bool hasLandedPx4OffboardStateLocked(const std::string& agentNs) const;

  /// 查詢 PX4 原生狀態是否允許 Arm（呼叫端必須持有 mutex_）
  bool isArmAllowedByPx4Locked(const std::string& agentNs) const;

  /// 查詢 PX4 原生狀態是否允許 Takeoff（呼叫端必須持有 mutex_）
  bool isTakeoffAllowedByPx4Locked(const std::string& agentNs) const;

  /// 查詢單機是否可執行命令（呼叫端必須持有 mutex_）
  bool isCommandAllowedForAgentLocked(const std::string& agentNs, CmdType cmd) const;

  /// 全體命令驗證：所有線上 Agent 都必須允許該命令（呼叫端必須持有 mutex_）
  bool isCommandAllowedForAllLocked(CmdType cmd, std::string* deniedAgent) const;

  /// 在同一把 mutex_ 內完成驗證並發布 cmd_validated（呼叫端必須持有 mutex_）
  bool publishValidatedLocked(const std::string& agentNs, CmdType cmd,
                              const std::string& rawJson, std::string* error);

  /// 拒絕命令並發布 cmd_ack 錯誤回執
  void rejectCommand(const std::string& agent, const std::string& command,
                     const std::string& message) const;

  EventBus* bus_ = nullptr;
  mutable std::recursive_mutex mutex_;
  bool initialized_ = false;

  /// 資料驅動轉移表：FlightState → 允許的 CmdType 集合
  std::unordered_map<FlightState, std::unordered_set<CmdType>> transitionTable_;

  /// 每台 Agent 的當前 FlightState
  std::unordered_map<std::string, FlightState> agentStates_;

  /// 目前線上的 Agent namespace 集合
  std::unordered_set<std::string> onlineAgents_;

  /// 已收到 px4_state_valid=true 的 Agent namespace 集合
  std::unordered_set<std::string> validPx4StateAgents_;

  /// Agent namespace → 已確認有效的 PX4 原生狀態
  std::unordered_map<std::string, AgentPx4State> agentPx4States_;
};
