/**
 * @file app_settings.cpp
 * @brief 使用者設定與站點設定實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "app/app_settings.hpp"

#include <QHostAddress>
#include <QRegularExpression>

#include <cmath>
#include <optional>

namespace gcs
{

namespace
{

// 鍵名與網頁版 localStorage 對應，方便對照
const QString KEY_AUTO_FOLLOW = QStringLiteral("ui/auto_follow");
const QString KEY_SHOW_ATTITUDE = QStringLiteral("ui/show_attitude");
const QString KEY_TAKEOFF_HEIGHT = QStringLiteral("command/takeoff_height");
const QString KEY_YAW_DEGREES = QStringLiteral("command/yaw_degrees");
const QString KEY_CIRCLE_RADIUS = QStringLiteral("command/form_circle_radius");
const QString KEY_CIRCLE_HEIGHT = QStringLiteral("command/form_circle_height");
const QString KEY_LINEAR_SPACING = QStringLiteral("command/form_linear_spacing");
const QString KEY_LINEAR_HEIGHT = QStringLiteral("command/form_linear_height");
const QString KEY_VERT_SPACING = QStringLiteral("command/form_vert_spacing");
const QString KEY_V_SHAPE_SPACING = QStringLiteral("command/form_v_shape_spacing");
const QString KEY_V_SHAPE_HEIGHT = QStringLiteral("command/form_v_shape_height");
const QString KEY_SITE_CONFIGURED = QStringLiteral("site/configured");
const QString KEY_DOMAIN_ID = QStringLiteral("site/domain_id");
const QString KEY_INTERFACE = QStringLiteral("site/network_interface");
const QString KEY_PEERS = QStringLiteral("site/peers");
const QString KEY_MULTICAST = QStringLiteral("site/multicast");
const QString KEY_PARTICIPANT_INDEX = QStringLiteral("site/participant_index");
const QString KEY_ORIGIN_ENABLED = QStringLiteral("site/display_origin/enabled");
const QString KEY_ORIGIN_SOURCE_LATITUDE = QStringLiteral("site/display_origin/source_latitude");
const QString KEY_ORIGIN_SOURCE_LONGITUDE = QStringLiteral("site/display_origin/source_longitude");
const QString KEY_ORIGIN_DISPLAY_LATITUDE = QStringLiteral("site/display_origin/display_latitude");
const QString KEY_ORIGIN_DISPLAY_LONGITUDE = QStringLiteral("site/display_origin/display_longitude");

// 預設值與範圍沿用網頁版
constexpr bool DEFAULT_AUTO_FOLLOW = true;
constexpr bool DEFAULT_SHOW_ATTITUDE = true;

/// 數值設定的預設值與合法範圍
struct NumberSpec
{
    double fallback;
    double min;
    double max;
};
constexpr NumberSpec TAKEOFF_HEIGHT{3.0, 0.5, 120.0};
constexpr NumberSpec YAW_DEGREES{15.0, -180.0, 180.0};
constexpr NumberSpec CIRCLE_RADIUS{10.0, 1.0, 200.0};
constexpr NumberSpec FORMATION_HEIGHT{20.0, 0.5, 120.0};
constexpr NumberSpec FORMATION_SPACING{5.0, 1.0, 200.0};
/// V 字翼上間距下限 2 m：與 Agent 預設 formation_min_distance_m 相同，過小的隊形會被 Agent 拒絕
constexpr NumberSpec V_SHAPE_SPACING{5.0, 2.0, 200.0};

/// ROS_DOMAIN_ID 上限（CycloneDDS 埠號計算可用範圍）
constexpr int MAX_DOMAIN_ID = 232;

/// 顯示原點緯度上限（度）：OSM 地圖（Web Mercator）只能顯示到約 ±85.05 度
constexpr double MAX_ORIGIN_LATITUDE_DEGREES = 85.0;
constexpr double MAX_ORIGIN_LONGITUDE_DEGREES = 180.0;
/// 座標文字的小數位數：1e-7 度約 1 公分，與 PX4 GPS 精度相當
constexpr int COORDINATE_DECIMALS = 7;

/// 緯度、經度
struct Coordinate
{
    double latitude = 0.0;
    double longitude = 0.0;
};

/**
 * @brief 由兩個數值組成座標並檢查範圍
 * @param latitude 緯度
 * @param is_latitude_number 緯度是否為數值
 * @param longitude 經度
 * @param is_longitude_number 經度是否為數值
 * @return 座標；任一值無效或超出範圍時為 std::nullopt
 */
std::optional<Coordinate> make_coordinate(double latitude, bool is_latitude_number, double longitude,
                                          bool is_longitude_number)
{
    const bool is_valid = is_latitude_number && is_longitude_number && std::isfinite(latitude)
                          && std::isfinite(longitude) && std::abs(latitude) <= MAX_ORIGIN_LATITUDE_DEGREES
                          && std::abs(longitude) <= MAX_ORIGIN_LONGITUDE_DEGREES;
    return is_valid ? std::optional<Coordinate>(Coordinate{latitude, longitude}) : std::nullopt;
}

/**
 * @brief 解析「緯度, 經度」文字；逗號與空白皆可作為分隔
 * @param text 座標文字
 * @return 座標；格式錯誤或超出範圍時為 std::nullopt
 */
std::optional<Coordinate> parse_coordinate(const QString& text)
{
    static const QRegularExpression SEPARATOR(QStringLiteral("[,\\s]+"));
    const QStringList parts = text.trimmed().split(SEPARATOR, Qt::SkipEmptyParts);
    if (parts.size() != 2)
    {
        return std::nullopt;
    }
    bool is_latitude_number = false;
    bool is_longitude_number = false;
    const double latitude = parts.at(0).toDouble(&is_latitude_number);
    const double longitude = parts.at(1).toDouble(&is_longitude_number);
    return make_coordinate(latitude, is_latitude_number, longitude, is_longitude_number);
}

/**
 * @brief 讀取保存的座標
 * @param settings 設定
 * @param latitude_key 緯度鍵
 * @param longitude_key 經度鍵
 * @return 座標；未保存或無效時為 std::nullopt
 */
std::optional<Coordinate> read_coordinate(const QSettings& settings, const QString& latitude_key,
                                          const QString& longitude_key)
{
    bool is_latitude_number = false;
    bool is_longitude_number = false;
    const double latitude = settings.value(latitude_key).toDouble(&is_latitude_number);
    const double longitude = settings.value(longitude_key).toDouble(&is_longitude_number);
    return make_coordinate(latitude, is_latitude_number, longitude, is_longitude_number);
}

/**
 * @brief 座標轉為「緯度, 經度」文字
 * @param coordinate 座標
 * @return 文字；沒有座標時為空字串
 */
QString format_coordinate(const std::optional<Coordinate>& coordinate)
{
    if (!coordinate.has_value())
    {
        return QString();
    }
    return QStringLiteral("%1, %2")
        .arg(coordinate->latitude, 0, 'f', COORDINATE_DECIMALS)
        .arg(coordinate->longitude, 0, 'f', COORDINATE_DECIMALS);
}

}  // namespace

AppSettings::AppSettings(const QString& file_path, QObject* parent)
    : QObject(parent),
      settings_(file_path.isEmpty() ? std::make_unique<QSettings>()
                                    : std::make_unique<QSettings>(file_path, QSettings::IniFormat))
{
}

double AppSettings::read_double(const QString& key, double fallback, double min, double max) const
{
    bool is_number = false;
    const double value = settings_->value(key).toDouble(&is_number);
    if (!is_number || !std::isfinite(value) || value < min || value > max)
    {
        return fallback;
    }
    return value;
}

void AppSettings::write_double(const QString& key, double value, double min, double max)
{
    // 不合法的值不保存，保留原值（網頁版 NumberInput 失焦時還原的行為）
    if (!std::isfinite(value) || value < min || value > max)
    {
        return;
    }
    settings_->setValue(key, value);
    emit changed();
}

bool AppSettings::auto_follow() const
{
    return settings_->value(KEY_AUTO_FOLLOW, DEFAULT_AUTO_FOLLOW).toBool();
}

void AppSettings::set_auto_follow(bool is_enabled)
{
    settings_->setValue(KEY_AUTO_FOLLOW, is_enabled);
    emit changed();
}

bool AppSettings::show_attitude() const
{
    return settings_->value(KEY_SHOW_ATTITUDE, DEFAULT_SHOW_ATTITUDE).toBool();
}

void AppSettings::set_show_attitude(bool is_enabled)
{
    settings_->setValue(KEY_SHOW_ATTITUDE, is_enabled);
    emit changed();
}

double AppSettings::takeoff_height() const
{
    return read_double(KEY_TAKEOFF_HEIGHT, TAKEOFF_HEIGHT.fallback, TAKEOFF_HEIGHT.min, TAKEOFF_HEIGHT.max);
}

void AppSettings::set_takeoff_height(double value)
{
    write_double(KEY_TAKEOFF_HEIGHT, value, TAKEOFF_HEIGHT.min, TAKEOFF_HEIGHT.max);
}

double AppSettings::yaw_degrees() const
{
    return read_double(KEY_YAW_DEGREES, YAW_DEGREES.fallback, YAW_DEGREES.min, YAW_DEGREES.max);
}

void AppSettings::set_yaw_degrees(double value)
{
    write_double(KEY_YAW_DEGREES, value, YAW_DEGREES.min, YAW_DEGREES.max);
}

double AppSettings::form_circle_radius() const
{
    return read_double(KEY_CIRCLE_RADIUS, CIRCLE_RADIUS.fallback, CIRCLE_RADIUS.min, CIRCLE_RADIUS.max);
}

void AppSettings::set_form_circle_radius(double value)
{
    write_double(KEY_CIRCLE_RADIUS, value, CIRCLE_RADIUS.min, CIRCLE_RADIUS.max);
}

double AppSettings::form_circle_height() const
{
    return read_double(KEY_CIRCLE_HEIGHT, FORMATION_HEIGHT.fallback, FORMATION_HEIGHT.min, FORMATION_HEIGHT.max);
}

void AppSettings::set_form_circle_height(double value)
{
    write_double(KEY_CIRCLE_HEIGHT, value, FORMATION_HEIGHT.min, FORMATION_HEIGHT.max);
}

double AppSettings::form_linear_spacing() const
{
    return read_double(KEY_LINEAR_SPACING, FORMATION_SPACING.fallback, FORMATION_SPACING.min, FORMATION_SPACING.max);
}

void AppSettings::set_form_linear_spacing(double value)
{
    write_double(KEY_LINEAR_SPACING, value, FORMATION_SPACING.min, FORMATION_SPACING.max);
}

double AppSettings::form_linear_height() const
{
    return read_double(KEY_LINEAR_HEIGHT, FORMATION_HEIGHT.fallback, FORMATION_HEIGHT.min, FORMATION_HEIGHT.max);
}

void AppSettings::set_form_linear_height(double value)
{
    write_double(KEY_LINEAR_HEIGHT, value, FORMATION_HEIGHT.min, FORMATION_HEIGHT.max);
}

double AppSettings::form_vert_spacing() const
{
    return read_double(KEY_VERT_SPACING, FORMATION_SPACING.fallback, FORMATION_SPACING.min, FORMATION_SPACING.max);
}

void AppSettings::set_form_vert_spacing(double value)
{
    write_double(KEY_VERT_SPACING, value, FORMATION_SPACING.min, FORMATION_SPACING.max);
}

double AppSettings::form_v_shape_spacing() const
{
    return read_double(KEY_V_SHAPE_SPACING, V_SHAPE_SPACING.fallback, V_SHAPE_SPACING.min, V_SHAPE_SPACING.max);
}

void AppSettings::set_form_v_shape_spacing(double value)
{
    write_double(KEY_V_SHAPE_SPACING, value, V_SHAPE_SPACING.min, V_SHAPE_SPACING.max);
}

double AppSettings::form_v_shape_height() const
{
    return read_double(KEY_V_SHAPE_HEIGHT, FORMATION_HEIGHT.fallback, FORMATION_HEIGHT.min, FORMATION_HEIGHT.max);
}

void AppSettings::set_form_v_shape_height(double value)
{
    write_double(KEY_V_SHAPE_HEIGHT, value, FORMATION_HEIGHT.min, FORMATION_HEIGHT.max);
}

bool AppSettings::is_site_configured() const
{
    return settings_->value(KEY_SITE_CONFIGURED, false).toBool();
}

int AppSettings::domain_id() const
{
    bool is_number = false;
    const int value = settings_->value(KEY_DOMAIN_ID).toInt(&is_number);
    return (is_number && value >= 0 && value <= MAX_DOMAIN_ID) ? value : 0;
}

QString AppSettings::network_interface() const
{
    return settings_->value(KEY_INTERFACE).toString().trimmed();
}

QStringList AppSettings::peers() const
{
    return settings_->value(KEY_PEERS).toStringList();
}

bool AppSettings::is_multicast_allowed() const
{
    return settings_->value(KEY_MULTICAST, true).toBool();
}

int AppSettings::participant_index() const
{
    bool is_number = false;
    const int value = settings_->value(KEY_PARTICIPANT_INDEX).toInt(&is_number);
    const bool is_valid = is_number && value >= 0 && value < static_cast<int>(link::PARTICIPANT_INDEX_LIMIT);
    return is_valid ? value : AUTO_PARTICIPANT_INDEX;
}

QString AppSettings::saveSite(int domain_id, const QString& network_interface, const QStringList& peers,
                              bool is_multicast_allowed, int participant_index)
{
    if (domain_id < 0 || domain_id > MAX_DOMAIN_ID)
    {
        return QStringLiteral("Domain ID 必須介於 0 到 %1").arg(MAX_DOMAIN_ID);
    }
    QStringList clean_peers;
    for (const QString& peer : peers)
    {
        const QString trimmed = peer.trimmed();
        if (trimmed.isEmpty())
        {
            continue;
        }
        // 允許 IPv4 或主機名稱；IPv4 格式錯誤時提早指出，避免 DDS 靜默失敗
        const bool looks_numeric = trimmed.front().isDigit();
        if (looks_numeric && QHostAddress(trimmed).protocol() != QAbstractSocket::IPv4Protocol)
        {
            return QStringLiteral("Agent 位址格式錯誤：%1").arg(trimmed);
        }
        clean_peers.append(trimmed);
    }
    if (!is_multicast_allowed && clean_peers.isEmpty())
    {
        return QStringLiteral("關閉 multicast 時至少需要一個 Agent 位址");
    }
    const bool is_index_valid = participant_index == AUTO_PARTICIPANT_INDEX
                                || (participant_index >= 0
                                    && participant_index < static_cast<int>(link::PARTICIPANT_INDEX_LIMIT));
    if (!is_index_valid)
    {
        return QStringLiteral("Participant index 必須為自動或 0 到 119");
    }

    settings_->setValue(KEY_DOMAIN_ID, domain_id);
    settings_->setValue(KEY_INTERFACE, network_interface.trimmed());
    settings_->setValue(KEY_PEERS, clean_peers);
    settings_->setValue(KEY_MULTICAST, is_multicast_allowed);
    settings_->setValue(KEY_PARTICIPANT_INDEX, participant_index);
    settings_->setValue(KEY_SITE_CONFIGURED, true);
    settings_->sync();
    emit siteChanged();
    return QString();
}

link::DdsNetworkConfig AppSettings::network_config() const
{
    link::DdsNetworkConfig config;
    config.domain_id = static_cast<uint32_t>(domain_id());
    config.network_interface = network_interface().toStdString();
    config.is_multicast_allowed = is_multicast_allowed();
    for (const QString& peer : peers())
    {
        config.peers.push_back(peer.toStdString());
    }
    const int index = participant_index();
    if (index != AUTO_PARTICIPANT_INDEX)
    {
        config.participant_index = static_cast<uint32_t>(index);
    }
    return config;
}

bool AppSettings::is_display_origin_enabled() const
{
    return display_origin().is_enabled;
}

QString AppSettings::source_origin_text() const
{
    return format_coordinate(read_coordinate(*settings_, KEY_ORIGIN_SOURCE_LATITUDE, KEY_ORIGIN_SOURCE_LONGITUDE));
}

QString AppSettings::display_origin_text() const
{
    return format_coordinate(read_coordinate(*settings_, KEY_ORIGIN_DISPLAY_LATITUDE, KEY_ORIGIN_DISPLAY_LONGITUDE));
}

ui::DisplayOrigin AppSettings::display_origin() const
{
    const auto source = read_coordinate(*settings_, KEY_ORIGIN_SOURCE_LATITUDE, KEY_ORIGIN_SOURCE_LONGITUDE);
    const auto display = read_coordinate(*settings_, KEY_ORIGIN_DISPLAY_LATITUDE, KEY_ORIGIN_DISPLAY_LONGITUDE);
    ui::DisplayOrigin origin;
    if (!source.has_value() || !display.has_value())
    {
        return origin;
    }
    origin.is_enabled = settings_->value(KEY_ORIGIN_ENABLED, false).toBool();
    origin.source_latitude = source->latitude;
    origin.source_longitude = source->longitude;
    origin.display_latitude = display->latitude;
    origin.display_longitude = display->longitude;
    return origin;
}

QString AppSettings::saveDisplayOrigin(bool is_enabled, const QString& source_text, const QString& display_text)
{
    const bool has_source = !source_text.trimmed().isEmpty();
    const bool has_display = !display_text.trimmed().isEmpty();
    const auto source = parse_coordinate(source_text);
    const auto display = parse_coordinate(display_text);
    if (has_source && !source.has_value())
    {
        return QStringLiteral("原始座標格式錯誤：請輸入「緯度, 經度」，緯度需介於 ±85 度");
    }
    if (has_display && !display.has_value())
    {
        return QStringLiteral("地圖座標格式錯誤：請輸入「緯度, 經度」，緯度需介於 ±85 度");
    }
    if (is_enabled && (!has_source || !has_display))
    {
        return QStringLiteral("啟用前請填入原始座標與地圖座標");
    }

    if (source.has_value())
    {
        settings_->setValue(KEY_ORIGIN_SOURCE_LATITUDE, source->latitude);
        settings_->setValue(KEY_ORIGIN_SOURCE_LONGITUDE, source->longitude);
    }
    if (display.has_value())
    {
        settings_->setValue(KEY_ORIGIN_DISPLAY_LATITUDE, display->latitude);
        settings_->setValue(KEY_ORIGIN_DISPLAY_LONGITUDE, display->longitude);
    }
    settings_->setValue(KEY_ORIGIN_ENABLED, is_enabled);
    settings_->sync();
    emit displayOriginChanged();
    return QString();
}

void AppSettings::resetUiSettings()
{
    settings_->setValue(KEY_AUTO_FOLLOW, DEFAULT_AUTO_FOLLOW);
    settings_->setValue(KEY_SHOW_ATTITUDE, DEFAULT_SHOW_ATTITUDE);
    emit changed();
}

}  // namespace gcs
