# =============================================================================
# @file test/CMakeLists.txt
# @brief 單元測試（Google Test，以 ctest 執行）
# @author Taiphoon GCS Team
# @date 2026-10-12
#
# 網頁版遺留的 test_*.cpp（Guard、Command 等）於 M2 改寫後才加入此清單。
# =============================================================================

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
