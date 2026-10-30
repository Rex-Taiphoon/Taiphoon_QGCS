/**
 * @file command_module.hpp
 * @brief 命令模組 — 將 Guard 驗證通過的命令發布至 Agent
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 依目標路由至 /{agent_namespace}/command 或 /uav_all_agent/command。
 * publish_validated 只能由組裝根連到 GuardModule::command_validated（AGENTS.md）。
 */
#pragma once

#include "core/agent_types.hpp"
#include "core/gcs_module.hpp"

namespace gcs
{

class IAgentLink;

/**
 * @class CommandModule
 * @brief 命令轉發
 */
class CommandModule : public GcsModule
{
    Q_OBJECT

public:
    /**
     * @brief 建立命令模組
     * @param link Agent 通訊介面，生命週期需長於本模組
     * @param parent 父物件
     */
    explicit CommandModule(IAgentLink* link, QObject* parent = nullptr);

    bool init() override;
    void shutdown() override;
    bool health_check() const override;
    QString module_name() const override { return QStringLiteral("CommandModule"); }

public slots:
    /**
     * @brief 發布已通過 Guard 驗證的命令
     * @param request 命令請求
     */
    void publish_validated(const gcs::CommandRequest& request);

    /**
     * @brief 發布已通過 Guard 驗證的航點任務上傳
     * @param agent_ns 單一 Agent namespace
     * @param mission 航點任務
     */
    void publish_mission_plan(const QString& agent_ns, const gcs::WaypointMissionData& mission);

signals:
    /** @brief 命令發布結果 */
    void command_acked(const gcs::CommandAck& ack);

    /** @brief 航點任務上傳的發布結果（command 為 MISSION_UPLOAD_ACK_NAME） */
    void mission_upload_acked(const gcs::CommandAck& ack);

private:
    IAgentLink* link_ = nullptr;
    bool is_initialized_ = false;
};

}  // namespace gcs
