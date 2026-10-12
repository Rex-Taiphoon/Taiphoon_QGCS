/**
 * @file gcs_wiring.hpp
 * @brief 組裝根 — 唯一建立模組間 signal／slot 連線的位置（ADR-0015）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 取代網頁版 EventBus 的主題訂閱。新增或修改跨模組連線只能在 gcs_wiring.cpp 進行，
 * 並同步更新 test/test_gcs_wiring.cpp。
 */
#pragma once

namespace gcs
{

class AgentLifecycleModule;
class CommandModule;
class FleetModule;
class GuardModule;
class TelemetryModule;

/// 需要互相連線的核心模組
struct CoreModules
{
    FleetModule* fleet = nullptr;
    TelemetryModule* telemetry = nullptr;
    GuardModule* guard = nullptr;
    CommandModule* command = nullptr;
    AgentLifecycleModule* lifecycle = nullptr;
};

/**
 * @brief 建立核心模組之間的連線
 *
 * 模組位於同一核心執行緒，一律使用 Qt::DirectConnection：
 * Guard 驗證通過後在同一次呼叫內交給 CommandModule 發布，不會插入其他狀態更新。
 *
 * @param modules 核心模組；任一為空時不建立任何連線
 * @return 是否成功
 */
bool wire_core_modules(const CoreModules& modules);

}  // namespace gcs
