/**
 * @file cyclone_agent_link.hpp
 * @brief 以內嵌 CycloneDDS 實作的 IAgentLink（ROS2 Humble 線上相容，ADR-0014）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 執行緒：本物件與核心模組同在核心執行緒，公開方法只能在該執行緒呼叫；
 * DDS listener 在 CycloneDDS 執行緒解碼後直接發出 signal，由 Qt 排入接收者的佇列。
 * listener 只讀取各自的 ReaderContext 與受 discovery_mutex_ 保護的探索表，
 * 不碰 agents_ 等核心執行緒資料（CODE_CONSTRAINT L1-13）。
 */
#pragma once

#include "core/i_agent_link.hpp"
#include "link/dds_config.hpp"

#include <dds/dds.h>

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace gcs::link
{

/**
 * @class CycloneAgentLink
 * @brief Agent 通訊的正式實作
 */
class CycloneAgentLink : public IAgentLink
{
    Q_OBJECT

public:
    /// 探索到的 DDS topic（除錯工具使用）
    struct DiscoveredTopic
    {
        QString topic;
        QString type;
    };

    explicit CycloneAgentLink(QObject* parent = nullptr);
    ~CycloneAgentLink() override;

    CycloneAgentLink(const CycloneAgentLink&) = delete;
    CycloneAgentLink& operator=(const CycloneAgentLink&) = delete;

    /**
     * @brief 建立 DDS domain、participant 與全體端點；已啟動時先停止再以新設定重建
     *
     * 已訂閱的 Agent 會以新設定重新建立端點，核心模組不需重新訂閱（網路改變時使用）。
     *
     * @param config 網路設定
     * @param error 失敗原因（可為 nullptr）
     * @return 是否成功
     */
    bool start(const DdsNetworkConfig& config, QString* error);

    /** @brief 刪除所有 DDS 實體；會等待執行中的 listener 結束 */
    void stop();

    /** @brief 是否已啟動 @return 是否已啟動 */
    bool is_running() const;

    /**
     * @brief 目前探索到的所有 writer topic（含型別），供除錯工具使用
     * @return 依 topic 排序且不重複的清單
     */
    std::vector<DiscoveredTopic> discovered_topics() const;

    void subscribe_agent(const QString& agent_ns) override;
    void unsubscribe_agent(const QString& agent_ns) override;
    QStringList discovered_state_agents() const override;
    bool publish_command(const QString& target, const QString& command_line, QString* error) override;
    bool request_shutdown(const QString& agent_ns, quint64 request_id, QString* error) override;

private:
    /// listener 要處理的資料種類
    enum class Channel
    {
        DISCOVERY,
        STATE,
        ATTITUDE,
        NED_TARGET,
        WAYPOINTS,
        TRAJECTORY,
        SHUTDOWN_REPLY
    };

    /// listener 的參數；生命週期必須長於對應 reader
    struct ReaderContext
    {
        CycloneAgentLink* link = nullptr;
        QString agent_ns;
        Channel channel = Channel::DISCOVERY;
    };

    /// 單一 Agent 的全部端點
    struct AgentEndpoints
    {
        std::vector<dds_entity_t> entities;
        std::vector<std::unique_ptr<ReaderContext>> contexts;
        dds_entity_t command_writer = 0;
        dds_entity_t shutdown_request_writer = 0;
        dds_entity_t shutdown_reply_reader = 0;
    };

    /**
     * @brief DDS listener 入口（CycloneDDS 執行緒）
     * @param reader 有資料的 reader
     * @param arg ReaderContext 指標
     */
    static void data_available_cb(dds_entity_t reader, void* arg);

    void handle_discovery(dds_entity_t reader);
    void handle_agent_data(dds_entity_t reader, const ReaderContext& context);

    dds_entity_t find_or_create_topic(const dds_topic_descriptor_t* descriptor, const std::string& name);
    dds_entity_t create_listened_reader(const dds_topic_descriptor_t* descriptor, const std::string& dds_topic,
                                        const dds_qos_t* qos, ReaderContext* context);
    bool create_agent_endpoints(const QString& agent_ns);
    void delete_agent_endpoints(const QString& agent_ns);

    dds_entity_t domain_ = 0;
    dds_entity_t participant_ = 0;
    dds_entity_t discovery_reader_ = 0;
    dds_entity_t all_command_writer_ = 0;
    uint32_t domain_id_ = 0;
    /// 本程式的 service client 識別碼，用於辨識屬於自己的 shutdown 回應
    uint64_t client_guid_ = 0;
    std::unique_ptr<ReaderContext> discovery_context_;
    std::map<std::string, dds_entity_t> topics_;
    /// 已訂閱的 Agent（重新啟動時據此重建端點）
    std::set<QString> subscribed_agents_;
    std::map<QString, AgentEndpoints> agents_;

    mutable std::mutex discovery_mutex_;
    /// DCPSPublication instance → topic；只在持有 discovery_mutex_ 時存取
    std::map<dds_instance_handle_t, DiscoveredTopic> publications_;
};

}  // namespace gcs::link
