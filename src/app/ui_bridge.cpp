/**
 * @file ui_bridge.cpp
 * @brief UiBridge 實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "app/ui_bridge.hpp"

#include "app/ui_format.hpp"
#include "modules/guard/guard_rules.hpp"

#include <QLoggingCategory>

#include <cmath>

namespace
{
Q_LOGGING_CATEGORY(lc_ui, "gcs.ui")
}  // namespace

namespace gcs
{

namespace
{

/// 每個 WGS84 控制點的元素數
constexpr int VALUES_PER_CONTROL_POINT = 3;
/// 毫秒轉秒
constexpr double MS_PER_SECOND = 1000.0;
/// 關閉請求在待回應表中的命令名稱（與 AgentLifecycleModule 回報相同）
const QString SHUTDOWN_COMMAND = QStringLiteral("AGENT_SHUTDOWN");
/// 關閉請求的介面逾時（毫秒）：必須長於 AgentLifecycleModule 的 5 秒，避免先於模組誤報逾時
constexpr int SHUTDOWN_UI_TIMEOUT_MS = 6000;

/**
 * @brief 組合待回應鍵
 * @param agent 目標
 * @param command 命令
 * @return 鍵
 */
QString pending_key(const QString& agent, const QString& command)
{
    return agent + QLatin1Char(':') + command;
}

}  // namespace

UiBridge::UiBridge(int command_timeout_ms, QObject* parent)
    : QObject(parent), command_timeout_ms_(command_timeout_ms), refresh_timer_(this)
{
    refresh_timer_.setInterval(REFRESH_INTERVAL_MS);
    connect(&refresh_timer_, &QTimer::timeout, this, &UiBridge::refresh);
    refresh_timer_.start();
    recompute_availability();
}

bool UiBridge::is_single_selected() const
{
    return command_protocol::is_single_agent(selected_agent_);
}

bool UiBridge::is_all_selected() const
{
    return selected_agent_ == ALL_AGENTS_TARGET;
}

bool UiBridge::can_shutdown_agent() const
{
    return is_single_selected() && online_agents_.contains(selected_agent_);
}

void UiBridge::selectAgent(const QString& agent)
{
    if (agent == selected_agent_)
    {
        return;
    }
    selected_agent_ = agent;
    emit selectionChanged();
    refresh_now();
}

QString UiBridge::flightModeLabel(int flight_mode) const
{
    return ui::flight_mode_label(flight_mode);
}

void UiBridge::notify(ToastModel::Kind kind, const QString& title, const QString& message)
{
    const QString text = title + QStringLiteral("：") + message;
    if (kind == ToastModel::Kind::SUCCESS)
    {
        // 成功類訊息只寫日誌，不彈出提示，避免頻繁遮擋畫面
        qCInfo(lc_ui).noquote() << text;
        return;
    }
    if (kind == ToastModel::Kind::ERROR)
    {
        qCCritical(lc_ui).noquote() << text;
    }
    else
    {
        qCWarning(lc_ui).noquote() << text;
    }
    toast_model_.add(kind, title, message);
}

bool UiBridge::begin_pending(const QString& agent, const QString& command, int timeout_ms)
{
    const QString key = pending_key(agent, command);
    if (pending_commands_.contains(key))
    {
        return false;
    }
    const quint64 id = next_pending_id_++;
    pending_commands_.insert(key, id);
    QTimer::singleShot(timeout_ms, this,
                       [this, key, id, agent, command, timeout_ms]()
                       {
                           const auto it = pending_commands_.constFind(key);
                           if (it == pending_commands_.cend() || *it != id)
                           {
                               return;
                           }
                           pending_commands_.remove(key);
                           notify(ToastModel::Kind::WARNING, QStringLiteral("%1 逾時").arg(command),
                                            QStringLiteral("%1 未在 %2 秒內回應 %3")
                                                .arg(agent)
                                                .arg(timeout_ms / MS_PER_SECOND)
                                                .arg(command));
                       });
    return true;
}

void UiBridge::sendCommand(const QString& command, const QVariantList& params)
{
    CommandRequest request;
    request.agent = is_single_selected() ? selected_agent_ : ALL_AGENTS_TARGET;
    request.command = command;
    for (const QVariant& value : params)
    {
        bool is_number = false;
        const double number = value.toDouble(&is_number);
        // 非數值一律轉成 NaN，由 Guard 以格式錯誤拒絕並回報，不在此靜默丟棄
        request.params.push_back(is_number ? number : std::nan(""));
    }
    if (!is_link_connected_)
    {
        notify(ToastModel::Kind::ERROR, QStringLiteral("DDS 未連線"),
                         QStringLiteral("無法發送 %1：通訊尚未啟動").arg(command));
        return;
    }
    if (!begin_pending(request.agent, command, command_timeout_ms_))
    {
        notify(ToastModel::Kind::WARNING, QStringLiteral("命令待回執"),
                         QStringLiteral("%1 已有 %2 等待回應，請勿重複發送").arg(request.agent, command));
        return;
    }
    emit command_requested(request);
}

void UiBridge::requestAgentShutdown()
{
    if (!can_shutdown_agent())
    {
        return;
    }
    if (!begin_pending(selected_agent_, SHUTDOWN_COMMAND, SHUTDOWN_UI_TIMEOUT_MS))
    {
        notify(ToastModel::Kind::WARNING, QStringLiteral("關閉請求待回應"),
                         QStringLiteral("%1 已有關閉請求等待回應，請勿重複發送").arg(selected_agent_));
        return;
    }
    emit shutdown_requested(selected_agent_);
}

void UiBridge::on_command_acked(const CommandAck& ack)
{
    pending_commands_.remove(pending_key(ack.agent, ack.command));
    if (ack.is_ok && ack.command == SHUTDOWN_COMMAND)
    {
        // 關閉請求有 service 回應，是唯一能確認對方已執行的命令
        notify(ToastModel::Kind::SUCCESS, QStringLiteral("%1 成功").arg(ack.command),
                         ack.message.isEmpty() ? QStringLiteral("%1 已接受 %2").arg(ack.agent, ack.command)
                                               : ack.message);
        return;
    }
    if (ack.is_ok)
    {
        // 命令協定為單向，發布成功只代表已送出，不代表 Agent 或飛控已執行
        const bool is_arm = ack.command == QLatin1String("ARM");
        const bool is_disarm = ack.command == QLatin1String("DISARM");
        const QString detail = (is_arm || is_disarm) ? QStringLiteral("，等待飛控解鎖狀態確認")
                                                     : QStringLiteral("，飛控執行結果請以狀態為準");
        notify(ToastModel::Kind::SUCCESS, QStringLiteral("%1 已送出").arg(ack.command),
                         ack.message.isEmpty()
                             ? QStringLiteral("已將 %1 送至 %2").arg(ack.command, ack.agent) + detail
                             : ack.message);
        if (is_arm || is_disarm)
        {
            begin_arm_check(ack.agent, is_arm);
        }
        return;
    }
    notify(ToastModel::Kind::ERROR, QStringLiteral("%1 失敗").arg(ack.command),
                     ack.message.isEmpty() ? QStringLiteral("%1 拒絕 %2 命令").arg(ack.agent, ack.command)
                                           : ack.message);
}

void UiBridge::begin_arm_check(const QString& agent, bool is_expected_armed)
{
    const QStringList targets = agent == ALL_AGENTS_TARGET ? online_agents_ : QStringList{agent};
    const int timeout_ms = command_timeout_ms_;
    for (const QString& target : targets)
    {
        // 同一台重新送出 ARM／DISARM 時以最新一筆為準，舊計時器會因識別碼不符而略過
        const quint64 id = next_pending_id_++;
        arm_checks_.insert(target, ArmCheck{id, is_expected_armed});
        QTimer::singleShot(timeout_ms, this,
                           [this, target, id, is_expected_armed, timeout_ms]()
                           {
                               const auto it = arm_checks_.constFind(target);
                               if (it == arm_checks_.cend() || it->id != id)
                               {
                                   return;
                               }
                               arm_checks_.remove(target);
                               notify(
                                   ToastModel::Kind::WARNING,
                                   QStringLiteral("%1 未%2").arg(target, is_expected_armed ? QStringLiteral("解鎖")
                                                                                          : QStringLiteral("上鎖")),
                                   QStringLiteral("%1 秒內未觀察到%2，飛控可能拒絕命令，請檢查 PX4 健康狀態")
                                       .arg(timeout_ms / MS_PER_SECOND)
                                       .arg(is_expected_armed ? QStringLiteral("解鎖") : QStringLiteral("上鎖")));
                           });
    }
}

void UiBridge::resolve_arm_check(const QString& agent_ns, const AgentStateData& state)
{
    const auto it = arm_checks_.constFind(agent_ns);
    // PX4 狀態無效時 armed 欄位不可信，繼續等待
    if (it == arm_checks_.cend() || !state.is_px4_state_valid || state.is_armed != it->is_expected_armed)
    {
        return;
    }
    const bool is_armed = it->is_expected_armed;
    arm_checks_.remove(agent_ns);
    const QString label = is_armed ? QStringLiteral("已解鎖") : QStringLiteral("已上鎖");
    notify(ToastModel::Kind::SUCCESS, agent_ns + QLatin1Char(' ') + label,
                     QStringLiteral("飛控回報 %1 %2").arg(agent_ns, label));
}

void UiBridge::on_state_updated(const QString& agent_ns, const AgentStateData& state)
{
    AgentView& view = views_[agent_ns];
    view.state = state;
    view.has_state = true;
    resolve_arm_check(agent_ns, state);
    mark_dirty();
}

void UiBridge::on_attitude_updated(const QString& agent_ns, const AgentAttitudeData& attitude)
{
    AgentView& view = views_[agent_ns];
    view.attitude = attitude;
    view.has_attitude = true;
    mark_dirty();
}

void UiBridge::on_trajectory_updated(const QString& agent_ns, const AgentTrajectoryData& trajectory)
{
    AgentView& view = views_[agent_ns];
    view.trajectory = trajectory;
    view.has_trajectory = true;
    mark_dirty();
}

void UiBridge::on_fleet_changed(const FleetSnapshot& snapshot)
{
    // 停滯或消失的 Agent 移除快取資料，避免地圖與面板殘留舊位置
    for (const QString& agent : snapshot.offline)
    {
        views_.remove(agent);
        if (arm_checks_.remove(agent))
        {
            notify(ToastModel::Kind::WARNING, QStringLiteral("%1 解鎖狀態未確認").arg(agent),
                             QStringLiteral("%1 已離線，無法確認 ARM／DISARM 是否生效").arg(agent));
        }
    }
    const bool is_list_changed = snapshot.online != online_agents_;
    online_agents_ = snapshot.online;

    // 與網頁版 fleetStore 相同：選取的單機離線時改選全體，沒有線上 Agent 則取消選取
    if (is_single_selected() && !online_agents_.contains(selected_agent_))
    {
        selected_agent_ = online_agents_.isEmpty() ? QString() : ALL_AGENTS_TARGET;
        emit selectionChanged();
    }
    if (is_list_changed)
    {
        emit fleetChanged();
    }
    mark_dirty();
}

void UiBridge::set_link_status(bool is_connected, const QString& message)
{
    is_link_connected_ = is_connected;
    link_message_ = message;
    emit linkStatusChanged();
}

void UiBridge::mark_dirty()
{
    is_dirty_ = true;
}

void UiBridge::refresh()
{
    if (!is_dirty_)
    {
        return;
    }
    refresh_now();
}

void UiBridge::refresh_now()
{
    is_dirty_ = false;
    fleet_model_.update(online_agents_, views_, selected_agent_);
    rebuild_summary();
    rebuild_trajectories();
    recompute_availability();
    emit telemetryChanged();
}

void UiBridge::recompute_availability()
{
    const QHash<CmdType, bool> previous = availability_;
    availability_.clear();

    const auto is_allowed_for = [this](const QString& agent, CmdType cmd)
    {
        const auto it = views_.constFind(agent);
        return it != views_.cend() && it->has_state && guard_rules::is_command_allowed_for_agent(it->state, cmd);
    };
    const auto is_allowed = [&](CmdType cmd)
    {
        if (is_single_selected())
        {
            return online_agents_.contains(selected_agent_) && is_allowed_for(selected_agent_, cmd);
        }
        if (is_all_selected() && !online_agents_.isEmpty())
        {
            for (const QString& agent : online_agents_)
            {
                if (!is_allowed_for(agent, cmd))
                {
                    return false;
                }
            }
            return true;
        }
        return false;
    };

    // 與 GuardModule 使用同一份規則；按鈕只是提示，實際閘門仍是 Guard
    for (const CmdType cmd : {CmdType::ARM, CmdType::DISARM, CmdType::MODE_OFFBOARD, CmdType::TAKEOFF,
                              CmdType::MOVE_FORWARD, CmdType::MODE_RTL, CmdType::MODE_LAND})
    {
        availability_.insert(cmd, is_allowed(cmd));
    }
    // YAW 僅支援單機（網頁版相同）；編隊僅支援全體
    availability_.insert(CmdType::YAW, is_single_selected() && is_allowed(CmdType::YAW));
    availability_.insert(CmdType::FORMATION_CIRCLE, is_all_selected() && !online_agents_.isEmpty());

    if (availability_ != previous)
    {
        emit availabilityChanged();
    }
}

void UiBridge::rebuild_summary()
{
    QVariantMap summary;
    summary.insert(QStringLiteral("agent"), selected_agent_);
    const auto it = views_.constFind(selected_agent_);
    const bool has_state = it != views_.cend() && it->has_state;
    summary.insert(QStringLiteral("hasTelemetry"), has_state);
    summary.insert(QStringLiteral("isWaitingForFlightController"),
                   online_agents_.contains(selected_agent_) && (!has_state || !it->state.is_px4_state_valid));
    if (has_state)
    {
        const AgentStateData& state = it->state;
        summary.insert(QStringLiteral("modeLabel"), ui::flight_mode_label(state.flight_mode));
        summary.insert(QStringLiteral("batteryText"), ui::format_battery_percent(state.battery_remaining));
        summary.insert(QStringLiteral("px4Label"), ui::px4_status_label(state));
        summary.insert(QStringLiteral("px4ModeLabel"), ui::px4_mode_label(state));
        summary.insert(QStringLiteral("isPx4Valid"), state.is_px4_state_valid);
        // 供飛行控制面板常駐顯示解鎖狀態；僅在 isPx4Valid 為 true 時可信
        summary.insert(QStringLiteral("isArmed"), state.is_px4_state_valid && state.is_armed);
        summary.insert(QStringLiteral("voltageText"), ui::format_battery_voltage(state.battery_voltage_v));
        summary.insert(QStringLiteral("currentText"), ui::format_battery_current(state.battery_current_a));
        // 儀表格以帶正負號、雙空白分隔顯示 N／E／D，等寬字下各軸對齊
        summary.insert(QStringLiteral("nedText"), QStringLiteral("%1  %2  %3")
                                                      .arg(ui::format_signed_number(state.position_ned[0], 1),
                                                           ui::format_signed_number(state.position_ned[1], 1),
                                                           ui::format_signed_number(state.position_ned[2], 1)));
        summary.insert(QStringLiteral("hasPosition"), ui::has_valid_global_position(state.position_global));
        summary.insert(QStringLiteral("latitude"), state.position_global[0]);
        summary.insert(QStringLiteral("longitude"), state.position_global[1]);
    }
    if (it != views_.cend() && it->has_attitude)
    {
        const auto heading = ui::heading_degrees(it->attitude.q);
        summary.insert(QStringLiteral("hasHeading"), heading.has_value());
        summary.insert(QStringLiteral("heading"), heading.value_or(0.0));
        // 機群面板姿態儀：四元數無效時不顯示
        const auto euler = ui::euler_degrees(it->attitude.q);
        summary.insert(QStringLiteral("hasAttitude"), euler.has_value());
        summary.insert(QStringLiteral("roll"), euler.has_value() ? euler->roll : 0.0);
        summary.insert(QStringLiteral("pitch"), euler.has_value() ? euler->pitch : 0.0);
    }
    selected_summary_ = summary;
}

void UiBridge::rebuild_trajectories()
{
    QVariantList list;
    for (const QString& agent : online_agents_)
    {
        const auto it = views_.constFind(agent);
        if (it == views_.cend() || !it->has_trajectory || !it->trajectory.is_valid)
        {
            continue;
        }
        const auto& points = it->trajectory.control_points;
        QVariantList path;
        for (size_t i = 0; i + VALUES_PER_CONTROL_POINT - 1 < points.size(); i += VALUES_PER_CONTROL_POINT)
        {
            const std::array<double, 3> position = {points[i], points[i + 1], points[i + 2]};
            if (!ui::has_valid_global_position(position))
            {
                continue;
            }
            path.append(QVariantMap{{QStringLiteral("latitude"), points[i]},
                                    {QStringLiteral("longitude"), points[i + 1]}});
        }
        if (path.size() < 2)
        {
            continue;
        }
        list.append(QVariantMap{{QStringLiteral("agent"), agent},
                                {QStringLiteral("isSelected"), agent == selected_agent_},
                                {QStringLiteral("path"), path}});
    }
    trajectories_ = list;
}

}  // namespace gcs
