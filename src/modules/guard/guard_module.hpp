/**
 * @file guard_module.hpp
 * @brief 安全防呆模組 — 命令發布前的唯一安全閘門
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 追蹤每台 Agent 的最新狀態，依 guard_rules 驗證命令；通過才發出 command_validated，
 * 由組裝根以 Qt::DirectConnection 連到 CommandModule，驗證與發布在同一次呼叫內完成。
 * 模組只在核心執行緒執行，不需要 mutex（ADR-0015）。
 */
#pragma once

#include "core/agent_types.hpp"
#include "core/command_protocol.hpp"
#include "core/gcs_module.hpp"

#include <QHash>
#include <QSet>
#include <QString>

namespace gcs
{

/**
 * @class GuardModule
 * @brief 依 Agent 狀態驗證命令請求
 */
class GuardModule : public GcsModule
{
    Q_OBJECT

public:
    using GcsModule::GcsModule;

    bool init() override;
    void shutdown() override;
    bool health_check() const override;
    QString module_name() const override { return QStringLiteral("GuardModule"); }

    /**
     * @brief 取得 Agent 目前的任務階段（測試與除錯用）
     * @param agent_ns Agent namespace
     * @return 任務階段；沒有資料時為 IDLE
     */
    FlightState agent_flight_state(const QString& agent_ns) const;

public slots:
    /**
     * @brief 接收 UI 的命令請求並驗證
     * @param request 命令請求
     */
    void request_command(const gcs::CommandRequest& request);

    /**
     * @brief 更新 Agent 狀態
     * @param agent_ns Agent namespace
     * @param state 最新 AgentState
     */
    void on_state_updated(const QString& agent_ns, const gcs::AgentStateData& state);

    /** @brief Agent 上線 @param agent_ns Agent namespace */
    void on_agent_online(const QString& agent_ns);

    /** @brief Agent 離線，清除其所有狀態 @param agent_ns Agent namespace */
    void on_agent_offline(const QString& agent_ns);

signals:
    /** @brief 驗證通過，只能連到 CommandModule */
    void command_validated(const gcs::CommandRequest& request);

    /** @brief 驗證失敗的回報 */
    void command_acked(const gcs::CommandAck& ack);

private:
    /**
     * @brief 驗證全體命令：每台線上 Agent 都必須允許
     * @param cmd 命令型別
     * @param error 失敗原因
     * @return 是否允許
     */
    bool validate_for_all(CmdType cmd, QString* error) const;

    /**
     * @brief 驗證單機命令
     * @param agent_ns Agent namespace
     * @param cmd 命令型別
     * @param error 失敗原因
     * @return 是否允許
     */
    bool validate_for_agent(const QString& agent_ns, CmdType cmd, QString* error) const;

    /**
     * @brief 拒絕命令並發出錯誤回報
     * @param agent 目標
     * @param command 命令名稱
     * @param message 拒絕原因
     */
    void reject(const QString& agent, const QString& command, const QString& message);

    bool is_initialized_ = false;
    /// 目前線上的 Agent
    QSet<QString> online_agents_;
    /// Agent → 最新 AgentState（只保存線上 Agent）
    QHash<QString, AgentStateData> agent_states_;
};

}  // namespace gcs
