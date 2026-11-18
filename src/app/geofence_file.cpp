/**
 * @file geofence_file.cpp
 * @brief 地理圍籬檔案讀寫實作
 * @author Taiphoon GCS Team
 * @date 2026-11-18
 */
#include "app/geofence_file.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cmath>

namespace gcs::geofence_file
{

namespace
{

const QString KEY_FORMAT = QStringLiteral("format");
const QString KEY_VERSION = QStringLiteral("version");
const QString KEY_MARGIN = QStringLiteral("margin_m");
const QString KEY_VERTICES = QStringLiteral("vertices");
const QString KEY_LATITUDE = QStringLiteral("latitude_deg");
const QString KEY_LONGITUDE = QStringLiteral("longitude_deg");

constexpr double MAX_LATITUDE_DEG = 90.0;
constexpr double MAX_LONGITUDE_DEG = 180.0;

}  // namespace

QByteArray to_json(const Content& content)
{
    QJsonArray vertices;
    for (const GeofenceVertexData& vertex : content.vertices)
    {
        vertices.append(QJsonObject{{KEY_LATITUDE, vertex.latitude_deg}, {KEY_LONGITUDE, vertex.longitude_deg}});
    }
    const QJsonObject root{
        {KEY_FORMAT, FORMAT_NAME},
        {KEY_VERSION, FORMAT_VERSION},
        {KEY_MARGIN, content.margin_m},
        {KEY_VERTICES, vertices},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QString from_json(const QByteArray& json, Content* content)
{
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        return QStringLiteral("不是有效的 JSON 檔案");
    }
    const QJsonObject root = document.object();
    if (root.value(KEY_FORMAT).toString() != FORMAT_NAME)
    {
        return QStringLiteral("不是 Taiphoon QGCS 的圍籬檔案");
    }
    const int version = root.value(KEY_VERSION).toInt();
    if (version < 1 || version > FORMAT_VERSION)
    {
        return QStringLiteral("不支援的圍籬檔案版本（%1），請更新 QGCS").arg(root.value(KEY_VERSION).toVariant().toString());
    }
    const QJsonValue margin = root.value(KEY_MARGIN);
    if (!margin.isDouble() || !std::isfinite(margin.toDouble()) || margin.toDouble() < geofence_limits::MIN_MARGIN_M
        || margin.toDouble() > geofence_limits::MAX_MARGIN_M)
    {
        return QStringLiteral("margin 必須在 %1～%2 公尺之間")
            .arg(geofence_limits::MIN_MARGIN_M, 0, 'f', 0)
            .arg(geofence_limits::MAX_MARGIN_M, 0, 'f', 0);
    }
    if (!root.value(KEY_VERTICES).isArray())
    {
        return QStringLiteral("檔案沒有頂點清單");
    }
    const QJsonArray vertices = root.value(KEY_VERTICES).toArray();
    if (vertices.size() < geofence_limits::MIN_VERTICES || vertices.size() > geofence_limits::MAX_VERTICES)
    {
        return QStringLiteral("頂點數必須在 %1～%2 之間（檔案有 %3 個）")
            .arg(geofence_limits::MIN_VERTICES)
            .arg(geofence_limits::MAX_VERTICES)
            .arg(vertices.size());
    }
    Content parsed;
    parsed.margin_m = margin.toDouble();
    for (qsizetype i = 0; i < vertices.size(); ++i)
    {
        const QJsonObject vertex = vertices.at(i).toObject();
        const QJsonValue latitude = vertex.value(KEY_LATITUDE);
        const QJsonValue longitude = vertex.value(KEY_LONGITUDE);
        if (!latitude.isDouble() || !longitude.isDouble() || !std::isfinite(latitude.toDouble())
            || !std::isfinite(longitude.toDouble()) || std::abs(latitude.toDouble()) > MAX_LATITUDE_DEG
            || std::abs(longitude.toDouble()) > MAX_LONGITUDE_DEG)
        {
            return QStringLiteral("第 %1 個頂點的座標無效").arg(i + 1);
        }
        parsed.vertices.push_back(GeofenceVertexData{latitude.toDouble(), longitude.toDouble()});
    }
    *content = parsed;
    return QString();
}

QString write(const QString& path, const Content& content)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        return QStringLiteral("無法寫入 %1：%2").arg(path, file.errorString());
    }
    file.write(to_json(content));
    if (!file.commit())
    {
        return QStringLiteral("無法寫入 %1：%2").arg(path, file.errorString());
    }
    return QString();
}

QString read(const QString& path, Content* content)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return QStringLiteral("無法開啟 %1：%2").arg(path, file.errorString());
    }
    if (file.size() > MAX_FILE_BYTES)
    {
        return QStringLiteral("檔案過大，不是圍籬檔案");
    }
    return from_json(file.readAll(), content);
}

}  // namespace gcs::geofence_file
