/**
 * @file command_protocol.hpp
 * @brief Agent 命令協定：命令辨識、參數驗證與命令列組裝（介面文件第 6 節）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 由網頁版 ws_protocol 移植；移除 WebSocket JSON 解析，驗證規則與錯誤訊息維持不變，
 * 供 GuardModule、CommandModule 與 UiBridge 共用同一份規則。
 */
#pragma once

#include "core/agent_types.hpp"

#include <QString>

namespace gcs
{

/// 命令型別（與 Agent 命令協定對應）
enum class CmdType
{
    ARM,
    DISARM,
    TAKEOFF,
    MOVE_FORWARD,
    MOVE_BACKWARD,
    MOVE_LEFT,
    MOVE_RIGHT,
    MOVE_UP,
    MOVE_DOWN,
    YAW,
    MOVE_POINT,
    HOLD,
    MODE_RTL,
    MODE_LAND,
    MODE_OFFBOARD,
    FORMATION_CIRCLE,
    FORMATION_HORI_LINEAR,
    FORMATION_VERT_LINEAR,
    FORMATION_CANCEL,
    UNKNOWN
};

namespace command_protocol
{

/**
 * @brief 命令名稱轉 CmdType（不分大小寫，接受介面文件的相容性簡寫）
 * @param command 命令名稱
 * @return 命令型別；無法辨識時為 CmdType::UNKNOWN
 */
CmdType parse_command(const QString& command);

/**
 * @brief 驗證命令請求的目標、命令名稱、參數數量與數值
 * @param request 命令請求
 * @return 空字串表示通過，否則為錯誤訊息
 */
QString validate_request(const CommandRequest& request);

/**
 * @brief 檢查命令目標是否合法（all 或 uav_{N}_agent，N 由 1 起算）
 * @param agent 目標字串
 * @return 是否合法
 */
bool is_agent_target_valid(const QString& agent);

/**
 * @brief 檢查是否為單一 Agent namespace（不含 all）
 * @param agent 目標字串
 * @return 是否為 uav_{N}_agent
 */
bool is_single_agent(const QString& agent);

/**
 * @brief 檢查是否為編隊命令（FORMATION_*）
 * @param command 命令名稱
 * @return 是否為編隊命令
 */
bool is_formation_command(const QString& command);

/**
 * @brief 檢查參數是否含 NaN／Inf（CODE_CONSTRAINT L1-08）
 * @param request 命令請求
 * @return 是否含非有限數值
 */
bool has_non_finite_param(const CommandRequest& request);

/**
 * @brief 組合送往 Agent 的命令列，例如 "FORMATION_CIRCLE 10 20.5"
 * @param request 已驗證的命令請求
 * @return 命令列；數值使用可完整還原的最短十進位表示
 */
QString make_command_line(const CommandRequest& request);

/**
 * @brief 建立命令回報
 * @param agent 目標
 * @param command 命令名稱
 * @param is_ok 是否成功
 * @param message 說明（成功時可為空）
 * @return 命令回報
 */
CommandAck make_ack(const QString& agent, const QString& command, bool is_ok, const QString& message = QString());

}  // namespace command_protocol
}  // namespace gcs
