# =============================================================================
# @file Dependencies.cmake
# @brief 第三方相依（版本固定，升級需依 AGENTS.md 先取得同意）
# @author Taiphoon GCS Team
# @date 2026-10-12
# =============================================================================
include(FetchContent)

# -----------------------------------------------------------------------------
# CycloneDDS：與 Agent 的 ROS2 Humble rmw_cyclonedds_cpp 同一系列（ADR-0014）
# -----------------------------------------------------------------------------
set(GCS_CYCLONEDDS_VERSION "0.10.5")

FetchContent_Declare(cyclonedds
    GIT_REPOSITORY https://github.com/eclipse-cyclonedds/cyclonedds.git
    GIT_TAG        ${GCS_CYCLONEDDS_VERSION}
    GIT_SHALLOW    TRUE
    # 原始碼內部分安全性測試檔路徑超過 Windows 預設長度限制。
    GIT_CONFIG     core.longpaths=true
)

# 只需要 C 函式庫與 idlc；安全性、共享記憶體與範例不使用。
# BUILD_TESTING 會被 CycloneDDS 讀取，本專案測試改由 GCS_BUILD_TESTS 控制。
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_IDLC ON CACHE BOOL "" FORCE)
set(BUILD_DDSPERF OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(ENABLE_SSL OFF CACHE BOOL "" FORCE)
set(ENABLE_SECURITY OFF CACHE BOOL "" FORCE)
set(ENABLE_SHM OFF CACHE BOOL "" FORCE)
set(ENABLE_ICEORYX OFF CACHE BOOL "" FORCE)
set(ENABLE_LTO OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(cyclonedds)

# -----------------------------------------------------------------------------
# GoogleTest
# -----------------------------------------------------------------------------
if(GCS_BUILD_TESTS)
    set(GCS_GOOGLETEST_VERSION "v1.15.2")

    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        ${GCS_GOOGLETEST_VERSION}
        GIT_SHALLOW    TRUE
    )

    # MSVC 需與主程式使用相同的 C 執行階段（/MD）。
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)
    include(GoogleTest)
endif()
