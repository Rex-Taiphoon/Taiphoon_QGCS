/**
 * @file qos_profile.cpp
 * @brief Agent 通訊的 QoS 表實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "link/qos_profile.hpp"

namespace gcs::link
{

namespace
{

/// 介面文件第 8 節所有通道的 history depth
constexpr int32_t INTERFACE_HISTORY_DEPTH = 10;

/// reliable writer 等待傳送緩衝的最長時間；與 rmw_cyclonedds 預設相同
constexpr dds_duration_t RELIABLE_MAX_BLOCKING_TIME = DDS_MSECS(100);

}  // namespace

void DdsQosDeleter::operator()(dds_qos_t* qos) const
{
    if (qos != nullptr)
    {
        dds_delete_qos(qos);
    }
}

QosProfile qos_for(AgentChannel channel, EndpointRole role)
{
    QosProfile profile;
    profile.depth = INTERFACE_HISTORY_DEPTH;

    switch (channel)
    {
    case AgentChannel::NED_TARGET:
    case AgentChannel::WAYPOINTS:
        // 介面文件：reliable (pub) / best_effort (sub)
        profile.reliability = (role == EndpointRole::WRITER) ? Reliability::RELIABLE : Reliability::BEST_EFFORT;
        break;
    case AgentChannel::FORMATION_EPOCHS:
        // 介面文件：reliable + transient_local，讓新加入者取得目前 epoch
        profile.reliability = Reliability::RELIABLE;
        profile.durability = Durability::TRANSIENT_LOCAL;
        break;
    case AgentChannel::STATE:
    case AgentChannel::ATTITUDE:
    case AgentChannel::PLANNING_TRAJECTORY:
    case AgentChannel::PLANNING_STATUS:
    case AgentChannel::COMMAND:
    case AgentChannel::SHUTDOWN_SERVICE:
        profile.reliability = Reliability::RELIABLE;
        break;
    }
    return profile;
}

DdsQosPtr create_dds_qos(const QosProfile& profile, EndpointRole role)
{
    DdsQosPtr qos(dds_create_qos());
    if (!qos)
    {
        return qos;
    }

    if (profile.reliability == Reliability::RELIABLE)
    {
        dds_qset_reliability(qos.get(), DDS_RELIABILITY_RELIABLE, RELIABLE_MAX_BLOCKING_TIME);
    }
    else
    {
        dds_qset_reliability(qos.get(), DDS_RELIABILITY_BEST_EFFORT, 0);
    }

    dds_qset_durability(qos.get(), profile.durability == Durability::TRANSIENT_LOCAL
                                       ? DDS_DURABILITY_TRANSIENT_LOCAL
                                       : DDS_DURABILITY_VOLATILE);
    dds_qset_history(qos.get(), DDS_HISTORY_KEEP_LAST, profile.depth);

    if (role == EndpointRole::WRITER)
    {
        const dds_data_representation_id_t representations[] = {DDS_DATA_REPRESENTATION_XCDR1};
        dds_qset_data_representation(qos.get(), 1, representations);
    }
    return qos;
}

}  // namespace gcs::link
