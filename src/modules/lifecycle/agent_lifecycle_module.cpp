/**
 * @file agent_lifecycle_module.cpp
 * @brief Agent 生命週期模組實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "modules/lifecycle/agent_lifecycle_module.hpp"

#include "core/command_protocol.hpp"
#include "core/i_agent_link.hpp"

#include <QLoggingCategory>
#include <QTimer>

namespace
{
Q_LOGGING_CATEGORY(lc_lifecycle, "gcs.lifecycle")
}  // namespace

namespace gcs
{

AgentLifecycleModule::AgentLifecycleModule(IAgentLink* link, QObject* parent) : GcsModule(parent), link_(link)
{
}

bool AgentLifecycleModule::init()
{
    if (link_ == nullptr)
    {
        qCCritical(lc_lifecycle) << "未提供 Agent 通訊介面";
        return false;
    }
    connect(link_, &IAgentLink::shutdown_replied, this, &AgentLifecycleModule::on_shutdown_replied);
    is_initialized_ = true;
    qCWarning(lc_lifecycle) << "遠端關閉功能已啟用且未要求授權碼；此操作不會觸發 RTL 或降落，請謹慎使用";
    return true;
}

void AgentLifecycleModule::shutdown()
{
    if (link_ != nullptr)
    {
        disconnect(link_, nullptr, this, nullptr);
    }
    pending_requests_.clear();
    is_initialized_ = false;
}

bool AgentLifecycleModule::health_check() const
{
    return is_initialized_ && link_ != nullptr;
}

void AgentLifecycleModule::set_response_timeout(std::chrono::milliseconds timeout)
{
    response_timeout_ = timeout;
}

void AgentLifecycleModule::request_shutdown(const QString& agent_ns)
{
    const QString agent = agent_ns.isEmpty() ? QStringLiteral("unknown") : agent_ns;
    if (!command_protocol::is_single_agent(agent_ns))
    {
        emit command_acked(command_protocol::make_ack(agent, SHUTDOWN_COMMAND, false,
                                                      QStringLiteral("關閉請求格式錯誤: 關閉僅允許單一 uav_{N}_agent")));
        return;
    }
    if (!is_initialized_)
    {
        emit command_acked(command_protocol::make_ack(agent, SHUTDOWN_COMMAND, false,
                                                      QStringLiteral("AgentLifecycleModule 尚未初始化")));
        return;
    }
    if (pending_requests_.contains(agent_ns))
    {
        emit command_acked(command_protocol::make_ack(agent, SHUTDOWN_COMMAND, false,
                                                      QStringLiteral("此 Agent 已有關閉請求等待回應")));
        return;
    }

    const quint64 request_id = next_request_id_++;
    QString link_error;
    if (!link_->request_shutdown(agent_ns, request_id, &link_error))
    {
        qCWarning(lc_lifecycle) << "無法送出 shutdown 請求：" << agent_ns << link_error;
        emit command_acked(command_protocol::make_ack(agent, SHUTDOWN_COMMAND, false,
                                                      QStringLiteral("Agent shutdown service 尚未就緒")));
        return;
    }

    pending_requests_.insert(agent_ns, request_id);
    QTimer::singleShot(response_timeout_, this,
                       [this, agent_ns, request_id]()
                       {
                           complete(agent_ns, request_id, false, QStringLiteral("等待 Agent shutdown service 回應逾時"));
                       });
    qCInfo(lc_lifecycle) << "已送出 shutdown 請求：" << agent_ns << "request_id=" << request_id;
}

void AgentLifecycleModule::on_shutdown_replied(const QString& agent_ns, quint64 request_id, bool is_success,
                                               const QString& message)
{
    const auto it = pending_requests_.constFind(agent_ns);
    if (it == pending_requests_.cend() || *it != request_id)
    {
        // 逾時後才到或屬於其他請求的回應，不得改變結果。
        return;
    }
    if (!is_success)
    {
        complete(agent_ns, request_id, false, message);
        return;
    }
    // 先通知機隊立即隔離，避免回應與下一次掃描之間仍把已關閉 Agent 當成命令目標。
    emit shutdown_accepted(agent_ns);
    complete(agent_ns, request_id, true, QStringLiteral("關閉請求已被 Agent 接受，已從 GCS 機隊隔離"));
}

void AgentLifecycleModule::complete(const QString& agent_ns, quint64 request_id, bool is_ok, const QString& message)
{
    const auto it = pending_requests_.find(agent_ns);
    if (it == pending_requests_.end() || *it != request_id)
    {
        return;
    }
    pending_requests_.erase(it);
    emit command_acked(command_protocol::make_ack(agent_ns, SHUTDOWN_COMMAND, is_ok, message));
}

}  // namespace gcs
