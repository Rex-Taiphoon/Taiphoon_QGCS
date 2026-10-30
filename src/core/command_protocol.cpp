/**
 * @file command_protocol.cpp
 * @brief Agent 命令協定實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "core/command_protocol.hpp"

#include <QRegularExpression>

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>

namespace gcs::command_protocol
{

namespace
{

/// 命令參數規格：依介面文件第 6.2 節的參數數量
struct CommandSpec
{
    const char* name;
    CmdType type;
    int min_params;
    int max_params;
};

/// 介面文件第 6.2 節的全部命令（含相容性簡寫）
constexpr std::array<CommandSpec, 27> COMMAND_SPECS = {{
    {"MODE_RTL", CmdType::MODE_RTL, 0, 0},
    {"MODE_LAND", CmdType::MODE_LAND, 0, 0},
    {"MODE_OFFBOARD", CmdType::MODE_OFFBOARD, 0, 0},
    {"HOLD", CmdType::HOLD, 0, 0},
    {"ARM", CmdType::ARM, 0, 0},
    {"DISARM", CmdType::DISARM, 0, 0},
    {"TAKEOFF", CmdType::TAKEOFF, 0, 1},
    {"MOVE_FORWARD", CmdType::MOVE_FORWARD, 1, 1},
    {"FORWARD", CmdType::MOVE_FORWARD, 1, 1},
    {"MOVE_BACKWARD", CmdType::MOVE_BACKWARD, 1, 1},
    {"BACKWARD", CmdType::MOVE_BACKWARD, 1, 1},
    {"MOVE_LEFT", CmdType::MOVE_LEFT, 1, 1},
    {"LEFT", CmdType::MOVE_LEFT, 1, 1},
    {"MOVE_RIGHT", CmdType::MOVE_RIGHT, 1, 1},
    {"RIGHT", CmdType::MOVE_RIGHT, 1, 1},
    {"MOVE_UP", CmdType::MOVE_UP, 1, 1},
    {"MOVE_DOWN", CmdType::MOVE_DOWN, 1, 1},
    {"YAW", CmdType::YAW, 1, 1},
    {"MOVE_POINT", CmdType::MOVE_POINT, 4, 4},
    {"FORMATION_CIRCLE", CmdType::FORMATION_CIRCLE, 2, 2},
    {"FORMATION_HORI_LINEAR", CmdType::FORMATION_HORI_LINEAR, 2, 2},
    {"FORMATION_VERT_LINEAR", CmdType::FORMATION_VERT_LINEAR, 1, 1},
    {"FORMATION_V_SHAPE", CmdType::FORMATION_V_SHAPE, 2, 2},
    {"FORMATION_CANCEL", CmdType::FORMATION_CANCEL, 0, 0},
    {"MISSION_START", CmdType::MISSION_START, 1, 1},
    {"MISSION_STOP", CmdType::MISSION_STOP, 0, 0},
    {"REBOOT_FC", CmdType::REBOOT_FC, 0, 1},
}};

/// double 最短可還原十進位表示所需的最大長度
constexpr std::size_t MAX_DOUBLE_TEXT_LENGTH = 32;
/// 絕對值小於此值的整數可由 double 精確表示（2^53），以定點格式輸出
constexpr double MAX_EXACT_INTEGER = 9007199254740992.0;
/// MISSION_START 識別碼範圍（WaypointMission.mission_id 為 uint32，0 表示沒有任務）
constexpr double MIN_MISSION_ID = 1.0;
constexpr double MAX_MISSION_ID = 4294967295.0;
/// REBOOT_FC 的參數：0 為一般重新啟動、1 為飛控狀態不明時的強制重新啟動
constexpr double REBOOT_NORMAL_FLAG = 0.0;
constexpr double REBOOT_FORCE_FLAG = 1.0;

/**
 * @brief 依命令名稱查詢規格
 * @param command 命令名稱（不分大小寫、忽略前後空白）
 * @return 規格指標；不支援時為 nullptr
 */
const CommandSpec* find_spec(const QString& command)
{
    const QString upper = command.trimmed().toUpper();
    for (const auto& spec : COMMAND_SPECS)
    {
        if (upper == QLatin1String(spec.name))
        {
            return &spec;
        }
    }
    return nullptr;
}

/**
 * @brief 數值是否為整數（不含小數部分）
 * @param value 有限數值
 * @return 是否為整數
 */
bool is_integer(double value)
{
    // 刻意精確比較：Agent 以整數解析的參數不得帶任何小數
    return std::trunc(value) == value;
}

/**
 * @brief 依命令檢查參數數值；參數數量與有限值已先檢查
 * @param spec 命令規格
 * @param params 參數
 * @return 空字串表示通過，否則為錯誤訊息
 */
QString validate_param_values(const CommandSpec& spec, const QVector<double>& params)
{
    switch (spec.type)
    {
    case CmdType::MISSION_START:
    {
        const double mission_id = params.at(0);
        if (!is_integer(mission_id) || mission_id < MIN_MISSION_ID || mission_id > MAX_MISSION_ID)
        {
            return QStringLiteral("MISSION_START 的任務識別碼需為 1～4294967295 的整數");
        }
        break;
    }
    case CmdType::REBOOT_FC:
        if (!params.isEmpty() && params.at(0) != REBOOT_NORMAL_FLAG && params.at(0) != REBOOT_FORCE_FLAG)
        {
            return QStringLiteral("REBOOT_FC 的參數只能是 0 或 1");
        }
        break;
    default:
        break;
    }
    return QString();
}

/**
 * @brief 將 double 轉為最短可還原表示，例如 3 → "3"、20.5 → "20.5"、1000000 → "1000000"
 * @param value 有限數值
 * @return 十進位字串
 */
QString format_number(double value)
{
    std::array<char, MAX_DOUBLE_TEXT_LENGTH> buffer{};
    // 最短表示會把 1000000 寫成 1e+06；Agent 以整數解析的參數（例如任務識別碼）不能出現指數
    const bool is_fixed = std::abs(value) < MAX_EXACT_INTEGER && is_integer(value);
    const auto result = is_fixed ? std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                                 std::chars_format::fixed)
                                 : std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc())
    {
        // 緩衝區足以容納任何 double；保守退回 Qt 格式化，避免回傳空字串。
        return QString::number(value, 'g', 17);
    }
    return QString::fromLatin1(buffer.data(), static_cast<int>(result.ptr - buffer.data()));
}

}  // namespace

CmdType parse_command(const QString& command)
{
    const CommandSpec* spec = find_spec(command);
    return spec != nullptr ? spec->type : CmdType::UNKNOWN;
}

QString validate_request(const CommandRequest& request)
{
    if (!is_agent_target_valid(request.agent))
    {
        return QStringLiteral("agent 僅允許 all 或 uav_{N}_agent");
    }
    if (request.command.trimmed().isEmpty())
    {
        return QStringLiteral("command 不可為空");
    }

    const CommandSpec* spec = find_spec(request.command);
    if (spec == nullptr)
    {
        return QStringLiteral("不支援的命令: ") + request.command.trimmed();
    }

    const auto count = static_cast<int>(request.params.size());
    if (count < spec->min_params || count > spec->max_params)
    {
        const QString name = QLatin1String(spec->name);
        if (spec->min_params == spec->max_params)
        {
            return QStringLiteral("命令 %1 需要 %2 個參數").arg(name).arg(spec->min_params);
        }
        return QStringLiteral("命令 %1 需要 %2 至 %3 個參數").arg(name).arg(spec->min_params).arg(spec->max_params);
    }

    if (has_non_finite_param(request))
    {
        return QStringLiteral("參數不得為 NaN 或 Inf");
    }
    if (request.agent == ALL_AGENTS_TARGET && is_single_agent_only(spec->type))
    {
        return QStringLiteral("命令 %1 只能指定單一 Agent").arg(QLatin1String(spec->name));
    }
    return validate_param_values(*spec, request.params);
}

bool is_single_agent_only(CmdType cmd)
{
    return cmd == CmdType::MISSION_START || cmd == CmdType::MISSION_STOP || cmd == CmdType::REBOOT_FC;
}

bool is_forced_reboot(const CommandRequest& request)
{
    return parse_command(request.command) == CmdType::REBOOT_FC && !request.params.isEmpty()
           && request.params.at(0) == REBOOT_FORCE_FLAG;
}

bool is_agent_target_valid(const QString& agent)
{
    // 介面規格使用 1-based UAV 索引，避免將不存在的 uav_0_agent 當成合法目標。
    static const QRegularExpression PATTERN(QStringLiteral("^(all|uav_[1-9][0-9]*_agent)$"));
    return PATTERN.match(agent).hasMatch();
}

bool is_single_agent(const QString& agent)
{
    return agent != ALL_AGENTS_TARGET && is_agent_target_valid(agent);
}

bool is_formation_command(const QString& command)
{
    return command.trimmed().toUpper().startsWith(QStringLiteral("FORMATION_"));
}

bool has_non_finite_param(const CommandRequest& request)
{
    for (const double value : request.params)
    {
        if (std::isnan(value) || std::isinf(value))
        {
            return true;
        }
    }
    return false;
}

QString make_command_line(const CommandRequest& request)
{
    QString line = request.command;
    for (const double value : request.params)
    {
        line += QLatin1Char(' ');
        line += format_number(value);
    }
    return line;
}

CommandAck make_ack(const QString& agent, const QString& command, bool is_ok, const QString& message)
{
    CommandAck ack;
    ack.agent = agent;
    ack.command = command;
    ack.is_ok = is_ok;
    ack.message = message;
    return ack;
}

}  // namespace gcs::command_protocol
