/**
 * @file guard_module.cpp
 * @brief 安全防呆模組實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 規則本身集中於 guard_rules.cpp；本檔負責狀態追蹤、全體與單機判斷及錯誤訊息。
 * 錯誤訊息沿用網頁版，操作人員看到的提示維持一致。
 */
#include "modules/guard/guard_module.hpp"

#include <QLoggingCategory>

namespace
{
Q_LOGGING_CATEGORY(lc_guard, "gcs.guard")
}  // namespace

namespace gcs
{

bool GuardModule::init()
{
    is_initialized_ = true;
    qCInfo(lc_guard) << "初始化完成，轉移表已載入";
    return true;
}

void GuardModule::shutdown()
{
    is_initialized_ = false;
    online_agents_.clear();
    agent_states_.clear();
    agent_health_.clear();
    last_valid_states_.clear();
}

bool GuardModule::health_check() const
{
    return is_initialized_;
}

FlightState GuardModule::agent_flight_state(const QString& agent_ns) const
{
    const auto it = agent_states_.constFind(agent_ns);
    return it != agent_states_.cend() ? static_cast<FlightState>(it->flight_mode) : FlightState::IDLE;
}

void GuardModule::on_state_updated(const QString& agent_ns, const AgentStateData& state)
{
    if (state.is_px4_state_valid)
    {
        last_valid_states_.insert(agent_ns, state);
    }
    if (!state.is_online)
    {
        // Agent 自行回報離線時立即清除，避免沿用過期狀態放行命令。
        on_agent_offline(agent_ns);
        return;
    }
    online_agents_.insert(agent_ns);
    agent_states_.insert(agent_ns, state);
}

void GuardModule::on_health_updated(const QString& agent_ns, const AgentHealthData& health)
{
    // 離線後才送達的健康狀態不保存，避免 Agent 再上線時沿用舊資料
    if (online_agents_.contains(agent_ns))
    {
        agent_health_.insert(agent_ns, health);
    }
}

void GuardModule::on_agent_online(const QString& agent_ns)
{
    online_agents_.insert(agent_ns);
}

void GuardModule::on_agent_offline(const QString& agent_ns)
{
    online_agents_.remove(agent_ns);
    agent_states_.remove(agent_ns);
    agent_health_.remove(agent_ns);
}

void GuardModule::request_command(const CommandRequest& request)
{
    const QString agent = request.agent.isEmpty() ? QStringLiteral("unknown") : request.agent;
    const QString command = request.command.isEmpty() ? QStringLiteral("unknown") : request.command;

    const QString protocol_error = command_protocol::validate_request(request);
    if (!protocol_error.isEmpty())
    {
        reject(agent, command, QStringLiteral("命令格式錯誤: ") + protocol_error);
        return;
    }

    const CmdType cmd = command_protocol::parse_command(request.command);
    if (cmd == CmdType::UNKNOWN)
    {
        reject(agent, command, QStringLiteral("不支援的命令"));
        return;
    }
    if (command_protocol::is_formation_command(request.command) && request.agent != ALL_AGENTS_TARGET)
    {
        reject(agent, command, QStringLiteral("FORMATION_* 僅允許全體模式"));
        return;
    }
    if (!is_initialized_)
    {
        reject(agent, command, QStringLiteral("GuardModule 未初始化"));
        return;
    }

    QString guard_error;
    const bool is_allowed = (request.agent == ALL_AGENTS_TARGET) ? validate_for_all(cmd, &guard_error)
                                                                  : validate_for_agent(request, cmd, &guard_error);
    if (!is_allowed)
    {
        reject(agent, command, guard_error);
        return;
    }
    emit command_validated(request);
}

void GuardModule::request_mission_upload(const QString& agent_ns, const WaypointMissionData& mission)
{
    const QString agent = agent_ns.isEmpty() ? QStringLiteral("unknown") : agent_ns;
    QString error;
    if (!command_protocol::is_single_agent(agent_ns))
    {
        error = QStringLiteral("航點任務只支援單機");
    }
    else if (!is_initialized_)
    {
        error = QStringLiteral("GuardModule 未初始化");
    }
    else if (!online_agents_.contains(agent_ns))
    {
        error = QStringLiteral("Agent 不在線上或尚未發現");
    }
    else
    {
        const auto it = agent_states_.constFind(agent_ns);
        error = it == agent_states_.cend() ? QStringLiteral("PX4 狀態尚未有效，拒絕上傳航點任務")
                                           : guard_rules::check_mission_upload(*it, mission);
    }
    if (!error.isEmpty())
    {
        qCInfo(lc_guard) << "航點任務上傳拒絕：agent=" << agent << "reason=" << error;
        emit mission_upload_acked(command_protocol::make_ack(agent, MISSION_UPLOAD_ACK_NAME, false, error));
        return;
    }
    emit mission_upload_validated(agent_ns, mission);
}

bool GuardModule::validate_for_all(CmdType cmd, QString* error) const
{
    // 編隊命令不經狀態驗證（由 CommandModule 廣播至 /uav_all_agent/command）
    if (guard_rules::is_formation_command(cmd))
    {
        return true;
    }
    if (online_agents_.isEmpty())
    {
        *error = QStringLiteral("沒有線上 Agent 可執行此命令");
        return false;
    }

    for (const QString& agent : online_agents_)
    {
        const auto it = agent_states_.constFind(agent);
        if (it == agent_states_.cend() || !it->is_px4_state_valid)
        {
            *error = QStringLiteral("Agent %1 的 PX4 狀態尚未有效，拒絕執行命令").arg(agent);
            return false;
        }
        if (!guard_rules::is_command_allowed_for_agent(*it, cmd))
        {
            *error = (cmd == CmdType::ARM || cmd == CmdType::TAKEOFF)
                         ? QStringLiteral("Agent %1 的 PX4 原生狀態不允許此命令").arg(agent)
                         : QStringLiteral("Agent %1 目前狀態不允許此命令").arg(agent);
            return false;
        }
    }
    return true;
}

bool GuardModule::validate_for_agent(const CommandRequest& request, CmdType cmd, QString* error) const
{
    if (!online_agents_.contains(request.agent))
    {
        *error = QStringLiteral("Agent 不在線上或尚未發現");
        return false;
    }

    // Agent 收到 odometry 前不發布 AgentState，強制重新啟動只靠健康狀態判斷，必須在狀態檢查之前處理
    if (cmd == CmdType::REBOOT_FC)
    {
        *error = guard_rules::check_reboot(reboot_facts(request.agent), command_protocol::is_forced_reboot(request));
        return error->isEmpty();
    }
    const auto it = agent_states_.constFind(request.agent);
    if (it == agent_states_.cend())
    {
        *error = QStringLiteral("PX4 狀態尚未有效，拒絕執行命令");
        return false;
    }
    // MISSION_STOP 只會讓 Agent 停下，PX4 狀態暫時無效時也必須能停止任務
    if (!it->is_px4_state_valid && cmd != CmdType::MISSION_STOP)
    {
        *error = QStringLiteral("PX4 狀態尚未有效，拒絕執行命令");
        return false;
    }
    if (!guard_rules::is_command_allowed_for_agent(*it, cmd))
    {
        *error = (cmd == CmdType::ARM || cmd == CmdType::TAKEOFF) ? QStringLiteral("PX4 原生狀態不允許此命令")
                                                                  : QStringLiteral("目前狀態不允許此命令");
        return false;
    }
    return true;
}

guard_rules::RebootFacts GuardModule::reboot_facts(const QString& agent_ns) const
{
    guard_rules::RebootFacts facts;
    const auto state = agent_states_.constFind(agent_ns);
    if (state != agent_states_.cend())
    {
        facts.state = *state;
    }
    const auto last_valid = last_valid_states_.constFind(agent_ns);
    if (last_valid != last_valid_states_.cend())
    {
        facts.last_valid_state = *last_valid;
    }
    const auto health = agent_health_.constFind(agent_ns);
    if (health != agent_health_.cend())
    {
        facts.health = *health;
    }
    return facts;
}

void GuardModule::reject(const QString& agent, const QString& command, const QString& message)
{
    qCInfo(lc_guard) << "命令拒絕：agent=" << agent << "cmd=" << command << "reason=" << message;
    emit command_acked(command_protocol::make_ack(agent, command, false, message));
}

}  // namespace gcs
