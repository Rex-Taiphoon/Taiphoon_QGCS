/**
 * @file agent_lifecycle_module.hpp
 * @brief Agent 容器生命週期模組 — 單機 shutdown service 橋接
 * @author Taiphoon GCS Team
 * @date 2026-08-06
 */
#pragma once

#include "core/event_bus.hpp"
#include "core/gcs_module.hpp"
#include <rclcpp/rclcpp.hpp>
#include <std_srvs/srv/set_bool.hpp>
#include <any>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

/**
 * @brief 將經後端授權的單機關閉請求轉送至 AgentSupervisor service
 */
class AgentLifecycleModule : public GcsModule
{
public:
    /** @brief 建立 AgentLifecycleModule */
    AgentLifecycleModule() = default;

    /** @brief 初始化 EventBus 訂閱與 ROS2 service client 節點 */
    bool init(EventBus& bus) override;

    /** @brief 釋放 service client 與待處理請求 */
    void shutdown() override;

    /** @brief 檢查 service client 節點是否已建立 */
    bool healthCheck() override;

    /** @brief 取得模組名稱 */
    std::string getName() const override { return "AgentLifecycleModule"; }

    /**
     * @brief 取得 ROS2 節點，供主程式加入 executor
     * @return Agent 生命週期 ROS2 節點
     */
    std::shared_ptr<rclcpp::Node> getNode() const { return node_; }

private:
    using ShutdownService = std_srvs::srv::SetBool;
    using ShutdownClient = rclcpp::Client<ShutdownService>;

    /// service 回應逾時時間；AgentSupervisor 應在容器結束前回應
    static constexpr std::chrono::seconds SHUTDOWN_RESPONSE_TIMEOUT{5};

    /// service 發現等待時間；避免首次操作因 DDS 尚未完成探索而誤判不可用
    static constexpr std::chrono::milliseconds SERVICE_DISCOVERY_TIMEOUT{1000};

    /**
     * @brief 處理前端送出的單機關閉請求
     * @param data EventBus 承載的 WebSocket JSON 字串
     */
    void onAgentShutdownRequest(const std::any& data);

    /**
     * @brief 在鎖定範圍內取得或建立指定 Agent 的 service client
     * @param agentNs 單一 Agent namespace
     * @return 對應的 shutdown service client
     */
    ShutdownClient::SharedPtr ensureShutdownClientLocked(const std::string& agentNs);

    /**
     * @brief 結束待處理請求並向前端回傳統一的 cmd_ack
     * @param agentNs 單一 Agent namespace
     * @param status 回執狀態（ok / error）
     * @param message 面向操作人員的結果訊息
     */
    void completeShutdownRequest(const std::string& agentNs, const std::string& status,
                                 const std::string& message);

    /**
     * @brief 立即拒絕尚未送出至 ROS2 的關閉請求
     * @param agentNs 單一 Agent namespace
     * @param message 面向操作人員的拒絕原因
     */
    void rejectShutdownRequest(const std::string& agentNs, const std::string& message) const;

    EventBus* bus_ = nullptr;
    std::shared_ptr<rclcpp::Node> node_;
    mutable std::mutex mutex_;
    bool initialized_ = false;

    /// Agent namespace → shutdown service client
    std::unordered_map<std::string, ShutdownClient::SharedPtr> shutdownClients_;

    /// 防止同一 Agent 在收到 service 回應前重複關閉
    std::unordered_set<std::string> pendingShutdownAgents_;

    /// Agent namespace → service 回應逾時計時器
    std::unordered_map<std::string, rclcpp::TimerBase::SharedPtr> shutdownTimeoutTimers_;
};
