/**
 * @file agent_types.cpp
 * @brief 核心型別的 metatype 註冊
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "core/agent_types.hpp"

namespace gcs
{

void register_core_metatypes()
{
    qRegisterMetaType<AgentStateData>();
    qRegisterMetaType<AgentAttitudeData>();
    qRegisterMetaType<NedTargetData>();
    qRegisterMetaType<WaypointSequenceData>();
    qRegisterMetaType<AgentTrajectoryData>();
    qRegisterMetaType<CommandRequest>();
    qRegisterMetaType<CommandAck>();
    qRegisterMetaType<FleetSnapshot>();
    qRegisterMetaType<MissionPlan>();
    qRegisterMetaType<MissionStatus>();
}

}  // namespace gcs
