/**
 * @file ros_naming.hpp
 * @brief ROS2 名稱與 DDS 名稱的對應規則（rmw_cyclonedds_cpp，ROS2 Humble）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * ROS2 topic、service 與型別在 DDS 網路上使用固定前綴與後綴，本檔集中這些規則，
 * 讓相容層與機隊發現使用同一份定義（ADR-0014）。
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace gcs::link
{

/// 全體廣播命令 topic（介面文件第 4.2.2 節）
inline constexpr std::string_view ALL_AGENTS_COMMAND_TOPIC = "/uav_all_agent/command";

/// 全體廣播的 Agent namespace，命令目標以此字串表示全體
inline constexpr std::string_view ALL_AGENTS_NAMESPACE = "uav_all_agent";

/// DDS 型別名稱（rmw_cyclonedds 對 <pkg>/msg/<Type> 的命名）
inline constexpr std::string_view TYPE_AGENT_STATE = "taiphoon_agent::msg::dds_::AgentState_";
inline constexpr std::string_view TYPE_AGENT_ATTITUDE = "taiphoon_agent::msg::dds_::AgentAttitude_";
inline constexpr std::string_view TYPE_AGENT_TRAJECTORY = "taiphoon_agent::msg::dds_::AgentTrajectory_";
inline constexpr std::string_view TYPE_STD_STRING = "std_msgs::msg::dds_::String_";
inline constexpr std::string_view TYPE_SET_BOOL_REQUEST = "std_srvs::srv::dds_::SetBool_Request_";
inline constexpr std::string_view TYPE_SET_BOOL_RESPONSE = "std_srvs::srv::dds_::SetBool_Response_";

/**
 * @brief 將 ROS2 topic 轉為 DDS topic 名稱
 * @param ros_topic 以 / 開頭的完整 ROS2 topic，例如 /uav_1_agent/state
 * @return DDS topic 名稱，例如 rt/uav_1_agent/state；格式錯誤時回傳 std::nullopt
 */
std::optional<std::string> to_dds_topic(std::string_view ros_topic);

/**
 * @brief 將 DDS topic 名稱還原為 ROS2 topic
 * @param dds_topic DDS topic 名稱
 * @return ROS2 topic；不是 rt/ 前綴的一般 topic 時回傳 std::nullopt
 */
std::optional<std::string> from_dds_topic(std::string_view dds_topic);

/**
 * @brief 取得 ROS2 service 請求的 DDS topic 名稱
 * @param ros_service 以 / 開頭的完整 service 名稱，例如 /uav_1_agent/shutdown
 * @return 例如 rq/uav_1_agent/shutdownRequest；格式錯誤時回傳 std::nullopt
 */
std::optional<std::string> to_service_request_topic(std::string_view ros_service);

/**
 * @brief 取得 ROS2 service 回應的 DDS topic 名稱
 * @param ros_service 以 / 開頭的完整 service 名稱
 * @return 例如 rr/uav_1_agent/shutdownReply；格式錯誤時回傳 std::nullopt
 */
std::optional<std::string> to_service_reply_topic(std::string_view ros_service);

/**
 * @brief 檢查是否為單機 Agent namespace（uav_{N}_agent，N 為 1 起算）
 * @param agent_ns 待檢查的字串
 * @return 是否合法
 */
bool is_agent_namespace(std::string_view agent_ns);

/**
 * @brief 從 Agent 狀態 topic 擷取 Agent namespace
 * @param ros_topic ROS2 topic，例如 /uav_3_agent/state
 * @return Agent namespace；不是 Agent 狀態 topic 時回傳 std::nullopt
 */
std::optional<std::string> agent_namespace_from_state_topic(std::string_view ros_topic);

/**
 * @brief 組合 Agent topic
 * @param agent_ns Agent namespace
 * @param suffix topic 後段，例如 state、planning/trajectory
 * @return 例如 /uav_1_agent/state
 */
std::string agent_topic(std::string_view agent_ns, std::string_view suffix);

}  // namespace gcs::link
