# =============================================================================
# @file Packaging.cmake
# @brief 安裝規則與 CPack：Windows NSIS／ZIP、macOS DMG、Linux TGZ（ADR-0013）
# @author Taiphoon GCS Team
# @date 2026-10-12
#
# 只安裝 component "gcs"：CycloneDDS 與 GoogleTest 以原始碼引入時也帶有自己的安裝規則，
# 不應進入使用者的安裝檔。用法：
#   cmake --install build/<preset> --component gcs --prefix <目錄>
#   cpack --config build/<preset>/CPackConfig.cmake
# =============================================================================
include(GNUInstallDirs)

set(GCS_INSTALL_COMPONENT gcs)

# -----------------------------------------------------------------------------
# 程式、CycloneDDS 共享函式庫與 Qt 部署
# -----------------------------------------------------------------------------
install(TARGETS Taiphoon_QGCS
    BUNDLE DESTINATION . COMPONENT ${GCS_INSTALL_COMPONENT}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${GCS_INSTALL_COMPONENT}
)

if(WIN32)
    install(FILES $<TARGET_FILE:ddsc> DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${GCS_INSTALL_COMPONENT})
elseif(NOT APPLE)
    install(FILES $<TARGET_FILE:ddsc> $<TARGET_SONAME_FILE:ddsc>
        DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${GCS_INSTALL_COMPONENT})
    set_target_properties(Taiphoon_QGCS PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
# macOS：macdeployqt 會把 ddsc.dylib 等非系統相依複製進 bundle 的 Frameworks

set(GCS_DEPLOY_ARGS
    TARGET Taiphoon_QGCS
    OUTPUT_SCRIPT GCS_DEPLOY_SCRIPT
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
)
if(WIN32)
    # MSVC 執行階段已以 app-local DLL 安裝，不需再附 vc_redist 安裝程式
    list(APPEND GCS_DEPLOY_ARGS DEPLOY_TOOL_OPTIONS --no-compiler-runtime)
endif()
qt_generate_deploy_qml_app_script(${GCS_DEPLOY_ARGS})
install(SCRIPT "${GCS_DEPLOY_SCRIPT}" COMPONENT ${GCS_INSTALL_COMPONENT})

# -----------------------------------------------------------------------------
# Windows：MSVC 執行階段以 app-local 方式隨程式安裝，使用者不需另外安裝 VC++ Redistributable
# -----------------------------------------------------------------------------
if(MSVC)
    # 環境變數為 Windows 路徑（反斜線），需轉為 CMake 路徑才能寫入安裝腳本
    file(TO_CMAKE_PATH "$ENV{VCToolsRedistDir}" GCS_VC_REDIST_DIR)
    file(GLOB GCS_VC_CRT_DIRS LIST_DIRECTORIES true "${GCS_VC_REDIST_DIR}/x64/Microsoft.VC*.CRT")
    list(LENGTH GCS_VC_CRT_DIRS GCS_VC_CRT_DIR_COUNT)
    if(GCS_VC_CRT_DIR_COUNT EQUAL 1)
        file(GLOB GCS_VC_CRT_DLLS "${GCS_VC_CRT_DIRS}/*.dll")
        install(FILES ${GCS_VC_CRT_DLLS} DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${GCS_INSTALL_COMPONENT})
    else()
        message(WARNING "找不到 MSVC 執行階段（VCToolsRedistDir），安裝檔將不含 VC++ 執行階段；"
                        "請在 VS 開發者環境（tools/dev-env.ps1）中 configure")
    endif()
endif()

# -----------------------------------------------------------------------------
# 授權聲明（Qt 以 LGPL 動態連結；CycloneDDS 為 EPL-2.0／EDL-1.0）
# -----------------------------------------------------------------------------
set(GCS_LICENSE_DIR "${CMAKE_INSTALL_DATADIR}/licenses")
if(APPLE)
    set(GCS_LICENSE_DIR "Taiphoon_QGCS.app/Contents/Resources/licenses")
endif()
install(FILES "${PROJECT_SOURCE_DIR}/packaging/licenses/THIRD_PARTY_NOTICES.md"
    DESTINATION ${GCS_LICENSE_DIR} COMPONENT ${GCS_INSTALL_COMPONENT})
install(FILES "${cyclonedds_SOURCE_DIR}/LICENSE" DESTINATION ${GCS_LICENSE_DIR}/cyclonedds
    RENAME LICENSE.txt COMPONENT ${GCS_INSTALL_COMPONENT})
install(FILES "${cyclonedds_SOURCE_DIR}/NOTICE.md" DESTINATION ${GCS_LICENSE_DIR}/cyclonedds
    COMPONENT ${GCS_INSTALL_COMPONENT})
# B612 字型（OFL-1.1）內嵌於程式資源，授權條文另附
install(FILES "${PROJECT_SOURCE_DIR}/resources/fonts/OFL.txt" DESTINATION ${GCS_LICENSE_DIR}/b612
    COMPONENT ${GCS_INSTALL_COMPONENT})
# Noto Sans TC 字型（OFL-1.1）內嵌於程式資源，授權條文另附
install(FILES "${PROJECT_SOURCE_DIR}/resources/fonts/NotoSansTC-OFL.txt" DESTINATION ${GCS_LICENSE_DIR}/noto-sans-tc
    RENAME OFL.txt COMPONENT ${GCS_INSTALL_COMPONENT})
get_filename_component(GCS_QT_LICENSE_DIR "${QT6_INSTALL_PREFIX}/../../Licenses" ABSOLUTE)
if(EXISTS "${GCS_QT_LICENSE_DIR}")
    install(DIRECTORY "${GCS_QT_LICENSE_DIR}/" DESTINATION ${GCS_LICENSE_DIR}/qt COMPONENT ${GCS_INSTALL_COMPONENT})
endif()

# -----------------------------------------------------------------------------
# CPack
# -----------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "Taiphoon_QGCS")
set(CPACK_PACKAGE_VENDOR "Taiphoon")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Taiphoon Qt 原生地面控制站")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "Taiphoon_QGCS-v${GCS_VERSION_FULL}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Taiphoon_QGCS")
# 安裝程式以此機碼偵測舊版並先移除；不可含版本號，否則升級後偵測不到舊版，
# 「新增或移除程式」會出現兩筆。CPack 預設取 CPACK_PACKAGE_INSTALL_DIRECTORY，此處明確固定。
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "Taiphoon_QGCS")
set(CPACK_PACKAGE_EXECUTABLES "Taiphoon_QGCS" "Taiphoon_QGCS")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/packaging/licenses/THIRD_PARTY_NOTICES.md")
set(CPACK_COMPONENTS_ALL ${GCS_INSTALL_COMPONENT})
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
# 單一安裝檔模式不套用 CPACK_COMPONENTS_ALL，需在此指定只安裝 gcs，
# 否則 CycloneDDS 的標頭、idlc 等開發檔也會被打包
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};${GCS_INSTALL_COMPONENT};/")
set(CPACK_STRIP_FILES OFF)

if(WIN32)
    find_program(GCS_MAKENSIS makensis PATHS "$ENV{ProgramFiles\(x86\)}/NSIS" "$ENV{ProgramFiles}/NSIS")
    if(GCS_MAKENSIS)
        set(CPACK_GENERATOR "NSIS;ZIP")
    else()
        message(STATUS "未找到 NSIS，cpack 只產生 ZIP")
        set(CPACK_GENERATOR "ZIP")
    endif()
    set(CPACK_NSIS_DISPLAY_NAME "Taiphoon_QGCS")
    set(CPACK_NSIS_PACKAGE_NAME "Taiphoon_QGCS")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\Taiphoon_QGCS.exe")
    # 安裝與移除程式使用相同的 Taiphoon 圖示（NSIS 需要反斜線路徑）
    file(TO_NATIVE_PATH "${PROJECT_SOURCE_DIR}/packaging/windows/Taiphoon_QGCS.ico" GCS_NSIS_ICON)
    string(REPLACE "\\" "\\\\" GCS_NSIS_ICON "${GCS_NSIS_ICON}")
    set(CPACK_NSIS_MUI_ICON "${GCS_NSIS_ICON}")
    set(CPACK_NSIS_MUI_UNIICON "${GCS_NSIS_ICON}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "${CMAKE_INSTALL_BINDIR}")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "Taiphoon_QGCS.exe")
    # DDS 需要接收 Agent 的 UDP 封包；安裝時建立只針對本程式、只允許同一子網路來源的入站規則，
    # 移除時一併刪除。Agent 需經路由器或 VPN 連線時，須由使用者自行放寬來源範圍。
    # 新增前先刪除同名規則：使用者未先移除舊版就覆蓋安裝時，規則才不會重複。
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
        "nsExec::ExecToLog 'netsh advfirewall firewall delete rule name=\\\"Taiphoon_QGCS DDS\\\"'
nsExec::ExecToLog 'netsh advfirewall firewall add rule name=\\\"Taiphoon_QGCS DDS\\\" dir=in action=allow protocol=UDP remoteip=localsubnet program=\\\"$INSTDIR\\\\bin\\\\Taiphoon_QGCS.exe\\\" enable=yes'")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
        "nsExec::ExecToLog 'netsh advfirewall firewall delete rule name=\\\"Taiphoon_QGCS DDS\\\"'")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "Taiphoon_QGCS")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
