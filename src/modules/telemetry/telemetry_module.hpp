/**
 * @file telemetry_module.hpp
 * @brief 遙測模組 — 依機隊上下線管理 Agent 訂閱，並轉發帶型別的遙測資料
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#pragma once

#include "core/agent_types.hpp"
#include "core/gcs_module.hpp"

#include <QSet>

namespace gcs
{

class IAgentLink;

/**
 * @class TelemetryModule
 * @brief 狀態聚合：只轉發已訂閱（線上）Agent 的資料
 */
class TelemetryModule : public GcsModule
{
    Q_OBJECT

public:
    /**
     * @brief 建立遙測模組
     * @param link Agent 通訊介面，生命週期需長於本模組
     * @param parent 父物件
     */
    explicit TelemetryModule(IAgentLink* link, QObject* parent = nullptr);

    bool init() override;
    void shutdown() override;
    bool health_check() const override;
    QString module_name() const override { return QStringLiteral("TelemetryModule"); }

    /**
     * @brief 查詢是否已訂閱指定 Agent
     * @param agent_ns Agent namespace
     * @return 是否已訂閱
     */
    bool is_subscribed(const QString& agent_ns) const;

public slots:
    /** @brief Agent 上線：建立訂閱 @param agent_ns Agent namespace */
    void on_agent_online(const QString& agent_ns);

    /** @brief Agent 離線：取消訂閱 @param agent_ns Agent namespace */
    void on_agent_offline(const QString& agent_ns);

signals:
    /** @brief AgentState 更新 */
    void state_updated(const QString& agent_ns, const gcs::AgentStateData& state);
    /** @brief AgentAttitude 更新 */
    void attitude_updated(const QString& agent_ns, const gcs::AgentAttitudeData& attitude);
    /** @brief NedTarget 更新 */
    void ned_target_updated(const QString& agent_ns, const gcs::NedTargetData& target);
    /** @brief WaypointSequence 更新 */
    void waypoints_updated(const QString& agent_ns, const gcs::WaypointSequenceData& waypoints);
    /** @brief EGO-Swarm 軌跡更新 */
    void trajectory_updated(const QString& agent_ns, const gcs::AgentTrajectoryData& trajectory);
    /** @brief Agent 回報航點任務狀態 */
    void mission_status_updated(const QString& agent_ns, const gcs::WaypointMissionStatusData& status);
    /** @brief Agent 事件（錯誤、警告、資訊） */
    void agent_event_received(const QString& agent_ns, const gcs::AgentEventData& event);
    /** @brief Agent 與 PX4 連線健康狀態更新 */
    void health_updated(const QString& agent_ns, const gcs::AgentHealthData& health);

private slots:
    void on_state_received(const QString& agent_ns, const gcs::AgentStateData& state);
    void on_attitude_received(const QString& agent_ns, const gcs::AgentAttitudeData& attitude);
    void on_ned_target_received(const QString& agent_ns, const gcs::NedTargetData& target);
    void on_waypoints_received(const QString& agent_ns, const gcs::WaypointSequenceData& waypoints);
    void on_trajectory_received(const QString& agent_ns, const gcs::AgentTrajectoryData& trajectory);
    void on_mission_status_received(const QString& agent_ns, const gcs::WaypointMissionStatusData& status);
    void on_event_received(const QString& agent_ns, const gcs::AgentEventData& event);
    void on_health_received(const QString& agent_ns, const gcs::AgentHealthData& health);

private:
    IAgentLink* link_ = nullptr;
    bool is_initialized_ = false;
    QSet<QString> subscribed_agents_;
};

}  // namespace gcs
