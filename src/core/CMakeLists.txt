# =============================================================================
# @file src/core/CMakeLists.txt
# @brief gcs_core：核心型別、介面與功能模組（只依賴 QtCore，ADR-0015）
# @author Taiphoon GCS Team
# @date 2026-10-12
#
# 模組原始碼位於 src/modules/，與 core 一起編譯成單一函式庫；
# 模組之間的連線不在此處，由 src/app/gcs_wiring.cpp 建立。
# =============================================================================
find_package(Qt6 ${GCS_QT_VERSION} EXACT REQUIRED COMPONENTS Core)

set(GCS_MODULES_DIR "${PROJECT_SOURCE_DIR}/src/modules")

# Q_OBJECT 標頭必須列入，AUTOMOC 才會產生對應的 moc 程式碼。
add_library(gcs_core STATIC
    agent_types.hpp
    agent_types.cpp
    command_protocol.hpp
    command_protocol.cpp
    gcs_module.hpp
    i_agent_link.hpp
    module_manager.hpp
    module_manager.cpp
    ${GCS_MODULES_DIR}/command/command_module.hpp
    ${GCS_MODULES_DIR}/command/command_module.cpp
    ${GCS_MODULES_DIR}/fleet/fleet_module.hpp
    ${GCS_MODULES_DIR}/fleet/fleet_module.cpp
    ${GCS_MODULES_DIR}/guard/guard_module.hpp
    ${GCS_MODULES_DIR}/guard/guard_module.cpp
    ${GCS_MODULES_DIR}/guard/guard_rules.hpp
    ${GCS_MODULES_DIR}/guard/guard_rules.cpp
    ${GCS_MODULES_DIR}/lifecycle/agent_lifecycle_module.hpp
    ${GCS_MODULES_DIR}/lifecycle/agent_lifecycle_module.cpp
    ${GCS_MODULES_DIR}/mission/mission_module.hpp
    ${GCS_MODULES_DIR}/mission/mission_module.cpp
    ${GCS_MODULES_DIR}/telemetry/telemetry_module.hpp
    ${GCS_MODULES_DIR}/telemetry/telemetry_module.cpp
)

target_include_directories(gcs_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gcs_core PUBLIC Qt6::Core)
