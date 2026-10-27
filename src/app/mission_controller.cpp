/**
 * @file mission_controller.cpp
 * @brief 航點任務介面狀態實作
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 */
#include "app/mission_controller.hpp"

#include "core/command_protocol.hpp"
#include "modules/mission/mission_module.hpp"
#include "voice/voice_keywords.hpp"

#include <QLoggingCategory>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace
{
Q_LOGGING_CATEGORY(lc_mission_ui, "gcs.ui.mission")
}  // namespace

namespace gcs
{

namespace
{

/// 每度緯度對應的距離（公尺，球體近似），只用於顯示航點間距
constexpr double METERS_PER_DEGREE = 111319.49;
constexpr double DEGREES_TO_RADIANS = 3.14159265358979323846 / 180.0;
/// 高度四捨五入到 0.1 公尺
constexpr double TENTHS_PER_METER = 10.0;
constexpr int MS_PER_SECOND = 1000;
const QString VOICE_TITLE = QStringLiteral("語音口令");

/**
 * @brief 兩個航點的水平距離
 * @param a 航點
 * @param b 航點
 * @return 距離（公尺）
 */
double horizontal_distance(const MissionWaypoint& a, const MissionWaypoint& b)
{
    const double north = (b.latitude - a.latitude) * METERS_PER_DEGREE;
    const double east = (b.longitude - a.longitude) * METERS_PER_DEGREE * std::cos(a.latitude * DEGREES_TO_RADIANS);
    return std::hypot(north, east);
}

/**
 * @brief 任務是否執行中
 * @param phase 任務階段
 * @return MOVING 或 DWELLING
 */
bool is_active_phase(MissionPhase phase)
{
    return phase == MissionPhase::MOVING || phase == MissionPhase::DWELLING;
}

/**
 * @brief UAV 簡稱，例如 uav_1_agent → UAV_1
 * @param agent Agent namespace
 * @return 簡稱
 */
QString short_name(const QString& agent)
{
    QString name = agent;
    name.remove(QStringLiteral("_agent"));
    return name.toUpper();
}

}  // namespace

MissionController::MissionController(int confirm_timeout_ms, QObject* parent)
    : QObject(parent), confirm_timeout_ms_(confirm_timeout_ms), confirm_timer_(this)
{
    clock_.start();
    confirm_timer_.setSingleShot(true);
    connect(&confirm_timer_, &QTimer::timeout, this,
            [this]()
            {
                if (pending_.has_value())
                {
                    pending_.reset();
                    notify(ToastModel::Kind::WARNING, QStringLiteral("航點任務未開始"),
                           QStringLiteral("%1 秒內未確認，已取消").arg(confirm_timeout_ms_ / MS_PER_SECOND));
                    emit changed();
                }
            });
}

bool MissionController::is_single_selected() const
{
    return command_protocol::is_single_agent(selected_agent_);
}

std::array<double, 3> MissionController::display_of(const MissionWaypoint& waypoint) const
{
    return ui::to_display_position({waypoint.latitude, waypoint.longitude, waypoint.altitude_m}, display_origin_);
}

QVariantList MissionController::waypoints() const
{
    QVariantList list;
    if (!is_single_selected())
    {
        return list;
    }
    const QVector<MissionWaypoint> points = waypoints_.value(selected_agent_);
    for (int i = 0; i < points.size(); ++i)
    {
        const auto position = display_of(points.at(i));
        list.append(QVariantMap{
            {QStringLiteral("index"), i},
            {QStringLiteral("latitude"), position[0]},
            {QStringLiteral("longitude"), position[1]},
            {QStringLiteral("altitude"), points.at(i).altitude_m},
            {QStringLiteral("distanceText"),
             i == 0 ? QString()
                    : QStringLiteral("%1 m").arg(std::round(horizontal_distance(points.at(i - 1), points.at(i))))},
        });
    }
    return list;
}

QVariantList MissionController::routes() const
{
    QVariantList list;
    for (auto it = waypoints_.cbegin(); it != waypoints_.cend(); ++it)
    {
        if (it.value().isEmpty())
        {
            continue;
        }
        QVariantList points;
        for (int i = 0; i < it.value().size(); ++i)
        {
            const auto position = display_of(it.value().at(i));
            points.append(QVariantMap{{QStringLiteral("index"), i},
                                      {QStringLiteral("latitude"), position[0]},
                                      {QStringLiteral("longitude"), position[1]}});
        }
        const MissionStatus status = statuses_.value(it.key());
        list.append(QVariantMap{
            {QStringLiteral("agent"), it.key()},
            {QStringLiteral("isSelected"), it.key() == selected_agent_},
            {QStringLiteral("currentIndex"), is_active_phase(status.phase) ? status.waypoint_index : -1},
            {QStringLiteral("points"), points},
        });
    }
    return list;
}

QVariantList MissionController::waypoint_markers() const
{
    QVariantList list;
    for (auto it = waypoints_.cbegin(); it != waypoints_.cend(); ++it)
    {
        const MissionStatus status = statuses_.value(it.key());
        const int current = is_active_phase(status.phase) ? status.waypoint_index : -1;
        for (int i = 0; i < it.value().size(); ++i)
        {
            const auto position = display_of(it.value().at(i));
            list.append(QVariantMap{
                {QStringLiteral("agent"), it.key()},
                {QStringLiteral("index"), i},
                {QStringLiteral("latitude"), position[0]},
                {QStringLiteral("longitude"), position[1]},
                {QStringLiteral("isSelected"), it.key() == selected_agent_},
                {QStringLiteral("isCurrent"), i == current},
            });
        }
    }
    return list;
}

void MissionController::setEditing(bool is_editing)
{
    if (is_editing_ == is_editing)
    {
        return;
    }
    is_editing_ = is_editing;
    emit changed();
}

int MissionController::waypoint_count() const
{
    return is_single_selected() ? static_cast<int>(waypoints_.value(selected_agent_).size()) : 0;
}

bool MissionController::can_edit() const
{
    return is_single_selected() && !is_running();
}

bool MissionController::is_running() const
{
    return is_single_selected() && is_active_phase(statuses_.value(selected_agent_).phase);
}

QString MissionController::block_reason() const
{
    if (!is_single_selected())
    {
        return QStringLiteral("需選取單機");
    }
    if (is_running())
    {
        return QStringLiteral("任務執行中");
    }
    if (waypoints_.value(selected_agent_).isEmpty())
    {
        return QStringLiteral("請先在地圖新增航點");
    }
    const auto it = states_.constFind(selected_agent_);
    if (it == states_.cend() || !it->is_px4_state_valid)
    {
        return QStringLiteral("等待飛控");
    }
    if (it->flight_mode != static_cast<uint8_t>(FlightState::HOVER))
    {
        return QStringLiteral("需在懸停狀態");
    }
    return QString();
}

bool MissionController::can_start() const
{
    return block_reason().isEmpty();
}

QString MissionController::start_block_reason() const
{
    return block_reason();
}

QString MissionController::status_text() const
{
    if (!is_single_selected() || !statuses_.contains(selected_agent_))
    {
        return QString();
    }
    const MissionStatus status = statuses_.value(selected_agent_);
    if (status.phase == MissionPhase::REJECTED)
    {
        return QStringLiteral("無法開始：") + status.message;
    }
    return status.message;
}

QString MissionController::confirm_text() const
{
    if (!pending_.has_value())
    {
        return QString();
    }
    return QStringLiteral("%1 執行航點任務（%2 個航點）？")
        .arg(short_name(pending_->plan.agent))
        .arg(pending_->plan.waypoints.size());
}

void MissionController::set_selected_agent(const QString& agent)
{
    if (agent == selected_agent_)
    {
        return;
    }
    selected_agent_ = agent;
    is_editing_ = false;
    // 確認的是當時選取的單機；切換目標後不能沿用
    pending_.reset();
    confirm_timer_.stop();
    emit changed();
}

void MissionController::set_display_origin(const ui::DisplayOrigin& origin)
{
    display_origin_ = origin;
    emit changed();
}

void MissionController::addWaypoint(double latitude, double longitude)
{
    if (!can_edit())
    {
        return;
    }
    const auto reported = ui::to_reported_position({latitude, longitude, 0.0}, display_origin_);
    if (!ui::has_valid_global_position(reported))
    {
        return;
    }
    QVector<MissionWaypoint>& points = waypoints_[selected_agent_];
    if (points.size() >= MissionModule::MAX_WAYPOINTS)
    {
        notify(ToastModel::Kind::WARNING, QStringLiteral("無法新增航點"),
               QStringLiteral("航點最多 %1 個").arg(MissionModule::MAX_WAYPOINTS));
        return;
    }
    // 新航點沿用該機目前高度；沒有高度資料或仍在地面時使用預設值
    double altitude = DEFAULT_ALTITUDE_M;
    const auto it = states_.constFind(selected_agent_);
    if (it != states_.cend() && it->is_px4_state_valid && std::isfinite(it->position_ned[2]))
    {
        const double current = -static_cast<double>(it->position_ned[2]);
        if (current >= MissionModule::MIN_ALTITUDE_M)
        {
            altitude = std::round(std::min(current, MissionModule::MAX_ALTITUDE_M) * TENTHS_PER_METER)
                       / TENTHS_PER_METER;
        }
    }
    points.append({reported[0], reported[1], altitude});
    pending_.reset();
    emit changed();
}

void MissionController::removeWaypoint(int index)
{
    if (!can_edit())
    {
        return;
    }
    QVector<MissionWaypoint>& points = waypoints_[selected_agent_];
    if (index < 0 || index >= points.size())
    {
        return;
    }
    points.removeAt(index);
    pending_.reset();
    emit changed();
}

void MissionController::clearWaypoints()
{
    if (!can_edit())
    {
        return;
    }
    waypoints_.remove(selected_agent_);
    pending_.reset();
    emit changed();
}

void MissionController::setWaypointAltitude(int index, double altitude_m)
{
    if (!can_edit() || !std::isfinite(altitude_m) || altitude_m < MissionModule::MIN_ALTITUDE_M
        || altitude_m > MissionModule::MAX_ALTITUDE_M)
    {
        return;
    }
    QVector<MissionWaypoint>& points = waypoints_[selected_agent_];
    if (index < 0 || index >= points.size())
    {
        return;
    }
    points[index].altitude_m = altitude_m;
    pending_.reset();
    emit changed();
}

void MissionController::requestStart()
{
    const QString reason = block_reason();
    if (!reason.isEmpty())
    {
        notify(ToastModel::Kind::WARNING, QStringLiteral("無法開始航點任務"), reason);
        return;
    }
    PendingStart pending;
    pending.plan.agent = selected_agent_;
    pending.plan.waypoints = waypoints_.value(selected_agent_);
    pending.requested_ms = clock_.elapsed();
    pending_ = pending;
    confirm_timer_.start(confirm_timeout_ms_);
    qCInfo(lc_mission_ui).noquote() << "等待確認：" << confirm_text();
    emit changed();
    emit confirmRequested();
}

void MissionController::confirmStart()
{
    if (!pending_.has_value())
    {
        return;
    }
    const MissionPlan plan = pending_->plan;
    pending_.reset();
    confirm_timer_.stop();
    emit changed();
    // 確認時再檢查一次：等待期間目標、航點或飛行狀態都可能改變
    const QString reason = block_reason();
    if (plan.agent != selected_agent_ || !reason.isEmpty())
    {
        notify(ToastModel::Kind::WARNING, QStringLiteral("航點任務未開始"),
               reason.isEmpty() ? QStringLiteral("選取目標已改變") : reason);
        return;
    }
    qCInfo(lc_mission_ui) << "確認開始航點任務：" << plan.agent << "航點" << plan.waypoints.size() << "個";
    emit mission_start_requested(plan);
}

void MissionController::cancelStart()
{
    if (!pending_.has_value())
    {
        return;
    }
    pending_.reset();
    confirm_timer_.stop();
    qCInfo(lc_mission_ui) << "已取消待確認的航點任務";
    emit changed();
}

void MissionController::stopMission()
{
    if (!is_running())
    {
        return;
    }
    emit mission_stop_requested(selected_agent_);
}

void MissionController::on_state_updated(const QString& agent_ns, const AgentStateData& state)
{
    const auto it = states_.constFind(agent_ns);
    const bool is_changed = it == states_.cend() || it->flight_mode != state.flight_mode
                            || it->is_px4_state_valid != state.is_px4_state_valid;
    states_.insert(agent_ns, state);
    if (!is_changed || agent_ns != selected_agent_)
    {
        return;
    }
    // 待確認期間離開懸停：確認時的狀態已與提示不同，直接取消
    if (pending_.has_value() && pending_->plan.agent == agent_ns && !block_reason().isEmpty())
    {
        pending_.reset();
        confirm_timer_.stop();
        notify(ToastModel::Kind::WARNING, QStringLiteral("航點任務未開始"),
               QStringLiteral("飛行狀態已改變，已取消待確認的任務"));
    }
    emit changed();
}

void MissionController::on_mission_status(const MissionStatus& status)
{
    statuses_.insert(status.agent, status);
    switch (status.phase)
    {
    case MissionPhase::REJECTED:
        notify(ToastModel::Kind::ERROR, QStringLiteral("%1 航點任務無法開始").arg(short_name(status.agent)),
               status.message);
        break;
    case MissionPhase::ABORTED:
        notify(ToastModel::Kind::WARNING, QStringLiteral("%1 航點任務中止").arg(short_name(status.agent)),
               status.message);
        break;
    default:
        notify(ToastModel::Kind::SUCCESS, QStringLiteral("%1 航點任務").arg(short_name(status.agent)),
               status.message);
        break;
    }
    emit changed();
}

void MissionController::on_fleet_changed(const FleetSnapshot& snapshot)
{
    // 與 UiBridge 相同以顯示清單判斷離線：飛機從地圖消失時，航點與任務一併移除
    bool is_changed = false;
    for (const QString& agent : snapshot.offline)
    {
        const bool has_mission = waypoints_.remove(agent);
        const bool has_status = statuses_.remove(agent);
        const bool has_state = states_.remove(agent);
        if (has_mission || has_status)
        {
            qCInfo(lc_mission_ui) << agent << "已離線，清除航點與任務狀態";
        }
        if (pending_.has_value() && pending_->plan.agent == agent)
        {
            pending_.reset();
            confirm_timer_.stop();
            is_changed = true;
        }
        is_changed = is_changed || has_mission || has_status || has_state;
    }
    if (is_changed)
    {
        emit changed();
    }
}

void MissionController::on_voice_keyword(const QString& keyword)
{
    qCInfo(lc_mission_ui).noquote() << "語音口令：" << keyword;
    if (keyword == voice::KEYWORD_START_MISSION)
    {
        if (!is_single_selected())
        {
            notify(ToastModel::Kind::WARNING, VOICE_TITLE,
                   QStringLiteral("語音任務只支援單機控制，請先選取一台無人機"));
            return;
        }
        requestStart();
        return;
    }
    if (keyword == voice::KEYWORD_CONFIRM)
    {
        if (!pending_.has_value())
        {
            notify(ToastModel::Kind::WARNING, VOICE_TITLE, QStringLiteral("沒有待確認的任務"));
            return;
        }
        if (clock_.elapsed() - pending_->requested_ms < VOICE_CONFIRM_MIN_DELAY_MS)
        {
            notify(ToastModel::Kind::WARNING, VOICE_TITLE, QStringLiteral("請在確認提示出現後，再按住說「確認」"));
            return;
        }
        confirmStart();
        return;
    }
    if (keyword == voice::KEYWORD_CANCEL)
    {
        cancelStart();
        return;
    }
    if (keyword == voice::KEYWORD_STOP_MISSION)
    {
        if (!is_running())
        {
            notify(ToastModel::Kind::WARNING, VOICE_TITLE, QStringLiteral("目前沒有執行中的任務"));
            return;
        }
        stopMission();
    }
}

void MissionController::notify(ToastModel::Kind kind, const QString& title, const QString& message)
{
    const QString text = title + QStringLiteral("：") + message;
    if (kind == ToastModel::Kind::SUCCESS)
    {
        qCInfo(lc_mission_ui).noquote() << text;
        return;
    }
    qCWarning(lc_mission_ui).noquote() << text;
    if (toasts_ != nullptr)
    {
        toasts_->add(kind, title, message);
    }
}

}  // namespace gcs
