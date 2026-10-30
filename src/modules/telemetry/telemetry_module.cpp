/**
 * @file telemetry_module.cpp
 * @brief 遙測模組實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "modules/telemetry/telemetry_module.hpp"

#include "core/i_agent_link.hpp"

#include <QLoggingCategory>

namespace
{
Q_LOGGING_CATEGORY(lc_telemetry, "gcs.telemetry")
}  // namespace

namespace gcs
{

TelemetryModule::TelemetryModule(IAgentLink* link, QObject* parent) : GcsModule(parent), link_(link)
{
}

bool TelemetryModule::init()
{
    if (link_ == nullptr)
    {
        qCCritical(lc_telemetry) << "未提供 Agent 通訊介面";
        return false;
    }
    // link 是注入的通訊介面而非其他模組，由本模組自行連線；DDS 執行緒發出時自動排入佇列。
    connect(link_, &IAgentLink::state_received, this, &TelemetryModule::on_state_received);
    connect(link_, &IAgentLink::attitude_received, this, &TelemetryModule::on_attitude_received);
    connect(link_, &IAgentLink::ned_target_received, this, &TelemetryModule::on_ned_target_received);
    connect(link_, &IAgentLink::waypoints_received, this, &TelemetryModule::on_waypoints_received);
    connect(link_, &IAgentLink::trajectory_received, this, &TelemetryModule::on_trajectory_received);
    connect(link_, &IAgentLink::mission_status_received, this, &TelemetryModule::on_mission_status_received);
    connect(link_, &IAgentLink::event_received, this, &TelemetryModule::on_event_received);
    connect(link_, &IAgentLink::health_received, this, &TelemetryModule::on_health_received);
    is_initialized_ = true;
    qCInfo(lc_telemetry) << "初始化完成";
    return true;
}

void TelemetryModule::shutdown()
{
    if (link_ != nullptr)
    {
        disconnect(link_, nullptr, this, nullptr);
        for (const QString& agent : subscribed_agents_)
        {
            link_->unsubscribe_agent(agent);
        }
    }
    subscribed_agents_.clear();
    is_initialized_ = false;
}

bool TelemetryModule::health_check() const
{
    return is_initialized_;
}

bool TelemetryModule::is_subscribed(const QString& agent_ns) const
{
    return subscribed_agents_.contains(agent_ns);
}

void TelemetryModule::on_agent_online(const QString& agent_ns)
{
    if (!is_initialized_ || subscribed_agents_.contains(agent_ns))
    {
        return;
    }
    link_->subscribe_agent(agent_ns);
    subscribed_agents_.insert(agent_ns);
    qCInfo(lc_telemetry) << "已訂閱 Agent：" << agent_ns;
}

void TelemetryModule::on_agent_offline(const QString& agent_ns)
{
    if (!subscribed_agents_.remove(agent_ns))
    {
        return;
    }
    if (link_ != nullptr)
    {
        link_->unsubscribe_agent(agent_ns);
    }
    qCInfo(lc_telemetry) << "已取消訂閱 Agent：" << agent_ns;
}

void TelemetryModule::on_state_received(const QString& agent_ns, const AgentStateData& state)
{
    // 取消訂閱後仍可能收到佇列中的舊資料，不得讓已離線 Agent 重新出現。
    if (subscribed_agents_.contains(agent_ns))
    {
        emit state_updated(agent_ns, state);
    }
}

void TelemetryModule::on_attitude_received(const QString& agent_ns, const AgentAttitudeData& attitude)
{
    if (subscribed_agents_.contains(agent_ns))
    {
        emit attitude_updated(agent_ns, attitude);
    }
}

void TelemetryModule::on_ned_target_received(const QString& agent_ns, const NedTargetData& target)
{
    if (subscribed_agents_.contains(agent_ns))
    {
        emit ned_target_updated(agent_ns, target);
    }
}

void TelemetryModule::on_waypoints_received(const QString& agent_ns, const WaypointSequenceData& waypoints)
{
    if (subscribed_agents_.contains(agent_ns))
    {
        emit waypoints_updated(agent_ns, waypoints);
    }
}

void TelemetryModule::on_trajectory_received(const QString& agent_ns, const AgentTrajectoryData& trajectory)
{
    if (subscribed_agents_.contains(agent_ns))
    {
        emit trajectory_updated(agent_ns, trajectory);
    }
}

void TelemetryModule::on_mission_status_received(const QString& agent_ns, const WaypointMissionStatusData& status)
{
    if (subscribed_agents_.contains(agent_ns))
    {
        emit mission_status_updated(agent_ns, status);
    }
}

void TelemetryModule::on_event_received(const QString& agent_ns, const AgentEventData& event)
{
    if (subscribed_agents_.contains(agent_ns))
    {
        emit agent_event_received(agent_ns, event);
    }
}

void TelemetryModule::on_health_received(const QString& agent_ns, const AgentHealthData& health)
{
    if (subscribed_agents_.contains(agent_ns))
    {
        emit health_updated(agent_ns, health);
    }
}

}  // namespace gcs
