/**
 * @file gcs_wiring.cpp
 * @brief 組裝根實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 網頁版 EventBus 主題與本檔連線的對應：
 *   state_update            Telemetry.state_updated → Guard、Fleet
 *   agent_online／offline   Fleet → Telemetry、Guard
 *   cmd_validated           Guard.command_validated → Command.publish_validated（唯一路徑）
 *   agent_shutdown_accepted Lifecycle.shutdown_accepted → Fleet
 * 航點任務（Qt 版新增）：Mission.command_requested → Guard.request_command，與 UI 命令走同一道閘門。
 * UI（UiBridge）的連線於 M4 加入；UI 只能連到 Guard.request_command 與 Lifecycle.request_shutdown。
 */
#include "app/gcs_wiring.hpp"

#include "modules/command/command_module.hpp"
#include "modules/fleet/fleet_module.hpp"
#include "modules/guard/guard_module.hpp"
#include "modules/lifecycle/agent_lifecycle_module.hpp"
#include "modules/mission/mission_module.hpp"
#include "modules/telemetry/telemetry_module.hpp"

#include <QLoggingCategory>

namespace
{
Q_LOGGING_CATEGORY(lc_wiring, "gcs.wiring")
}  // namespace

namespace gcs
{

bool wire_core_modules(const CoreModules& modules)
{
    if (modules.fleet == nullptr || modules.telemetry == nullptr || modules.guard == nullptr
        || modules.command == nullptr || modules.lifecycle == nullptr || modules.mission == nullptr)
    {
        qCCritical(lc_wiring) << "核心模組不完整，未建立任何連線";
        return false;
    }
    constexpr auto DIRECT = Qt::DirectConnection;

    // 遙測 → 防呆與機隊
    QObject::connect(modules.telemetry, &TelemetryModule::state_updated, modules.guard,
                     &GuardModule::on_state_updated, DIRECT);
    QObject::connect(modules.telemetry, &TelemetryModule::state_updated, modules.fleet,
                     &FleetModule::on_state_updated, DIRECT);

    // 機隊上下線 → 遙測訂閱與防呆狀態
    QObject::connect(modules.fleet, &FleetModule::agent_online, modules.telemetry, &TelemetryModule::on_agent_online,
                     DIRECT);
    QObject::connect(modules.fleet, &FleetModule::agent_online, modules.guard, &GuardModule::on_agent_online, DIRECT);
    QObject::connect(modules.fleet, &FleetModule::agent_offline, modules.telemetry,
                     &TelemetryModule::on_agent_offline, DIRECT);
    QObject::connect(modules.fleet, &FleetModule::agent_offline, modules.guard, &GuardModule::on_agent_offline, DIRECT);

    // 命令只能經 Guard 驗證後交給 CommandModule（AGENTS.md）
    QObject::connect(modules.guard, &GuardModule::command_validated, modules.command,
                     &CommandModule::publish_validated, DIRECT);

    // Shutdown 成功 → 機隊立即隔離
    QObject::connect(modules.lifecycle, &AgentLifecycleModule::shutdown_accepted, modules.fleet,
                     &FleetModule::on_shutdown_accepted, DIRECT);

    // 航點任務：每段 MOVE_POINT 與停止時的 HOLD 都交給 Guard 驗證，不直接連到 CommandModule
    QObject::connect(modules.mission, &MissionModule::command_requested, modules.guard, &GuardModule::request_command,
                     DIRECT);
    QObject::connect(modules.telemetry, &TelemetryModule::state_updated, modules.mission,
                     &MissionModule::on_state_updated, DIRECT);
    QObject::connect(modules.telemetry, &TelemetryModule::attitude_updated, modules.mission,
                     &MissionModule::on_attitude_updated, DIRECT);
    QObject::connect(modules.fleet, &FleetModule::agent_offline, modules.mission, &MissionModule::on_agent_offline,
                     DIRECT);
    // 兩種回報都要接：Guard 拒絕與 CommandModule 發布結果；其他成功命令代表操作人員介入
    QObject::connect(modules.guard, &GuardModule::command_acked, modules.mission, &MissionModule::on_command_acked,
                     DIRECT);
    QObject::connect(modules.command, &CommandModule::command_acked, modules.mission,
                     &MissionModule::on_command_acked, DIRECT);

    qCInfo(lc_wiring) << "核心模組連線完成";
    return true;
}

}  // namespace gcs
