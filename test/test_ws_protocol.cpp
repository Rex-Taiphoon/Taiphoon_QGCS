/**
 * @file test_ws_protocol.cpp
 * @brief WebSocket 命令協定單元測試
 * @author Taiphoon GCS Team
 * @date 2026-08-05
 */
#include "core/ws_protocol.hpp"
#include <gtest/gtest.h>
#include <limits>
#include <string>

using json = nlohmann::json;

TEST(WsProtocolTest, GcsShutdownRequiresExplicitBooleanConfirmation)
{
    EXPECT_TRUE(ws_protocol::is_confirmed_gcs_shutdown(R"({"type":"gcs_shutdown_request","confirmed":true})"));
    const std::vector<std::string> invalid = {
        R"({"type":"gcs_shutdown_request"})",
        R"({"type":"gcs_shutdown_request","confirmed":false})",
        R"({"type":"gcs_shutdown_request","confirmed":"true"})",
        R"({"type":"gcs_shutdown_request","confirmed":1})",
        R"({"type":"agent_shutdown_request","confirmed":true})",
        R"({"type":1,"confirmed":true})", "null", "[]", "not-json"
    };
    for (const auto& raw : invalid)
    {
        EXPECT_FALSE(ws_protocol::is_confirmed_gcs_shutdown(raw)) << raw;
    }
}

TEST(WsProtocolTest, ParseCmdRequestWithParams) {
  const std::string raw = R"({"type":"cmd_request","agent":"all","command":"FORMATION_CIRCLE","params":[10,20.5]})";
  ws_protocol::CmdRequest request;
  ASSERT_TRUE(ws_protocol::parseCmdRequest(raw, request));
  EXPECT_EQ(request.agent, "all");
  EXPECT_EQ(request.command, "FORMATION_CIRCLE");
  ASSERT_EQ(request.params.size(), 2u);
  EXPECT_EQ(ws_protocol::makeCommandLine(request), "FORMATION_CIRCLE 10 20.5");
}

TEST(WsProtocolTest, RejectNonCmdRequest) {
  EXPECT_FALSE(ws_protocol::isCmdRequest(R"({"type":"state_update","agent":"uav_1_agent"})"));
  ws_protocol::CmdRequest request;
  EXPECT_FALSE(ws_protocol::parseCmdRequest("not-json", request));
}

TEST(WsProtocolTest, MakeCmdAckContainsProtocolFields) {
  const std::string raw = ws_protocol::makeCmdAck("uav_1_agent", "ARM", "ok");
  const json ack = json::parse(raw);
  EXPECT_EQ(ack["type"], "cmd_ack");
  EXPECT_EQ(ack["agent"], "uav_1_agent");
  EXPECT_EQ(ack["command"], "ARM");
  EXPECT_EQ(ack["status"], "ok");
  EXPECT_FALSE(ack.contains("message"));
}

TEST(WsProtocolTest, DetectNonFiniteParam) {
  ws_protocol::CmdRequest request;
  request.agent = "uav_1_agent";
  request.command = "MOVE_FORWARD";
  request.params.push_back(json(std::numeric_limits<double>::quiet_NaN()));
  EXPECT_TRUE(ws_protocol::hasNonFiniteParam(request));

  request.params.clear();
  request.params.push_back(json(3.0));
  EXPECT_FALSE(ws_protocol::hasNonFiniteParam(request));
}

TEST(WsProtocolTest, AcceptsOnlyValidAgentFormats) {
  EXPECT_TRUE(ws_protocol::isAgentValid("all"));
  EXPECT_TRUE(ws_protocol::isAgentValid("uav_1_agent"));
  EXPECT_TRUE(ws_protocol::isAgentValid("uav_12_agent"));
  EXPECT_FALSE(ws_protocol::isAgentValid(""));
  EXPECT_FALSE(ws_protocol::isAgentValid("uav_1"));
  EXPECT_FALSE(ws_protocol::isAgentValid("all_agent"));
  EXPECT_FALSE(ws_protocol::isAgentValid("uav_agent"));
  EXPECT_FALSE(ws_protocol::isAgentValid("uav_0_agent"));
  EXPECT_FALSE(ws_protocol::isAgentValid("fmu1_agent"));
}

TEST(WsProtocolTest, RejectsEmptyAgentAndCommand) {
  ws_protocol::CmdRequest request;
  std::string error;
  const std::string raw = R"({"type":"cmd_request","agent":"","command":"ARM"})";
  EXPECT_FALSE(ws_protocol::parseCmdRequest(raw, request, &error));
  EXPECT_NE(error.find("agent"), std::string::npos);

  const std::string raw2 = R"({"type":"cmd_request","agent":"all","command":""})";
  EXPECT_FALSE(ws_protocol::parseCmdRequest(raw2, request, &error));
  EXPECT_NE(error.find("command"), std::string::npos);
}

TEST(WsProtocolTest, RejectsInvalidAgentAndParamTypes) {
  ws_protocol::CmdRequest request;
  std::string error;

  const std::string invalidAgent = R"({"type":"cmd_request","agent":"uav_1","command":"ARM"})";
  EXPECT_FALSE(ws_protocol::parseCmdRequest(invalidAgent, request, &error));
  EXPECT_NE(error.find("agent"), std::string::npos);

  const std::string invalidParam = R"({"type":"cmd_request","agent":"all",)"
    R"("command":"FORMATION_CIRCLE","params":[10,true]})";
  EXPECT_FALSE(ws_protocol::parseCmdRequest(invalidParam, request, &error));
  EXPECT_NE(error.find("參數"), std::string::npos);

  const std::string stringParam = R"({"type":"cmd_request","agent":"all",)"
    R"("command":"FORMATION_CIRCLE","params":["10",20]})";
  EXPECT_FALSE(ws_protocol::parseCmdRequest(stringParam, request, &error));
  EXPECT_NE(error.find("數字"), std::string::npos);
}

TEST(WsProtocolTest, RejectsWrongParamCount) {
  ws_protocol::CmdRequest request;
  std::string error;
  const std::string raw = R"({"type":"cmd_request","agent":"uav_1_agent","command":"MOVE_FORWARD","params":[]})";
  EXPECT_FALSE(ws_protocol::parseCmdRequest(raw, request, &error));
  EXPECT_NE(error.find("需要"), std::string::npos);
}

TEST(WsProtocolTest, AcceptsYawWithSingleFiniteAngle) {
  ws_protocol::CmdRequest request;
  std::string error;
  const std::string raw = R"({"type":"cmd_request","agent":"uav_1_agent","command":"YAW","params":[-90]})";

  ASSERT_TRUE(ws_protocol::parseCmdRequest(raw, request, &error));
  ASSERT_EQ(request.params.size(), 1u);
  EXPECT_DOUBLE_EQ(request.params.front().get<double>(), -90.0);
  EXPECT_EQ(ws_protocol::makeCommandLine(request), "YAW -90");
}

TEST(WsProtocolTest, RejectsYawWithoutExactlyOneParameter) {
  ws_protocol::CmdRequest request;
  std::string error;
  const std::string noParam = R"({"type":"cmd_request","agent":"uav_1_agent","command":"YAW"})";
  const std::string twoParams = R"({"type":"cmd_request","agent":"uav_1_agent","command":"YAW","params":[10,20]})";

  EXPECT_FALSE(ws_protocol::parseCmdRequest(noParam, request, &error));
  EXPECT_NE(error.find("需要"), std::string::npos);
  EXPECT_FALSE(ws_protocol::parseCmdRequest(twoParams, request, &error));
  EXPECT_NE(error.find("需要"), std::string::npos);
}

TEST(WsProtocolTest, RejectsNonFiniteParamDuringValidation) {
  ws_protocol::CmdRequest request;
  request.agent = "uav_1_agent";
  request.command = "MOVE_FORWARD";
  request.params.push_back(json(std::numeric_limits<double>::quiet_NaN()));
  const std::string error = ws_protocol::validateCmdRequest(request);
  EXPECT_NE(error.find("NaN"), std::string::npos);
}

TEST(WsProtocolTest, DetectsFormationCommands) {
  EXPECT_TRUE(ws_protocol::isFormationCommand("FORMATION_CIRCLE"));
  EXPECT_TRUE(ws_protocol::isFormationCommand("formation_cancel"));
  EXPECT_FALSE(ws_protocol::isFormationCommand("MODE_RTL"));
}

TEST(WsProtocolTest, ParsesSingleAgentShutdownRequest) {
  const std::string raw = R"({"type":"agent_shutdown_request","agent":"uav_3_agent"})";
  ws_protocol::AgentShutdownRequest request;
  std::string error;

  ASSERT_TRUE(ws_protocol::isAgentShutdownRequest(raw));
  ASSERT_TRUE(ws_protocol::parseAgentShutdownRequest(raw, request, &error));
  EXPECT_EQ(request.agent, "uav_3_agent");
}

TEST(WsProtocolTest, RejectsAllAgentShutdownRequest) {
  const std::string raw = R"({"type":"agent_shutdown_request","agent":"all"})";
  ws_protocol::AgentShutdownRequest request;
  std::string error;

  EXPECT_FALSE(ws_protocol::parseAgentShutdownRequest(raw, request, &error));
  EXPECT_NE(error.find("單一"), std::string::npos);
}

TEST(WsProtocolTest, ParsesRos2ExecRequest) {
  const std::string raw = R"({"type":"ros2_exec_request","request_id":"abc-123","command":"ros2 topic list"})";
  ws_protocol::Ros2ExecRequest request;
  std::string error;

  ASSERT_TRUE(ws_protocol::isRos2ExecRequest(raw));
  ASSERT_TRUE(ws_protocol::parseRos2ExecRequest(raw, request, &error));
  EXPECT_EQ(request.request_id, "abc-123");
  EXPECT_EQ(request.command, "ros2 topic list");
}

TEST(WsProtocolTest, RejectsNonRos2ExecCommand) {
  const std::string raw = R"({"type":"ros2_exec_request","request_id":"r1","command":"docker ps"})";
  ws_protocol::Ros2ExecRequest request;
  std::string error;

  EXPECT_FALSE(ws_protocol::parseRos2ExecRequest(raw, request, &error));
  EXPECT_NE(error.find("ros2"), std::string::npos);
}

TEST(WsProtocolTest, RejectsEmptyRos2ExecFields) {
  ws_protocol::Ros2ExecRequest request;
  std::string error;

  const std::string noId = R"({"type":"ros2_exec_request","command":"ros2 node list"})";
  EXPECT_FALSE(ws_protocol::parseRos2ExecRequest(noId, request, &error));
  EXPECT_NE(error.find("request_id"), std::string::npos);

  const std::string emptyCmd = R"({"type":"ros2_exec_request","request_id":"r2","command":""})";
  EXPECT_FALSE(ws_protocol::parseRos2ExecRequest(emptyCmd, request, &error));
  EXPECT_NE(error.find("command"), std::string::npos);
}

TEST(WsProtocolTest, MakeRos2ExecResponseContainsProtocolFields) {
  const std::string raw = ws_protocol::makeRos2ExecResponse("r1", "ros2 topic list", "ok", "output-line");
  const json response = json::parse(raw);
  EXPECT_EQ(response["type"], "ros2_exec_response");
  EXPECT_EQ(response["request_id"], "r1");
  EXPECT_EQ(response["command"], "ros2 topic list");
  EXPECT_EQ(response["status"], "ok");
  EXPECT_EQ(response["output"], "output-line");
}
