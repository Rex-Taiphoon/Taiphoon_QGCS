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

/// 介面文件第 8 節的 history depth（未另行規定的通道）
constexpr int32_t INTERFACE_HISTORY_DEPTH = 10;
/// 只需要最新一筆的狀態通道（mission/status、health）
constexpr int32_t LATEST_ONLY_DEPTH = 1;
/// events 的 history depth：短時間內連續發生的事件不會被後到的覆蓋
constexpr int32_t EVENTS_HISTORY_DEPTH = 20;

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
    case AgentChannel::MISSION_STATUS:
        // transient_local：QGCS 晚加入或重新啟動時立即取得 Agent 執行中任務的最後狀態；
        // Agent writer 也必須是 transient_local，否則 volatile writer 與本 reader 無法配對
        profile.reliability = Reliability::RELIABLE;
        profile.durability = Durability::TRANSIENT_LOCAL;
        profile.depth = LATEST_ONLY_DEPTH;
        break;
    case AgentChannel::HEALTH:
        profile.reliability = Reliability::RELIABLE;
        profile.depth = LATEST_ONLY_DEPTH;
        break;
    case AgentChannel::EVENTS:
        // volatile：連上前的舊事件不重播，避免把已排除的錯誤當成新錯誤
        profile.reliability = Reliability::RELIABLE;
        profile.depth = EVENTS_HISTORY_DEPTH;
        break;
    case AgentChannel::MISSION_PLAN:
        // volatile：Agent 重新啟動後不會收到舊航線；發布前確認已有訂閱者，並以 mission/status 確認收到
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
