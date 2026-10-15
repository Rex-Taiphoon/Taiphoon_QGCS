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
    log_file.hpp
    log_file.cpp
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
    COMPONENTS Gui Qml Quick QuickControls2 Location Positioning ShaderTools
)

qt_add_executable(Taiphoon_QGCS WIN32 MACOSX_BUNDLE
    main.cpp
)

# 程式圖示（tools/make_icons.ps1 產生）：Windows 以資源檔嵌入 exe，macOS 放入 bundle Resources；
# iOS 需 asset catalog，尚未提供（ADR-0017）
if(WIN32)
    target_sources(Taiphoon_QGCS PRIVATE "${PROJECT_SOURCE_DIR}/packaging/windows/Taiphoon_QGCS.rc")
elseif(APPLE AND NOT IOS)
    set(GCS_MACOS_ICON "${PROJECT_SOURCE_DIR}/packaging/macos/Taiphoon_QGCS.icns")
    set_source_files_properties("${GCS_MACOS_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(Taiphoon_QGCS PRIVATE "${GCS_MACOS_ICON}")
endif()

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

# 狀態列橫式 Logo（白色去背）；resources/taiphoon-logo.png 僅作為 tools/make_icons.ps1 的圖示來源
set(GCS_LOGO "${PROJECT_SOURCE_DIR}/resources/taiphoon-wordmark.png")
set_source_files_properties("${GCS_LOGO}" PROPERTIES QT_RESOURCE_ALIAS "resources/taiphoon-wordmark.png")
# 視窗圖示（標題列；Linux 工作列）
set(GCS_WINDOW_ICON "${PROJECT_SOURCE_DIR}/resources/taiphoon-icon.png")
set_source_files_properties("${GCS_WINDOW_ICON}" PROPERTIES QT_RESOURCE_ALIAS "resources/taiphoon-icon.png")

# 介面字型：B612／B612 Mono（OFL-1.1，Airbus 駕駛艙顯示器字型）與中文備援 Noto Sans TC（OFL-1.1），
# 由 main.cpp 註冊
set(GCS_FONT_DIR "${PROJECT_SOURCE_DIR}/resources/fonts")
set(GCS_FONTS
    ${GCS_FONT_DIR}/B612-Regular.ttf
    ${GCS_FONT_DIR}/B612-Bold.ttf
    ${GCS_FONT_DIR}/B612Mono-Regular.ttf
    ${GCS_FONT_DIR}/B612Mono-Bold.ttf
    ${GCS_FONT_DIR}/NotoSansTC-VF.ttf
)
foreach(font_file IN LISTS GCS_FONTS)
    get_filename_component(font_name "${font_file}" NAME)
    set_source_files_properties("${font_file}" PROPERTIES QT_RESOURCE_ALIAS "resources/fonts/${font_name}")
endforeach()

qt_add_qml_module(Taiphoon_QGCS
    URI TaiphoonQgcs
    VERSION 1.0
    QML_FILES ${GCS_QML_FILES}
    RESOURCES ${GCS_LOGO} ${GCS_WINDOW_ICON} ${GCS_FONTS}
)

# 地圖深色化 shader：建置時以 qsb 編譯，執行期不需要 ShaderTools
qt_add_shaders(Taiphoon_QGCS "gcs_shaders"
    PREFIX "/qt/qml/TaiphoonQgcs"
    BASE "${GCS_QML_DIR}"
    FILES "${GCS_QML_DIR}/shaders/map_dark.frag"
)

target_compile_definitions(Taiphoon_QGCS PRIVATE GCS_VERSION_FULL="${GCS_VERSION_FULL}")

target_link_libraries(Taiphoon_QGCS PRIVATE
    gcs_ui
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Location
    Qt6::Positioning
)

set_target_properties(Taiphoon_QGCS PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER tw.com.taiphoon.qgcs
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
if(IOS)
    # 同一個 App 支援 iPhone（1）與 iPad（2）
    set_target_properties(Taiphoon_QGCS PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/packaging/ios/Info.plist.in"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    )
else()
    set_target_properties(Taiphoon_QGCS PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_ICON_FILE Taiphoon_QGCS.icns
    )
endif()

include(Packaging)
