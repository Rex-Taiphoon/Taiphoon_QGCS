# =============================================================================
# @file src/app/CMakeLists.txt
# @brief 組裝根、核心執行環境與 Qt 桌面程式
# @author Taiphoon GCS Team
# @date 2026-10-12
# =============================================================================

# 組裝根獨立成函式庫，單元測試才能驗證「命令無法繞過 Guard」。
add_library(gcs_wiring STATIC
    gcs_wiring.hpp
    gcs_wiring.cpp
)
target_link_libraries(gcs_wiring PUBLIC gcs_core)

# 核心執行環境：桌面程式與無介面工具共用
add_library(gcs_runtime STATIC
    core_runtime.hpp
    core_runtime.cpp
)
target_link_libraries(gcs_runtime PUBLIC gcs_wiring gcs_link)

if(NOT GCS_BUILD_APP)
    return()
endif()

find_package(Qt6 ${GCS_QT_VERSION} EXACT REQUIRED COMPONENTS Gui Qml Quick QuickControls2)

qt_add_executable(taiphoon_qgcs WIN32 MACOSX_BUNDLE
    main.cpp
)

# QML 檔集中放在 src/qml/，以別名放入模組根目錄。
set(GCS_QML_FILES "${PROJECT_SOURCE_DIR}/src/qml/Main.qml")
foreach(qml_file IN LISTS GCS_QML_FILES)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()

qt_add_qml_module(taiphoon_qgcs
    URI TaiphoonQgcs
    VERSION 1.0
    QML_FILES ${GCS_QML_FILES}
)

target_compile_definitions(taiphoon_qgcs PRIVATE GCS_VERSION_FULL="${GCS_VERSION_FULL}")

target_link_libraries(taiphoon_qgcs PRIVATE
    gcs_runtime
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

set_target_properties(taiphoon_qgcs PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER tw.com.taiphoon.qgcs
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
