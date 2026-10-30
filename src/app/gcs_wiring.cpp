/**
 * @file gcs_wiring.cpp
 * @brief 組裝根實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 網頁版 EventBus 主題與本檔連線的對應：
 *   state_update            Telemetry.state_updated → Guard、Fleet
 *   （Qt 版新增）           Telemetry.health_updated → Guard（強制重新啟動飛控）、Fleet（在線判斷）
 *   agent_online／offline   Fleet → Telemetry、Guard
 *   cmd_validated           Guard.command_validated → Command.publish_validated（唯一路徑）
 *   agent_shutdown_accepted Lifecycle.shutdown_accepted → Fleet
 * 航點任務（Qt 版新增，ADR-0020）：Mission 的上傳請求 → Guard.request_mission_upload → Command.publish_mission_plan，
 * MISSION_START／MISSION_STOP 由 Mission.command_requested → Guard.request_command，與 UI 命令走同一道閘門；
 * Agent 回報的任務狀態 Telemetry.mission_status_updated → Mission；
 * Fleet.agent_offline 與 fleet_changed 的離線清單（含資料停滯）→ Mission 回報失去聯繫（Agent 會繼續執行）。
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
    // Agent 收到 odometry 前只發布健康狀態：Fleet 以此判斷在線，Guard 以此判斷可否強制重新啟動飛控
    QObject::connect(modules.telemetry, &TelemetryModule::health_updated, modules.guard,
                     &GuardModule::on_health_updated, DIRECT);
    QObject::connect(modules.telemetry, &TelemetryModule::health_updated, modules.fleet,
                     &FleetModule::on_health_updated, DIRECT);

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

    // 航點任務上傳：與命令相同只能經 Guard 驗證後交給 CommandModule
    QObject::connect(modules.mission, &MissionModule::mission_upload_requested, modules.guard,
                     &GuardModule::request_mission_upload, DIRECT);
    QObject::connect(modules.guard, &GuardModule::mission_upload_validated, modules.command,
                     &CommandModule::publish_mission_plan, DIRECT);
    // 上傳結果只送給 MissionModule：上傳不是命令，UI 的命令提示不需要顯示
    QObject::connect(modules.guard, &GuardModule::mission_upload_acked, modules.mission,
                     &MissionModule::on_mission_upload_acked, DIRECT);
    QObject::connect(modules.command, &CommandModule::mission_upload_acked, modules.mission,
                     &MissionModule::on_mission_upload_acked, DIRECT);
    // MISSION_START／MISSION_STOP 交給 Guard 驗證，不直接連到 CommandModule
    QObject::connect(modules.mission, &MissionModule::command_requested, modules.guard, &GuardModule::request_command,
                     DIRECT);
    // 兩種回報都要接：Guard 拒絕與 CommandModule 發布結果（MISSION_START 是否送出）
    QObject::connect(modules.guard, &GuardModule::command_acked, modules.mission, &MissionModule::on_command_acked,
                     DIRECT);
    QObject::connect(modules.command, &CommandModule::command_acked, modules.mission,
                     &MissionModule::on_command_acked, DIRECT);
    // 任務進度以 Agent 回報為準
    QObject::connect(modules.telemetry, &TelemetryModule::mission_status_updated, modules.mission,
                     &MissionModule::on_mission_status_updated, DIRECT);
    QObject::connect(modules.fleet, &FleetModule::agent_offline, modules.mission, &MissionModule::on_agent_offline,
                     DIRECT);
    // 資料停滯不會發出 agent_offline；以顯示清單判斷，與畫面移除該機的時機一致
    QObject::connect(modules.fleet, &FleetModule::fleet_changed, modules.mission, &MissionModule::on_fleet_changed,
                     DIRECT);

    qCInfo(lc_wiring) << "核心模組連線完成";
    return true;
}

}  // namespace gcs
