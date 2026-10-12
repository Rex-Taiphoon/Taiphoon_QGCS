# =============================================================================
# @file test/CMakeLists.txt
# @brief 單元測試（Google Test，以 ctest 執行）
# @author Taiphoon GCS Team
# @date 2026-10-12
# =============================================================================
find_package(Qt6 ${GCS_QT_VERSION} EXACT REQUIRED COMPONENTS Core Test)

# -----------------------------------------------------------------------------
# 測試共用：建立 QCoreApplication 的主程式與 FakeAgentLink
# -----------------------------------------------------------------------------
add_library(gcs_test_support STATIC
    support/test_main.cpp
    fakes/fake_agent_link.hpp
    fakes/fake_agent_link.cpp
)
target_include_directories(gcs_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gcs_test_support PUBLIC gcs_core Qt6::Test GTest::gtest)

# -----------------------------------------------------------------------------
# M2：核心模組與組裝根（安全關鍵：Guard 全組合、命令無法繞過 Guard）
# -----------------------------------------------------------------------------
set(GCS_CORE_TESTS
    test_command_protocol
    test_guard_transition
    test_command_module
    test_fleet_module
    test_telemetry_module
    test_agent_lifecycle_module
    test_gcs_wiring
)

foreach(test_name IN LISTS GCS_CORE_TESTS)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE gcs_test_support gcs_wiring)
    gtest_discover_tests(${test_name} DISCOVERY_MODE PRE_TEST)
endforeach()

# -----------------------------------------------------------------------------
# M1：CycloneDDS ROS2 相容層
# -----------------------------------------------------------------------------
set(GCS_LINK_TESTS
    test_ros_naming
    test_qos_profile
    test_dds_config
    test_cdr_wire_format
)

foreach(test_name IN LISTS GCS_LINK_TESTS)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE gcs_link GTest::gtest_main)
    # DDS 測試會建立 participant；個別執行避免同一程序重複建立 domain。
    gtest_discover_tests(${test_name} DISCOVERY_MODE PRE_TEST)
endforeach()

# M3：通訊層（需要 QCoreApplication 的測試使用共用主程式）
set(GCS_LINK_QT_TESTS
    test_message_conversion
    test_cyclone_agent_link
)

foreach(test_name IN LISTS GCS_LINK_QT_TESTS)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE gcs_test_support gcs_link)
    gtest_discover_tests(${test_name} DISCOVERY_MODE PRE_TEST)
endforeach()

add_executable(test_core_runtime test_core_runtime.cpp)
target_link_libraries(test_core_runtime PRIVATE gcs_test_support gcs_runtime)
gtest_discover_tests(test_core_runtime DISCOVERY_MODE PRE_TEST)

# -----------------------------------------------------------------------------
# M4～M6：介面橋接、設定、網路設定與除錯工具（不需畫面）
# -----------------------------------------------------------------------------
set(GCS_UI_TESTS
    test_ui_bridge
    test_app_support
    test_network_controller
)

foreach(test_name IN LISTS GCS_UI_TESTS)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE gcs_test_support gcs_ui)
    gtest_discover_tests(${test_name} DISCOVERY_MODE PRE_TEST)
endforeach()

# QML 互動（以真實的 QQuickWindow 事件傳遞驗證安全相關操作）
if(GCS_BUILD_APP)
    find_package(Qt6 ${GCS_QT_VERSION} EXACT REQUIRED COMPONENTS Gui Quick)
    add_executable(test_slide_to_confirm test_slide_to_confirm.cpp)
    target_compile_definitions(test_slide_to_confirm PRIVATE GCS_QML_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src/qml")
    target_link_libraries(test_slide_to_confirm PRIVATE Qt6::Gui Qt6::Quick Qt6::Test GTest::gtest)
    gtest_discover_tests(test_slide_to_confirm DISCOVERY_MODE PRE_TEST)
endif()

# 取出 writer 序列化結果需要 ddsi_serdata.h；安裝版 CycloneDDS 會公開此標頭，
# 以原始碼建置時 ddsc 未將其加入 include 路徑。
target_include_directories(test_cdr_wire_format PRIVATE "${cyclonedds_SOURCE_DIR}/src/core/ddsi/include")

# -----------------------------------------------------------------------------
# 建置工具
# -----------------------------------------------------------------------------
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
add_test(NAME test_msg2idl
    COMMAND Python3::Interpreter -B "${CMAKE_CURRENT_SOURCE_DIR}/test_msg2idl.py"
)
