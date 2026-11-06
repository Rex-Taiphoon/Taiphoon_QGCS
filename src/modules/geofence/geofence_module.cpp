/**
 * @file geofence_module.cpp
 * @brief 地理圍籬模組實作
 * @author Taiphoon GCS Team
 * @date 2026-11-06
 */
#include "modules/geofence/geofence_module.hpp"

#include "core/command_protocol.hpp"

#include <QLoggingCategory>
#include <QRegularExpression>

#include <algorithm>
#include <map>

namespace
{
Q_LOGGING_CATEGORY(lc_geofence, "gcs.geofence")
}  // namespace

namespace gcs
{

namespace
{

/// 尚未列入線上清單
constexpr qint64 NOT_ONLINE = -1;

/**
 * @brief 取出 uav_N_agent 的機號
 * @param agent Agent 命名空間
 * @return 機號；格式不符時為 -1
 */
qint64 agent_number(const QString& agent)
{
    static const QRegularExpression PATTERN(QStringLiteral("^uav_([0-9]+)_agent$"));
    const QRegularExpressionMatch match = PATTERN.match(agent);
    bool is_ok = false;
    const qint64 number = match.hasMatch() ? match.captured(1).toLongLong(&is_ok) : -1;
    return is_ok ? number : -1;
}

/**
 * @brief 依機號排序（uav_2 在 uav_10 之前），不依賴系統語系；格式不符者排在後面並依字串排序
 * @param a 第一個 Agent
 * @param b 第二個 Agent
 * @return a 是否排在 b 之前
 */
bool is_agent_before(const QString& a, const QString& b)
{
    const qint64 number_a = agent_number(a);
    const qint64 number_b = agent_number(b);
    if ((number_a >= 0) != (number_b >= 0))
    {
        return number_a >= 0;
    }
    if (number_a != number_b)
    {
        return number_a < number_b;
    }
    return a < b;
}

/**
 * @brief 圍籬狀況的日誌文字
 * @param phase 狀況
 * @return 文字
 */
QString phase_log_text(GeofenceAgentPhase phase)
{
    switch (phase)
    {
    case GeofenceAgentPhase::WAITING:
        return QStringLiteral("等待回報");
    case GeofenceAgentPhase::NO_REPORT:
        return QStringLiteral("未回報");
    case GeofenceAgentPhase::NONE:
        return QStringLiteral("沒有圍籬");
    case GeofenceAgentPhase::PENDING:
        return QStringLiteral("等待 Home");
    case GeofenceAgentPhase::ACTIVE:
        return QStringLiteral("生效");
    case GeofenceAgentPhase::REJECTED:
        return QStringLiteral("拒絕");
    case GeofenceAgentPhase::MISMATCH:
        return QStringLiteral("版本不一致");
    }
    return QString();
}

/**
 * @brief 狀況是否來自 Agent 目前的圍籬（可比對多數版本）
 * @param phase 狀況
 * @return 是否
 */
bool is_settled_phase(GeofenceAgentPhase phase)
{
    return phase == GeofenceAgentPhase::NONE || phase == GeofenceAgentPhase::PENDING
           || phase == GeofenceAgentPhase::ACTIVE;
}

}  // namespace

GeofenceModule::GeofenceModule(const GeofenceTiming& timing, QObject* parent)
    : GcsModule(parent), timing_(timing), tick_timer_(this)
{
    tick_timer_.setInterval(timing_.tick_ms);
    connect(&tick_timer_, &QTimer::timeout, this, [this]() {
        refresh();
        update_ticking();
    });
}

bool GeofenceModule::init()
{
    clock_.start();
    is_initialized_ = true;
    qCInfo(lc_geofence) << "初始化完成";
    return true;
}

void GeofenceModule::shutdown()
{
    tick_timer_.stop();
    agents_.clear();
    pending_.reset();
    requested_.reset();
    overview_ = GeofenceOverview();
    is_initialized_ = false;
}

bool GeofenceModule::health_check() const
{
    return is_initialized_;
}

uint32_t GeofenceModule::next_fence_id() const
{
    const uint32_t next = max_seen_id_ + 1;
    return next == 0 ? 1 : next;
}

void GeofenceModule::request_set(const GeofenceData& fence)
{
    GeofenceData request = fence;
    request.fence_id = next_fence_id();
    request.action = GeofenceAction::SET;
    submit(request);
}

void GeofenceModule::request_clear()
{
    GeofenceData request;
    request.fence_id = next_fence_id();
    request.action = GeofenceAction::CLEAR;
    request.margin_m = 0.0F;
    submit(request);
}

void GeofenceModule::submit(GeofenceData fence)
{
    if (!is_initialized_)
    {
        emit request_acked(command_protocol::make_ack(ALL_AGENTS_TARGET, GEOFENCE_ACK_NAME, false,
                                                      QStringLiteral("GeofenceModule 未初始化")));
        return;
    }
    max_seen_id_ = std::max(max_seen_id_, fence.fence_id);
    pending_ = fence;
    qCInfo(lc_geofence) << "送出圍籬" << (fence.action == GeofenceAction::CLEAR ? "清除" : "設定")
                        << "fence_id=" << fence.fence_id << "頂點" << fence.vertices.size() << "個";
    emit geofence_requested(fence);
}

void GeofenceModule::on_geofence_acked(const CommandAck& ack)
{
    if (ack.command != GEOFENCE_ACK_NAME || !pending_.has_value())
    {
        return;
    }
    if (ack.is_ok)
    {
        requested_ = pending_;
        requested_ms_ = now_ms();
    }
    pending_.reset();
    emit request_acked(ack);
    refresh();
    update_ticking();
}

void GeofenceModule::on_geofence_status_updated(const QString& agent_ns, const GeofenceStatusData& status)
{
    auto it = agents_.find(agent_ns);
    if (it == agents_.end())
    {
        it = agents_.insert(agent_ns, AgentFence{std::nullopt, NOT_ONLINE});
    }
    it->status = status;
    max_seen_id_ = std::max({max_seen_id_, status.fence_id, status.last_request_id});
    refresh();
    update_ticking();
}

void GeofenceModule::on_fleet_changed(const FleetSnapshot& snapshot)
{
    const qint64 now = now_ms();
    for (const QString& agent : snapshot.online)
    {
        auto it = agents_.find(agent);
        if (it == agents_.end())
        {
            agents_.insert(agent, AgentFence{std::nullopt, now});
        }
        else if (it->online_since_ms == NOT_ONLINE)
        {
            it->online_since_ms = now;
        }
    }
    for (auto it = agents_.begin(); it != agents_.end();)
    {
        if (snapshot.online.contains(it.key()))
        {
            ++it;
        }
        else if (snapshot.offline.contains(it.key()))
        {
            // 資料停滯的 Agent 保留最後狀態，恢復時不必等待重送
            it->online_since_ms = NOT_ONLINE;
            ++it;
        }
        else
        {
            it = agents_.erase(it);
        }
    }
    refresh();
    update_ticking();
}

GeofenceAgentRow GeofenceModule::make_row(const QString& agent_ns, const AgentFence& agent) const
{
    GeofenceAgentRow row;
    row.agent = agent_ns;
    qint64 wait_start = agent.online_since_ms;
    if (requested_.has_value())
    {
        wait_start = std::max(wait_start, requested_ms_);
    }
    const bool is_timed_out = now_ms() - wait_start >= timing_.report_timeout_ms;
    if (!agent.status.has_value())
    {
        row.phase = is_timed_out ? GeofenceAgentPhase::NO_REPORT : GeofenceAgentPhase::WAITING;
        return row;
    }
    const GeofenceStatusData& status = *agent.status;
    const bool is_disabled = status.state == static_cast<uint8_t>(GeofenceState::DISABLED);
    row.fence_id = is_disabled ? 0 : status.fence_id;
    row.last_result = status.last_result;
    row.is_position_inside = status.is_position_inside;
    row.detail = status.detail;
    if (requested_.has_value() && status.last_request_id < requested_->fence_id)
    {
        row.phase = is_timed_out ? GeofenceAgentPhase::NO_REPORT : GeofenceAgentPhase::WAITING;
        return row;
    }
    const bool is_rejected = status.last_result == static_cast<uint8_t>(GeofenceResult::REJECTED_INVALID)
                             || status.last_result == static_cast<uint8_t>(GeofenceResult::REJECTED_TOO_FAR);
    if (requested_.has_value() && status.last_request_id == requested_->fence_id && is_rejected)
    {
        row.phase = GeofenceAgentPhase::REJECTED;
        return row;
    }
    if (is_disabled)
    {
        row.phase = GeofenceAgentPhase::NONE;
    }
    else if (status.state == static_cast<uint8_t>(GeofenceState::PENDING))
    {
        row.phase = GeofenceAgentPhase::PENDING;
    }
    else
    {
        row.phase = GeofenceAgentPhase::ACTIVE;
    }
    return row;
}

void GeofenceModule::refresh()
{
    GeofenceOverview next;
    if (requested_.has_value())
    {
        next.requested_fence_id = requested_->fence_id;
        next.requested_action = requested_->action;
    }
    for (auto it = agents_.cbegin(); it != agents_.cend(); ++it)
    {
        if (it->online_since_ms != NOT_ONLINE)
        {
            next.rows.push_back(make_row(it.key(), *it));
        }
    }
    std::sort(next.rows.begin(), next.rows.end(),
              [](const GeofenceAgentRow& a, const GeofenceAgentRow& b) { return is_agent_before(a.agent, b.agent); });

    // 多數機目前的版本；票數相同時取較新的版本
    std::map<uint32_t, int> votes;
    bool has_missing_status = false;
    for (const GeofenceAgentRow& row : next.rows)
    {
        const AgentFence& agent = agents_.value(row.agent);
        if (agent.status.has_value())
        {
            ++votes[row.fence_id];
        }
        else
        {
            has_missing_status = true;
        }
    }
    uint32_t majority = 0;
    int majority_votes = 0;
    for (const auto& [fence_id, count] : votes)
    {
        if (count >= majority_votes)
        {
            majority = fence_id;
            majority_votes = count;
        }
    }
    next.is_consistent = votes.size() <= 1 && !has_missing_status;

    const bool has_requested_set = requested_.has_value() && requested_->action == GeofenceAction::SET;
    const uint32_t requested_key = has_requested_set ? requested_->fence_id : 0;
    for (GeofenceAgentRow& row : next.rows)
    {
        const bool is_requested_version = requested_.has_value() && row.fence_id == requested_key;
        if (is_settled_phase(row.phase) && row.fence_id != majority && !is_requested_version)
        {
            row.phase = GeofenceAgentPhase::MISMATCH;
        }
    }

    if (majority != 0)
    {
        const GeofenceStatusData* source = nullptr;
        for (const GeofenceAgentRow& row : next.rows)
        {
            const auto agent = agents_.constFind(row.agent);
            if (row.fence_id != majority || !agent->status.has_value())
            {
                continue;
            }
            if (source == nullptr || agent->status->state == static_cast<uint8_t>(GeofenceState::ACTIVE))
            {
                source = &*agent->status;
            }
        }
        if (source != nullptr)
        {
            next.fence_id = majority;
            next.margin_m = source->margin_m;
            next.vertices = source->vertices;
        }
    }

    if (next == overview_)
    {
        return;
    }
    for (const GeofenceAgentRow& row : next.rows)
    {
        const auto previous = std::find_if(overview_.rows.cbegin(), overview_.rows.cend(),
                                           [&row](const GeofenceAgentRow& old) { return old.agent == row.agent; });
        const bool is_new = previous == overview_.rows.cend();
        if (is_new || previous->phase != row.phase || previous->fence_id != row.fence_id)
        {
            qCInfo(lc_geofence) << row.agent << "圍籬" << phase_log_text(row.phase) << "fence_id=" << row.fence_id
                                << row.detail;
        }
        if ((is_new || previous->is_position_inside) && !row.is_position_inside)
        {
            qCWarning(lc_geofence) << row.agent << "目前位置在圍籬外（含 margin）";
        }
    }
    overview_ = next;
    emit overview_changed(overview_);
}

void GeofenceModule::update_ticking()
{
    const bool is_waiting = std::any_of(overview_.rows.cbegin(), overview_.rows.cend(), [](const GeofenceAgentRow& row) {
        return row.phase == GeofenceAgentPhase::WAITING;
    });
    if (is_waiting && !tick_timer_.isActive())
    {
        tick_timer_.start();
    }
    else if (!is_waiting)
    {
        tick_timer_.stop();
    }
}

qint64 GeofenceModule::now_ms() const
{
    return clock_.isValid() ? clock_.elapsed() : 0;
}

}  // namespace gcs
