/**
 * @file dds_config.cpp
 * @brief CycloneDDS 設定 XML 產生實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "link/dds_config.hpp"

#include <sstream>

namespace gcs::link
{

namespace
{

/**
 * @brief XML 屬性與文字跳脫
 * @param text 原始文字
 * @return 跳脫後的文字
 */
std::string escape_xml(const std::string& text)
{
    std::string result;
    result.reserve(text.size());
    for (const char ch : text)
    {
        switch (ch)
        {
        case '&':
            result += "&amp;";
            break;
        case '<':
            result += "&lt;";
            break;
        case '>':
            result += "&gt;";
            break;
        case '"':
            result += "&quot;";
            break;
        case '\'':
            result += "&apos;";
            break;
        default:
            result += ch;
            break;
        }
    }
    return result;
}

/**
 * @brief 判斷網卡設定是 IPv4 位址還是網卡名稱
 * @param value 使用者設定值
 * @return 是否為 IPv4 位址格式
 */
bool looks_like_ipv4(const std::string& value)
{
    if (value.empty())
    {
        return false;
    }
    int dot_count = 0;
    for (const char ch : value)
    {
        if (ch == '.')
        {
            ++dot_count;
        }
        else if (ch < '0' || ch > '9')
        {
            return false;
        }
    }
    return dot_count == 3;
}

/**
 * @brief 設定錯誤時寫入錯誤訊息
 * @param error 輸出位置（可為 nullptr）
 * @param message 錯誤訊息
 */
void set_error(std::string* error, const std::string& message)
{
    if (error != nullptr)
    {
        *error = message;
    }
}

}  // namespace

std::optional<std::string> build_cyclonedds_xml(const DdsNetworkConfig& config, std::string* error)
{
    if (config.participant_index.has_value() && *config.participant_index >= PARTICIPANT_INDEX_LIMIT)
    {
        set_error(error, "participant index 必須小於 120");
        return std::nullopt;
    }
    if (!config.is_multicast_allowed && config.peers.empty())
    {
        // 沒有 multicast 也沒有單播入口時，任何 Agent 都不可能被發現。
        set_error(error, "關閉 multicast 時至少需要一個 Agent 探索入口");
        return std::nullopt;
    }

    std::ostringstream xml;
    xml << "<CycloneDDS><Domain Id=\"any\"><General>";
    if (!config.network_interface.empty())
    {
        const char* attribute = looks_like_ipv4(config.network_interface) ? "address" : "name";
        xml << "<Interfaces><NetworkInterface " << attribute << "=\""
            << escape_xml(config.network_interface) << "\"/></Interfaces>";
    }
    xml << "<AllowMulticast>" << (config.is_multicast_allowed ? "default" : "false") << "</AllowMulticast>";
    if (!config.external_address.empty())
    {
        xml << "<ExternalNetworkAddress>" << escape_xml(config.external_address) << "</ExternalNetworkAddress>";
    }
    xml << "</General>";

    // 單一接收 socket 讓使用的埠號可預期，便於防火牆設定（ADR-0011）。
    xml << "<Compatibility><ManySocketsMode>false</ManySocketsMode></Compatibility>";

    // 所有 socket 由單一接收執行緒處理。macOS／Linux 預設每個 socket 一條專屬執行緒，
    // 關閉 domain 時必須送封包給自己才能喚醒；多播送不回本機（區域網路權限被拒、
    // GitHub macOS runner）時 dds_delete 會永久卡住。Windows 預設即為 false。
    xml << "<Internal><MultipleReceiveThreads>false</MultipleReceiveThreads></Internal>";

    xml << "<Discovery><ParticipantIndex>";
    if (config.participant_index.has_value())
    {
        xml << *config.participant_index;
    }
    else
    {
        xml << "auto";
    }
    xml << "</ParticipantIndex><MaxAutoParticipantIndex>" << MAX_AUTO_PARTICIPANT_INDEX
        << "</MaxAutoParticipantIndex>";
    if (!config.peers.empty())
    {
        xml << "<Peers>";
        for (const auto& peer : config.peers)
        {
            xml << "<Peer Address=\"" << escape_xml(peer) << "\"/>";
        }
        xml << "</Peers>";
    }
    xml << "</Discovery>";

    xml << "<Tracing><Verbosity>warning</Verbosity><OutputFile>stderr</OutputFile></Tracing>";
    xml << "</Domain></CycloneDDS>";
    return xml.str();
}

dds_entity_t create_participant(const DdsNetworkConfig& config, std::string* error)
{
    const auto xml = build_cyclonedds_xml(config, error);
    if (!xml.has_value())
    {
        return DDS_RETCODE_BAD_PARAMETER;
    }

    const dds_entity_t domain = dds_create_domain(config.domain_id, xml->c_str());
    if (domain < 0)
    {
        set_error(error, std::string("建立 DDS domain 失敗：") + dds_strretcode(domain));
        return domain;
    }

    const dds_entity_t participant = dds_create_participant(config.domain_id, nullptr, nullptr);
    if (participant < 0)
    {
        set_error(error, std::string("建立 DDS participant 失敗：") + dds_strretcode(participant));
    }
    return participant;
}

}  // namespace gcs::link
