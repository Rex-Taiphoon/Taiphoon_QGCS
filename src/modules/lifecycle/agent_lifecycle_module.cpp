/**
 * @file agent_lifecycle_module.cpp
 * @brief Agent 容器生命週期模組實作
 * @author Taiphoon GCS Team
 * @date 2026-08-06
 */
#include "modules/lifecycle/agent_lifecycle_module.hpp"
#include "core/ws_protocol.hpp"
#include <exception>

namespace
{

constexpr const char* SHUTDOWN_COMMAND = "AGENT_SHUTDOWN";

}  // namespace

bool AgentLifecycleModule::init(EventBus& bus)
{
    bus_ = &bus;

    node_ = std::make_shared<rclcpp::Node>("gcs_agent_lifecycle_node");

    bus_->subscribe("agent_shutdown_request", [this](const std::string&, const std::any& data)
    {
        onAgentShutdownRequest(data);
    });

    initialized_ = true;
    RCLCPP_WARN(node_->get_logger(),
                "遠端關閉功能已啟用且未要求授權碼；此操作不會觸發 RTL 或降落，請謹慎使用");
    return true;
}

void AgentLifecycleModule::shutdown()
{
    std::lock_guard<std::mutex> lock(mutex_);
    initialized_ = false;
    shutdownTimeoutTimers_.clear();
    pendingShutdownAgents_.clear();
    shutdownClients_.clear();
    node_.reset();
}

bool AgentLifecycleModule::healthCheck()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_ && node_ != nullptr;
}

void AgentLifecycleModule::onAgentShutdownRequest(const std::any& data)
{
    ws_protocol::AgentShutdownRequest request;
    try
    {
        const auto rawJson = std::any_cast<std::string>(data);
        std::string protocolError;
        if (!ws_protocol::parseAgentShutdownRequest(rawJson, request, &protocolError))
        {
            const std::string agent = request.agent.empty() ? "unknown" : request.agent;
            rejectShutdownRequest(agent, "關閉請求格式錯誤: " + protocolError);
            return;
        }
    }
    catch (const std::exception& e)
    {
        RCLCPP_ERROR(node_->get_logger(), "關閉請求解析失敗: %s", e.what());
        rejectShutdownRequest("unknown", "關閉請求解析失敗");
        return;
    }

    ShutdownClient::SharedPtr client;
    std::string rejection;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || node_ == nullptr)
        {
            rejection = "AgentLifecycleModule 尚未初始化";
        }
        else if (pendingShutdownAgents_.find(request.agent) != pendingShutdownAgents_.end())
        {
            rejection = "此 Agent 已有關閉請求等待回應";
        }
        else
        {
            client = ensureShutdownClientLocked(request.agent);
            if (client == nullptr || !client->wait_for_service(SERVICE_DISCOVERY_TIMEOUT))
            {
                rejection = "Agent shutdown service 尚未就緒";
            }
            else
            {
                pendingShutdownAgents_.insert(request.agent);
                shutdownTimeoutTimers_[request.agent] = node_->create_wall_timer(
                    SHUTDOWN_RESPONSE_TIMEOUT, [this, agent = request.agent]()
                    {
                        completeShutdownRequest(agent, "error", "等待 Agent shutdown service 回應逾時");
                    });
            }
        }
    }
    if (!rejection.empty())
    {
        rejectShutdownRequest(request.agent, rejection);
        return;
    }

    auto serviceRequest = std::make_shared<ShutdownService::Request>();
    serviceRequest->data = true;
    const std::string agent = request.agent;

    try
    {
        client->async_send_request(serviceRequest, [this, agent](ShutdownClient::SharedFuture future)
        {
            try
            {
                const auto response = future.get();
                if (response == nullptr)
                {
                    completeShutdownRequest(agent, "error", "Agent shutdown service 回傳空回應");
                    return;
                }
                if (!response->success)
                {
                    completeShutdownRequest(agent, "error", response->message);
                    return;
                }
                if (bus_ != nullptr)
                {
                    // 先通知 Fleet 立即隔離目標，避免 service 回覆與下一次 DDS 掃描
                    // 之間仍將已關閉 Agent 視為可用命令目標。
                    bus_->publish("agent_shutdown_accepted", agent);
                }
                completeShutdownRequest(agent, "ok", "關閉請求已被 Agent 接受，已從 GCS 機隊隔離");
            }
            catch (const std::exception& e)
            {
                RCLCPP_ERROR(node_->get_logger(), "Agent shutdown service 回應失敗: %s", e.what());
                completeShutdownRequest(agent, "error", "等待 Agent shutdown service 回應失敗");
            }
        });
    }
    catch (const std::exception& e)
    {
        RCLCPP_ERROR(node_->get_logger(), "無法發送 Agent shutdown service 請求: %s", e.what());
        completeShutdownRequest(agent, "error", "無法發送 Agent shutdown service 請求");
    }
}

AgentLifecycleModule::ShutdownClient::SharedPtr
AgentLifecycleModule::ensureShutdownClientLocked(const std::string& agentNs)
{
    const auto existing = shutdownClients_.find(agentNs);
    if (existing != shutdownClients_.end())
    {
        return existing->second;
    }
    if (node_ == nullptr)
    {
        return nullptr;
    }

    const std::string serviceName = "/" + agentNs + "/shutdown";
    auto client = node_->create_client<ShutdownService>(serviceName);
    shutdownClients_[agentNs] = client;
    RCLCPP_INFO(node_->get_logger(), "建立 Agent shutdown service client: %s", serviceName.c_str());
    return client;
}

void AgentLifecycleModule::completeShutdownRequest(const std::string& agentNs, const std::string& status,
                                                   const std::string& message)
{
    EventBus* bus = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pendingShutdownAgents_.erase(agentNs) == 0)
        {
            return;
        }
        shutdownTimeoutTimers_.erase(agentNs);
        bus = bus_;
    }

    if (bus != nullptr)
    {
        bus->publish("cmd_ack", ws_protocol::makeCmdAck(agentNs, SHUTDOWN_COMMAND, status, message));
    }
}

void AgentLifecycleModule::rejectShutdownRequest(const std::string& agentNs,
                                                 const std::string& message) const
{
    if (bus_ != nullptr)
    {
        bus_->publish("cmd_ack", ws_protocol::makeCmdAck(agentNs, SHUTDOWN_COMMAND, "error", message));
    }
}
