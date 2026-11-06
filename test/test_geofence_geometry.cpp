/**
 * @file test_geofence_geometry.cpp
 * @brief 地理圍籬幾何：合法性、自交、包含、與邊界距離及 margin 內縮
 * @author Taiphoon GCS Team
 * @date 2026-11-06
 */
#include "core/geofence_geometry.hpp"
#include "fakes/geofence_samples.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace gcs;
using namespace gcs::geofence_geometry;
using gcs::test::offset_from_center;
using gcs::test::square_fence;

namespace
{

/**
 * @brief 以場域中心為圓心的正多邊形
 * @param count 頂點數
 * @param radius_m 半徑（公尺）
 * @return 頂點
 */
std::vector<GeofenceVertexData> regular_polygon(int count, double radius_m)
{
    std::vector<GeofenceVertexData> vertices;
    for (int i = 0; i < count; ++i)
    {
        const double angle = 2.0 * 3.14159265358979323846 * i / count;
        vertices.push_back(offset_from_center(radius_m * std::cos(angle), radius_m * std::sin(angle)));
    }
    return vertices;
}

}  // namespace

TEST(GeofenceGeometryTest, LocalConversionRoundTrips)
{
    const GeofenceVertexData point = offset_from_center(30.0, -40.0);
    const LocalPoint local = to_local(point, gcs::test::FENCE_CENTER);
    EXPECT_NEAR(local.north_m, 30.0, 1e-6);
    EXPECT_NEAR(local.east_m, -40.0, 1e-6);
    // 緯度 1e-5 度約 1.11 公尺
    const LocalPoint north = to_local({25.00001, 121.5}, gcs::test::FENCE_CENTER);
    EXPECT_NEAR(north.north_m, 1.113, 0.01);
}

TEST(GeofenceGeometryTest, ValidateChecksVertexCountMarginCoordinatesAndShape)
{
    EXPECT_TRUE(validate(square_fence(), 0.5F).isEmpty());
    EXPECT_TRUE(validate(regular_polygon(3, 10.0), 0.0F).isEmpty());
    EXPECT_TRUE(validate(regular_polygon(geofence_limits::MAX_VERTICES, 50.0), 10.0F).isEmpty());

    EXPECT_TRUE(validate(regular_polygon(2, 10.0), 0.5F).contains(QStringLiteral("3～64")));
    EXPECT_FALSE(validate(regular_polygon(geofence_limits::MAX_VERTICES + 1, 50.0), 0.5F).isEmpty());
    EXPECT_FALSE(validate({}, 0.5F).isEmpty());

    EXPECT_TRUE(validate(square_fence(), -0.1F).contains(QStringLiteral("margin")));
    EXPECT_FALSE(validate(square_fence(), 10.1F).isEmpty());
    EXPECT_FALSE(validate(square_fence(), std::numeric_limits<float>::quiet_NaN()).isEmpty());

    std::vector<GeofenceVertexData> invalid = square_fence();
    invalid[2].latitude_deg = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(validate(invalid, 0.5F).contains(QStringLiteral("第 3 個頂點")));
    invalid = square_fence();
    invalid[1].latitude_deg = 91.0;
    EXPECT_FALSE(validate(invalid, 0.5F).isEmpty());
    invalid = square_fence();
    invalid[0].longitude_deg = 181.0;
    EXPECT_FALSE(validate(invalid, 0.5F).isEmpty());

    // 面積幾乎為 0（三點近乎共線）
    const std::vector<GeofenceVertexData> sliver = {offset_from_center(0, 0), offset_from_center(0, 10),
                                                    offset_from_center(0.01, 5)};
    EXPECT_FALSE(validate(sliver, 0.0F).isEmpty());
}

TEST(GeofenceGeometryTest, SelfIntersectionDetected)
{
    EXPECT_FALSE(is_self_intersecting(square_fence()));
    EXPECT_FALSE(is_self_intersecting(regular_polygon(12, 20.0)));
    // 凹多邊形（L 形）不是自交
    const std::vector<GeofenceVertexData> l_shape = {offset_from_center(0, 0), offset_from_center(0, 20),
                                                     offset_from_center(10, 20), offset_from_center(10, 10),
                                                     offset_from_center(20, 10), offset_from_center(20, 0)};
    EXPECT_FALSE(is_self_intersecting(l_shape));
    EXPECT_TRUE(validate(l_shape, 1.0F).isEmpty());

    // 蝴蝶結
    const std::vector<GeofenceVertexData> bowtie = {offset_from_center(0, 0), offset_from_center(10, 10),
                                                    offset_from_center(0, 10), offset_from_center(10, 0)};
    EXPECT_TRUE(is_self_intersecting(bowtie));
    EXPECT_TRUE(validate(bowtie, 0.5F).contains(QStringLiteral("交叉")));

    // 重複頂點（長度 0 的邊）
    std::vector<GeofenceVertexData> duplicated = square_fence();
    duplicated.insert(duplicated.begin() + 1, duplicated[1]);
    EXPECT_TRUE(is_self_intersecting(duplicated));

    // 折返：相鄰邊共線且方向相反
    const std::vector<GeofenceVertexData> spike = {offset_from_center(0, 0), offset_from_center(0, 20),
                                                   offset_from_center(0, 10), offset_from_center(10, 10)};
    EXPECT_TRUE(is_self_intersecting(spike));

    // 不相鄰的邊碰到頂點
    const std::vector<GeofenceVertexData> touching = {offset_from_center(0, 0), offset_from_center(0, 20),
                                                      offset_from_center(10, 20), offset_from_center(0, 10),
                                                      offset_from_center(10, 0)};
    EXPECT_TRUE(is_self_intersecting(touching));
}

TEST(GeofenceGeometryTest, AreaContainsAndDistance)
{
    EXPECT_NEAR(area_m2(square_fence(10.0)), 400.0, 0.5);
    EXPECT_DOUBLE_EQ(area_m2(regular_polygon(2, 10.0)), 0.0);

    const auto fence = square_fence(10.0);
    EXPECT_TRUE(contains(fence, gcs::test::FENCE_CENTER));
    EXPECT_TRUE(contains(fence, offset_from_center(9.9, -9.9)));
    EXPECT_FALSE(contains(fence, offset_from_center(10.5, 0)));
    EXPECT_FALSE(contains(fence, offset_from_center(0, -30)));
    // 邊界上視為在內
    EXPECT_TRUE(contains(fence, offset_from_center(10.0, 0.0)));

    EXPECT_NEAR(distance_to_boundary_m(fence, gcs::test::FENCE_CENTER), 10.0, 0.01);
    EXPECT_NEAR(distance_to_boundary_m(fence, offset_from_center(8.0, 0.0)), 2.0, 0.01);
    EXPECT_NEAR(distance_to_boundary_m(fence, offset_from_center(13.0, 0.0)), 3.0, 0.01);

    EXPECT_TRUE(is_inside_with_margin(fence, 1.0, offset_from_center(8.9, 0.0)));
    EXPECT_FALSE(is_inside_with_margin(fence, 1.0, offset_from_center(9.5, 0.0)));
    EXPECT_FALSE(is_inside_with_margin(fence, 0.0, offset_from_center(10.5, 0.0)));
    // 允許數公分的換算誤差
    EXPECT_TRUE(is_inside_with_margin(fence, 1.0, offset_from_center(9.02, 0.0)));
}

TEST(GeofenceGeometryTest, InsetShrinksPolygonByMargin)
{
    const auto fence = square_fence(10.0);
    EXPECT_TRUE(inset(fence, 0.0).empty());

    const auto shrunk = inset(fence, 2.0);
    ASSERT_EQ(shrunk.size(), fence.size());
    EXPECT_NEAR(area_m2(shrunk), 256.0, 0.5);
    for (const auto& vertex : shrunk)
    {
        EXPECT_NEAR(distance_to_boundary_m(fence, vertex), 2.0, 0.05);
    }

    // 順時針頂點也內縮（不是外擴）
    std::vector<GeofenceVertexData> clockwise(fence.rbegin(), fence.rend());
    EXPECT_NEAR(area_m2(inset(clockwise, 2.0)), 256.0, 0.5);

    // margin 大於場域時無法內縮（翻轉後面積正負號相同，也要排除）
    EXPECT_TRUE(inset(fence, 11.0).empty());
    EXPECT_TRUE(inset(fence, 25.0).empty());
    EXPECT_FALSE(inset(fence, 9.0).empty());
}
