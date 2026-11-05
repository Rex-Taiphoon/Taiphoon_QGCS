/**
 * @file mission_controller.cpp
 * @brief 航點任務介面狀態實作
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 */
#include "app/mission_controller.hpp"

#include "core/command_protocol.hpp"
#include "modules/guard/guard_rules.hpp"
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
const QString COMMAND_ARM = QStringLiteral("ARM");
const QString COMMAND_TAKEOFF = QStringLiteral("TAKEOFF");
const QString COMMAND_LAND = QStringLiteral("MODE_LAND");

/**
 * @brief 語音飛行命令的中文動作名稱
 * @param command 命令名稱
 * @return 動作名稱
 */
QString command_action(const QString& command)
{
    if (command == COMMAND_ARM)
    {
        return QStringLiteral("解鎖");
    }
    if (command == COMMAND_TAKEOFF)
    {
        return QStringLiteral("起飛至 %1 m").arg(MissionController::VOICE_TAKEOFF_HEIGHT_M);
    }
    if (command == COMMAND_LAND)
    {
        return QStringLiteral("降落");
    }
    return command;
}

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
 * @brief 任務是否開始中或執行中（可停止、不可編輯）
 * @param phase 任務階段
 * @return STARTING、MOVING 或 DWELLING
 */
bool is_active_phase(MissionPhase phase)
{
    return phase == MissionPhase::STARTING || phase == MissionPhase::MOVING || phase == MissionPhase::DWELLING;
}

}  // namespace

MissionController::MissionController(int confirm_timeout_ms, int auto_start_delay_ms, QObject* parent)
    : QObject(parent),
      confirm_timeout_ms_(confirm_timeout_ms),
      auto_start_delay_ms_(auto_start_delay_ms),
      confirm_timer_(this),
      countdown_timer_(this)
{
    clock_.start();
    confirm_timer_.setSingleShot(true);
    // 倒數秒數取自剩餘時間；Linux 的粗略計時器可能把到期時間延後最多 5%，5 秒倒數會先顯示 6
    confirm_timer_.setTimerType(Qt::PreciseTimer);
    connect(&confirm_timer_, &QTimer::timeout, this, &MissionController::on_confirm_timeout);
    countdown_timer_.setInterval(COUNTDOWN_TICK_MS);
    connect(&countdown_timer_, &QTimer::timeout, this, &MissionController::on_countdown_tick);
}

void MissionController::clear_pending()
{
    pending_.reset();
    confirm_timer_.stop();
    countdown_timer_.stop();
    last_countdown_seconds_ = 0;
}

void MissionController::on_confirm_timeout()
{
    if (!pending_.has_value())
    {
        return;
    }
    if (pending_->is_auto_start)
    {
        // 倒數結束：與按 EXEC 相同，執行前再核對一次目標與飛行狀態
        qCInfo(lc_mission_ui) << "語音倒數結束，自動執行" << (pending_->command.isEmpty() ? "航點任務" : "命令")
                              << pending_->command;
        confirmStart();
        return;
    }
    clear_pending();
    notify(ToastModel::Kind::WARNING, QStringLiteral("航點任務未開始"),
           QStringLiteral("%1 秒內未確認，已取消").arg(confirm_timeout_ms_ / MS_PER_SECOND));
    emit changed();
}

void MissionController::on_countdown_tick()
{
    const int seconds = auto_start_seconds();
    if (seconds != last_countdown_seconds_)
    {
        last_countdown_seconds_ = seconds;
        emit countdownChanged();
    }
}

int MissionController::auto_start_seconds() const
{
    if (!is_auto_start_pending() || !confirm_timer_.isActive())
    {
        return 0;
    }
    const int remaining_ms = std::max(confirm_timer_.remainingTime(), 0);
    return (remaining_ms + MS_PER_SECOND - 1) / MS_PER_SECOND;
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

bool MissionController::is_status_unknown() const
{
    return is_single_selected() && statuses_.value(selected_agent_).phase == MissionPhase::LOST;
}

bool MissionController::can_stop() const
{
    return is_running() || is_status_unknown();
}

QString MissionController::block_reason() const
{
    if (!is_single_selected())
    {
        return QStringLiteral("需選取單機");
    }
    if (is_running())
    {
        return statuses_.value(selected_agent_).phase == MissionPhase::STARTING ? QStringLiteral("上傳中")
                                                                                 : QStringLiteral("任務執行中");
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
    if (status.phase == MissionPhase::LOST)
    {
        return QStringLiteral("狀態不明，Agent 繼續執行：") + status.message;
    }
    return status.message;
}

QString MissionController::confirm_text() const
{
    if (!pending_.has_value())
    {
        return QString();
    }
    const QString name = ui::agent_short_name(pending_->plan.agent);
    if (!pending_->command.isEmpty())
    {
        return QStringLiteral("%1 即將%2").arg(name, command_action(pending_->command));
    }
    const QString format = pending_->is_auto_start ? QStringLiteral("%1 即將巡邏：執行航點任務（%2 個航點）")
                                                   : QStringLiteral("%1 執行航點任務（%2 個航點）？");
    return format.arg(name).arg(pending_->plan.waypoints.size());
}

QString MissionController::command_block_reason(const QString& command) const
{
    if (!is_single_selected())
    {
        return QStringLiteral("需選取單機");
    }
    const auto it = states_.constFind(selected_agent_);
    if (it == states_.cend() || !it->is_px4_state_valid)
    {
        return QStringLiteral("等待飛控");
    }
    if (!guard_rules::is_command_allowed_for_agent(*it, command_protocol::parse_command(command)))
    {
        const QString mode = ui::flight_mode_label(it->flight_mode);
        if (command == COMMAND_ARM)
        {
            return QStringLiteral("目前狀態（%1）無法解鎖，需已落地、未解鎖且在 Offboard 模式").arg(mode);
        }
        if (command == COMMAND_TAKEOFF)
        {
            return QStringLiteral("目前狀態（%1）無法起飛，需已落地、已解鎖且在 Offboard 模式").arg(mode);
        }
        return QStringLiteral("目前狀態（%1）無法%2").arg(mode, command_action(command));
    }
    return QString();
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
    clear_pending();
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
    clear_pending_mission();
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
    clear_pending_mission();
    emit changed();
}

void MissionController::clearWaypoints()
{
    if (!can_edit())
    {
        return;
    }
    waypoints_.remove(selected_agent_);
    clear_pending_mission();
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
    clear_pending_mission();
    emit changed();
}

void MissionController::requestStart()
{
    request_start(false);
}

void MissionController::request_start(bool is_auto_start)
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
    pending.is_auto_start = is_auto_start;
    start_pending(pending);
}

void MissionController::request_voice_command(const QString& command, const QVariantList& params)
{
    const QString reason = command_block_reason(command);
    if (!reason.isEmpty())
    {
        notify(ToastModel::Kind::WARNING, QStringLiteral("無法%1").arg(command_action(command)), reason);
        return;
    }
    PendingStart pending;
    pending.plan.agent = selected_agent_;
    pending.command = command;
    pending.params = params;
    pending.is_auto_start = true;
    start_pending(pending);
}

void MissionController::start_pending(const PendingStart& pending)
{
    // 新的口令取代前一個暫存的動作：一次只倒數一個，避免操作人員不知道哪個會先執行
    clear_pending();
    pending_ = pending;
    pending_->requested_ms = clock_.elapsed();
    confirm_timer_.start(pending.is_auto_start ? auto_start_delay_ms_ : confirm_timeout_ms_);
    if (pending.is_auto_start)
    {
        countdown_timer_.start();
        last_countdown_seconds_ = auto_start_seconds();
        qCInfo(lc_mission_ui).noquote() << "開始倒數" << auto_start_delay_ms_ / MS_PER_SECOND << "秒："
                                        << confirm_text();
    }
    else
    {
        qCInfo(lc_mission_ui).noquote() << "等待確認：" << confirm_text();
    }
    emit changed();
    emit countdownChanged();
    emit confirmRequested();
}

void MissionController::clear_pending_mission()
{
    if (is_mission_pending())
    {
        clear_pending();
    }
}

void MissionController::confirmStart()
{
    if (!pending_.has_value())
    {
        return;
    }
    const PendingStart pending = *pending_;
    const MissionPlan& plan = pending.plan;
    clear_pending();
    emit changed();
    if (!pending.command.isEmpty())
    {
        // 執行前再檢查一次：倒數期間目標或飛行狀態都可能改變；最終仍由 GuardModule 驗證
        const QString reason = command_block_reason(pending.command);
        if (plan.agent != selected_agent_ || !reason.isEmpty())
        {
            notify(ToastModel::Kind::WARNING, QStringLiteral("未%1").arg(command_action(pending.command)),
                   reason.isEmpty() ? QStringLiteral("選取目標已改變") : reason);
            return;
        }
        qCInfo(lc_mission_ui) << "語音命令送出：" << plan.agent << pending.command << pending.params;
        emit flight_command_requested(pending.command, pending.params);
        return;
    }
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
    const QString what = pending_->command.isEmpty() ? QStringLiteral("航點任務") : pending_->command;
    const bool was_auto_start = pending_->is_auto_start;
    clear_pending();
    qCInfo(lc_mission_ui).noquote() << (was_auto_start ? "已取消倒數中的" : "已取消待確認的") << what;
    emit changed();
}

void MissionController::stopMission()
{
    if (!can_stop())
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
    if (pending_.has_value() && pending_->plan.agent == agent_ns)
    {
        const QString command = pending_->command;
        const QString reason = command.isEmpty() ? block_reason() : command_block_reason(command);
        if (!reason.isEmpty())
        {
            clear_pending();
            notify(ToastModel::Kind::WARNING,
                   command.isEmpty() ? QStringLiteral("航點任務未開始")
                                     : QStringLiteral("未%1").arg(command_action(command)),
                   QStringLiteral("飛行狀態已改變，已取消：%1").arg(reason));
        }
    }
    emit changed();
}

void MissionController::on_mission_status(const MissionStatus& status)
{
    statuses_.insert(status.agent, status);
    const QString name = ui::agent_short_name(status.agent);
    switch (status.phase)
    {
    case MissionPhase::REJECTED:
        notify(ToastModel::Kind::ERROR, QStringLiteral("%1 航點任務無法開始").arg(name), status.message);
        break;
    case MissionPhase::ABORTED:
        notify(ToastModel::Kind::WARNING, QStringLiteral("%1 航點任務中止").arg(name), status.message);
        break;
    case MissionPhase::LOST:
        notify(ToastModel::Kind::WARNING, QStringLiteral("%1 航點任務狀態不明").arg(name), status.message);
        break;
    default:
        notify(ToastModel::Kind::SUCCESS, QStringLiteral("%1 航點任務").arg(name), status.message);
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
            clear_pending();
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
    const bool is_known = keyword == voice::KEYWORD_PATROL || keyword == voice::KEYWORD_ARM
                          || keyword == voice::KEYWORD_TAKEOFF || keyword == voice::KEYWORD_LAND;
    if (!is_known)
    {
        return;
    }
    if (!is_single_selected())
    {
        notify(ToastModel::Kind::WARNING, VOICE_TITLE, QStringLiteral("語音口令只支援單機控制，請先選取一台無人機"));
        return;
    }
    // 所有口令都倒數後才執行：誤辨識時操作人員可在倒數期間按 CLR 或 Esc 取消
    if (keyword == voice::KEYWORD_PATROL)
    {
        request_start(true);
    }
    else if (keyword == voice::KEYWORD_ARM)
    {
        request_voice_command(COMMAND_ARM, {});
    }
    else if (keyword == voice::KEYWORD_TAKEOFF)
    {
        request_voice_command(COMMAND_TAKEOFF, {VOICE_TAKEOFF_HEIGHT_M});
    }
    else
    {
        request_voice_command(COMMAND_LAND, {});
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
