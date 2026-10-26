# =============================================================================
# @file Voice.cmake
# @brief 語音口令：sherpa-onnx 預編譯 C API 與中文關鍵詞模型（版本固定，ADR-0018）
# @author Taiphoon GCS Team
# @date 2026-10-26
#
# 程式以 QLibrary 在執行期載入 sherpa-onnx，連結期不依賴它；檔案放在建置目錄的 bin/voice/：
#   bin/voice/            sherpa-onnx C API 與 onnxruntime 動態函式庫
#   bin/voice/model/      關鍵詞模型（encoder／decoder／joiner 與 tokens.txt）
# 安裝規則見 Packaging.cmake（macOS 的動態函式庫放 bundle 的 Frameworks）；缺少時語音口令停用，其餘功能不受影響。
# 已下載過模型時可設定 GCS_KWS_MODEL_URL 為本機檔案路徑（例如 C:/Downloads/xxx.tar.bz2），避免重複下載。
# =============================================================================
option(GCS_ENABLE_VOICE "建置語音口令（sherpa-onnx 關鍵詞辨識）" ON)

set(GCS_VOICE_RUNTIME_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/voice")
set(GCS_HAS_SHERPA_ONNX OFF)

# 官方沒有 iOS 的預編譯 C API
if(IOS)
    set(GCS_ENABLE_VOICE OFF)
endif()

if(GCS_ENABLE_VOICE)
    set(GCS_SHERPA_ONNX_VERSION "1.13.8")
    # 只取 C API 的動態函式庫（-lib）、不含語音合成（no-tts）；Windows 取 /MD Release，C 介面不受 Debug 執行階段影響
    if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(GCS_SHERPA_ONNX_PLATFORM "win-x64-shared-MD-Release-no-tts-lib")
        set(GCS_SHERPA_ONNX_SHA256 "b90992b888710715d613a4fedcd75d5b4db294ef64f3194e4f77ea0a8b387441")
    elseif(APPLE)
        set(GCS_SHERPA_ONNX_PLATFORM "osx-universal2-shared-no-tts-lib")
        set(GCS_SHERPA_ONNX_SHA256 "bdcc7c266d355697584dd4efb9dc766e45e18e87cec1fc553002a32cfcfab9a7")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        set(GCS_SHERPA_ONNX_PLATFORM "linux-x64-shared-no-tts-lib")
        set(GCS_SHERPA_ONNX_SHA256 "bf2d998c8b07012cd5098f3b92673bc1333fd9b927767d7cb664be8190d8bc0b")
    else()
        message(STATUS "此平台沒有 sherpa-onnx 預編譯函式庫，停用語音口令")
        set(GCS_ENABLE_VOICE OFF)
    endif()
endif()

if(GCS_ENABLE_VOICE)
    set(GCS_SHERPA_ONNX_ARCHIVE "sherpa-onnx-v${GCS_SHERPA_ONNX_VERSION}-${GCS_SHERPA_ONNX_PLATFORM}")
    FetchContent_Declare(sherpa_onnx_prebuilt
        URL https://github.com/k2-fsa/sherpa-onnx/releases/download/v${GCS_SHERPA_ONNX_VERSION}/${GCS_SHERPA_ONNX_ARCHIVE}.tar.bz2
        URL_HASH SHA256=${GCS_SHERPA_ONNX_SHA256}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )

    # 中文關鍵詞模型（WenetSpeech 3.3M）；發布頁未提供摘要，雜湊為官方檔案下載後計算
    set(GCS_KWS_MODEL_NAME "sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01")
    set(GCS_KWS_MODEL_URL
        "https://github.com/k2-fsa/sherpa-onnx/releases/download/kws-models/${GCS_KWS_MODEL_NAME}.tar.bz2"
        CACHE STRING "語音模型下載位置；可改為本機已下載檔案的路徑")
    FetchContent_Declare(kws_model
        URL ${GCS_KWS_MODEL_URL}
        URL_HASH SHA256=b2f7c89690dc8ce4c6ed6afeab7cd800c36ad1421fb6b6302b4a4b194cf7f35f
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(sherpa_onnx_prebuilt kws_model)

    # -lib 預編譯檔只含動態函式庫；C API 標頭取自同一版本的原始碼標籤（只用其中的型別定義）
    set(GCS_SHERPA_ONNX_INCLUDE_DIR "${CMAKE_BINARY_DIR}/_deps/sherpa_onnx_include")
    set(GCS_SHERPA_ONNX_HEADER "${GCS_SHERPA_ONNX_INCLUDE_DIR}/sherpa-onnx/c-api/c-api.h")
    if(NOT EXISTS "${GCS_SHERPA_ONNX_HEADER}")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/k2-fsa/sherpa-onnx/v${GCS_SHERPA_ONNX_VERSION}/sherpa-onnx/c-api/c-api.h"
            "${GCS_SHERPA_ONNX_HEADER}"
            EXPECTED_HASH SHA256=2a1b95084be8fd1deb3228fcad2fd3f7f0258b64582f7402281ec174c7b7f4ce
            STATUS GCS_SHERPA_ONNX_HEADER_STATUS
        )
        list(GET GCS_SHERPA_ONNX_HEADER_STATUS 0 GCS_SHERPA_ONNX_HEADER_CODE)
        if(NOT GCS_SHERPA_ONNX_HEADER_CODE EQUAL 0)
            file(REMOVE "${GCS_SHERPA_ONNX_HEADER}")
            message(FATAL_ERROR "下載 sherpa-onnx c-api.h 失敗：${GCS_SHERPA_ONNX_HEADER_STATUS}")
        endif()
    endif()

    file(GLOB GCS_SHERPA_ONNX_RUNTIME_LIBS
        "${sherpa_onnx_prebuilt_SOURCE_DIR}/lib/*.dll"
        "${sherpa_onnx_prebuilt_SOURCE_DIR}/bin/*.dll"
        "${sherpa_onnx_prebuilt_SOURCE_DIR}/lib/*.dylib"
        "${sherpa_onnx_prebuilt_SOURCE_DIR}/lib/*.so*"
    )
    # 只需要 C API 與 onnxruntime；C++ API 不使用
    list(FILTER GCS_SHERPA_ONNX_RUNTIME_LIBS EXCLUDE REGEX "cxx-api")
    if(NOT GCS_SHERPA_ONNX_RUNTIME_LIBS)
        message(FATAL_ERROR "sherpa-onnx 預編譯檔沒有動態函式庫：${sherpa_onnx_prebuilt_SOURCE_DIR}")
    endif()
    set(GCS_KWS_MODEL_FILES
        "${kws_model_SOURCE_DIR}/encoder-epoch-12-avg-2-chunk-16-left-64.onnx"
        "${kws_model_SOURCE_DIR}/decoder-epoch-12-avg-2-chunk-16-left-64.onnx"
        "${kws_model_SOURCE_DIR}/joiner-epoch-12-avg-2-chunk-16-left-64.onnx"
        "${kws_model_SOURCE_DIR}/tokens.txt"
    )
    # 安裝規則（Packaging.cmake）直接取用這兩份清單，不安裝建置目錄中可能殘留的其他檔案
    file(COPY ${GCS_SHERPA_ONNX_RUNTIME_LIBS} DESTINATION "${GCS_VOICE_RUNTIME_DIR}" FOLLOW_SYMLINK_CHAIN)
    file(COPY ${GCS_KWS_MODEL_FILES} DESTINATION "${GCS_VOICE_RUNTIME_DIR}/model")
    set(GCS_HAS_SHERPA_ONNX ON)
    message(STATUS "語音口令：sherpa-onnx ${GCS_SHERPA_ONNX_VERSION}，執行期檔案位於 ${GCS_VOICE_RUNTIME_DIR}")
endif()
