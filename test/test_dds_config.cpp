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
    EXPECT_TRUE(contains(*xml, "<MaxAutoParticipantIndex>48</MaxAutoParticipantIndex>"));
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

TEST(DdsConfig, PeerProbeCoversDesktopSimulationAgents)
{
    // 對應 Taiphoon_Agent 啟動器：最多 4 台 Agent，每台 12 個 index，各占 meta／data 兩埠
    constexpr uint32_t AGENT_COUNT = 4;
    constexpr uint32_t INDEX_PER_AGENT = 12;
    constexpr uint32_t PORT_STEP = 2;
    constexpr uint32_t UNICAST_META_OFFSET = 10;
    // 最後一台 Agent 最後一個 discovery 埠相對 domain base 的偏移（domain 0 為 7504）
    constexpr uint32_t LAST_AGENT_META_OFFSET =
        UNICAST_META_OFFSET + AGENT_COUNT * INDEX_PER_AGENT * PORT_STEP - PORT_STEP;
    // 不含 port 的 peer 最後探測的 discovery 埠偏移（index 上限-1）
    constexpr uint32_t LAST_PROBED_META_OFFSET = UNICAST_META_OFFSET + PORT_STEP * (MAX_AUTO_PARTICIPANT_INDEX - 1);
    EXPECT_GE(LAST_PROBED_META_OFFSET, LAST_AGENT_META_OFFSET);
    EXPECT_LT(MAX_AUTO_PARTICIPANT_INDEX, PARTICIPANT_INDEX_LIMIT);
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
