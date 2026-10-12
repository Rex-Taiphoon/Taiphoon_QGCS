/**
 * @file ws_protocol.cpp
 * @brief WebSocket 命令協定輔助實作
 * @author Taiphoon GCS Team
 * @date 2026-08-05
 */
#include "core/ws_protocol.hpp"
#include <cctype>
#include <cmath>
#include <iostream>
#include <regex>
#include <vector>
#include <cstddef>
#include <exception>

using json = nlohmann::json;

namespace ws_protocol {

bool is_confirmed_gcs_shutdown(const std::string& raw)
{
    const auto message = json::parse(raw, nullptr, false);
    if (!message.is_object())
    {
        return false;
    }
    const auto type = message.find("type");
    const auto confirmed = message.find("confirmed");
    return type != message.end() && type->is_string() && *type == "gcs_shutdown_request"
        && confirmed != message.end() && confirmed->is_boolean() && confirmed->get<bool>();
}

namespace {

/// 命令參數規格：僅記錄介面第 5 節允許的參數數量與型別
struct CommandSpec {
  const char* name;
  std::size_t min_params;
  std::size_t max_params;
};

const std::vector<CommandSpec>& commandSpecs() {
  static const std::vector<CommandSpec> specs = {
    {"MODE_RTL", 0, 0},
    {"MODE_LAND", 0, 0},
    {"MODE_OFFBOARD", 0, 0},
    {"HOLD", 0, 0},
    {"ARM", 0, 0},
    {"DISARM", 0, 0},
    {"TAKEOFF", 0, 1},
    {"MOVE_FORWARD", 1, 1},
    {"FORWARD", 1, 1},
    {"MOVE_BACKWARD", 1, 1},
    {"BACKWARD", 1, 1},
    {"MOVE_LEFT", 1, 1},
    {"LEFT", 1, 1},
    {"MOVE_RIGHT", 1, 1},
    {"RIGHT", 1, 1},
    {"MOVE_UP", 1, 1},
    {"MOVE_DOWN", 1, 1},
    {"YAW", 1, 1},
    {"MOVE_POINT", 4, 4},
    {"FORMATION_CIRCLE", 2, 2},
    {"FORMATION_HORI_LINEAR", 2, 2},
    {"FORMATION_VERT_LINEAR", 1, 1},
    {"FORMATION_CANCEL", 0, 0}
  };
  return specs;
}

std::string trim(const std::string& value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string toUpper(const std::string& value) {
  std::string result = value;
  for (auto& ch : result) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return result;
}

void setError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

const CommandSpec* findCommandSpec(const std::string& command) {
  const std::string upper = toUpper(trim(command));
  for (const auto& spec : commandSpecs()) {
    if (upper == spec.name) {
      return &spec;
    }
  }
  return nullptr;
}

}  // namespace

bool isCmdRequest(const std::string& raw) {
  try {
    auto j = json::parse(raw);
    return j.value("type", "") == "cmd_request";
  } catch (const std::exception& e) {
    std::cerr << "[WsProtocol] cmd_request 判斷失敗: " << e.what() << std::endl;
    return false;
  }
}

bool isAgentShutdownRequest(const std::string& raw) {
  try {
    const auto j = json::parse(raw);
    return j.value("type", "") == "agent_shutdown_request";
  } catch (const std::exception& e) {
    std::cerr << "[WsProtocol] agent_shutdown_request 判斷失敗: " << e.what() << std::endl;
    return false;
  }
}

bool isRos2ExecRequest(const std::string& raw) {
  try {
    const auto j = json::parse(raw);
    return j.value("type", "") == "ros2_exec_request";
  } catch (const std::exception& e) {
    std::cerr << "[WsProtocol] ros2_exec_request 判斷失敗: " << e.what() << std::endl;
    return false;
  }
}

bool parseCmdRequest(const std::string& raw, CmdRequest& request) {
  return parseCmdRequest(raw, request, nullptr);
}

bool parseCmdRequest(const std::string& raw, CmdRequest& request, std::string* error) {
  request = CmdRequest{};
  setError(error, "");
  try {
    auto j = json::parse(raw);
    if (j.value("type", "") != "cmd_request") {
      setError(error, "type 必須為 cmd_request");
      return false;
    }
    if (!j.contains("agent") || !j["agent"].is_string()) {
      setError(error, "agent 必須為字串");
      return false;
    }
    request.agent = j["agent"].get<std::string>();
    if (!j.contains("command") || !j["command"].is_string()) {
      setError(error, "command 必須為字串");
      return false;
    }
    request.command = j["command"].get<std::string>();

    if (j.contains("params")) {
      if (!j["params"].is_array()) {
        setError(error, "params 必須為陣列");
        return false;
      }
      for (const auto& p : j["params"]) {
        if (!p.is_number() && !p.is_string()) {
          setError(error, "參數必須為數字或字串");
          return false;
        }
        request.params.push_back(p);
      }
    }

    const std::string validationMessage = validateCmdRequest(request);
    if (!validationMessage.empty()) {
      setError(error, validationMessage);
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    setError(error, std::string("JSON 解析失敗: ") + e.what());
    return false;
  }
}

bool parseAgentShutdownRequest(const std::string& raw, AgentShutdownRequest& request,
                               std::string* error) {
  request = AgentShutdownRequest{};
  setError(error, "");
  try {
    const auto j = json::parse(raw);
    if (j.value("type", "") != "agent_shutdown_request") {
      setError(error, "type 必須為 agent_shutdown_request");
      return false;
    }
    if (!j.contains("agent") || !j["agent"].is_string()) {
      setError(error, "agent 必須為字串");
      return false;
    }
    request.agent = j["agent"].get<std::string>();
    if (request.agent == "all" || !isAgentValid(request.agent)) {
      setError(error, "關閉僅允許單一 uav_{N}_agent");
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    setError(error, std::string("JSON 解析失敗: ") + e.what());
    return false;
  }
}

bool parseRos2ExecRequest(const std::string& raw, Ros2ExecRequest& request,
                          std::string* error) {
  request = Ros2ExecRequest{};
  setError(error, "");
  try {
    const auto j = json::parse(raw);
    if (j.value("type", "") != "ros2_exec_request") {
      setError(error, "type 必須為 ros2_exec_request");
      return false;
    }
    if (!j.contains("request_id") || !j["request_id"].is_string()) {
      setError(error, "request_id 必須為字串");
      return false;
    }
    request.request_id = j["request_id"].get<std::string>();
    if (request.request_id.empty()) {
      setError(error, "request_id 不可為空");
      return false;
    }
    if (!j.contains("command") || !j["command"].is_string()) {
      setError(error, "command 必須為字串");
      return false;
    }
    request.command = trim(j["command"].get<std::string>());
    if (request.command.empty()) {
      setError(error, "command 不可為空");
      return false;
    }
    if (request.command.rfind("ros2", 0) != 0) {
      setError(error, "僅允許執行以 ros2 開頭的指令");
      return false;
    }
    return true;
  } catch (const std::exception& e) {
    setError(error, std::string("JSON 解析失敗: ") + e.what());
    return false;
  }
}

std::string validateCmdRequest(const CmdRequest& request) {
  if (!isAgentValid(request.agent)) {
    return "agent 僅允許 all 或 uav_{N}_agent";
  }
  if (trim(request.command).empty()) {
    return "command 不可為空";
  }

  const CommandSpec* spec = findCommandSpec(request.command);
  if (spec == nullptr) {
    return "不支援的命令: " + trim(request.command);
  }

  if (request.params.size() < spec->min_params ||
      request.params.size() > spec->max_params) {
    if (spec->min_params == spec->max_params) {
      return "命令 " + std::string(spec->name) + " 需要 " +
             std::to_string(spec->min_params) + " 個參數";
    }
    return "命令 " + std::string(spec->name) + " 需要 " +
           std::to_string(spec->min_params) + " 至 " +
           std::to_string(spec->max_params) + " 個參數";
  }

  for (const auto& p : request.params) {
    if (!p.is_number() && !p.is_string()) {
      return "參數必須為數字或字串";
    }
    if (p.is_number()) {
      const double value = p.get<double>();
      if (!std::isfinite(value)) {
        return "參數不得為 NaN 或 Inf";
      }
    } else {
      return "命令 " + std::string(spec->name) + " 的參數必須為數字";
    }
  }
  return "";
}

bool isAgentValid(const std::string& agent) {
  // 介面規格使用 1-based UAV 索引，避免將不存在的 uav_0_agent 當成合法目標。
  static const std::regex pattern(R"(^(all|uav_[1-9][0-9]*_agent)$)");
  return std::regex_match(agent, pattern);
}

bool isFormationCommand(const std::string& command) {
  const std::string upper = toUpper(trim(command));
  return upper.rfind("FORMATION_", 0) == 0;
}

std::string makeCmdAck(const std::string& agent, const std::string& command,
                       const std::string& status, const std::string& message) {
  json ack;
  ack["type"] = "cmd_ack";
  ack["agent"] = agent;
  ack["command"] = command;
  ack["status"] = status;
  if (!message.empty()) {
    ack["message"] = message;
  }
  return ack.dump();
}

std::string makeRos2ExecResponse(const std::string& request_id, const std::string& command,
                                 const std::string& status, const std::string& output) {
  json response;
  response["type"] = "ros2_exec_response";
  response["request_id"] = request_id;
  response["command"] = command;
  response["status"] = status;
  response["output"] = output;
  return response.dump();
}

std::string makeCommandLine(const CmdRequest& request) {
  std::string line = request.command;
  for (const auto& p : request.params) {
    line += " ";
    if (p.is_string()) {
      line += p.get<std::string>();
    } else {
      line += p.dump();
    }
  }
  return line;
}

bool hasNonFiniteParam(const CmdRequest& request) {
  for (const auto& p : request.params) {
    if (p.is_number()) {
      const double value = p.get<double>();
      if (!std::isfinite(value)) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace ws_protocol
