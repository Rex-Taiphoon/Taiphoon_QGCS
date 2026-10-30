/**
 * @file guard_module.hpp
 * @brief 安全防呆模組 — 命令發布前的唯一安全閘門
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 追蹤每台 Agent 的最新狀態、健康狀態與最後一次 PX4 狀態有效時的狀態，依 guard_rules 驗證命令與航點任務上傳；
 * 通過才發出 command_validated／mission_upload_validated，由組裝根以 Qt::DirectConnection 連到 CommandModule，
 * 驗證與發布在同一次呼叫內完成。
 * 模組只在核心執行緒執行，不需要 mutex（ADR-0015）。
 */
#pragma once

#include "core/agent_types.hpp"
#include "core/command_protocol.hpp"
#include "core/gcs_module.hpp"
#include "modules/guard/guard_rules.hpp"

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
     * @brief 接收航點任務上傳請求並驗證（開始條件與 MISSION_START 相同）
     * @param agent_ns 單一 Agent namespace
     * @param mission 航點任務
     */
    void request_mission_upload(const QString& agent_ns, const gcs::WaypointMissionData& mission);

    /**
     * @brief 更新 Agent 狀態
     * @param agent_ns Agent namespace
     * @param state 最新 AgentState
     */
    void on_state_updated(const QString& agent_ns, const gcs::AgentStateData& state);

    /**
     * @brief 更新 Agent 健康狀態（強制重新啟動飛控的判斷依據）
     * @param agent_ns Agent namespace
     * @param health 最新 AgentHealth
     */
    void on_health_updated(const QString& agent_ns, const gcs::AgentHealthData& health);

    /** @brief Agent 上線 @param agent_ns Agent namespace */
    void on_agent_online(const QString& agent_ns);

    /** @brief Agent 離線，清除其狀態（保留最後有效狀態） @param agent_ns Agent namespace */
    void on_agent_offline(const QString& agent_ns);

signals:
    /** @brief 驗證通過，只能連到 CommandModule */
    void command_validated(const gcs::CommandRequest& request);

    /** @brief 驗證失敗的回報 */
    void command_acked(const gcs::CommandAck& ack);

    /** @brief 航點任務上傳驗證通過，只能連到 CommandModule */
    void mission_upload_validated(const QString& agent_ns, const gcs::WaypointMissionData& mission);

    /** @brief 航點任務上傳被拒絕的回報（command 為 MISSION_UPLOAD_ACK_NAME） */
    void mission_upload_acked(const gcs::CommandAck& ack);

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
     * @param request 命令請求（REBOOT_FC 依參數判斷是否強制）
     * @param cmd 命令型別
     * @param error 失敗原因
     * @return 是否允許
     */
    bool validate_for_agent(const CommandRequest& request, CmdType cmd, QString* error) const;

    /**
     * @brief 整理判斷重新啟動飛控所需的已知資訊
     * @param agent_ns Agent namespace
     * @return 已知資訊
     */
    guard_rules::RebootFacts reboot_facts(const QString& agent_ns) const;

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
    /// Agent → 最新 AgentHealth（只保存線上 Agent）
    QHash<QString, AgentHealthData> agent_health_;
    /// Agent → 最後一次 PX4 狀態有效時的 AgentState；離線後保留，PX4 斷線再上線時仍能判斷是否可能在空中
    QHash<QString, AgentStateData> last_valid_states_;
};

}  // namespace gcs
