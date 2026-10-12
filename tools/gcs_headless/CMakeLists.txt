# =============================================================================
# @file tools/gcs_headless/CMakeLists.txt
# @brief M3 無介面整合驗證工具（開發用，不隨產品安裝）
# @author Taiphoon GCS Team
# @date 2026-10-12
# =============================================================================
add_executable(gcs_headless main.cpp)
target_link_libraries(gcs_headless PRIVATE gcs_runtime)
