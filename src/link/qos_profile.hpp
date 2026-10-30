/**
 * @file qos_profile.hpp
 * @brief Agent 通訊的 QoS 表（介面文件第 8 節）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 所有 GCS 端點的 QoS 只能由此表取得，並以單元測試核對介面文件。
 */
#pragma once

#include <dds/dds.h>

#include <cstdint>
#include <memory>

namespace gcs::link
{

/// GCS 使用的 Agent 通訊通道
enum class AgentChannel
{
    STATE,                 ///< /{ns}/state
    ATTITUDE,              ///< /{ns}/attitude
    NED_TARGET,            ///< /{ns}/ned_target
    WAYPOINTS,             ///< /{ns}/waypoints
    PLANNING_TRAJECTORY,   ///< /{ns}/planning/trajectory
    PLANNING_STATUS,       ///< /{ns}/planning/status
    FORMATION_EPOCHS,      ///< /uav_all_agent/swarm/formation_epochs
    COMMAND,               ///< /{ns}/command、/uav_all_agent/command
    SHUTDOWN_SERVICE,      ///< /{ns}/shutdown
    MISSION_PLAN,          ///< /{ns}/mission/plan
    MISSION_STATUS,        ///< /{ns}/mission/status
    EVENTS,                ///< /{ns}/events
    HEALTH                 ///< /{ns}/health
};

/// GCS 端點角色
enum class EndpointRole
{
    READER,
    WRITER
};

/// 可靠性
enum class Reliability
{
    BEST_EFFORT,
    RELIABLE
};

/// 持久性
enum class Durability
{
    VOLATILE,
    TRANSIENT_LOCAL
};

/// 單一端點的 QoS 設定（history 一律 keep_last）
struct QosProfile
{
    Reliability reliability = Reliability::RELIABLE;
    Durability durability = Durability::VOLATILE;
    int32_t depth = 10;
};

/// 釋放 dds_qos_t 的刪除器
struct DdsQosDeleter
{
    /** @brief 釋放 QoS 物件 @param qos 待釋放的 QoS */
    void operator()(dds_qos_t* qos) const;
};

/// RAII 持有的 dds_qos_t
using DdsQosPtr = std::unique_ptr<dds_qos_t, DdsQosDeleter>;

/**
 * @brief 依通道與角色查表取得 QoS
 * @param channel 通訊通道
 * @param role GCS 端點角色
 * @return QoS 設定
 */
QosProfile qos_for(AgentChannel channel, EndpointRole role);

/**
 * @brief 建立 CycloneDDS QoS 物件
 *
 * Writer 固定使用 XCDR1：ROS2 Humble rmw_cyclonedds 以 XCDR1 解碼，
 * XCDR2 的 8 位元組欄位對齊方式不同，會使 double 欄位錯位。
 *
 * @param profile QoS 設定
 * @param role 端點角色
 * @return QoS 物件；建立失敗時為空指標
 */
DdsQosPtr create_dds_qos(const QosProfile& profile, EndpointRole role);

}  // namespace gcs::link
