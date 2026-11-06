/**
 * @file geofence_samples.hpp
 * @brief 測試用地理圍籬樣本：以公尺描述的正方形圍籬與 GeofenceStatus
 * @author Taiphoon GCS Team
 * @date 2026-11-06
 */
#pragma once

#include "core/agent_types.hpp"
#include "core/geofence_geometry.hpp"

#include <vector>

namespace gcs::test
{

/// 測試場域中心（與航點測試相同的 25.0, 121.5）
inline const GeofenceVertexData FENCE_CENTER{25.0, 121.5};

/**
 * @brief 相對場域中心的位置
 * @param north_m 北向公尺
 * @param east_m 東向公尺
 * @return WGS84 位置
 */
inline GeofenceVertexData offset_from_center(double north_m, double east_m)
{
    return geofence_geometry::to_global({east_m, north_m}, FENCE_CENTER);
}

/**
 * @brief 以場域中心為中心的正方形圍籬（逆時針）
 * @param half_m 半邊長（公尺）
 * @return 四個頂點
 */
inline std::vector<GeofenceVertexData> square_fence(double half_m = 10.0)
{
    return {offset_from_center(-half_m, -half_m), offset_from_center(-half_m, half_m),
            offset_from_center(half_m, half_m), offset_from_center(half_m, -half_m)};
}

/**
 * @brief 圍籬設定
 * @param fence_id 版本號
 * @param vertices 頂點
 * @param margin_m margin
 * @return 設定
 */
inline GeofenceData make_fence(uint32_t fence_id, const std::vector<GeofenceVertexData>& vertices,
                               float margin_m = geofence_limits::DEFAULT_MARGIN_M)
{
    GeofenceData fence;
    fence.fence_id = fence_id;
    fence.action = GeofenceAction::SET;
    fence.margin_m = margin_m;
    fence.vertices = vertices;
    return fence;
}

/**
 * @brief Agent 回報的圍籬狀態
 * @param state 狀態
 * @param fence_id 目前版本（DISABLED 時應為 0）
 * @param last_request_id 最近處理的版本
 * @param result 最近處理的結果
 * @param vertices 頂點
 * @param margin_m margin
 * @return GeofenceStatus
 */
inline GeofenceStatusData make_fence_status(GeofenceState state, uint32_t fence_id, uint32_t last_request_id,
                                            GeofenceResult result = GeofenceResult::ACCEPTED,
                                            const std::vector<GeofenceVertexData>& vertices = square_fence(),
                                            float margin_m = geofence_limits::DEFAULT_MARGIN_M)
{
    GeofenceStatusData status;
    status.state = static_cast<uint8_t>(state);
    status.fence_id = fence_id;
    status.last_request_id = last_request_id;
    status.last_result = static_cast<uint8_t>(result);
    if (state != GeofenceState::DISABLED)
    {
        status.vertices = vertices;
        status.margin_m = margin_m;
    }
    return status;
}

}  // namespace gcs::test
