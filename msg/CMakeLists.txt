# =============================================================================
# @file CMakeLists.txt
# @brief taiphoon_agent 純介面包（GCS 內建副本）
#
# 此套件僅含 ROS2 介面定義（.msg），不含任何節點實作。
# 型別名稱必須維持 taiphoon_agent/msg/...，才能與 Taiphoon_Agent
# 發布端在 DDS 網路上互通（型別名稱 + 型別 hash 需完全一致）。
# 介面檔案來源：Taiphoon_Agent_Interfaces/（請依 AGENTS.md 規範同步）。
# =============================================================================

cmake_minimum_required(VERSION 3.8)
project(taiphoon_agent)

find_package(ament_cmake REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(rosidl_default_generators REQUIRED)

set(msg_files
  msg/AgentState.msg
  msg/AgentAttitude.msg
  msg/AgentTrajectory.msg
  msg/FormationEpoch.msg
  msg/NedTarget.msg
  msg/PlannerStatus.msg
  msg/RelativeAgentObservation.msg
  msg/Waypoint.msg
  msg/WaypointSequence.msg
)

rosidl_generate_interfaces(${PROJECT_NAME}
  ${msg_files}
  DEPENDENCIES builtin_interfaces
)

ament_export_dependencies(rosidl_default_runtime)
ament_export_dependencies(builtin_interfaces)

ament_package()
