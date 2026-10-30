/**
 * @file fleet_module.cpp
 * @brief 機隊管理模組實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "modules/fleet/fleet_module.hpp"

#include "core/command_protocol.hpp"
#include "core/i_agent_link.hpp"

#include <QLoggingCategory>

#include <algorithm>
#include <iterator>

namespace
{
Q_LOGGING_CATEGORY(lc_fleet, "gcs.fleet")

/**
 * @brief 排序後轉為 QStringList，讓 UI 顯示順序穩定
 * @param agents Agent 集合
 * @return 排序後的清單
 */
QStringList sorted_list(const QSet<QString>& agents)
{
    QStringList list(agents.cbegin(), agents.cend());
    std::sort(list.begin(), list.end());
    return list;
}

}  // namespace

namespace gcs
{

FleetModule::FleetModule(IAgentLink* link, QObject* parent) : GcsModule(parent), link_(link), scan_timer_(this)
{
}

bool FleetModule::init()
{
    if (link_ == nullptr)
    {
        qCCritical(lc_fleet) << "未提供 Agent 通訊介面";
        return false;
    }
    scan_timer_.setInterval(SCAN_INTERVAL);
    connect(&scan_timer_, &QTimer::timeout, this, &FleetModule::scan_now);
    scan_timer_.start();
    is_initialized_ = true;
    qCInfo(lc_fleet) << "初始化完成，掃描週期" << SCAN_INTERVAL.count() << "毫秒";
    return true;
}

void FleetModule::shutdown()
{
    scan_timer_.stop();
    is_initialized_ = false;
    online_agents_.clear();
    last_seen_.clear();
    shutdown_suppressed_agents_.clear();
}

bool FleetModule::health_check() const
{
    return is_initialized_ && link_ != nullptr;
}

QSet<QString> FleetModule::online_agents() const
{
    return online_agents_;
}

void FleetModule::scan_now()
{
    scan_at(Clock::now());
}

void FleetModule::scan_at(Clock::time_point now)
{
    if (link_ == nullptr)
    {
        return;
    }
    // 只接受介面規格的 uav_{N}_agent；舊式或格式錯誤的名稱不得成為命令目標。
    QSet<QString> current;
    for (const QString& agent : link_->discovered_state_agents())
    {
        if (command_protocol::is_single_agent(agent))
        {
            current.insert(agent);
        }
    }

    // Shutdown 成功後，DDS 探索可能短時間仍保留舊 topic；topic 消失後才解除抑制。
    for (auto it = shutdown_suppressed_agents_.begin(); it != shutdown_suppressed_agents_.end();)
    {
        it = current.contains(*it) ? std::next(it) : shutdown_suppressed_agents_.erase(it);
    }

    QSet<QString> added;
    for (const QString& agent : current)
    {
        if (!shutdown_suppressed_agents_.contains(agent) && !online_agents_.contains(agent))
        {
            added.insert(agent);
        }
    }
    QSet<QString> removed;
    for (const QString& agent : online_agents_)
    {
        if (!current.contains(agent))
        {
            removed.insert(agent);
        }
    }

    // 新增者以發現時間作為寬限期起點
    for (const QString& agent : added)
    {
        online_agents_.insert(agent);
        last_seen_.insert(agent, now);
    }
    for (const QString& agent : removed)
    {
        online_agents_.remove(agent);
        last_seen_.remove(agent);
    }

    // 顯示清單以實際資料流為準：超過 STALE_TIMEOUT 未收到狀態或健康資料即列為離線
    FleetSnapshot snapshot;
    QSet<QString> verified_online;
    QSet<QString> offline = removed;
    for (const QString& agent : online_agents_)
    {
        const auto seen = last_seen_.constFind(agent);
        if (seen != last_seen_.cend() && now - *seen <= STALE_TIMEOUT)
        {
            verified_online.insert(agent);
        }
        else
        {
            offline.insert(agent);
        }
    }
    snapshot.online = sorted_list(verified_online);
    snapshot.offline = sorted_list(offline);

    for (const QString& agent : sorted_list(added))
    {
        qCInfo(lc_fleet) << "Agent 上線：" << agent;
        emit agent_online(agent);
    }
    // topic 消失才通知離線；資料短暫停滯時保留訂閱以利復原
    for (const QString& agent : sorted_list(removed))
    {
        qCInfo(lc_fleet) << "Agent 離線：" << agent;
        emit agent_offline(agent);
    }
    emit fleet_changed(snapshot);
}

void FleetModule::record_state_at(const QString& agent_ns, Clock::time_point now)
{
    if (agent_ns.isEmpty())
    {
        return;
    }
    last_seen_.insert(agent_ns, now);
}

void FleetModule::on_state_updated(const QString& agent_ns, const AgentStateData& state)
{
    Q_UNUSED(state)
    record_state_at(agent_ns, Clock::now());
}

void FleetModule::on_health_updated(const QString& agent_ns, const AgentHealthData& health)
{
    Q_UNUSED(health)
    record_state_at(agent_ns, Clock::now());
}

void FleetModule::on_shutdown_accepted(const QString& agent_ns)
{
    shutdown_suppressed_agents_.insert(agent_ns);
    const bool was_online = online_agents_.remove(agent_ns);
    last_seen_.remove(agent_ns);

    // 不等下一次掃描，立即讓其他模組停止使用目標 Agent。
    emit agent_offline(agent_ns);

    FleetSnapshot snapshot;
    snapshot.online = sorted_list(online_agents_);
    snapshot.offline = QStringList{agent_ns};
    emit fleet_changed(snapshot);

    qCInfo(lc_fleet) << "Shutdown Agent 已隔離：" << agent_ns << (was_online ? "（原為線上）" : "（原本不在線上）");
}

}  // namespace gcs
