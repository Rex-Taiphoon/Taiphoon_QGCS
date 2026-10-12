# =============================================================================
# @file src/link/CMakeLists.txt
# @brief CycloneDDS ROS2 線上相容層（ADR-0014）
# @author Taiphoon GCS Team
# @date 2026-10-12
# =============================================================================
add_library(gcs_link STATIC
    ros_naming.cpp
    qos_profile.cpp
    dds_config.cpp
)

target_include_directories(gcs_link PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gcs_link PUBLIC gcs_agent_types CycloneDDS::ddsc)
