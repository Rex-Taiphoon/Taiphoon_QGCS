# =============================================================================
# @file src/link/CMakeLists.txt
# @brief CycloneDDS ROS2 線上相容層（ADR-0014）
# @author Taiphoon GCS Team
# @date 2026-10-12
# =============================================================================
add_library(gcs_link STATIC
    ros_naming.hpp
    ros_naming.cpp
    qos_profile.hpp
    qos_profile.cpp
    dds_config.hpp
    dds_config.cpp
    message_conversion.hpp
    message_conversion.cpp
    cyclone_agent_link.hpp
    cyclone_agent_link.cpp
)

target_include_directories(gcs_link PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gcs_link PUBLIC gcs_core gcs_agent_types CycloneDDS::ddsc)
