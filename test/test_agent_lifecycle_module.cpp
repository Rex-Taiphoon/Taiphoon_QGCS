/**
 * @file test_agent_lifecycle_module.cpp
 * @brief AgentLifecycleModule 單元測試 — 單機限制與 shutdown service 橋接
 * @author Taiphoon GCS Team
 * @date 2026-08-06
 */
#include "core/event_bus.hpp"
#include "modules/lifecycle/agent_lifecycle_module.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_srvs/srv/set_bool.hpp>
#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

using json = nlohmann::json;
using namespace std::chrono_literals;

namespace
{

std::string makeShutdownRequest(const std::string& agent)
{
    json request;
    request["type"] = "agent_shutdown_request";
    request["agent"] = agent;
    return request.dump();
}

}  // namespace

class AgentLifecycleModuleTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        const int argc = 1;
        const char* argv[] = {"test_agent_lifecycle_module"};
        rclcpp::init(argc, argv);
    }

    static void TearDownTestSuite()
    {
        rclcpp::shutdown();
    }
};

TEST_F(AgentLifecycleModuleTest, RejectsAllAgentShutdownRequest)
{
    EventBus bus;
    AgentLifecycleModule lifecycle;
    ASSERT_TRUE(lifecycle.init(bus));

    std::string acknowledgement;
    bus.subscribe("cmd_ack", [&acknowledgement](const std::string&, const std::any& data)
    {
        acknowledgement = std::any_cast<std::string>(data);
    });

    bus.publish("agent_shutdown_request", makeShutdownRequest("all"));

    ASSERT_FALSE(acknowledgement.empty());
    const json parsed = json::parse(acknowledgement);
    EXPECT_EQ(parsed["command"], "AGENT_SHUTDOWN");
    EXPECT_EQ(parsed["status"], "error");
    EXPECT_NE(parsed["message"].get<std::string>().find("單一"), std::string::npos);
    lifecycle.shutdown();
}

TEST_F(AgentLifecycleModuleTest, RejectsShutdownWhenServiceUnavailable)
{
    EventBus bus;
    AgentLifecycleModule lifecycle;
    ASSERT_TRUE(lifecycle.init(bus));

    std::string acknowledgement;
    std::string acceptedAgent;
    bus.subscribe("agent_shutdown_accepted", [&acceptedAgent](const std::string&, const std::any& data)
    {
        acceptedAgent = std::any_cast<std::string>(data);
    });
    bus.subscribe("cmd_ack", [&acknowledgement](const std::string&, const std::any& data)
    {
        acknowledgement = std::any_cast<std::string>(data);
    });

    // 使用不存在的 Agent namespace，避免與執行中的模擬器服務互相干擾
    bus.publish("agent_shutdown_request", makeShutdownRequest("uav_99_agent"));

    ASSERT_FALSE(acknowledgement.empty());
    const json parsed = json::parse(acknowledgement);
    EXPECT_EQ(parsed["status"], "error");
    EXPECT_NE(parsed["message"].get<std::string>().find("shutdown service"), std::string::npos);
    EXPECT_TRUE(acceptedAgent.empty());
    lifecycle.shutdown();
}

TEST_F(AgentLifecycleModuleTest, SingleAgentRequestCallsShutdownService)
{
    EventBus bus;
    AgentLifecycleModule lifecycle;
    ASSERT_TRUE(lifecycle.init(bus));

    auto serviceNode = std::make_shared<rclcpp::Node>("test_agent_supervisor_node");
    std::atomic_bool receivedShutdownRequest{false};
    auto service = serviceNode->create_service<std_srvs::srv::SetBool>(
        "/uav_3_agent/shutdown",
        [&receivedShutdownRequest](const std::shared_ptr<std_srvs::srv::SetBool::Request> request,
                                   std::shared_ptr<std_srvs::srv::SetBool::Response> response)
        {
            if (request == nullptr || response == nullptr)
            {
                return;
            }
            receivedShutdownRequest.store(request->data);
            response->success = true;
            response->message = "測試服務已接受關閉請求";
        });

    rclcpp::executors::MultiThreadedExecutor executor;
    executor.add_node(lifecycle.getNode());
    executor.add_node(serviceNode);
    std::thread spinThread([&executor]()
    {
        executor.spin();
    });

    std::mutex acknowledgementMutex;
    std::condition_variable acknowledgementCv;
    std::string acknowledgement;
    std::string shutdownAcceptedAgent;
    bus.subscribe("agent_shutdown_accepted", [&shutdownAcceptedAgent](const std::string&, const std::any& data)
    {
        shutdownAcceptedAgent = std::any_cast<std::string>(data);
    });
    bus.subscribe("cmd_ack", [&acknowledgement, &acknowledgementCv, &acknowledgementMutex](
        const std::string&, const std::any& data)
    {
        {
            std::lock_guard<std::mutex> lock(acknowledgementMutex);
            acknowledgement = std::any_cast<std::string>(data);
        }
        acknowledgementCv.notify_one();
    });

    bus.publish("agent_shutdown_request", makeShutdownRequest("uav_3_agent"));

    bool receivedAcknowledgement = false;
    {
        std::unique_lock<std::mutex> lock(acknowledgementMutex);
        receivedAcknowledgement = acknowledgementCv.wait_for(lock, 3s, [&acknowledgement]()
        {
            return !acknowledgement.empty();
        });
    }

    executor.cancel();
    spinThread.join();
    service.reset();
    serviceNode.reset();
    lifecycle.shutdown();

    ASSERT_TRUE(receivedAcknowledgement);

    const json parsed = json::parse(acknowledgement);
    EXPECT_EQ(parsed["agent"], "uav_3_agent");
    EXPECT_EQ(parsed["command"], "AGENT_SHUTDOWN");
    EXPECT_EQ(parsed["status"], "ok");
    EXPECT_TRUE(receivedShutdownRequest.load());
    EXPECT_EQ(shutdownAcceptedAgent, "uav_3_agent");
}
