# =============================================================================
# @file src/voice/CMakeLists.txt
# @brief gcs_voice：語音口令辨識（只依賴 QtCore；sherpa-onnx 於執行期載入，ADR-0018）
# @author Taiphoon GCS Team
# @date 2026-10-26
# =============================================================================
find_package(Qt6 ${GCS_QT_VERSION} EXACT REQUIRED COMPONENTS Core)

add_library(gcs_voice STATIC
    keyword_spotter.hpp
    keyword_spotter.cpp
    voice_keywords.hpp
    wav_file.hpp
    wav_file.cpp
)
target_include_directories(gcs_voice PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(gcs_voice PUBLIC Qt6::Core)

if(GCS_HAS_SHERPA_ONNX)
    # 只使用標頭中的型別定義，函式一律以 QLibrary 取得，不連結 sherpa-onnx
    target_include_directories(gcs_voice PRIVATE "${GCS_SHERPA_ONNX_INCLUDE_DIR}")
    target_compile_definitions(gcs_voice PRIVATE GCS_HAS_SHERPA_ONNX)
endif()
