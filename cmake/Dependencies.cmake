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

# iOS SDK 沒有 net/if_media.h，CycloneDDS 介面類型判斷的 Apple 分支無法編譯；只在 iOS 建置時修補（ADR-0017）
set(GCS_CYCLONEDDS_PATCH "")
if(IOS)
    set(GCS_CYCLONEDDS_PATCH
        PATCH_COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_LIST_DIR}/patches/cyclonedds-ios-ifaddrs.cmake")
endif()

FetchContent_Declare(cyclonedds
    GIT_REPOSITORY https://github.com/eclipse-cyclonedds/cyclonedds.git
    GIT_TAG        ${GCS_CYCLONEDDS_VERSION}
    GIT_SHALLOW    TRUE
    # 原始碼內部分安全性測試檔路徑超過 Windows 預設長度限制。
    GIT_CONFIG     core.longpaths=true
    ${GCS_CYCLONEDDS_PATCH}
)

# 只需要 C 函式庫與 idlc；安全性、共享記憶體與範例不使用。
# BUILD_TESTING 會被 CycloneDDS 讀取，本專案測試改由 GCS_BUILD_TESTS 控制。
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
# 交叉編譯（iOS）時編出的 idlc 無法在主機執行；CycloneDDS 的 idlc_generate 會改從 PATH 尋找
# 主機版 idlc，需先以 tools/build-host-idlc.sh 建置（ADR-0017）。
if(CMAKE_CROSSCOMPILING)
    set(BUILD_IDLC OFF CACHE BOOL "" FORCE)
else()
    set(BUILD_IDLC ON CACHE BOOL "" FORCE)
endif()
# iOS App 不能附帶一般動態函式庫，ddsc 改為靜態連結（本專案自己的函式庫皆已明確指定 STATIC）。
if(IOS)
    set(BUILD_SHARED_LIBS OFF)
endif()
set(BUILD_DDSPERF OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(ENABLE_SSL OFF CACHE BOOL "" FORCE)
set(ENABLE_SECURITY OFF CACHE BOOL "" FORCE)
set(ENABLE_SHM OFF CACHE BOOL "" FORCE)
set(ENABLE_ICEORYX OFF CACHE BOOL "" FORCE)
set(ENABLE_LTO OFF CACHE BOOL "" FORCE)
# CycloneDDS 自帶的 CPack 會尋找 MSVC 執行階段；本專案於 Packaging.cmake 自行處理，不使用其打包規則。
set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_NO_WARNINGS TRUE)

FetchContent_MakeAvailable(cyclonedds)

# idlc_generate 原本隨 idlc 子目錄載入；交叉編譯不編 idlc 時需自行載入
if(CMAKE_CROSSCOMPILING)
    include("${cyclonedds_SOURCE_DIR}/cmake/Modules/Generate.cmake")
endif()

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
