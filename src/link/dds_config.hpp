/**
 * @file dds_config.hpp
 * @brief 由站點設定產生 CycloneDDS 設定 XML
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 延續 ADR-0011 的 Wi-Fi 單播參數，但改由程式產生並以 dds_create_domain() 套用，
 * 使用者不需手動編輯 XML。
 */
#pragma once

#include <dds/dds.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gcs::link
{

/// DDS 網路設定
struct DdsNetworkConfig
{
    /// ROS_DOMAIN_ID，需與 Agent 相同
    uint32_t domain_id = 0;
    /// 指定網卡名稱或 IPv4 位址；空字串表示由 CycloneDDS 自動選擇
    std::string network_interface;
    /// 是否允許 multicast（SITL 使用；Wi-Fi 單播部署關閉）
    bool is_multicast_allowed = true;
    /// 單播探索入口（Agent 位址），可為空
    std::vector<std::string> peers;
    /// 對外公告的位址（NAT 後方時使用），空字串表示不設定
    std::string external_address;
    /// 固定 participant index；未設定時使用 auto
    std::optional<uint32_t> participant_index;
};

/// 自動 participant index 上限；這是本機 participant 容量，與 Agent 數量無關（ADR-0011）
inline constexpr uint32_t MAX_AUTO_PARTICIPANT_INDEX = 20;

/// CycloneDDS 允許的 participant index 上限（不含）
inline constexpr uint32_t PARTICIPANT_INDEX_LIMIT = 120;

/**
 * @brief 產生 CycloneDDS 設定 XML
 * @param config 網路設定
 * @param error 設定不合法時的錯誤訊息（可為 nullptr）
 * @return XML 字串；設定不合法時回傳 std::nullopt
 */
std::optional<std::string> build_cyclonedds_xml(const DdsNetworkConfig& config, std::string* error = nullptr);

/**
 * @brief 依設定建立 DDS domain 與 participant
 * @param config 網路設定
 * @param error 失敗時的錯誤訊息（可為 nullptr）
 * @return participant 實體；失敗時為負值
 */
dds_entity_t create_participant(const DdsNetworkConfig& config, std::string* error = nullptr);

}  // namespace gcs::link
