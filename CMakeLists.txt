# =============================================================================
# @file CMakeLists.txt
# @brief Taiphoon_QGCS 頂層建置設定
# @author Taiphoon GCS Team
# @date 2026-10-12
#
# 目標：gcs_core（核心與模組，只依賴 QtCore）、gcs_link（CycloneDDS 相容層）、
# gcs_wiring（組裝根）、taiphoon_qgcs（桌面程式）；架構見 docs/qt-cross-platform-development.md。
# =============================================================================
cmake_minimum_required(VERSION 3.25)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" GCS_VERSION_FULL)
string(STRIP "${GCS_VERSION_FULL}" GCS_VERSION_FULL)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" GCS_VERSION_NUMBER "${GCS_VERSION_FULL}")

project(TaiphoonQgcs
    VERSION ${GCS_VERSION_NUMBER}
    DESCRIPTION "Taiphoon Qt 原生地面控制站"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 執行檔與 DLL 輸出到同一目錄，Windows 上不需額外複製 ddsc.dll 即可執行。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# 原始碼含繁體中文字串，MSVC 必須明確以 UTF-8 解讀。
add_compile_options(
    "$<$<C_COMPILER_ID:MSVC>:/utf-8>"
    "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
)

option(GCS_BUILD_APP "建置 Qt 桌面程式" ON)
option(GCS_BUILD_TOOLS "建置開發工具（dds_probe）" ON)
option(GCS_BUILD_TESTS "建置單元測試" ON)

# Qt 版本固定（ADR-0013）；不同版本一律拒絕，避免開發者與 CI 結果不一致。
set(GCS_QT_VERSION "6.11.2")
find_package(Qt6 ${GCS_QT_VERSION} EXACT REQUIRED COMPONENTS Core)
qt_standard_project_setup(REQUIRES ${GCS_QT_VERSION})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

# 放在第三方相依之後，只影響本專案目標：windows.h 的 ERROR、min、max 巨集
# 會與介面規格的 FlightState::ERROR 及標準函式庫衝突。
if(WIN32)
    add_compile_definitions(NOMINMAX NOGDI WIN32_LEAN_AND_MEAN)
endif()

add_subdirectory(idl)

add_subdirectory(src/core)
add_subdirectory(src/link)
add_subdirectory(src/app)

if(GCS_BUILD_TOOLS)
    add_subdirectory(tools/dds_probe)
    add_subdirectory(tools/gcs_headless)
endif()

if(GCS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
