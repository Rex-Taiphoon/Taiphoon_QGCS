/**
 * @file command_module.cpp
 * @brief 命令模組實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "modules/command/command_module.hpp"

#include "core/command_protocol.hpp"
#include "core/i_agent_link.hpp"

#include <QLoggingCategory>

namespace
{
Q_LOGGING_CATEGORY(lc_command, "gcs.command")
}  // namespace

namespace gcs
{

CommandModule::CommandModule(IAgentLink* link, QObject* parent) : GcsModule(parent), link_(link)
{
}

bool CommandModule::init()
{
    if (link_ == nullptr)
    {
        qCCritical(lc_command) << "未提供 Agent 通訊介面";
        return false;
    }
    is_initialized_ = true;
    qCInfo(lc_command) << "初始化完成";
    return true;
}

void CommandModule::shutdown()
{
    is_initialized_ = false;
}

bool CommandModule::health_check() const
{
    return is_initialized_ && link_ != nullptr;
}

void CommandModule::publish_validated(const CommandRequest& request)
{
    const QString agent = request.agent.isEmpty() ? QStringLiteral("unknown") : request.agent;
    const QString command = request.command.isEmpty() ? QStringLiteral("unknown") : request.command;

    // 縱深防禦：即使上游已驗證，發布前仍再次檢查格式與 NaN／Inf（CODE_CONSTRAINT L1-08）。
    const QString protocol_error = command_protocol::validate_request(request);
    if (!protocol_error.isEmpty())
    {
        qCWarning(lc_command) << "命令格式錯誤：" << protocol_error;
        emit command_acked(command_protocol::make_ack(agent, command, false,
                                                      QStringLiteral("命令格式錯誤: ") + protocol_error));
        return;
    }
    if (!is_initialized_ || link_ == nullptr)
    {
        emit command_acked(command_protocol::make_ack(agent, command, false, QStringLiteral("CommandModule 未初始化")));
        return;
    }

    const QString line = command_protocol::make_command_line(request);
    QString link_error;
    if (!link_->publish_command(request.agent, line, &link_error))
    {
        qCWarning(lc_command) << "命令發布失敗：" << request.agent << line << link_error;
        emit command_acked(command_protocol::make_ack(agent, command, false,
                                                      QStringLiteral("命令發布失敗: ") + link_error));
        return;
    }

    qCInfo(lc_command) << "已發布命令至" << request.agent << ":" << line;
    emit command_acked(command_protocol::make_ack(request.agent, request.command, true));
}

}  // namespace gcs
