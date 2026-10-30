/**
 * @file fleet_model.cpp
 * @brief 機群資料模型實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "app/fleet_model.hpp"

#include "app/ui_format.hpp"

#include <QRegularExpression>

namespace gcs
{

FleetModel::FleetModel(QObject* parent) : QAbstractListModel(parent)
{
}

int FleetModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(agents_.size());
}

QHash<int, QByteArray> FleetModel::roleNames() const
{
    return {
        {AgentRole, "agent"},
        {ShortNameRole, "shortName"},
        {IsSelectedRole, "isSelected"},
        {IsWaitingForFlightControllerRole, "isWaitingForFlightController"},
        {ModeLabelRole, "modeLabel"},
        {Px4ModeLabelRole, "px4ModeLabel"},
        {BatteryTextRole, "batteryText"},
        {VoltageTextRole, "voltageText"},
        {CurrentTextRole, "currentText"},
        {AltitudeTextRole, "altitudeText"},
        {HasPositionRole, "hasPosition"},
        {LatitudeRole, "latitude"},
        {LongitudeRole, "longitude"},
        {HasHeadingRole, "hasHeading"},
        {HeadingRole, "heading"},
        {HasAttitudeRole, "hasAttitude"},
        {RollRole, "roll"},
        {PitchRole, "pitch"},
        {Px4StatusLabelRole, "px4StatusLabel"},
        {NedTextRole, "nedText"},
        {PositionTextRole, "positionText"},
        {Px4LinkTextRole, "px4LinkText"},
        {PreflightTextRole, "preflightText"},
        {HealthWarningRole, "healthWarning"},
        {IsPx4LinkLostRole, "isPx4LinkLost"},
    };
}

QVariant FleetModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= agents_.size())
    {
        return {};
    }
    const QString& agent = agents_.at(index.row());
    const AgentView view = views_.value(agent);
    const AgentStateData& state = view.state;
    // Agent topic 在線不代表已收到有效 PX4 狀態，避免顯示尚未回報的預設值
    const bool is_waiting = !view.has_state || !state.is_px4_state_valid;

    switch (role)
    {
    case AgentRole:
        return agent;
    case ShortNameRole:
    {
        static const QRegularExpression PATTERN(QStringLiteral("^uav_(\\d+)_agent$"));
        const auto match = PATTERN.match(agent);
        return match.hasMatch() ? match.captured(1) : agent;
    }
    case IsSelectedRole:
        return agent == selected_agent_;
    case IsWaitingForFlightControllerRole:
        return is_waiting;
    case ModeLabelRole:
        return view.has_state ? ui::flight_mode_label(state.flight_mode) : QStringLiteral("—");
    case Px4ModeLabelRole:
        return view.has_state ? ui::px4_mode_label(state) : QStringLiteral("—");
    case BatteryTextRole:
        return ui::format_battery_percent(state.battery_remaining);
    case VoltageTextRole:
        return ui::format_battery_voltage(state.battery_voltage_v);
    case CurrentTextRole:
        return ui::format_battery_current(state.battery_current_a);
    case AltitudeTextRole:
        // NED 的 Z 向下為正，取反以顯示向上為正的相對高度
        return ui::format_number(-static_cast<double>(state.position_ned[2]), 1);
    case HasPositionRole:
        return view.has_state
               && ui::has_valid_global_position(ui::to_display_position(state.position_global, display_origin_));
    case LatitudeRole:
        return ui::to_display_position(state.position_global, display_origin_)[0];
    case LongitudeRole:
        return ui::to_display_position(state.position_global, display_origin_)[1];
    case HasHeadingRole:
        return view.has_attitude && ui::heading_degrees(view.attitude.q).has_value();
    case HeadingRole:
        return view.has_attitude ? ui::heading_degrees(view.attitude.q).value_or(0.0) : 0.0;
    case HasAttitudeRole:
        return view.has_attitude && ui::euler_degrees(view.attitude.q).has_value();
    case RollRole:
        return view.has_attitude ? ui::euler_degrees(view.attitude.q).value_or(ui::EulerDegrees{}).roll : 0.0;
    case PitchRole:
        return view.has_attitude ? ui::euler_degrees(view.attitude.q).value_or(ui::EulerDegrees{}).pitch : 0.0;
    // 以下供影像獨立視窗顯示各機完整狀態（狀態列只顯示選取機）
    case Px4StatusLabelRole:
        return view.has_state ? ui::px4_status_label(state) : QStringLiteral("—");
    case NedTextRole:
        return view.has_state ? ui::format_ned(state.position_ned) : QStringLiteral("—");
    case PositionTextRole:
        return view.has_state ? ui::format_lat_lon(ui::to_display_position(state.position_global, display_origin_))
                              : QStringLiteral("—");
    case Px4LinkTextRole:
        return view.has_health ? ui::px4_link_text(view.health) : QStringLiteral("—");
    case PreflightTextRole:
        return view.has_health ? ui::preflight_text(view.health) : QStringLiteral("—");
    case HealthWarningRole:
        // 機群表警示標記：PX4 資料中斷優先於預檢未通過；沒有健康狀態時不標記
        if (!view.has_health)
        {
            return QString();
        }
        if (ui::is_px4_link_lost(view.health))
        {
            return QStringLiteral("PX4 %1").arg(ui::px4_link_text(view.health));
        }
        return ui::is_preflight_failed(view.health) ? QStringLiteral("PX4 預檢未通過") : QString();
    case IsPx4LinkLostRole:
        return view.has_health && ui::is_px4_link_lost(view.health);
    default:
        return {};
    }
}

void FleetModel::update(const QStringList& online, const QHash<QString, AgentView>& views,
                        const QString& selected_agent, const ui::DisplayOrigin& display_origin)
{
    views_ = views;
    selected_agent_ = selected_agent;
    display_origin_ = display_origin;
    if (online != agents_)
    {
        apply_membership(online);
        emit countChanged();
    }
    if (!agents_.isEmpty())
    {
        emit dataChanged(index(0), index(static_cast<int>(agents_.size()) - 1));
    }
}

void FleetModel::apply_membership(const QStringList& online)
{
    // 先由後往前移除離線的列，前面的索引不受影響
    for (int i = static_cast<int>(agents_.size()) - 1; i >= 0; --i)
    {
        if (!online.contains(agents_.at(i)))
        {
            beginRemoveRows(QModelIndex(), i, i);
            agents_.removeAt(i);
            endRemoveRows();
        }
    }
    // 再依新清單的位置插入新上線的列
    for (int i = 0; i < online.size(); ++i)
    {
        if (i < agents_.size() && agents_.at(i) == online.at(i))
        {
            continue;
        }
        if (agents_.contains(online.at(i)))
        {
            // 既有列的順序改變（正常排序下不會發生），無法只用插入表達，整體重設
            beginResetModel();
            agents_ = online;
            endResetModel();
            return;
        }
        beginInsertRows(QModelIndex(), i, i);
        agents_.insert(i, online.at(i));
        endInsertRows();
    }
}

}  // namespace gcs
