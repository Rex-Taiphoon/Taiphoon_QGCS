/**
 * @file ui_wiring.cpp
 * @brief 組裝根（UI 部分）實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "app/ui_wiring.hpp"

#include "app/diagnostics_controller.hpp"
#include "app/ui_bridge.hpp"
#include "modules/command/command_module.hpp"
#include "modules/fleet/fleet_module.hpp"
#include "modules/guard/guard_module.hpp"
#include "modules/lifecycle/agent_lifecycle_module.hpp"
#include "modules/telemetry/telemetry_module.hpp"

#include <QLoggingCategory>

namespace
{
Q_LOGGING_CATEGORY(lc_ui_wiring, "gcs.wiring.ui")
}  // namespace

namespace gcs
{

bool wire_ui(const CoreModules& modules, UiBridge* bridge)
{
    if (bridge == nullptr || modules.fleet == nullptr || modules.telemetry == nullptr || modules.guard == nullptr
        || modules.command == nullptr || modules.lifecycle == nullptr)
    {
        qCCritical(lc_ui_wiring) << "UI 組裝參數不完整，未建立任何連線";
        return false;
    }

    // UI → 核心：只有這兩條（AGENTS.md：UI 不得直接連到 CommandModule 或 IAgentLink）
    QObject::connect(bridge, &UiBridge::command_requested, modules.guard, &GuardModule::request_command);
    QObject::connect(bridge, &UiBridge::shutdown_requested, modules.lifecycle,
                     &AgentLifecycleModule::request_shutdown);

    // 核心 → UI：遙測、機隊與命令結果
    QObject::connect(modules.telemetry, &TelemetryModule::state_updated, bridge, &UiBridge::on_state_updated);
    QObject::connect(modules.telemetry, &TelemetryModule::attitude_updated, bridge, &UiBridge::on_attitude_updated);
    QObject::connect(modules.telemetry, &TelemetryModule::trajectory_updated, bridge,
                     &UiBridge::on_trajectory_updated);
    QObject::connect(modules.fleet, &FleetModule::fleet_changed, bridge, &UiBridge::on_fleet_changed);
    QObject::connect(modules.guard, &GuardModule::command_acked, bridge, &UiBridge::on_command_acked);
    QObject::connect(modules.command, &CommandModule::command_acked, bridge, &UiBridge::on_command_acked);
    QObject::connect(modules.lifecycle, &AgentLifecycleModule::command_acked, bridge, &UiBridge::on_command_acked);

    qCInfo(lc_ui_wiring) << "UI 連線完成";
    return true;
}

bool wire_diagnostics(const CoreModules& modules, DiagnosticsController* diagnostics)
{
    if (diagnostics == nullptr || modules.fleet == nullptr || modules.telemetry == nullptr
        || modules.guard == nullptr || modules.command == nullptr || modules.lifecycle == nullptr)
    {
        qCCritical(lc_ui_wiring) << "除錯工具組裝參數不完整，未建立任何連線";
        return false;
    }
    QObject::connect(modules.telemetry, &TelemetryModule::state_updated, diagnostics,
                     &DiagnosticsController::on_state_updated);
    QObject::connect(modules.telemetry, &TelemetryModule::attitude_updated, diagnostics,
                     &DiagnosticsController::on_attitude_updated);
    QObject::connect(modules.telemetry, &TelemetryModule::trajectory_updated, diagnostics,
                     &DiagnosticsController::on_trajectory_updated);
    QObject::connect(modules.fleet, &FleetModule::agent_online, diagnostics, &DiagnosticsController::on_agent_online);
    QObject::connect(modules.fleet, &FleetModule::agent_offline, diagnostics,
                     &DiagnosticsController::on_agent_offline);
    QObject::connect(modules.guard, &GuardModule::command_acked, diagnostics, &DiagnosticsController::on_command_acked);
    QObject::connect(modules.command, &CommandModule::command_acked, diagnostics,
                     &DiagnosticsController::on_command_acked);
    QObject::connect(modules.lifecycle, &AgentLifecycleModule::command_acked, diagnostics,
                     &DiagnosticsController::on_command_acked);
    return true;
}

}  // namespace gcs
