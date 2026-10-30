/**
 * @file mission_module.cpp
 * @brief 航點任務模組實作
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 */
#include "modules/mission/mission_module.hpp"

#include "core/command_protocol.hpp"

#include <QLoggingCategory>
#include <QRandomGenerator>

#include <limits>

namespace
{
Q_LOGGING_CATEGORY(lc_mission, "gcs.mission")
}  // namespace

namespace gcs
{

namespace
{

constexpr double MS_PER_SECOND = 1000.0;
/// 尚未開始或沒有航點資訊時的航點索引
constexpr int NO_WAYPOINT = -1;
const QString MISSION_START = QStringLiteral("MISSION_START");
const QString MISSION_STOP = QStringLiteral("MISSION_STOP");

/**
 * @brief Agent 回報的狀態值是否為已知的 AgentMissionState
 * @param state 原始數值
 * @return 是否已知
 */
bool is_known_state(uint8_t state)
{
    return state <= static_cast<uint8_t>(AgentMissionState::REJECTED);
}

}  // namespace

MissionModule::MissionModule(const MissionTiming& timing, QObject* parent)
    : GcsModule(parent), timing_(timing), tick_timer_(this)
{
    tick_timer_.setInterval(timing_.tick_ms);
    connect(&tick_timer_, &QTimer::timeout, this, &MissionModule::on_tick);
}

bool MissionModule::init()
{
    clock_.start();
    is_initialized_ = true;
    qCInfo(lc_mission) << "初始化完成";
    return true;
}

void MissionModule::shutdown()
{
    tick_timer_.stop();
    missions_.clear();
    is_initialized_ = false;
}

bool MissionModule::health_check() const
{
    return is_initialized_;
}

bool MissionModule::is_running(const QString& agent_ns) const
{
    const auto it = missions_.constFind(agent_ns);
    if (it == missions_.cend())
    {
        return false;
    }
    if (it->pending.has_value())
    {
        return true;
    }
    const MissionPhase phase = it->last_reported.has_value() ? it->last_reported->phase : MissionPhase::IDLE;
    return phase == MissionPhase::MOVING || phase == MissionPhase::DWELLING;
}

qint64 MissionModule::now_ms() const
{
    return clock_.isValid() ? clock_.elapsed() : 0;
}

uint32_t MissionModule::new_mission_id(uint32_t previous)
{
    uint32_t mission_id = 0;
    // 0 表示沒有任務；與目前追蹤的任務不同，Agent 週期重送的舊狀態才不會被當成新任務的回報
    while (mission_id == 0 || mission_id == previous)
    {
        mission_id = QRandomGenerator::global()->generate();
    }
    return mission_id;
}

void MissionModule::start_mission(const MissionPlan& plan)
{
    const int count = static_cast<int>(plan.waypoints.size());
    QString error;
    if (!is_initialized_)
    {
        error = QStringLiteral("航點任務模組未初始化");
    }
    else if (!command_protocol::is_single_agent(plan.agent))
    {
        error = QStringLiteral("航點任務只支援單機");
    }
    else if (is_running(plan.agent))
    {
        error = QStringLiteral("已在執行航點任務");
    }
    if (!error.isEmpty())
    {
        qCWarning(lc_mission) << "拒絕開始航點任務：" << plan.agent << error;
        report(plan.agent, 0, MissionPhase::REJECTED, NO_WAYPOINT, count, error);
        return;
    }

    AgentMission& mission = missions_[plan.agent];
    WaypointMissionData upload;
    upload.mission_id = new_mission_id(mission.tracked_mission_id);
    upload.altitude_frame = MISSION_ALTITUDE_PX4_LOCAL;
    upload.waypoints.reserve(plan.waypoints.size());
    for (const MissionWaypoint& waypoint : plan.waypoints)
    {
        MissionWaypointData target;
        target.latitude_deg = waypoint.latitude;
        target.longitude_deg = waypoint.longitude;
        target.altitude_m = static_cast<float>(waypoint.altitude_m);
        target.dwell_s = static_cast<float>(timing_.dwell_ms / MS_PER_SECOND);
        // 維持目前航向：與 QGCS 逐段下令時「機頭維持任務開始時的方向」相同
        target.yaw_deg = std::numeric_limits<float>::quiet_NaN();
        upload.waypoints.push_back(target);
    }

    PendingStart pending;
    pending.mission_id = upload.mission_id;
    pending.waypoint_count = count;
    pending.stage = StartStage::AWAITING_UPLOAD_ACK;
    pending.stage_started_ms = now_ms();
    mission.pending = pending;
    mission.tracked_mission_id = upload.mission_id;
    ensure_ticking();

    qCInfo(lc_mission) << "上傳航點任務：" << plan.agent << "mission_id=" << upload.mission_id << "航點" << count << "個";
    report(plan.agent, upload.mission_id, MissionPhase::STARTING, NO_WAYPOINT, count,
           QStringLiteral("上傳 %1 個航點，等待 Agent 開始").arg(count));
    // Guard 與 CommandModule 以 DirectConnection 在此呼叫內同步回報，之後不得再使用 mission 參考
    emit mission_upload_requested(plan.agent, upload);
}

void MissionModule::on_mission_upload_acked(const CommandAck& ack)
{
    const auto it = missions_.find(ack.agent);
    if (it == missions_.end() || !it->pending.has_value() || it->pending->stage != StartStage::AWAITING_UPLOAD_ACK)
    {
        return;
    }
    if (!ack.is_ok)
    {
        reject_pending(ack.agent, ack.message);
        return;
    }
    it->pending->stage = StartStage::AWAITING_LOADED;
    it->pending->stage_started_ms = now_ms();
}

void MissionModule::send_start(const QString& agent_ns)
{
    const auto it = missions_.find(agent_ns);
    if (it == missions_.end() || !it->pending.has_value())
    {
        return;
    }
    it->pending->stage = StartStage::AWAITING_START_ACK;
    it->pending->stage_started_ms = now_ms();
    CommandRequest request;
    request.agent = agent_ns;
    request.command = MISSION_START;
    request.params = {static_cast<double>(it->pending->mission_id)};
    qCInfo(lc_mission) << agent_ns << "Agent 已載入任務，送出" << MISSION_START << it->pending->mission_id;
    // Guard 與 CommandModule 以 DirectConnection 在此呼叫內同步回報，之後不得再使用 it
    emit command_requested(request);
}

void MissionModule::on_command_acked(const CommandAck& ack)
{
    const auto it = missions_.find(ack.agent);
    if (it == missions_.end() || !it->pending.has_value() || it->pending->stage != StartStage::AWAITING_START_ACK
        || ack.command != MISSION_START)
    {
        // 其他命令是否中止任務由 Agent 判斷（COMMAND_OVERRIDE），並以任務狀態回報
        return;
    }
    if (!ack.is_ok)
    {
        // Agent 已載入的任務會在載入逾時後自行清除
        reject_pending(ack.agent, QStringLiteral("MISSION_START 未送出：") + ack.message);
        return;
    }
    it->pending->stage = StartStage::AWAITING_RUNNING;
    it->pending->stage_started_ms = now_ms();
}

void MissionModule::on_mission_status_updated(const QString& agent_ns, const WaypointMissionStatusData& status)
{
    if (!is_known_state(status.state))
    {
        qCWarning(lc_mission) << agent_ns << "收到未知的任務狀態" << status.state << "，略過";
        return;
    }
    AgentMission& mission = missions_[agent_ns];
    mission.status_received_ms = now_ms();
    const auto state = static_cast<AgentMissionState>(status.state);

    if (mission.pending.has_value())
    {
        // 開始中只看本機任務的回報；週期重送的其他任務狀態（例如上一個任務的 COMPLETED）略過
        if (status.mission_id != mission.pending->mission_id)
        {
            return;
        }
        switch (state)
        {
        case AgentMissionState::REJECTED:
            reject_pending(agent_ns, reason_text(status));
            return;
        case AgentMissionState::LOADED:
            if (mission.pending->stage == StartStage::AWAITING_LOADED)
            {
                send_start(agent_ns);
            }
            return;
        case AgentMissionState::RUNNING:
        case AgentMissionState::DWELLING:
        case AgentMissionState::COMPLETED:
        case AgentMissionState::ABORTED:
            mission.pending.reset();
            follow_agent_status(agent_ns, status);
            return;
        case AgentMissionState::IDLE:
            return;
        }
        return;
    }
    follow_agent_status(agent_ns, status);
}

void MissionModule::follow_agent_status(const QString& agent_ns, const WaypointMissionStatusData& status)
{
    const auto it = missions_.find(agent_ns);
    if (it == missions_.end())
    {
        return;
    }
    const int index = status.current_index;
    const int count = status.waypoint_count;
    switch (static_cast<AgentMissionState>(status.state))
    {
    case AgentMissionState::RUNNING:
    case AgentMissionState::DWELLING:
        if (status.mission_id != it->tracked_mission_id)
        {
            // QGCS 重新啟動、晚加入或其他地面站開始的任務：接手追蹤
            qCInfo(lc_mission) << agent_ns << "接手追蹤 Agent 執行中的任務 mission_id=" << status.mission_id;
            it->tracked_mission_id = status.mission_id;
        }
        if (static_cast<AgentMissionState>(status.state) == AgentMissionState::RUNNING)
        {
            report(agent_ns, status.mission_id, MissionPhase::MOVING, index, count,
                   QStringLiteral("前往第 %1/%2 個航點").arg(index + 1).arg(count));
        }
        else
        {
            report(agent_ns, status.mission_id, MissionPhase::DWELLING, index, count,
                   QStringLiteral("已到達第 %1/%2 個航點，停留中").arg(index + 1).arg(count));
        }
        return;
    case AgentMissionState::COMPLETED:
        // 晚加入時看到的舊任務結果不回報，避免一啟動就出現與目前操作無關的提示
        if (status.mission_id == it->tracked_mission_id)
        {
            report(agent_ns, status.mission_id, MissionPhase::COMPLETED, index, count,
                   QStringLiteral("已到達最後一個航點，懸停中"));
        }
        return;
    case AgentMissionState::ABORTED:
        if (status.mission_id == it->tracked_mission_id)
        {
            report(agent_ns, status.mission_id, MissionPhase::ABORTED, index, count,
                   QStringLiteral("任務中止：") + reason_text(status));
        }
        return;
    case AgentMissionState::IDLE:
        if (is_following(*it))
        {
            // Agent 重新啟動後任務遺失，重啟後回報 IDLE
            report(agent_ns, it->tracked_mission_id, MissionPhase::ABORTED, NO_WAYPOINT,
                   it->last_reported->waypoint_count, QStringLiteral("Agent 回報沒有任務（可能已重新啟動），任務已結束"));
        }
        return;
    case AgentMissionState::LOADED:
    case AgentMissionState::REJECTED:
        // 不是本機開始中的任務（例如逾時後才到的回報），不影響目前顯示
        return;
    }
}

void MissionModule::stop_mission(const QString& agent_ns)
{
    const auto it = missions_.find(agent_ns);
    if (it == missions_.end() || (!it->pending.has_value() && !is_following(*it)))
    {
        qCInfo(lc_mission) << agent_ns << "沒有執行中的航點任務";
        return;
    }
    if (it->pending.has_value())
    {
        // 開始流程中停止：Agent 可能已載入或剛開始，MISSION_STOP 會停在目前位置或清除已載入的任務
        const PendingStart pending = *it->pending;
        it->pending.reset();
        report(agent_ns, pending.mission_id, MissionPhase::ABORTED, NO_WAYPOINT, pending.waypoint_count,
               QStringLiteral("操作人員停止任務"));
    }
    CommandRequest request;
    request.agent = agent_ns;
    request.command = MISSION_STOP;
    qCInfo(lc_mission) << agent_ns << "操作人員停止任務，送出" << MISSION_STOP;
    emit command_requested(request);
}

void MissionModule::on_agent_offline(const QString& agent_ns)
{
    report_lost(agent_ns, QStringLiteral("與 Agent 失去連線，無法得知任務進度；Agent 會繼續執行任務"));
}

void MissionModule::on_fleet_changed(const FleetSnapshot& snapshot)
{
    // 資料停滯時 topic 仍在，不會收到 agent_offline；以顯示清單判斷，與畫面移除該機的時機一致
    for (const QString& agent : snapshot.offline)
    {
        report_lost(agent, QStringLiteral("Agent 資料停滯，無法得知任務進度；Agent 會繼續執行任務"));
    }
}

void MissionModule::report_lost(const QString& agent_ns, const QString& message)
{
    const auto it = missions_.find(agent_ns);
    if (it == missions_.end())
    {
        return;
    }
    if (it->pending.has_value())
    {
        const PendingStart pending = *it->pending;
        it->pending.reset();
        report(agent_ns, pending.mission_id, MissionPhase::LOST, NO_WAYPOINT, pending.waypoint_count,
               QStringLiteral("開始任務時與 Agent 失去聯繫，無法確認任務是否開始"));
        return;
    }
    if (it->last_reported.has_value()
        && (it->last_reported->phase == MissionPhase::MOVING || it->last_reported->phase == MissionPhase::DWELLING))
    {
        report(agent_ns, it->tracked_mission_id, MissionPhase::LOST, it->last_reported->waypoint_index,
               it->last_reported->waypoint_count, message);
    }
}

void MissionModule::reject_pending(const QString& agent_ns, const QString& message)
{
    const auto it = missions_.find(agent_ns);
    if (it == missions_.end() || !it->pending.has_value())
    {
        return;
    }
    const PendingStart pending = *it->pending;
    it->pending.reset();
    report(agent_ns, pending.mission_id, MissionPhase::REJECTED, NO_WAYPOINT, pending.waypoint_count, message);
}

void MissionModule::ensure_ticking()
{
    if (!tick_timer_.isActive())
    {
        tick_timer_.start();
    }
}

void MissionModule::on_tick()
{
    const qint64 now = now_ms();
    bool should_keep_ticking = false;
    for (const QString& agent : missions_.keys())
    {
        const auto it = missions_.find(agent);
        if (it == missions_.end())
        {
            continue;
        }
        if (it->pending.has_value())
        {
            if (now - it->pending->stage_started_ms <= timing_.handshake_timeout_ms)
            {
                should_keep_ticking = true;
                continue;
            }
            const QString seconds = QString::number(timing_.handshake_timeout_ms / MS_PER_SECOND);
            if (it->pending->stage == StartStage::AWAITING_RUNNING)
            {
                // Agent 若之後才開始，週期重送的 RUNNING 仍會被接手追蹤
                reject_pending(agent, QStringLiteral("送出 MISSION_START 後 %1 秒內 Agent 未開始任務").arg(seconds));
            }
            else
            {
                reject_pending(agent, QStringLiteral("%1 秒內 Agent 未回應航點任務上傳，請確認 Agent 版本支援航點任務")
                                          .arg(seconds));
            }
            continue;
        }
        const MissionPhase phase = it->last_reported.has_value() ? it->last_reported->phase : MissionPhase::IDLE;
        if (phase != MissionPhase::MOVING && phase != MissionPhase::DWELLING)
        {
            continue;
        }
        if (now - it->status_received_ms > timing_.status_timeout_ms)
        {
            report_lost(agent, QStringLiteral("超過 %1 秒沒有收到任務狀態，無法得知任務進度")
                                   .arg(timing_.status_timeout_ms / MS_PER_SECOND));
            continue;
        }
        should_keep_ticking = true;
    }
    if (!should_keep_ticking)
    {
        tick_timer_.stop();
    }
}

bool MissionModule::is_following(const AgentMission& mission)
{
    if (!mission.last_reported.has_value())
    {
        return false;
    }
    const MissionPhase phase = mission.last_reported->phase;
    return phase == MissionPhase::MOVING || phase == MissionPhase::DWELLING || phase == MissionPhase::LOST;
}

QString MissionModule::reason_text(const WaypointMissionStatusData& status)
{
    const int waypoint = status.current_index + 1;
    QString text;
    switch (static_cast<AgentMissionReason>(status.reason))
    {
    case AgentMissionReason::INVALID_PLAN:
        text = QStringLiteral("Agent 判定航點內容不合法（第 %1 個航點）").arg(waypoint);
        break;
    case AgentMissionReason::OUT_OF_RANGE:
        text = QStringLiteral("第 %1 個航點超出 Agent 允許的距離").arg(waypoint);
        break;
    case AgentMissionReason::NOT_READY:
        text = QStringLiteral("Agent 尚未就緒（沒有有效狀態或座標換算尚未建立）");
        break;
    case AgentMissionReason::NOT_HOVERING:
        text = QStringLiteral("需在懸停狀態才能開始任務");
        break;
    case AgentMissionReason::BUSY:
        text = QStringLiteral("Agent 正在執行其他任務或編隊");
        break;
    case AgentMissionReason::ID_MISMATCH:
        text = QStringLiteral("任務識別碼不符或載入已逾時");
        break;
    case AgentMissionReason::OPERATOR_STOP:
        text = QStringLiteral("操作人員停止任務，已停在目前位置");
        break;
    case AgentMissionReason::COMMAND_OVERRIDE:
        text = QStringLiteral("收到其他命令（操作人員介入），已停在目前位置");
        break;
    case AgentMissionReason::FLIGHT_STATE:
        text = QStringLiteral("飛行狀態已離開懸停／移動");
        break;
    case AgentMissionReason::LANDED:
        text = QStringLiteral("已落地或上鎖");
        break;
    case AgentMissionReason::LEG_TIMEOUT:
        text = QStringLiteral("第 %1 個航點逾時未到達，已停在目前位置").arg(waypoint);
        break;
    case AgentMissionReason::NONE:
        text = QStringLiteral("未提供原因");
        break;
    default:
        text = QStringLiteral("原因代碼 %1").arg(status.reason);
        break;
    }
    if (!status.detail.isEmpty())
    {
        text += QStringLiteral("（%1）").arg(status.detail);
    }
    return text;
}

void MissionModule::report(const QString& agent_ns, uint32_t mission_id, MissionPhase phase, int index, int count,
                           const QString& message)
{
    AgentMission& mission = missions_[agent_ns];
    const bool is_event = phase == MissionPhase::REJECTED || phase == MissionPhase::STARTING;
    if (!is_event && mission.last_reported.has_value() && mission.last_reported_mission_id == mission_id
        && mission.last_reported->phase == phase && mission.last_reported->waypoint_index == index
        && mission.last_reported->waypoint_count == count)
    {
        return;
    }
    MissionStatus status;
    status.agent = agent_ns;
    status.phase = phase;
    status.waypoint_index = index;
    status.waypoint_count = count;
    status.message = message;
    // REJECTED 不改變 Agent 端的任務，保留原本的追蹤狀態（例如晚加入時的執行中任務）
    if (phase != MissionPhase::REJECTED || !is_following(mission))
    {
        mission.last_reported = status;
        mission.last_reported_mission_id = mission_id;
    }
    if (phase == MissionPhase::ABORTED || phase == MissionPhase::REJECTED || phase == MissionPhase::LOST)
    {
        qCWarning(lc_mission) << agent_ns << message;
    }
    else
    {
        qCInfo(lc_mission) << agent_ns << message;
    }
    if (phase == MissionPhase::MOVING || phase == MissionPhase::DWELLING)
    {
        // 接手追蹤的任務也要檢查任務狀態是否停止更新
        ensure_ticking();
    }
    emit mission_status_changed(status);
}

}  // namespace gcs
