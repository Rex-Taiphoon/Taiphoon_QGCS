/**
 * @file ui_wiring.hpp
 * @brief 組裝根（UI 部分）— 唯一建立 UiBridge 與核心模組連線的位置（ADR-0015）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#pragma once

#include "app/gcs_wiring.hpp"

namespace gcs
{

class DiagnosticsController;
class MissionController;
class UiBridge;

/**
 * @brief 連接 UiBridge 與核心模組
 *
 * UiBridge 只能送出兩種請求：命令只到 GuardModule::request_command，
 * 關閉只到 AgentLifecycleModule::request_shutdown；其餘均為核心 → UI 的單向通知。
 * 模組在核心執行緒、UiBridge 在主執行緒，Qt 自動以 queued connection 傳遞。
 *
 * @param modules 核心模組
 * @param bridge UiBridge
 * @return 是否成功；任一指標為空時不建立任何連線
 */
bool wire_ui(const CoreModules& modules, UiBridge* bridge);

/**
 * @brief 連接唯讀除錯工具；只接收核心事件，不建立任何 UI → 核心的連線
 * @param modules 核心模組
 * @param diagnostics 除錯工具
 * @return 是否成功
 */
bool wire_diagnostics(const CoreModules& modules, DiagnosticsController* diagnostics);

/**
 * @brief 連接航點任務介面與 MissionModule
 *
 * 介面只送出「開始」與「停止」兩種任務請求；任務中的每一個命令都由 MissionModule
 * 交給 GuardModule 驗證，介面不會直接產生 Agent 命令。
 *
 * @param modules 核心模組
 * @param controller 航點任務介面
 * @return 是否成功
 */
bool wire_mission(const CoreModules& modules, MissionController* controller);

}  // namespace gcs
