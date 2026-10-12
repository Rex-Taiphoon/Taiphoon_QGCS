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
constexpr double FULL_TURN_DEGREES = 360.0;
constexpr double QUARTER_TURN_DEGREES = 90.0;
constexpr double MAX_LATITUDE_DEGREES = 90.0;
constexpr double MAX_LONGITUDE_DEGREES = 180.0;

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

bool has_valid_global_position(const std::array<double, 3>& position)
{
    const double latitude = position[0];
    const double longitude = position[1];
    return std::isfinite(latitude) && std::isfinite(longitude) && std::abs(latitude) <= MAX_LATITUDE_DEGREES
           && std::abs(longitude) <= MAX_LONGITUDE_DEGREES;
}

}  // namespace gcs::ui
