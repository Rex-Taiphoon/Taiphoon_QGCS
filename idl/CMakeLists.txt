# =============================================================================
# @file idl/CMakeLists.txt
# @brief 由 msg/ 介面產生 DDS 型別（msg → IDL → idlc C 程式碼）
# @author Taiphoon GCS Team
# @date 2026-10-12
#
# msg/ 更新後重新建置即會重新產生，延續「編譯前同步介面檔案」的規範。
# 使用 NO_TYPE_INFO：與 rmw_cyclonedds 端點相同，不傳送 XTypes 型別資訊，只以型別名稱配對。
# =============================================================================
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)

file(GLOB GCS_AGENT_MSG_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/msg/msg/*.msg")
if(NOT GCS_AGENT_MSG_FILES)
    message(FATAL_ERROR "msg/msg/ 內沒有介面檔案，請先同步 ../Taiphoon_Agent_Interfaces/")
endif()

set(GCS_AGENT_IDL "${CMAKE_CURRENT_BINARY_DIR}/taiphoon_agent.idl")
set(GCS_ROS_COMMON_IDL "${CMAKE_CURRENT_BINARY_DIR}/ros_common.idl")

add_custom_command(
    OUTPUT  "${GCS_AGENT_IDL}"
    COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/msg2idl.py"
            --package taiphoon_agent --output "${GCS_AGENT_IDL}" ${GCS_AGENT_MSG_FILES}
    DEPENDS ${GCS_AGENT_MSG_FILES} "${PROJECT_SOURCE_DIR}/tools/msg2idl.py"
    COMMENT "由 msg/ 產生 taiphoon_agent.idl"
    VERBATIM
)

# 與產生的 IDL 放在同一目錄，產生的 C 標頭才能以相同相對路徑互相引用。
add_custom_command(
    OUTPUT  "${GCS_ROS_COMMON_IDL}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/ros_common.idl" "${GCS_ROS_COMMON_IDL}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ros_common.idl"
    VERBATIM
)

idlc_generate(
    TARGET gcs_agent_idl
    FILES "${GCS_ROS_COMMON_IDL}" "${GCS_AGENT_IDL}"
    INCLUDES "${CMAKE_CURRENT_BINARY_DIR}"
    DEFAULT_EXTENSIBILITY final
    NO_TYPE_INFO
)

# 產生的 .c 只編譯進這個靜態函式庫一次，避免 INTERFACE 來源傳遞造成重複定義。
add_library(gcs_agent_types STATIC)
target_link_libraries(gcs_agent_types
    PRIVATE gcs_agent_idl
    PUBLIC  CycloneDDS::ddsc
)
target_include_directories(gcs_agent_types PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")

# idlc 產生的 opcode 表混用兩種列舉，屬產生器寫法，不影響正確性。
target_compile_options(gcs_agent_types PRIVATE "$<$<C_COMPILER_ID:MSVC>:/wd5287>")
