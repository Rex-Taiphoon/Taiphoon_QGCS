/**
 * @file ui_format.cpp
 * @brief 介面顯示用文字與數值換算實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "app/ui_format.hpp"

#include <QStringList>

#include <cmath>

namespace gcs::ui
{

namespace
{

/// 四元數範數低於此值視為無效姿態
constexpr double MIN_QUATERNION_NORM = 1e-6;
constexpr double RADIANS_TO_DEGREES = 180.0 / 3.14159265358979323846;
constexpr double DEGREES_TO_RADIANS = 3.14159265358979323846 / 180.0;
/// 經度縮放因子（緯度的 cos）低於此值時接近極點，不做顯示換算
constexpr double MIN_LONGITUDE_SCALE = 1e-6;
constexpr double FULL_TURN_DEGREES = 360.0;
constexpr double QUARTER_TURN_DEGREES = 90.0;
constexpr double MAX_LATITUDE_DEGREES = 90.0;
constexpr double MAX_LONGITUDE_DEGREES = 180.0;
/// 經緯度顯示的小數位數（約 0.1 公尺）
constexpr int LAT_LON_DECIMALS = 6;
/// 電量比例轉百分比
constexpr double PERCENT = 100.0;
/// PX4 BatteryStatus 以 -1 表示 remaining、current_a 無效
constexpr float PX4_BATTERY_INVALID = -1.0F;
/// PX4 BatteryStatus 以 0 表示 voltage_v 無效
constexpr float PX4_VOLTAGE_INVALID = 0.0F;
/// 判斷無效標記值的容許誤差
constexpr float INVALID_MARKER_TOLERANCE = 1e-3F;
/// VehicleStatus 超過此秒數未更新即視為中斷；與 Agent 判斷 px4_state_valid 的時限相同（介面文件 AgentState 一節）
constexpr float PX4_STATUS_STALE_S = 5.0F;
/// PX4 VehicleCommandAck.result（介面文件 AgentEvent 一節）
constexpr int16_t PX4_RESULT_ACCEPTED = 0;
constexpr int16_t PX4_RESULT_TEMPORARILY_REJECTED = 1;
constexpr int16_t PX4_RESULT_DENIED = 2;
constexpr int16_t PX4_RESULT_UNSUPPORTED = 3;
constexpr int16_t PX4_RESULT_FAILED = 4;
constexpr int16_t PX4_RESULT_IN_PROGRESS = 5;
constexpr int16_t PX4_RESULT_CANCELLED = 6;

/**
 * @brief 是否為 PX4 的無效標記值
 * @param value 欄位值
 * @param marker 無效標記值
 * @return 與標記值相等（含容許誤差）
 */
bool is_invalid_marker(float value, float marker)
{
    return std::abs(value - marker) < INVALID_MARKER_TOLERANCE;
}

/// 正規化後的四元數
struct Quaternion
{
    double w;
    double x;
    double y;
    double z;
};

/**
 * @brief 正規化四元數
 * @param q 四元數 (w, x, y, z)
 * @return 正規化結果；無效時為 std::nullopt
 */
std::optional<Quaternion> normalized(const std::array<float, 4>& q)
{
    for (const float value : q)
    {
        if (!std::isfinite(value))
        {
            return std::nullopt;
        }
    }
    const double norm = std::sqrt(static_cast<double>(q[0]) * q[0] + static_cast<double>(q[1]) * q[1]
                                  + static_cast<double>(q[2]) * q[2] + static_cast<double>(q[3]) * q[3]);
    if (!std::isfinite(norm) || norm < MIN_QUATERNION_NORM)
    {
        return std::nullopt;
    }
    return Quaternion{q[0] / norm, q[1] / norm, q[2] / norm, q[3] / norm};
}

}  // namespace

QString flight_mode_label(int flight_mode)
{
    static const QStringList LABELS = {
        QStringLiteral("閒置"),          QStringLiteral("Offboard 初始化"), QStringLiteral("Offboard 已啟用"),
        QStringLiteral("已解鎖"),        QStringLiteral("起飛中"),          QStringLiteral("懸停中"),
        QStringLiteral("返航中"),        QStringLiteral("降落中"),          QStringLiteral("移動中"),
        QStringLiteral("RTL 降落後"),    QStringLiteral("錯誤"),
    };
    if (flight_mode >= 0 && flight_mode < LABELS.size())
    {
        return LABELS.at(flight_mode);
    }
    return QStringLiteral("模式 %1").arg(flight_mode);
}

QString px4_status_label(const AgentStateData& state)
{
    if (!state.is_px4_state_valid)
    {
        return QStringLiteral("PX4 狀態未確認");
    }
    if (state.is_armed)
    {
        return state.is_landed ? QStringLiteral("已解鎖 · 已落地") : QStringLiteral("已解鎖 · 飛行中");
    }
    return QStringLiteral("已上鎖 · 已落地");
}

QString px4_mode_label(const AgentStateData& state)
{
    if (!state.is_px4_state_valid)
    {
        return QStringLiteral("未確認");
    }
    // 索引為 px4_msgs VehicleStatus 的 NAVIGATION_STATE_* 值；FREE 保留值為空字串
    static const QStringList LABELS = {
        QStringLiteral("Manual"),         QStringLiteral("Altitude"),        QStringLiteral("Position"),
        QStringLiteral("Mission"),        QStringLiteral("Hold"),            QStringLiteral("Return"),
        QStringLiteral("Position Slow"),  QStringLiteral("Guided Course"),   QStringLiteral("Altitude Cruise"),
        QStringLiteral("Manual Parking"), QStringLiteral("Acro"),            QString(),
        QStringLiteral("Descend"),        QStringLiteral("Termination"),     QStringLiteral("Offboard"),
        QStringLiteral("Stabilized"),     QString(),                         QStringLiteral("Takeoff"),
        QStringLiteral("Land"),           QStringLiteral("Follow Me"),       QStringLiteral("Precision Land"),
        QStringLiteral("Orbit"),          QStringLiteral("VTOL Takeoff"),    QStringLiteral("External 1"),
        QStringLiteral("External 2"),     QStringLiteral("External 3"),      QStringLiteral("External 4"),
        QStringLiteral("External 5"),     QStringLiteral("External 6"),      QStringLiteral("External 7"),
        QStringLiteral("External 8"),
    };
    const int nav_state = state.px4_nav_state;
    if (nav_state < LABELS.size() && !LABELS.at(nav_state).isEmpty())
    {
        return LABELS.at(nav_state);
    }
    return QStringLiteral("未知 %1").arg(nav_state);
}

std::optional<double> heading_degrees(const std::array<float, 4>& q)
{
    const auto n = normalized(q);
    if (!n.has_value())
    {
        return std::nullopt;
    }
    const double yaw = std::atan2(2.0 * (n->w * n->z + n->x * n->y), 1.0 - 2.0 * (n->y * n->y + n->z * n->z));
    return std::fmod(yaw * RADIANS_TO_DEGREES + FULL_TURN_DEGREES, FULL_TURN_DEGREES);
}

std::optional<EulerDegrees> euler_degrees(const std::array<float, 4>& q)
{
    const auto n = normalized(q);
    if (!n.has_value())
    {
        return std::nullopt;
    }
    EulerDegrees euler;
    euler.roll = std::atan2(2.0 * (n->w * n->x + n->y * n->z), 1.0 - 2.0 * (n->x * n->x + n->y * n->y))
                 * RADIANS_TO_DEGREES;
    const double sin_pitch = 2.0 * (n->w * n->y - n->z * n->x);
    euler.pitch = (std::abs(sin_pitch) >= 1.0 ? std::copysign(QUARTER_TURN_DEGREES, sin_pitch)
                                              : std::asin(sin_pitch) * RADIANS_TO_DEGREES);
    euler.yaw = std::atan2(2.0 * (n->w * n->z + n->x * n->y), 1.0 - 2.0 * (n->y * n->y + n->z * n->z))
                * RADIANS_TO_DEGREES;
    return euler;
}

QString format_number(double value, int decimals)
{
    return std::isfinite(value) ? QString::number(value, 'f', decimals) : QStringLiteral("—");
}

QString format_signed_number(double value, int decimals)
{
    if (!std::isfinite(value))
    {
        return QStringLiteral("—");
    }
    const QString magnitude = QString::number(std::abs(value), 'f', decimals);
    // 四捨五入後為零時不顯示負號，避免出現「−0.0」
    const bool is_negative = value < 0.0 && magnitude.toDouble() != 0.0;
    return (is_negative ? QStringLiteral("−") : QStringLiteral("+")) + magnitude;
}

QString format_battery_percent(float remaining)
{
    return is_invalid_marker(remaining, PX4_BATTERY_INVALID)
               ? QStringLiteral("—")
               : format_number(static_cast<double>(remaining) * PERCENT, 0);
}

QString format_battery_voltage(float voltage_v)
{
    return is_invalid_marker(voltage_v, PX4_VOLTAGE_INVALID) ? QStringLiteral("—") : format_number(voltage_v, 1);
}

QString format_battery_current(float current_a)
{
    return is_invalid_marker(current_a, PX4_BATTERY_INVALID) ? QStringLiteral("—") : format_number(current_a, 1);
}

QString format_ned(const std::array<float, 3>& position_ned)
{
    return QStringLiteral("%1  %2  %3")
        .arg(format_signed_number(position_ned[0], 1), format_signed_number(position_ned[1], 1),
             format_signed_number(position_ned[2], 1));
}

QString format_lat_lon(const std::array<double, 3>& position)
{
    if (!has_valid_global_position(position))
    {
        return QStringLiteral("—");
    }
    return QStringLiteral("%1 %2  %3 %4")
        .arg(std::abs(position[0]), 0, 'f', LAT_LON_DECIMALS)
        .arg(position[0] >= 0.0 ? QStringLiteral("N") : QStringLiteral("S"))
        .arg(std::abs(position[1]), 0, 'f', LAT_LON_DECIMALS)
        .arg(position[1] >= 0.0 ? QStringLiteral("E") : QStringLiteral("W"));
}

bool has_valid_global_position(const std::array<double, 3>& position)
{
    const double latitude = position[0];
    const double longitude = position[1];
    return std::isfinite(latitude) && std::isfinite(longitude) && std::abs(latitude) <= MAX_LATITUDE_DEGREES
           && std::abs(longitude) <= MAX_LONGITUDE_DEGREES;
}

std::array<double, 3> to_display_position(const std::array<double, 3>& position, const DisplayOrigin& origin)
{
    if (!origin.is_enabled || !has_valid_global_position(position))
    {
        return position;
    }
    // 局部平面近似：同一經度差在不同緯度代表的東向距離不同，先依參考點緯度換成距離、
    // 再依顯示位置緯度換回經度；深圳與台北緯度相差約 2.5 度，直接加減經度的東西向距離會差約 2%
    const double source_scale = std::cos(origin.source_latitude * DEGREES_TO_RADIANS);
    const double display_scale = std::cos(origin.display_latitude * DEGREES_TO_RADIANS);
    if (source_scale < MIN_LONGITUDE_SCALE || display_scale < MIN_LONGITUDE_SCALE)
    {
        return position;
    }
    // 經度差與結果都折返到 ±180 度內，跨越換日線時不會繞地球一圈
    const double delta_longitude = std::remainder(position[1] - origin.source_longitude, FULL_TURN_DEGREES);
    const double latitude = origin.display_latitude + (position[0] - origin.source_latitude);
    const double longitude = std::remainder(
        origin.display_longitude + delta_longitude * source_scale / display_scale, FULL_TURN_DEGREES);
    return {latitude, longitude, position[2]};
}

std::array<double, 3> to_reported_position(const std::array<double, 3>& position, const DisplayOrigin& origin)
{
    // 交換來源與顯示參考點即為反向換算
    DisplayOrigin inverse = origin;
    inverse.source_latitude = origin.display_latitude;
    inverse.source_longitude = origin.display_longitude;
    inverse.display_latitude = origin.source_latitude;
    inverse.display_longitude = origin.source_longitude;
    return to_display_position(position, inverse);
}


QString agent_short_name(const QString& agent_ns)
{
    QString name = agent_ns;
    name.remove(QStringLiteral("_agent"));
    return name.toUpper();
}

QString agent_event_label(uint16_t code)
{
    namespace ec = agent_event_code;
    switch (code)
    {
    case ec::COMMAND_UNKNOWN:
        return QStringLiteral("無法辨識的命令");
    case ec::COMMAND_INVALID_PARAMS:
        return QStringLiteral("命令參數錯誤");
    case ec::COMMAND_REJECTED:
        return QStringLiteral("Agent 拒絕命令");
    case ec::COMMAND_FAILED:
        return QStringLiteral("命令執行失敗");
    case ec::PX4_COMMAND_REJECTED:
        return QStringLiteral("飛控拒絕命令");
    case ec::PX4_COMMAND_TIMEOUT:
        return QStringLiteral("飛控未回應命令");
    case ec::PX4_COMMAND_ACCEPTED:
        return QStringLiteral("飛控接受命令");
    case ec::TAKEOFF_CANCELLED:
        return QStringLiteral("起飛已取消");
    case ec::OFFBOARD_LOST:
        return QStringLiteral("飛控離開 Offboard");
    case ec::PX4_STATUS_STALE:
        return QStringLiteral("飛控資料中斷");
    case ec::PX4_STATUS_RECOVERED:
        return QStringLiteral("飛控資料恢復");
    case ec::PX4_POSITION_STALE:
        return QStringLiteral("飛控位置資料中斷");
    case ec::PX4_PREFLIGHT_FAILED:
        return QStringLiteral("飛控預檢未通過");
    case ec::PX4_FAILSAFE:
        return QStringLiteral("飛控失效保護啟動");
    case ec::PX4_RESTART_DETECTED:
        return QStringLiteral("偵測到飛控重新啟動");
    case ec::PLANNER_COMMAND_REJECTED:
        return QStringLiteral("路徑規劃拒絕命令");
    case ec::PLANNER_EMERGENCY_BRAKE:
        return QStringLiteral("緊急煞停");
    case ec::PLANNER_FAILURE_PERSISTENT:
        return QStringLiteral("路徑規劃持續失敗");
    case ec::SAFETY_POSITION_DEVIATION:
        return QStringLiteral("位置偏差過大");
    default:
        return QStringLiteral("Agent 事件 %1").arg(code);
    }
}

QString px4_command_result_label(int16_t result)
{
    switch (result)
    {
    case PX4_RESULT_ACCEPTED:
        return QStringLiteral("已接受");
    case PX4_RESULT_TEMPORARILY_REJECTED:
        return QStringLiteral("暫時拒絕");
    case PX4_RESULT_DENIED:
        return QStringLiteral("拒絕");
    case PX4_RESULT_UNSUPPORTED:
        return QStringLiteral("不支援");
    case PX4_RESULT_FAILED:
        return QStringLiteral("失敗");
    case PX4_RESULT_IN_PROGRESS:
        return QStringLiteral("執行中");
    case PX4_RESULT_CANCELLED:
        return QStringLiteral("已取消");
    default:
        return QStringLiteral("未知");
    }
}

QString format_agent_event_message(const AgentEventData& event)
{
    QStringList parts;
    if (!event.command.isEmpty())
    {
        parts.append(event.command);
    }
    if (!event.detail.isEmpty())
    {
        parts.append(event.detail);
    }
    QString text = parts.isEmpty() ? QStringLiteral("代碼 %1").arg(event.code) : parts.join(QStringLiteral("；"));
    if (event.px4_result != AGENT_EVENT_NO_PX4_RESULT)
    {
        text += QStringLiteral("（PX4 結果 %1：%2）")
                    .arg(event.px4_result)
                    .arg(px4_command_result_label(event.px4_result));
    }
    return text;
}

bool is_px4_link_lost(const AgentHealthData& health)
{
    return health.px4_status_age_s < 0.0F || health.px4_status_age_s > PX4_STATUS_STALE_S;
}

QString px4_link_text(const AgentHealthData& health)
{
    if (health.px4_status_age_s < 0.0F)
    {
        return QStringLiteral("未連線");
    }
    if (is_px4_link_lost(health))
    {
        return QStringLiteral("中斷 %1 秒").arg(format_number(health.px4_status_age_s, 0));
    }
    return QStringLiteral("正常");
}

bool is_preflight_failed(const AgentHealthData& health)
{
    return !is_px4_link_lost(health) && !health.is_px4_pre_flight_checks_pass;
}

QString preflight_text(const AgentHealthData& health)
{
    if (is_px4_link_lost(health))
    {
        return QStringLiteral("—");
    }
    return health.is_px4_pre_flight_checks_pass ? QStringLiteral("PASS") : QStringLiteral("FAIL");
}

QString flight_controller_wait_reason(bool has_state, const AgentStateData& state, bool has_health,
                                      const AgentHealthData& health)
{
    if (has_state && state.is_px4_state_valid)
    {
        return QString();
    }
    if (!has_health)
    {
        return has_state ? QStringLiteral("等待飛控") : QStringLiteral("等待 Agent 狀態");
    }
    if (health.px4_status_age_s < 0.0F)
    {
        return QStringLiteral("未收到 PX4 資料");
    }
    if (is_px4_link_lost(health))
    {
        return QStringLiteral("PX4 資料中斷 %1 秒").arg(format_number(health.px4_status_age_s, 0));
    }
    // Agent 收到第一筆 odometry 才發布 AgentState
    if (!has_state)
    {
        return QStringLiteral("未收到 PX4 位置資料");
    }
    if (health.px4_land_detected_age_s < 0.0F || health.px4_land_detected_age_s > PX4_STATUS_STALE_S)
    {
        return QStringLiteral("未收到 PX4 落地偵測");
    }
    return QStringLiteral("等待飛控");
}

}  // namespace gcs::ui
