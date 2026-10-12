/**
 * @file test_command_module.cpp
 * @brief CommandModule 單元測試 — cmd_validated 路由、回執與 publisher 清理
 * @author Taiphoon GCS Team
 * @date 2026-08-05
 */
#include "modules/command/command_module.hpp"
#include "core/event_bus.hpp"
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include <nlohmann/json.hpp>
#include <any>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

using json = nlohmann::json;

namespace {

std::string makeCmdValidated(const std::string& agent, const std::string& command) {
  json j;
  j["type"] = "cmd_request";
  j["agent"] = agent;
  j["command"] = command;
  return j.dump();
}

}  // namespace

class CommandModuleTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    const int argc = 1;
    const char* argv[] = {"test_command_module"};
    rclcpp::init(argc, argv);
  }

  static void TearDownTestSuite() {
    rclcpp::shutdown();
  }

  void SetUp() override {
    commandModule_ = std::make_unique<CommandModule>();
    ASSERT_TRUE(commandModule_->init(bus_));
  }

  void TearDown() override {
    commandModule_->shutdown();
    commandModule_.reset();
  }

  EventBus bus_;
  std::unique_ptr<CommandModule> commandModule_;
};

TEST_F(CommandModuleTest, ValidSingleAgentCmdValidatedPublishesAndAcksOk) {
  std::string ack;
  bus_.subscribe("cmd_ack", [&ack](const std::string&, const std::any& data) {
    ack = std::any_cast<std::string>(data);
  });

  bus_.publish("cmd_validated", makeCmdValidated("uav_1_agent", "MODE_OFFBOARD"));

  ASSERT_FALSE(ack.empty());
  const json parsed = json::parse(ack);
  EXPECT_EQ(parsed["type"], "cmd_ack");
  EXPECT_EQ(parsed["agent"], "uav_1_agent");
  EXPECT_EQ(parsed["command"], "MODE_OFFBOARD");
  EXPECT_EQ(parsed["status"], "ok");
  EXPECT_TRUE(commandModule_->hasPublisherFor("uav_1_agent"));
}

TEST_F(CommandModuleTest, ValidAllModeCmdValidatedPublishesAndAcksOk) {
  std::string ack;
  bus_.subscribe("cmd_ack", [&ack](const std::string&, const std::any& data) {
    ack = std::any_cast<std::string>(data);
  });

  bus_.publish("cmd_validated", makeCmdValidated("all", "MODE_RTL"));

  ASSERT_FALSE(ack.empty());
  const json parsed = json::parse(ack);
  EXPECT_EQ(parsed["type"], "cmd_ack");
  EXPECT_EQ(parsed["agent"], "all");
  EXPECT_EQ(parsed["command"], "MODE_RTL");
  EXPECT_EQ(parsed["status"], "ok");

    // DDS graph 非同步更新，必須等待探索完成，不能假設發布後立即可見。
    constexpr auto GRAPH_DISCOVERY_TIMEOUT = std::chrono::seconds(5);
    constexpr auto GRAPH_POLL_INTERVAL = std::chrono::milliseconds(20);
    const auto node = commandModule_->getNode();
    ASSERT_NE(node, nullptr);
    const auto deadline = std::chrono::steady_clock::now() + GRAPH_DISCOVERY_TIMEOUT;
    auto topics = node->get_topic_names_and_types();
    while (topics.find("/uav_all_agent/command") == topics.end()
           && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(GRAPH_POLL_INTERVAL);
        topics = node->get_topic_names_and_types();
    }
    EXPECT_NE(topics.find("/uav_all_agent/command"), topics.end());
}

TEST_F(CommandModuleTest, MalformedCmdValidatedReturnsErrorAck) {
  std::string ack;
  bus_.subscribe("cmd_ack", [&ack](const std::string&, const std::any& data) {
    ack = std::any_cast<std::string>(data);
  });

  bus_.publish("cmd_validated", std::string("not-json"));

  ASSERT_FALSE(ack.empty());
  const json parsed = json::parse(ack);
  EXPECT_EQ(parsed["type"], "cmd_ack");
  EXPECT_EQ(parsed["status"], "error");
  EXPECT_NE(parsed["message"].get<std::string>().find("命令格式錯誤"), std::string::npos);
}

TEST_F(CommandModuleTest, AgentOfflineRemovesSinglePublisher) {
  bus_.publish("cmd_validated", makeCmdValidated("uav_1_agent", "MODE_OFFBOARD"));
  ASSERT_TRUE(commandModule_->hasPublisherFor("uav_1_agent"));

  bus_.publish("agent_offline", std::string("uav_1_agent"));

  EXPECT_FALSE(commandModule_->hasPublisherFor("uav_1_agent"));
}

TEST_F(CommandModuleTest, AgentOfflineDoesNotDisableAllAgentBroadcast) {
  std::string ack;
  bus_.subscribe("cmd_ack", [&ack](const std::string&, const std::any& data) {
    ack = std::any_cast<std::string>(data);
  });

  bus_.publish("cmd_validated", makeCmdValidated("all", "MODE_RTL"));
  bus_.publish("agent_offline", std::string("uav_1_agent"));
  ack.clear();
  bus_.publish("cmd_validated", makeCmdValidated("all", "MODE_LAND"));

  ASSERT_FALSE(ack.empty());
  const json parsed = json::parse(ack);
  EXPECT_EQ(parsed["agent"], "all");
  EXPECT_EQ(parsed["command"], "MODE_LAND");
  EXPECT_EQ(parsed["status"], "ok");
}
