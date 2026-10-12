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

namespace
{

/// 電量比例轉百分比
constexpr double PERCENT = 100.0;

}  // namespace

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
    case BatteryTextRole:
        return ui::format_number(static_cast<double>(state.battery_remaining) * PERCENT, 0);
    case VoltageTextRole:
        return ui::format_number(state.battery_voltage_v, 1);
    case CurrentTextRole:
        return ui::format_number(state.battery_current_a, 1);
    case AltitudeTextRole:
        // NED 的 Z 向下為正，取反以顯示向上為正的相對高度
        return ui::format_number(-static_cast<double>(state.position_ned[2]), 1);
    case HasPositionRole:
        return view.has_state && ui::has_valid_global_position(state.position_global);
    case LatitudeRole:
        return state.position_global[0];
    case LongitudeRole:
        return state.position_global[1];
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
    default:
        return {};
    }
}

void FleetModel::update(const QStringList& online, const QHash<QString, AgentView>& views,
                        const QString& selected_agent)
{
    views_ = views;
    selected_agent_ = selected_agent;
    if (online != agents_)
    {
        beginResetModel();
        agents_ = online;
        endResetModel();
        emit countChanged();
        return;
    }
    if (!agents_.isEmpty())
    {
        emit dataChanged(index(0), index(static_cast<int>(agents_.size()) - 1));
    }
}

}  // namespace gcs
