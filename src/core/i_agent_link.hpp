/**
 * @file i_agent_link.hpp
 * @brief 核心模組與 Agent 通訊的唯一介面（取代網頁版各模組持有的 rclcpp::Node）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 正式實作為 link 層的 CycloneAgentLink（ADR-0014），測試使用 FakeAgentLink。
 * 實作可在任意執行緒發出 signal；模組以 Qt 自動連線接收，跨執行緒時自動排入佇列。
 * QML 與 UiBridge 禁止直接使用本介面（AGENTS.md）。
 */
#pragma once

#include "core/agent_types.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

namespace gcs
{

/**
 * @class IAgentLink
 * @brief Agent 通訊介面：訂閱遙測、發布命令與航點任務、呼叫 shutdown service、查詢探索結果
 */
class IAgentLink : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~IAgentLink() override = default;

    /**
     * @brief 開始接收指定 Agent 的遙測 topic
     * @param agent_ns Agent namespace，例如 uav_1_agent
     */
    virtual void subscribe_agent(const QString& agent_ns) = 0;

    /**
     * @brief 停止接收指定 Agent 的遙測並釋放其單機端點
     * @param agent_ns Agent namespace
     */
    virtual void unsubscribe_agent(const QString& agent_ns) = 0;

    /**
     * @brief 取得目前 DDS 網路上有 /{ns}/state 發布者的 Agent
     * @return Agent namespace 清單
     */
    virtual QStringList discovered_state_agents() const = 0;

    /**
     * @brief 發布命令字串
     * @param target uav_{N}_agent 或 ALL_AGENTS_TARGET
     * @param command_line 依介面文件第 6 節組成的命令列
     * @param error 失敗原因（可為 nullptr）；例如尚無訂閱者時命令會遺失，必須回報失敗
     * @return 是否已送出
     */
    virtual bool publish_command(const QString& target, const QString& command_line, QString* error) = 0;

    /**
     * @brief 發布航點任務上傳（/{ns}/mission/plan）
     * @param agent_ns 單一 Agent namespace
     * @param mission 航點任務
     * @param error 失敗原因（可為 nullptr）；mission/plan 為 volatile，尚無訂閱者時必須回報失敗
     * @return 是否已送出
     */
    virtual bool publish_mission_plan(const QString& agent_ns, const gcs::WaypointMissionData& mission,
                                      QString* error) = 0;

    /**
     * @brief 送出 shutdown service 請求（request.data = true）
     * @param agent_ns 單一 Agent namespace
     * @param request_id 用於比對回應的識別碼
     * @param error 失敗原因（可為 nullptr）
     * @return 是否已送出；service 尚未配對時回傳 false
     */
    virtual bool request_shutdown(const QString& agent_ns, quint64 request_id, QString* error) = 0;

signals:
    /** @brief 收到 AgentState */
    void state_received(const QString& agent_ns, const gcs::AgentStateData& state);
    /** @brief 收到 AgentAttitude */
    void attitude_received(const QString& agent_ns, const gcs::AgentAttitudeData& attitude);
    /** @brief 收到 NedTarget */
    void ned_target_received(const QString& agent_ns, const gcs::NedTargetData& target);
    /** @brief 收到 WaypointSequence */
    void waypoints_received(const QString& agent_ns, const gcs::WaypointSequenceData& waypoints);
    /** @brief 收到 planning/trajectory */
    void trajectory_received(const QString& agent_ns, const gcs::AgentTrajectoryData& trajectory);
    /** @brief 收到 shutdown service 回應 */
    void shutdown_replied(const QString& agent_ns, quint64 request_id, bool is_success, const QString& message);
    /** @brief 收到 mission/status */
    void mission_status_received(const QString& agent_ns, const gcs::WaypointMissionStatusData& status);
    /** @brief 收到 events */
    void event_received(const QString& agent_ns, const gcs::AgentEventData& event);
    /** @brief 收到 health */
    void health_received(const QString& agent_ns, const gcs::AgentHealthData& health);
};

}  // namespace gcs
