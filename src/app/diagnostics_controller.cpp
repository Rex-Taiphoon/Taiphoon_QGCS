/**
 * @file diagnostics_controller.cpp
 * @brief 唯讀除錯工具實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "app/diagnostics_controller.hpp"

#include "app/ui_format.hpp"
#include "core/command_protocol.hpp"

#include <QDateTime>
#include <QRegularExpression>

#include <algorithm>
#include <utility>

namespace gcs
{

namespace
{

/// 每個 WGS84 控制點的元素數
constexpr size_t VALUES_PER_CONTROL_POINT = 3;
/// events 指令預設顯示筆數
constexpr int DEFAULT_EVENT_COUNT = 20;

}  // namespace

DiagnosticsController::DiagnosticsController(QObject* parent) : QObject(parent)
{
    append(QStringLiteral("輸入 help 查看可用指令。本工具為唯讀，不提供發布或呼叫。"));
}

void DiagnosticsController::set_topic_source(TopicSource source)
{
    topic_source_ = std::move(source);
}

void DiagnosticsController::append(const QString& text)
{
    output_lines_.append(text.split(QLatin1Char('\n')));
    while (output_lines_.size() > MAX_OUTPUT_LINES)
    {
        output_lines_.removeFirst();
    }
    emit outputChanged();
}

void DiagnosticsController::clear()
{
    output_lines_.clear();
    emit outputChanged();
}

void DiagnosticsController::record_event(const QString& text)
{
    events_.append(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss ")) + text);
    while (events_.size() > MAX_EVENTS)
    {
        events_.removeFirst();
    }
}

QString DiagnosticsController::help_text() const
{
    return QStringLiteral(
        "可用指令：\n"
        "  topic list                         列出 DDS 網路上的 topic 與型別\n"
        "  topic echo <uav_N_agent> [state|attitude|trajectory]  顯示最新資料\n"
        "  agents                             列出已收到資料的 Agent\n"
        "  events [筆數]                      顯示最近的模組事件\n"
        "  clear                              清除畫面");
}

QString DiagnosticsController::topic_list() const
{
    if (!topic_source_)
    {
        return QStringLiteral("DDS 尚未啟動");
    }
    const auto topics = topic_source_();
    if (topics.empty())
    {
        return QStringLiteral("目前沒有探索到任何 topic");
    }
    QStringList lines;
    for (const auto& entry : topics)
    {
        lines << QStringLiteral("%1  [%2]").arg(entry.topic, entry.type);
    }
    lines << QStringLiteral("共 %1 個").arg(topics.size());
    return lines.join(QLatin1Char('\n'));
}

QString DiagnosticsController::echo(const QString& agent_ns, const QString& channel) const
{
    if (channel == QLatin1String("state"))
    {
        const auto it = states_.constFind(agent_ns);
        if (it == states_.cend())
        {
            return QStringLiteral("%1 尚無 state 資料").arg(agent_ns);
        }
        const AgentStateData& s = *it;
        return QStringLiteral("stamp: %1.%2\nposition_ned: [%3, %4, %5]\nposition_global: [%6, %7, %8]\n"
                              "flight_mode: %9 (%10)\nbattery_remaining: %11\nbattery_voltage_v: %12\n"
                              "battery_current_a: %13\nonline: %14\narmed: %15\nlanded: %16\n"
                              "px4_nav_state: %17\npx4_state_valid: %18")
            .arg(s.stamp.sec)
            .arg(s.stamp.nanosec, 9, 10, QLatin1Char('0'))
            .arg(s.position_ned[0])
            .arg(s.position_ned[1])
            .arg(s.position_ned[2])
            .arg(s.position_global[0], 0, 'f', 7)
            .arg(s.position_global[1], 0, 'f', 7)
            .arg(s.position_global[2], 0, 'f', 2)
            .arg(s.flight_mode)
            .arg(ui::flight_mode_label(s.flight_mode))
            .arg(s.battery_remaining)
            .arg(s.battery_voltage_v)
            .arg(s.battery_current_a)
            .arg(s.is_online)
            .arg(s.is_armed)
            .arg(s.is_landed)
            .arg(s.px4_nav_state)
            .arg(s.is_px4_state_valid);
    }
    if (channel == QLatin1String("attitude"))
    {
        const auto it = attitudes_.constFind(agent_ns);
        if (it == attitudes_.cend())
        {
            return QStringLiteral("%1 尚無 attitude 資料").arg(agent_ns);
        }
        return QStringLiteral("q: [%1, %2, %3, %4]").arg(it->q[0]).arg(it->q[1]).arg(it->q[2]).arg(it->q[3]);
    }
    if (channel == QLatin1String("trajectory"))
    {
        const auto it = trajectories_.constFind(agent_ns);
        if (it == trajectories_.cend())
        {
            return QStringLiteral("%1 尚無 trajectory 資料").arg(agent_ns);
        }
        return QStringLiteral("trajectory_id: %1\nagent_index: %2\ncontrol_points: %3 點\nduration_s: %4\n"
                              "terminal_hold: %5\nvalid: %6")
            .arg(it->trajectory_id)
            .arg(it->agent_index)
            .arg(it->control_points.size() / VALUES_PER_CONTROL_POINT)
            .arg(it->duration_s)
            .arg(it->is_terminal_hold)
            .arg(it->is_valid);
    }
    return QStringLiteral("未知的資料種類：%1（可用 state、attitude、trajectory）").arg(channel);
}

void DiagnosticsController::execute(const QString& line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty())
    {
        return;
    }
    append(QStringLiteral("> ") + trimmed);
    static const QRegularExpression SPACES(QStringLiteral("\\s+"));
    QStringList words = trimmed.split(SPACES, Qt::SkipEmptyParts);
    // 相容網頁版習慣：允許以 ros2 開頭
    if (!words.isEmpty() && words.first() == QLatin1String("ros2"))
    {
        words.removeFirst();
    }
    const QString verb = words.value(0).toLower();
    const QString sub = words.value(1).toLower();

    if (verb == QLatin1String("help"))
    {
        append(help_text());
    }
    else if (verb == QLatin1String("clear"))
    {
        clear();
    }
    else if (verb == QLatin1String("topic") && sub == QLatin1String("list"))
    {
        append(topic_list());
    }
    else if (verb == QLatin1String("topic") && sub == QLatin1String("echo"))
    {
        QString agent = words.value(2);
        // 接受 /uav_1_agent/state 形式
        static const QRegularExpression TOPIC(QStringLiteral("^/?(uav_[1-9][0-9]*_agent)/(state|attitude|planning/trajectory)$"));
        const auto match = TOPIC.match(agent);
        QString channel = words.value(3, QStringLiteral("state")).toLower();
        if (match.hasMatch())
        {
            agent = match.captured(1);
            channel = match.captured(2) == QLatin1String("planning/trajectory") ? QStringLiteral("trajectory")
                                                                               : match.captured(2);
        }
        append(command_protocol::is_single_agent(agent) ? echo(agent, channel)
                                                        : QStringLiteral("請指定 uav_N_agent"));
    }
    else if (verb == QLatin1String("agents"))
    {
        QStringList agents = states_.keys();
        agents.sort();
        append(agents.isEmpty() ? QStringLiteral("尚未收到任何 Agent 資料") : agents.join(QLatin1Char('\n')));
    }
    else if (verb == QLatin1String("events"))
    {
        bool is_number = false;
        const int requested = words.value(1).toInt(&is_number);
        const int count = is_number && requested > 0 ? requested : DEFAULT_EVENT_COUNT;
        const QStringList recent = events_.mid(std::max(0, static_cast<int>(events_.size()) - count));
        append(recent.isEmpty() ? QStringLiteral("尚無事件") : recent.join(QLatin1Char('\n')));
    }
    else if (sub == QLatin1String("pub") || sub == QLatin1String("call") || verb == QLatin1String("pub")
             || verb == QLatin1String("call"))
    {
        append(QStringLiteral("除錯工具為唯讀，不提供發布或呼叫；命令請使用飛行控制面板（經 Guard 驗證）"));
    }
    else
    {
        append(QStringLiteral("未知指令。") + help_text());
    }
}

void DiagnosticsController::on_state_updated(const QString& agent_ns, const AgentStateData& state)
{
    states_.insert(agent_ns, state);
}

void DiagnosticsController::on_attitude_updated(const QString& agent_ns, const AgentAttitudeData& attitude)
{
    attitudes_.insert(agent_ns, attitude);
}

void DiagnosticsController::on_trajectory_updated(const QString& agent_ns, const AgentTrajectoryData& trajectory)
{
    trajectories_.insert(agent_ns, trajectory);
}

void DiagnosticsController::on_agent_online(const QString& agent_ns)
{
    record_event(QStringLiteral("Agent 上線 %1").arg(agent_ns));
}

void DiagnosticsController::on_agent_offline(const QString& agent_ns)
{
    record_event(QStringLiteral("Agent 離線 %1").arg(agent_ns));
    states_.remove(agent_ns);
    attitudes_.remove(agent_ns);
    trajectories_.remove(agent_ns);
}

void DiagnosticsController::on_command_acked(const CommandAck& ack)
{
    record_event(QStringLiteral("命令 %1 %2 %3 %4")
                     .arg(ack.agent, ack.command, ack.is_ok ? QStringLiteral("成功") : QStringLiteral("失敗"),
                          ack.message));
}

}  // namespace gcs
