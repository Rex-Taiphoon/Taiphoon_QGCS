/**
 * @file message_conversion.hpp
 * @brief idlc 產生的 DDS 型別 → 核心型別的轉換
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 核心模組不依賴 DDS 型別（ADR-0015），所有轉換集中於此並以單元測試覆蓋。
 */
#pragma once

#include "core/agent_types.hpp"

#include "ros_common.h"
#include "taiphoon_agent.h"

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

}  // namespace gcs::link
