/**
 * @file fake_agent_link.cpp
 * @brief 測試用 IAgentLink 實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "fake_agent_link.hpp"

namespace gcs::test
{

void FakeAgentLink::subscribe_agent(const QString& agent_ns)
{
    subscribed_agents.insert(agent_ns);
}

void FakeAgentLink::unsubscribe_agent(const QString& agent_ns)
{
    subscribed_agents.remove(agent_ns);
}

QStringList FakeAgentLink::discovered_state_agents() const
{
    return discovered_agents;
}

bool FakeAgentLink::publish_command(const QString& target, const QString& command_line, QString* error)
{
    if (!is_command_channel_ready)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("尚無訂閱者");
        }
        return false;
    }
    published_commands.push_back({target, command_line});
    return true;
}

bool FakeAgentLink::publish_mission_plan(const QString& agent_ns, const WaypointMissionData& mission, QString* error)
{
    if (!is_mission_channel_ready)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Agent 沒有訂閱航點任務");
        }
        return false;
    }
    published_missions.push_back({agent_ns, mission});
    return true;
}

bool FakeAgentLink::request_shutdown(const QString& agent_ns, quint64 request_id, QString* error)
{
    if (!is_shutdown_service_ready)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("service 未配對");
        }
        return false;
    }
    shutdown_requests.push_back({agent_ns, request_id});
    return true;
}

AgentStateData make_state(FlightState mode, bool is_armed, bool is_landed, uint8_t nav_state, bool is_px4_state_valid)
{
    AgentStateData state;
    state.flight_mode = static_cast<uint8_t>(mode);
    state.is_online = true;
    state.is_armed = is_armed;
    state.is_landed = is_landed;
    state.px4_nav_state = nav_state;
    state.is_px4_state_valid = is_px4_state_valid;
    return state;
}

CommandRequest make_request(const QString& agent, const QString& command, const QVector<double>& params)
{
    CommandRequest request;
    request.agent = agent;
    request.command = command;
    request.params = params;
    return request;
}

}  // namespace gcs::test
