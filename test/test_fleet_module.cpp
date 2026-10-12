/**
 * @file test_fleet_module.cpp
 * @brief FleetModule shutdown 隔離與 DDS topic 暫存測試
 * @author Taiphoon GCS Team
 * @date 2026-08-20
 */
#include "modules/fleet/fleet_module.hpp"
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include <taiphoon_agent/msg/agent_state.hpp>
#include <any>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

using namespace std::chrono_literals;

class FleetModuleTest : public ::testing::Test
{
protected:
  // 每個案例使用獨立 ROS2 context，避免上一案例的 DDS graph 快取殘留。
  void SetUp() override
  {
    const int argc = 1;
    const char* argv[] = {"test_fleet_module"};
    rclcpp::init(argc, argv);
  }

  void TearDown() override
  {
    rclcpp::shutdown();
  }
};

TEST_F(FleetModuleTest, ShutdownAcceptedImmediatelyRemovesAndSuppressesAgent)
{
  EventBus bus;
  FleetModule fleet;
  ASSERT_TRUE(fleet.init(bus));

  auto publisherNode = std::make_shared<rclcpp::Node>("fleet_test_agent_node");
  auto publisher = publisherNode->create_publisher<taiphoon_agent::msg::AgentState>(
    "/uav_1_agent/state", 10);
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(fleet.getNode());
  executor.add_node(publisherNode);

  // 等待 ROS2 graph 發現模擬 Agent 的 state topic。
  for (int index = 0; index < 10; ++index)
  {
    executor.spin_some();
    std::this_thread::sleep_for(50ms);
  }
  fleet.scanNow();
  ASSERT_TRUE(fleet.getOnlineAgents().find("uav_1_agent") != fleet.getOnlineAgents().end());

  std::string offlineAgent;
  bus.subscribe("agent_offline", [&offlineAgent](const std::string&, const std::any& data)
  {
    offlineAgent = std::any_cast<std::string>(data);
  });

  bus.publish("agent_shutdown_accepted", std::string("uav_1_agent"));
  EXPECT_EQ(offlineAgent, "uav_1_agent");
  EXPECT_TRUE(fleet.getOnlineAgents().empty());

  // topic 尚未從 DDS graph 消失時，下一次掃描也不可重新加入。
  fleet.scanNow();
  EXPECT_TRUE(fleet.getOnlineAgents().empty());

  publisher.reset();
  publisherNode.reset();
  fleet.shutdown();
}

TEST_F(FleetModuleTest, IgnoresLegacyFmuAgentStateTopic)
{
  EventBus bus;
  FleetModule fleet;
  ASSERT_TRUE(fleet.init(bus));

  auto publisherNode = std::make_shared<rclcpp::Node>("fleet_legacy_test_agent_node");
  auto publisher = publisherNode->create_publisher<taiphoon_agent::msg::AgentState>(
    "/fmu1_agent/state", 10);
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(fleet.getNode());
  executor.add_node(publisherNode);

  for (int index = 0; index < 10; ++index)
  {
    executor.spin_some();
    std::this_thread::sleep_for(50ms);
  }

  fleet.scanNow();
  EXPECT_TRUE(fleet.getOnlineAgents().empty());

  publisher.reset();
  publisherNode.reset();
  fleet.shutdown();
}
