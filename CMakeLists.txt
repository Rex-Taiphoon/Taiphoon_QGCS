cmake_minimum_required(VERSION 3.8)
project(taiphoon_gcs)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(taiphoon_agent REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)

# 收集原始碼
set(CORE_SOURCES
  src/core/module_manager.cpp
  src/core/ws_server.cpp
  src/core/ws_protocol.cpp
)

set(MODULE_SOURCES
  src/modules/telemetry/telemetry_module.cpp
  src/modules/fleet/fleet_module.cpp
  src/modules/guard/guard_module.cpp
  src/modules/command/command_module.cpp
  src/modules/lifecycle/agent_lifecycle_module.cpp
  src/modules/map/map_module.cpp
  src/modules/debug/debug_terminal_module.cpp
)

add_executable(taiphoon_gcs_node
  src/main.cpp
  ${CORE_SOURCES}
  ${MODULE_SOURCES}
)

target_include_directories(taiphoon_gcs_node PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(taiphoon_gcs_node
  rclcpp::rclcpp
  std_msgs::std_msgs__rosidl_typesupport_cpp
  ${Boost_LIBRARIES}
)

ament_target_dependencies(taiphoon_gcs_node
  rclcpp
  std_msgs
  std_srvs
  taiphoon_agent
  nlohmann_json
)

install(TARGETS taiphoon_gcs_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY
  ${CMAKE_CURRENT_SOURCE_DIR}/frontend/dist/
  DESTINATION share/${PROJECT_NAME}/frontend
  OPTIONAL
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_telemetry_serialization
    test/test_telemetry_serialization.cpp
    src/modules/telemetry/telemetry_module.cpp
  )
  target_include_directories(test_telemetry_serialization PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  ament_target_dependencies(test_telemetry_serialization rclcpp taiphoon_agent nlohmann_json)

  ament_add_gtest(test_event_bus test/test_event_bus.cpp)
  target_include_directories(test_event_bus PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

  ament_add_gtest(test_ws_protocol
    test/test_ws_protocol.cpp
    src/core/ws_protocol.cpp
  )
  target_include_directories(test_ws_protocol PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  ament_target_dependencies(test_ws_protocol nlohmann_json)

  ament_add_gtest(test_guard_transition
    test/test_guard_transition.cpp
    src/core/ws_protocol.cpp
    src/modules/guard/guard_module.cpp
  )
  target_include_directories(test_guard_transition PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  ament_target_dependencies(test_guard_transition nlohmann_json)

  ament_add_gtest(test_command_module
    test/test_command_module.cpp
    src/core/ws_protocol.cpp
    src/modules/command/command_module.cpp
  )
  target_include_directories(test_command_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(test_command_module
    rclcpp::rclcpp
    std_msgs::std_msgs__rosidl_typesupport_cpp
  )
  ament_target_dependencies(test_command_module
    rclcpp
    std_msgs
    taiphoon_agent
    nlohmann_json
  )

  ament_add_gtest(test_agent_lifecycle_module
    test/test_agent_lifecycle_module.cpp
    src/core/ws_protocol.cpp
    src/modules/lifecycle/agent_lifecycle_module.cpp
  )
  target_include_directories(test_agent_lifecycle_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(test_agent_lifecycle_module rclcpp::rclcpp)
  ament_target_dependencies(test_agent_lifecycle_module
    rclcpp
    std_srvs
    nlohmann_json
  )

  ament_add_gtest(test_fleet_module
    test/test_fleet_module.cpp
    src/modules/fleet/fleet_module.cpp
  )
  target_include_directories(test_fleet_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  ament_target_dependencies(test_fleet_module
    rclcpp
    taiphoon_agent
    nlohmann_json
  )
endif()

ament_package()
