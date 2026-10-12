# =============================================================================
# @file tools/dds_probe/CMakeLists.txt
# @brief M1 DDS 相容性驗證工具（開發用，不隨產品安裝）
# @author Taiphoon GCS Team
# @date 2026-10-12
# =============================================================================
add_executable(dds_probe main.cpp)
target_link_libraries(dds_probe PRIVATE gcs_link)
