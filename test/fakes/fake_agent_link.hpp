/**
 * @file fake_agent_link.hpp
 * @brief 測試用 IAgentLink：記錄模組的呼叫，並可模擬 Agent 端事件
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#pragma once

#include "core/i_agent_link.hpp"

#include <QSet>
#include <QStringList>

#include <vector>

namespace gcs::test
{

/// 一筆已發布的命令
struct PublishedCommand
{
    QString target;
    QString command_line;
};

/// 一筆已發布的航點任務
struct PublishedMission
{
    QString agent_ns;
    WaypointMissionData mission;
};

/// 一筆 shutdown 請求
struct ShutdownRequest
{
    QString agent_ns;
    quint64 request_id = 0;
};

/**
 * @class FakeAgentLink
 * @brief 不經網路的 IAgentLink
 */
class FakeAgentLink : public IAgentLink
{
    Q_OBJECT

public:
    using IAgentLink::IAgentLink;

    void subscribe_agent(const QString& agent_ns) override;
    void unsubscribe_agent(const QString& agent_ns) override;
    QStringList discovered_state_agents() const override;
    bool publish_command(const QString& target, const QString& command_line, QString* error) override;
    bool publish_mission_plan(const QString& agent_ns, const WaypointMissionData& mission, QString* error) override;
    bool request_shutdown(const QString& agent_ns, quint64 request_id, QString* error) override;

    /// 模擬 DDS 探索到的 state 發布者
    QStringList discovered_agents;
    /// 為 false 時 publish_command 失敗（模擬尚無訂閱者）
    bool is_command_channel_ready = true;
    /// 為 false 時 request_shutdown 失敗（模擬 service 未配對）
    bool is_shutdown_service_ready = true;
    /// 為 false 時 publish_mission_plan 失敗（模擬 Agent 沒有訂閱 mission/plan）
    bool is_mission_channel_ready = true;

    /// 目前已訂閱的 Agent
    QSet<QString> subscribed_agents;
    /// 依序記錄已發布的命令
    std::vector<PublishedCommand> published_commands;
    /// 依序記錄已發布的航點任務
    std::vector<PublishedMission> published_missions;
    /// 依序記錄 shutdown 請求
    std::vector<ShutdownRequest> shutdown_requests;
};

/**
 * @brief 建立 PX4 狀態有效的線上 AgentState
 * @param mode 任務階段
 * @param is_armed PX4 是否解鎖
 * @param is_landed PX4 是否落地
 * @param nav_state PX4 nav_state
 * @param is_px4_state_valid PX4 狀態是否有效
 * @return AgentState
 */
AgentStateData make_state(FlightState mode, bool is_armed = false, bool is_landed = true,
                          uint8_t nav_state = PX4_NAV_STATE_OFFBOARD, bool is_px4_state_valid = true);

/**
 * @brief 建立命令請求
 * @param agent 目標
 * @param command 命令名稱
 * @param params 參數
 * @return 命令請求
 */
CommandRequest make_request(const QString& agent, const QString& command, const QVector<double>& params = {});

}  // namespace gcs::test
