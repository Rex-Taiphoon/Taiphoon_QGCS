/**
 * @file test_dds_config.cpp
 * @brief CycloneDDS 設定 XML 產生測試
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "link/dds_config.hpp"

#include <gtest/gtest.h>

using namespace gcs::link;

namespace
{

/// 測試專用 domain，避免與現場 ROS_DOMAIN_ID 0 的流量混在一起
constexpr uint32_t TEST_DOMAIN_ID = 43;

/**
 * @brief 檢查字串包含子字串
 * @param text 完整字串
 * @param part 子字串
 * @return 是否包含
 */
bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

}  // namespace

TEST(DdsConfig, DefaultUsesMulticastAndAutoIndex)
{
    const auto xml = build_cyclonedds_xml(DdsNetworkConfig{});
    ASSERT_TRUE(xml.has_value());
    EXPECT_TRUE(contains(*xml, "<AllowMulticast>default</AllowMulticast>"));
    EXPECT_TRUE(contains(*xml, "<ParticipantIndex>auto</ParticipantIndex>"));
    EXPECT_TRUE(contains(*xml, "<MaxAutoParticipantIndex>20</MaxAutoParticipantIndex>"));
    EXPECT_TRUE(contains(*xml, "<ManySocketsMode>false</ManySocketsMode>"));
    EXPECT_TRUE(contains(*xml, "<MultipleReceiveThreads>false</MultipleReceiveThreads>"));
    EXPECT_FALSE(contains(*xml, "<Peers>"));
    EXPECT_FALSE(contains(*xml, "<Interfaces>"));
}

TEST(DdsConfig, UnicastSiteConfig)
{
    DdsNetworkConfig config;
    config.is_multicast_allowed = false;
    config.peers = {"192.168.0.31", "192.168.0.32"};
    config.network_interface = "192.168.0.20";
    config.participant_index = 100;

    const auto xml = build_cyclonedds_xml(config);
    ASSERT_TRUE(xml.has_value());
    EXPECT_TRUE(contains(*xml, "<AllowMulticast>false</AllowMulticast>"));
    EXPECT_TRUE(contains(*xml, "<NetworkInterface address=\"192.168.0.20\"/>"));
    EXPECT_TRUE(contains(*xml, "<Peer Address=\"192.168.0.31\"/><Peer Address=\"192.168.0.32\"/>"));
    EXPECT_TRUE(contains(*xml, "<ParticipantIndex>100</ParticipantIndex>"));
}

TEST(DdsConfig, InterfaceNameAndEscaping)
{
    DdsNetworkConfig config;
    config.network_interface = "Wi-Fi \"<2>\"";
    const auto xml = build_cyclonedds_xml(config);
    ASSERT_TRUE(xml.has_value());
    EXPECT_TRUE(contains(*xml, "<NetworkInterface name=\"Wi-Fi &quot;&lt;2&gt;&quot;\"/>"));
}

TEST(DdsConfig, RejectsUnreachableConfig)
{
    DdsNetworkConfig config;
    config.is_multicast_allowed = false;
    std::string error;
    EXPECT_FALSE(build_cyclonedds_xml(config, &error).has_value());
    EXPECT_FALSE(error.empty());
}

TEST(DdsConfig, RejectsParticipantIndexOutOfRange)
{
    DdsNetworkConfig config;
    config.participant_index = PARTICIPANT_INDEX_LIMIT;
    std::string error;
    EXPECT_FALSE(build_cyclonedds_xml(config, &error).has_value());
    EXPECT_FALSE(error.empty());
}

TEST(DdsConfig, CycloneDdsAcceptsGeneratedXml)
{
    DdsNetworkConfig config;
    config.domain_id = TEST_DOMAIN_ID;
    config.peers = {"127.0.0.1"};
    std::string error;
    const dds_entity_t participant = create_participant(config, &error);
    EXPECT_GT(participant, 0) << error;
    dds_delete(DDS_CYCLONEDDS_HANDLE);
}
