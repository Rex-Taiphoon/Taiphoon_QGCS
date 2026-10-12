/**
 * @file ws_protocol.hpp
 * @brief WebSocket 命令協定的解析與序列化輔助
 * @author Taiphoon GCS Team
 * @date 2026-08-05
 *
 * 集中處理前端 cmd_request 的解析、命令列組裝與 cmd_ack 產生，
 * 讓 GuardModule / CommandModule 與前端型別共用同一份協定。
 */
#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace ws_protocol {

/**
 * @brief 驗證 GCS 關閉請求含明確的布林確認值
 * @param raw WebSocket 收到的 JSON 字串
 * @return 是否為已確認的 GCS 關閉請求
 */
bool is_confirmed_gcs_shutdown(const std::string& raw);

/**
 * @struct CmdRequest
 * @brief 前端 WebSocket 命令請求的解析結果
 */
struct CmdRequest {
  /// 目標 Agent（單機 namespace 或 "all"）
  std::string agent;
  /// 命令名稱（例如 ARM、TAKEOFF）
  std::string command;
  /// 命令參數（保留原始 JSON 型別，供格式化與數值檢查）
  std::vector<nlohmann::json> params;
};

/**
 * @struct AgentShutdownRequest
 * @brief 前端對單一 Agent 發出的關閉請求
 */
struct AgentShutdownRequest {
  /// 僅允許單一 Agent namespace，不可為 all
  std::string agent;
};

/**
 * @struct Ros2ExecRequest
 * @brief 前端除錯終端機送出的 ROS2 CLI 執行請求
 */
struct Ros2ExecRequest {
  /// 前端產生的請求識別碼，用於對應回應
  std::string request_id;
  /// 欲執行的指令；僅允許以 ros2 開頭
  std::string command;
};

/**
 * @brief 判斷原始訊息是否為 cmd_request
 * @param raw WebSocket 收到的原始 JSON 字串
 * @return 是否為 cmd_request 訊息
 */
bool isCmdRequest(const std::string& raw);

/**
 * @brief 判斷原始訊息是否為 agent_shutdown_request
 * @param raw WebSocket 收到的原始 JSON 字串
 * @return 是否為 Agent 關閉請求訊息
 */
bool isAgentShutdownRequest(const std::string& raw);

/**
 * @brief 判斷原始訊息是否為 ros2_exec_request
 * @param raw WebSocket 收到的原始 JSON 字串
 * @return 是否為 ROS2 執行請求訊息
 */
bool isRos2ExecRequest(const std::string& raw);

/**
 * @brief 解析 cmd_request 原始 JSON
 * @param raw WebSocket 收到的原始 JSON 字串
 * @param request 解析結果
 * @return 解析是否成功
 */
bool parseCmdRequest(const std::string& raw, CmdRequest& request);

/**
 * @brief 解析 cmd_request 原始 JSON，並回傳驗證錯誤
 * @param raw WebSocket 收到的原始 JSON 字串
 * @param request 解析結果
 * @param error 驗證失敗時的錯誤訊息（可為 nullptr）
 * @return 解析與驗證是否全部成功
 */
bool parseCmdRequest(const std::string& raw, CmdRequest& request, std::string* error);

/**
 * @brief 解析單機 Agent 關閉請求並回傳驗證錯誤
 * @param raw WebSocket 收到的原始 JSON 字串
 * @param request 解析結果
 * @param error 驗證失敗時的錯誤訊息（可為 nullptr）
 * @return 解析與驗證是否全部通過
 */
bool parseAgentShutdownRequest(const std::string& raw, AgentShutdownRequest& request,
                               std::string* error = nullptr);

/**
 * @brief 解析 ros2_exec_request 並回傳驗證錯誤
 * @param raw WebSocket 收到的原始 JSON 字串
 * @param request 解析結果
 * @param error 驗證失敗時的錯誤訊息（可為 nullptr）
 * @return 解析與驗證是否全部通過
 */
bool parseRos2ExecRequest(const std::string& raw, Ros2ExecRequest& request,
                          std::string* error = nullptr);

/**
 * @brief 驗證已解析的 cmd_request
 * @param request 已解析的命令請求
 * @return 空字串表示通過，否則為錯誤訊息
 */
std::string validateCmdRequest(const CmdRequest& request);

/**
 * @brief 檢查目標是否為合法 Agent（all 或 uav_{N}_agent）
 * @param agent 目標字串
 * @return 是否合法
 */
bool isAgentValid(const std::string& agent);

/**
 * @brief 檢查命令是否為編隊命令（FORMATION_*）
 * @param command 命令名稱
 * @return 是否為編隊命令
 */
bool isFormationCommand(const std::string& command);

/**
 * @brief 產生 cmd_ack JSON 字串
 * @param agent 目標 Agent
 * @param command 命令名稱
 * @param status 回執狀態（ok / error）
 * @param message 額外訊息（可為空）
 * @return JSON 字串
 */
std::string makeCmdAck(const std::string& agent, const std::string& command,
                       const std::string& status, const std::string& message = "");

/**
 * @brief 產生 ros2_exec_response JSON 字串
 * @param request_id 對應的請求識別碼
 * @param command 原始指令文字
 * @param status 執行狀態（ok / error / timeout）
 * @param output 合併的標準輸出與錯誤輸出（可為空）
 * @return JSON 字串
 */
std::string makeRos2ExecResponse(const std::string& request_id, const std::string& command,
                                 const std::string& status, const std::string& output);

/**
 * @brief 將命令與參數組合成 ROS2 topic 的命令列
 * @param request 已解析的命令請求
 * @return 命令列（例如 "FORMATION_CIRCLE 10 20"）
 */
std::string makeCommandLine(const CmdRequest& request);

/**
 * @brief 檢查命令參數是否包含 NaN / Inf
 * @param request 已解析的命令請求
 * @return 是否包含非有限數值
 */
bool hasNonFiniteParam(const CmdRequest& request);

}  // namespace ws_protocol
