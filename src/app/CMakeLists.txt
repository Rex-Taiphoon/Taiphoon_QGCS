# =============================================================================
# @file src/app/CMakeLists.txt
# @brief 組裝根、核心執行環境、介面橋接與 Qt 桌面程式
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

# 介面橋接與設定：不依賴 QtGui，可在無畫面環境單元測試
find_package(Qt6 ${GCS_QT_VERSION} EXACT REQUIRED COMPONENTS Network)
add_library(gcs_ui STATIC
    app_settings.hpp
    app_settings.cpp
    diagnostics_controller.hpp
    diagnostics_controller.cpp
    fleet_model.hpp
    fleet_model.cpp
    network_controller.hpp
    network_controller.cpp
    toast_model.hpp
    toast_model.cpp
    ui_bridge.hpp
    ui_bridge.cpp
    ui_format.hpp
    ui_format.cpp
    ui_wiring.hpp
    ui_wiring.cpp
)
target_link_libraries(gcs_ui PUBLIC gcs_runtime Qt6::Network)

if(NOT GCS_BUILD_APP)
    return()
endif()

find_package(Qt6 ${GCS_QT_VERSION} EXACT REQUIRED
    COMPONENTS Gui Qml Quick QuickControls2 Location Positioning
)

qt_add_executable(taiphoon_qgcs WIN32 MACOSX_BUNDLE
    main.cpp
)

# QML 檔集中放在 src/qml/，以別名放入模組根目錄
set(GCS_QML_DIR "${PROJECT_SOURCE_DIR}/src/qml")
set(GCS_QML_FILES
    ${GCS_QML_DIR}/Main.qml
    ${GCS_QML_DIR}/Theme.qml
    ${GCS_QML_DIR}/ActionButton.qml
    ${GCS_QML_DIR}/AttitudeIndicator.qml
    ${GCS_QML_DIR}/CommandPanel.qml
    ${GCS_QML_DIR}/ConfirmDialog.qml
    ${GCS_QML_DIR}/DebugInspector.qml
    ${GCS_QML_DIR}/FleetPanel.qml
    ${GCS_QML_DIR}/Icon.qml
    ${GCS_QML_DIR}/MapView.qml
    ${GCS_QML_DIR}/NumberField.qml
    ${GCS_QML_DIR}/SettingsDialog.qml
    ${GCS_QML_DIR}/SiteSetupDialog.qml
    ${GCS_QML_DIR}/SlideToConfirm.qml
    ${GCS_QML_DIR}/StatusBar.qml
    ${GCS_QML_DIR}/Toasts.qml
)
foreach(qml_file IN LISTS GCS_QML_FILES)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()
set_source_files_properties("${GCS_QML_DIR}/Theme.qml" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

set(GCS_LOGO "${PROJECT_SOURCE_DIR}/resources/taiphoon-logo.png")
set_source_files_properties("${GCS_LOGO}" PROPERTIES QT_RESOURCE_ALIAS "resources/taiphoon-logo.png")

qt_add_qml_module(taiphoon_qgcs
    URI TaiphoonQgcs
    VERSION 1.0
    QML_FILES ${GCS_QML_FILES}
    RESOURCES ${GCS_LOGO}
)

target_compile_definitions(taiphoon_qgcs PRIVATE GCS_VERSION_FULL="${GCS_VERSION_FULL}")

target_link_libraries(taiphoon_qgcs PRIVATE
    gcs_ui
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Location
    Qt6::Positioning
)

set_target_properties(taiphoon_qgcs PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER tw.com.taiphoon.qgcs
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
