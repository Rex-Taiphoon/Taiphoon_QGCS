/**
 * @file geofence_geometry.cpp
 * @brief 地理圍籬幾何實作
 * @author Taiphoon GCS Team
 * @date 2026-11-06
 */
#include "core/geofence_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gcs::geofence_geometry
{

namespace
{

/// WGS84 赤道半徑換算的每度弧長（公尺）
constexpr double METERS_PER_DEGREE = 6378137.0 * 3.14159265358979323846 / 180.0;
constexpr double DEGREES_TO_RADIANS = 3.14159265358979323846 / 180.0;
constexpr double MAX_LATITUDE_DEGREES = 90.0;
constexpr double MAX_LONGITUDE_DEGREES = 180.0;
/// 長度、面積與共線判斷的容許誤差（公尺）；遠小於定位精度
constexpr double LENGTH_EPSILON_M = 1e-3;
/// 預檢 margin 的寬容量（公尺）：兩端換算方式略有差異，邊界附近交給 Agent 判定，避免 QGCS 誤擋
constexpr double MARGIN_TOLERANCE_M = 0.05;

/**
 * @brief 二維外積 (b - a) × (c - a)
 * @param a 起點
 * @param b 第二點
 * @param c 第三點
 * @return 外積；正值表示 a→b→c 逆時針
 */
double cross(const LocalPoint& a, const LocalPoint& b, const LocalPoint& c)
{
    return (b.east_m - a.east_m) * (c.north_m - a.north_m) - (b.north_m - a.north_m) * (c.east_m - a.east_m);
}

/**
 * @brief 兩點距離
 * @param a 第一點
 * @param b 第二點
 * @return 距離（公尺）
 */
double distance(const LocalPoint& a, const LocalPoint& b)
{
    return std::hypot(b.east_m - a.east_m, b.north_m - a.north_m);
}

/**
 * @brief 點到線段的最短距離
 * @param p 點
 * @param a 線段起點
 * @param b 線段終點
 * @return 距離（公尺）
 */
double distance_to_segment(const LocalPoint& p, const LocalPoint& a, const LocalPoint& b)
{
    const double dx = b.east_m - a.east_m;
    const double dy = b.north_m - a.north_m;
    const double length_squared = dx * dx + dy * dy;
    if (length_squared < LENGTH_EPSILON_M * LENGTH_EPSILON_M)
    {
        return distance(p, a);
    }
    const double t =
        std::clamp(((p.east_m - a.east_m) * dx + (p.north_m - a.north_m) * dy) / length_squared, 0.0, 1.0);
    return distance(p, LocalPoint{a.east_m + t * dx, a.north_m + t * dy});
}

/**
 * @brief 共線的點 p 是否落在線段 ab 的範圍內
 * @param p 點
 * @param a 線段起點
 * @param b 線段終點
 * @return 是否在範圍內
 */
bool is_within_segment_box(const LocalPoint& p, const LocalPoint& a, const LocalPoint& b)
{
    return p.east_m <= std::max(a.east_m, b.east_m) + LENGTH_EPSILON_M
           && p.east_m >= std::min(a.east_m, b.east_m) - LENGTH_EPSILON_M
           && p.north_m <= std::max(a.north_m, b.north_m) + LENGTH_EPSILON_M
           && p.north_m >= std::min(a.north_m, b.north_m) - LENGTH_EPSILON_M;
}

/**
 * @brief 兩條線段是否相交（含端點接觸與共線重疊）
 * @param a1 第一條起點
 * @param a2 第一條終點
 * @param b1 第二條起點
 * @param b2 第二條終點
 * @return 是否相交
 */
bool segments_intersect(const LocalPoint& a1, const LocalPoint& a2, const LocalPoint& b1, const LocalPoint& b2)
{
    // 以長度正規化外積，讓共線判斷的門檻是距離（公尺）而非面積
    const auto side = [](const LocalPoint& a, const LocalPoint& b, const LocalPoint& c)
    {
        const double length = distance(a, b);
        const double value = length > LENGTH_EPSILON_M ? cross(a, b, c) / length : 0.0;
        if (std::abs(value) < LENGTH_EPSILON_M)
        {
            return 0;
        }
        return value > 0.0 ? 1 : -1;
    };
    const int d1 = side(b1, b2, a1);
    const int d2 = side(b1, b2, a2);
    const int d3 = side(a1, a2, b1);
    const int d4 = side(a1, a2, b2);
    if (d1 * d2 < 0 && d3 * d4 < 0)
    {
        return true;
    }
    return (d1 == 0 && is_within_segment_box(a1, b1, b2)) || (d2 == 0 && is_within_segment_box(a2, b1, b2))
           || (d3 == 0 && is_within_segment_box(b1, a1, a2)) || (d4 == 0 && is_within_segment_box(b2, a1, a2));
}

/**
 * @brief 頂點換算為局部平面座標（以第一點為原點）
 * @param vertices 頂點
 * @return 局部平面座標
 */
std::vector<LocalPoint> to_local_polygon(const std::vector<GeofenceVertexData>& vertices)
{
    std::vector<LocalPoint> points;
    if (vertices.empty())
    {
        return points;
    }
    points.reserve(vertices.size());
    for (const GeofenceVertexData& vertex : vertices)
    {
        points.push_back(to_local(vertex, vertices.front()));
    }
    return points;
}

/**
 * @brief 有號面積（逆時針為正）
 * @param points 局部平面頂點
 * @return 有號面積（平方公尺）
 */
double signed_area(const std::vector<LocalPoint>& points)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const LocalPoint& a = points.at(i);
        const LocalPoint& b = points.at((i + 1) % points.size());
        sum += a.east_m * b.north_m - b.east_m * a.north_m;
    }
    return sum / 2.0;
}

/**
 * @brief 局部平面多邊形是否自交
 * @param points 局部平面頂點
 * @return 是否自交
 */
bool is_local_self_intersecting(const std::vector<LocalPoint>& points)
{
    const std::size_t count = points.size();
    if (count < static_cast<std::size_t>(geofence_limits::MIN_VERTICES))
    {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        const LocalPoint& a1 = points.at(i);
        const LocalPoint& a2 = points.at((i + 1) % count);
        if (distance(a1, a2) < LENGTH_EPSILON_M)
        {
            return true;
        }
        // 相鄰邊只在共線折返時重疊
        const LocalPoint& next = points.at((i + 2) % count);
        const double next_length = distance(a2, next);
        if (next_length > LENGTH_EPSILON_M && std::abs(cross(a1, a2, next)) / next_length < LENGTH_EPSILON_M
            && ((a1.east_m - a2.east_m) * (next.east_m - a2.east_m)
                + (a1.north_m - a2.north_m) * (next.north_m - a2.north_m))
                   > 0.0)
        {
            return true;
        }
        for (std::size_t j = i + 2; j < count; ++j)
        {
            // 第一條邊與最後一條邊共用第一個頂點
            if (i == 0 && j == count - 1)
            {
                continue;
            }
            if (segments_intersect(a1, a2, points.at(j), points.at((j + 1) % count)))
            {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief 兩條直線（各以一點與方向表示）的交點
 * @param p 第一條線上的點
 * @param r 第一條線的方向
 * @param q 第二條線上的點
 * @param s 第二條線的方向
 * @param result 交點輸出（不可為 nullptr）
 * @return 是否有唯一交點（平行時為 false）
 */
bool line_intersection(const LocalPoint& p, const LocalPoint& r, const LocalPoint& q, const LocalPoint& s,
                       LocalPoint* result)
{
    if (result == nullptr)
    {
        return false;
    }
    const double denominator = r.east_m * s.north_m - r.north_m * s.east_m;
    const double scale = std::hypot(r.east_m, r.north_m) * std::hypot(s.east_m, s.north_m);
    if (scale < LENGTH_EPSILON_M || std::abs(denominator) / scale < LENGTH_EPSILON_M)
    {
        return false;
    }
    const double t = ((q.east_m - p.east_m) * s.north_m - (q.north_m - p.north_m) * s.east_m) / denominator;
    *result = LocalPoint{p.east_m + t * r.east_m, p.north_m + t * r.north_m};
    return true;
}

}  // namespace

LocalPoint to_local(const GeofenceVertexData& point, const GeofenceVertexData& origin)
{
    const double cos_latitude = std::cos(origin.latitude_deg * DEGREES_TO_RADIANS);
    return LocalPoint{(point.longitude_deg - origin.longitude_deg) * METERS_PER_DEGREE * cos_latitude,
                      (point.latitude_deg - origin.latitude_deg) * METERS_PER_DEGREE};
}

GeofenceVertexData to_global(const LocalPoint& point, const GeofenceVertexData& origin)
{
    const double cos_latitude = std::cos(origin.latitude_deg * DEGREES_TO_RADIANS);
    GeofenceVertexData result;
    result.latitude_deg = origin.latitude_deg + point.north_m / METERS_PER_DEGREE;
    result.longitude_deg = cos_latitude > LENGTH_EPSILON_M
                               ? origin.longitude_deg + point.east_m / (METERS_PER_DEGREE * cos_latitude)
                               : origin.longitude_deg;
    return result;
}

bool is_self_intersecting(const std::vector<GeofenceVertexData>& vertices)
{
    return is_local_self_intersecting(to_local_polygon(vertices));
}

double area_m2(const std::vector<GeofenceVertexData>& vertices)
{
    if (vertices.size() < static_cast<std::size_t>(geofence_limits::MIN_VERTICES))
    {
        return 0.0;
    }
    return std::abs(signed_area(to_local_polygon(vertices)));
}

QString validate(const std::vector<GeofenceVertexData>& vertices, float margin_m)
{
    const auto count = static_cast<int>(vertices.size());
    if (count < geofence_limits::MIN_VERTICES || count > geofence_limits::MAX_VERTICES)
    {
        return QStringLiteral("圍籬需要 %1～%2 個頂點（目前 %3 個）")
            .arg(geofence_limits::MIN_VERTICES)
            .arg(geofence_limits::MAX_VERTICES)
            .arg(count);
    }
    if (!std::isfinite(margin_m) || margin_m < geofence_limits::MIN_MARGIN_M
        || margin_m > geofence_limits::MAX_MARGIN_M)
    {
        return QStringLiteral("margin 需介於 %1～%2 m")
            .arg(static_cast<double>(geofence_limits::MIN_MARGIN_M))
            .arg(static_cast<double>(geofence_limits::MAX_MARGIN_M));
    }
    for (int i = 0; i < count; ++i)
    {
        const GeofenceVertexData& vertex = vertices.at(static_cast<std::size_t>(i));
        if (!std::isfinite(vertex.latitude_deg) || !std::isfinite(vertex.longitude_deg)
            || std::abs(vertex.latitude_deg) > MAX_LATITUDE_DEGREES
            || std::abs(vertex.longitude_deg) > MAX_LONGITUDE_DEGREES)
        {
            return QStringLiteral("第 %1 個頂點座標無效").arg(i + 1);
        }
    }
    if (is_self_intersecting(vertices))
    {
        return QStringLiteral("圍籬的邊互相交叉或頂點重疊");
    }
    if (area_m2(vertices) < MIN_AREA_M2)
    {
        return QStringLiteral("圍籬面積過小");
    }
    return {};
}

bool contains(const std::vector<GeofenceVertexData>& polygon, const GeofenceVertexData& point)
{
    if (polygon.size() < static_cast<std::size_t>(geofence_limits::MIN_VERTICES))
    {
        return false;
    }
    const std::vector<LocalPoint> points = to_local_polygon(polygon);
    const LocalPoint p = to_local(point, polygon.front());
    bool is_inside = false;
    for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++)
    {
        const LocalPoint& a = points.at(i);
        const LocalPoint& b = points.at(j);
        if (distance_to_segment(p, a, b) < LENGTH_EPSILON_M)
        {
            return true;
        }
        if ((a.north_m > p.north_m) != (b.north_m > p.north_m))
        {
            const double crossing_east =
                a.east_m + (p.north_m - a.north_m) * (b.east_m - a.east_m) / (b.north_m - a.north_m);
            if (p.east_m < crossing_east)
            {
                is_inside = !is_inside;
            }
        }
    }
    return is_inside;
}

double distance_to_boundary_m(const std::vector<GeofenceVertexData>& polygon, const GeofenceVertexData& point)
{
    if (polygon.size() < 2)
    {
        return 0.0;
    }
    const std::vector<LocalPoint> points = to_local_polygon(polygon);
    const LocalPoint p = to_local(point, polygon.front());
    double minimum = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        minimum = std::min(minimum, distance_to_segment(p, points.at(i), points.at((i + 1) % points.size())));
    }
    return minimum;
}

bool is_inside_with_margin(const std::vector<GeofenceVertexData>& polygon, double margin_m,
                           const GeofenceVertexData& point)
{
    return contains(polygon, point) && distance_to_boundary_m(polygon, point) >= margin_m - MARGIN_TOLERANCE_M;
}

std::vector<GeofenceVertexData> inset(const std::vector<GeofenceVertexData>& vertices, double margin_m)
{
    const std::size_t count = vertices.size();
    if (!std::isfinite(margin_m) || margin_m < LENGTH_EPSILON_M
        || count < static_cast<std::size_t>(geofence_limits::MIN_VERTICES) || is_self_intersecting(vertices))
    {
        return {};
    }
    const std::vector<LocalPoint> points = to_local_polygon(vertices);
    const double area = signed_area(points);
    // 逆時針時內側在邊的左方，順時針時在右方
    const double inward = area > 0.0 ? 1.0 : -1.0;
    std::vector<LocalPoint> shifted_starts;
    std::vector<LocalPoint> directions;
    for (std::size_t i = 0; i < count; ++i)
    {
        const LocalPoint& a = points.at(i);
        const LocalPoint& b = points.at((i + 1) % count);
        const LocalPoint direction{b.east_m - a.east_m, b.north_m - a.north_m};
        const double length = std::hypot(direction.east_m, direction.north_m);
        const LocalPoint normal{-direction.north_m / length * inward, direction.east_m / length * inward};
        shifted_starts.push_back(LocalPoint{a.east_m + normal.east_m * margin_m, a.north_m + normal.north_m * margin_m});
        directions.push_back(direction);
    }
    std::vector<LocalPoint> result;
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t previous = (i + count - 1) % count;
        LocalPoint corner;
        if (!line_intersection(shifted_starts.at(previous), directions.at(previous), shifted_starts.at(i),
                               directions.at(i), &corner))
        {
            // 相鄰邊共線：直接沿用平移後的頂點
            corner = shifted_starts.at(i);
        }
        result.push_back(corner);
    }
    // margin 大於場域時平移後的邊會越過對邊：多邊形整個翻轉（例如正方形轉 180 度，面積正負號不變），
    // 因此逐邊確認方向與原本相同
    for (std::size_t i = 0; i < count; ++i)
    {
        const LocalPoint& a = result.at(i);
        const LocalPoint& b = result.at((i + 1) % count);
        const double dot = (b.east_m - a.east_m) * directions.at(i).east_m
                           + (b.north_m - a.north_m) * directions.at(i).north_m;
        if (dot <= 0.0)
        {
            return {};
        }
    }
    const double result_area = signed_area(result);
    if (result_area * area <= 0.0 || std::abs(result_area) < MIN_AREA_M2 || is_local_self_intersecting(result))
    {
        return {};
    }
    std::vector<GeofenceVertexData> global;
    global.reserve(result.size());
    for (const LocalPoint& point : result)
    {
        global.push_back(to_global(point, vertices.front()));
    }
    return global;
}

}  // namespace gcs::geofence_geometry
