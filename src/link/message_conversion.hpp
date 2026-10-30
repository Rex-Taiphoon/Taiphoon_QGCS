/**
 * @file message_conversion.hpp
 * @brief idlc 產生的 DDS 型別與核心型別的轉換
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 核心模組不依賴 DDS 型別（ADR-0015），所有轉換集中於此並以單元測試覆蓋。
 */
#pragma once

#include "core/agent_types.hpp"

#include "ros_common.h"
#include "taiphoon_agent.h"

#include <vector>

namespace gcs::link
{

/** @brief AgentState 轉換 @param sample DDS 樣本 @return 核心型別 */
AgentStateData to_core(const taiphoon_agent_msg_dds__AgentState_& sample);

/** @brief AgentAttitude 轉換 @param sample DDS 樣本 @return 核心型別 */
AgentAttitudeData to_core(const taiphoon_agent_msg_dds__AgentAttitude_& sample);

/** @brief NedTarget 轉換 @param sample DDS 樣本 @return 核心型別 */
NedTargetData to_core(const taiphoon_agent_msg_dds__NedTarget_& sample);

/** @brief WaypointSequence 轉換 @param sample DDS 樣本 @return 核心型別 */
WaypointSequenceData to_core(const taiphoon_agent_msg_dds__WaypointSequence_& sample);

/**
 * @brief AgentTrajectory 轉換
 *
 * control_points 長度不是 3 的倍數時捨棄不完整的尾端點（介面文件第 3.6 節要求 3 的倍數）。
 *
 * @param sample DDS 樣本
 * @return 核心型別
 */
AgentTrajectoryData to_core(const taiphoon_agent_msg_dds__AgentTrajectory_& sample);

/** @brief WaypointMissionStatus 轉換 @param sample DDS 樣本 @return 核心型別 */
WaypointMissionStatusData to_core(const taiphoon_agent_msg_dds__WaypointMissionStatus_& sample);

/** @brief AgentEvent 轉換 @param sample DDS 樣本 @return 核心型別 */
AgentEventData to_core(const taiphoon_agent_msg_dds__AgentEvent_& sample);

/** @brief AgentHealth 轉換 @param sample DDS 樣本 @return 核心型別 */
AgentHealthData to_core(const taiphoon_agent_msg_dds__AgentHealth_& sample);

/**
 * @brief 建立要發布的 WaypointMission 樣本
 *
 * 樣本的航點序列指向 waypoint_buffer 的記憶體，且不由 DDS 釋放；
 * 寫入完成前 waypoint_buffer 不得修改或釋放。
 *
 * @param mission 航點任務
 * @param stamp 發布時間
 * @param waypoint_buffer 航點緩衝（不可為 nullptr）
 * @return DDS 樣本；waypoint_buffer 為 nullptr 時航點為空
 */
taiphoon_agent_msg_dds__WaypointMission_ to_dds(const WaypointMissionData& mission, const RosTime& stamp,
                                                std::vector<taiphoon_agent_msg_dds__MissionWaypoint_>* waypoint_buffer);

}  // namespace gcs::link
