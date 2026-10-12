/**
 * @file ros_naming.cpp
 * @brief ROS2 名稱與 DDS 名稱的對應規則實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "link/ros_naming.hpp"

#include <regex>

namespace gcs::link
{

namespace
{

/// rmw_cyclonedds 一般 topic 前綴
constexpr std::string_view TOPIC_PREFIX = "rt";
/// rmw_cyclonedds service 請求／回應前綴與後綴
constexpr std::string_view REQUEST_PREFIX = "rq";
constexpr std::string_view REPLY_PREFIX = "rr";
constexpr std::string_view REQUEST_SUFFIX = "Request";
constexpr std::string_view REPLY_SUFFIX = "Reply";

/**
 * @brief 檢查 ROS2 完整名稱格式：以 / 開頭、不以 / 結尾、不含空白
 * @param name ROS2 名稱
 * @return 是否合法
 */
bool is_fully_qualified(std::string_view name)
{
    if (name.size() < 2 || name.front() != '/' || name.back() == '/')
    {
        return false;
    }
    return name.find_first_of(" \t\r\n") == std::string_view::npos;
}

}  // namespace

std::optional<std::string> to_dds_topic(std::string_view ros_topic)
{
    if (!is_fully_qualified(ros_topic))
    {
        return std::nullopt;
    }
    return std::string(TOPIC_PREFIX) + std::string(ros_topic);
}

std::optional<std::string> from_dds_topic(std::string_view dds_topic)
{
    if (dds_topic.size() <= TOPIC_PREFIX.size()
        || dds_topic.substr(0, TOPIC_PREFIX.size()) != TOPIC_PREFIX)
    {
        return std::nullopt;
    }
    const std::string_view ros_topic = dds_topic.substr(TOPIC_PREFIX.size());
    if (!is_fully_qualified(ros_topic))
    {
        return std::nullopt;
    }
    return std::string(ros_topic);
}

std::optional<std::string> to_service_request_topic(std::string_view ros_service)
{
    if (!is_fully_qualified(ros_service))
    {
        return std::nullopt;
    }
    return std::string(REQUEST_PREFIX) + std::string(ros_service) + std::string(REQUEST_SUFFIX);
}

std::optional<std::string> to_service_reply_topic(std::string_view ros_service)
{
    if (!is_fully_qualified(ros_service))
    {
        return std::nullopt;
    }
    return std::string(REPLY_PREFIX) + std::string(ros_service) + std::string(REPLY_SUFFIX);
}

bool is_agent_namespace(std::string_view agent_ns)
{
    // 介面規格第 7 節：UAV 索引為 1 起算，不接受前導零。
    static const std::regex AGENT_NS_PATTERN("^uav_[1-9][0-9]*_agent$");
    return std::regex_match(agent_ns.begin(), agent_ns.end(), AGENT_NS_PATTERN);
}

std::optional<std::string> agent_namespace_from_state_topic(std::string_view ros_topic)
{
    // 只辨識完整的 state topic，沿用網頁版 FleetModule 的規則。
    static const std::regex STATE_TOPIC_PATTERN("^/(uav_[1-9][0-9]*_agent)/state$");
    std::match_results<std::string_view::const_iterator> match;
    if (!std::regex_match(ros_topic.begin(), ros_topic.end(), match, STATE_TOPIC_PATTERN))
    {
        return std::nullopt;
    }
    return match[1].str();
}

std::string agent_topic(std::string_view agent_ns, std::string_view suffix)
{
    std::string topic;
    topic.reserve(agent_ns.size() + suffix.size() + 2);
    topic += '/';
    topic += agent_ns;
    topic += '/';
    topic += suffix;
    return topic;
}

}  // namespace gcs::link
