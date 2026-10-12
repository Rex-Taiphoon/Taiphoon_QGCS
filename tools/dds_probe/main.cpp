/**
 * @file main.cpp
 * @brief dds_probe：M1 DDS 相容性驗證工具（開發用，不隨產品發布）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 以內嵌 CycloneDDS 驗證與 ROS2 Humble + rmw_cyclonedds_cpp Agent 的互通（ADR-0014）：
 *   list           讀取 DCPSPublication，顯示 topic 出現與消失及偵測到的 Agent（項目 3）
 *   echo           訂閱 Agent state／attitude／planning/trajectory 並解碼（項目 1、5）
 *   noop-command   對 command topic 發布固定的無效命令 GCS_PROBE_NOOP（項目 2）
 *   shutdown       呼叫 Agent shutdown service（項目 4，需 --confirm）
 *   fake-agent     模擬 Agent 的 state 發布、命令接收與 shutdown service，供本機自我測試
 *
 * 安全限制：本工具不提供任意命令發布。noop-command 只送 Agent 無法解析的字串，
 * Agent 只會記錄日誌，不會執行任何動作；正式命令一律經 GuardModule（AGENTS.md）。
 */
#include "link/dds_config.hpp"
#include "link/qos_profile.hpp"
#include "link/ros_naming.hpp"

#include "ros_common.h"
#include "taiphoon_agent.h"

#include <dds/dds.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace gcs::link;

namespace
{

/// 驗證命令發布使用的無效命令；Agent 解析為 UNKNOWN 後只記錄日誌
constexpr const char* NOOP_COMMAND = "GCS_PROBE_NOOP";
/// 等待端點配對的最長時間（毫秒）
constexpr int MATCH_TIMEOUT_MS = 5000;
/// 等待 shutdown 回應的最長時間（毫秒）；與網頁版 AgentLifecycleModule 相同
constexpr int SHUTDOWN_REPLY_TIMEOUT_MS = 5000;
/// 輪詢間隔（毫秒）
constexpr int POLL_INTERVAL_MS = 50;
/// 單次 take 的樣本上限
constexpr size_t MAX_SAMPLES = 32;
/// echo 每個 topic 印出內容的最短間隔（秒），避免 10 Hz 資料洗版
constexpr double ECHO_PRINT_INTERVAL_S = 1.0;
/// fake-agent 發布頻率（Hz），與 Agent state 名目頻率相同
constexpr int FAKE_AGENT_RATE_HZ = 10;
/// fake-agent 預設位置（台北），與網頁版地圖預設中心相同
constexpr double FAKE_LATITUDE_DEG = 25.03997;
constexpr double FAKE_LONGITUDE_DEG = 121.51195;
/// FlightState::HOVER 與 PX4 NAVIGATION_STATE_OFFBOARD
constexpr uint8_t FLIGHT_STATE_HOVER = 5;
constexpr uint8_t PX4_NAV_STATE_OFFBOARD = 14;

std::atomic<bool> g_is_running{true};

/** @brief Ctrl+C 處理 @param signal_number 訊號編號 */
void handle_signal(int signal_number)
{
    (void)signal_number;
    g_is_running = false;
}

/// 命令列選項
struct ProbeOptions
{
    std::string command;
    std::string target;
    DdsNetworkConfig network;
    double duration_s = 10.0;
    bool is_confirmed = false;
    /// 非空時將 CycloneDDS 詳細追蹤寫入此檔，用於排除網路問題
    std::string trace_file;
};

/** @brief 印出用法 */
void print_usage()
{
    std::cout
        << "用法：dds_probe <命令> [目標] [選項]\n"
           "命令：\n"
           "  list                       列出 DDS 網路上的 topic 與 Agent\n"
           "  echo <uav_N_agent>         訂閱並顯示 Agent 狀態、姿態與軌跡\n"
           "  noop-command <ns|all>      發布無效命令 GCS_PROBE_NOOP（Agent 只記錄日誌）\n"
           "  shutdown <uav_N_agent>     呼叫 Agent shutdown service（需 --confirm）\n"
           "  fake-agent <uav_N_agent>   模擬 Agent（本機自我測試）\n"
           "選項：\n"
           "  --domain <id>              ROS_DOMAIN_ID（預設 0）\n"
           "  --interface <名稱或 IPv4>  指定網卡\n"
           "  --peer <位址>              單播探索入口，可重複\n"
           "  --external-address <IPv4>  對外公告的位址（位於 NAT 之後時使用）\n"
           "  --no-multicast             關閉 multicast（需搭配 --peer）\n"
           "  --participant-index <n>    固定 participant index\n"
           "  --duration <秒>            執行時間（預設 10）\n"
           "  --confirm                  確認執行 shutdown\n"
           "  --trace <檔案>             將 CycloneDDS 詳細追蹤寫入檔案\n";
}

/**
 * @brief 解析命令列
 * @param argc 參數數量
 * @param argv 參數內容
 * @return 解析結果；格式錯誤時回傳 std::nullopt
 */
std::optional<ProbeOptions> parse_options(int argc, char* argv[])
{
    if (argc < 2)
    {
        return std::nullopt;
    }
    ProbeOptions options;
    options.command = argv[1];
    std::vector<std::string> positional;
    for (int i = 2; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const bool has_value = (i + 1) < argc;
        try
        {
            if (arg == "--domain" && has_value)
            {
                options.network.domain_id = static_cast<uint32_t>(std::stoul(argv[++i]));
            }
            else if (arg == "--interface" && has_value)
            {
                options.network.network_interface = argv[++i];
            }
            else if (arg == "--peer" && has_value)
            {
                options.network.peers.emplace_back(argv[++i]);
            }
            else if (arg == "--external-address" && has_value)
            {
                options.network.external_address = argv[++i];
            }
            else if (arg == "--no-multicast")
            {
                options.network.is_multicast_allowed = false;
            }
            else if (arg == "--participant-index" && has_value)
            {
                options.network.participant_index = static_cast<uint32_t>(std::stoul(argv[++i]));
            }
            else if (arg == "--duration" && has_value)
            {
                options.duration_s = std::stod(argv[++i]);
            }
            else if (arg == "--trace" && has_value)
            {
                options.trace_file = argv[++i];
            }
            else if (arg == "--confirm")
            {
                options.is_confirmed = true;
            }
            else if (!arg.empty() && arg[0] == '-')
            {
                std::cerr << "未知選項：" << arg << "\n";
                return std::nullopt;
            }
            else
            {
                positional.push_back(arg);
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "選項 " << arg << " 的數值無效：" << e.what() << "\n";
            return std::nullopt;
        }
    }
    if (!positional.empty())
    {
        options.target = positional.front();
    }
    return options;
}

/**
 * @brief 建立 topic
 * @param participant participant
 * @param descriptor idlc 產生的型別描述
 * @param dds_topic DDS topic 名稱
 * @return topic 實體；失敗時為負值
 */
dds_entity_t create_topic(dds_entity_t participant, const dds_topic_descriptor_t* descriptor,
                          const std::string& dds_topic)
{
    const dds_entity_t topic = dds_create_topic(participant, descriptor, dds_topic.c_str(), nullptr, nullptr);
    if (topic < 0)
    {
        std::cerr << "建立 topic " << dds_topic << " 失敗：" << dds_strretcode(topic) << "\n";
    }
    return topic;
}

/**
 * @brief 依 QoS 表建立 reader
 * @return reader 實體；失敗時為負值
 */
dds_entity_t create_reader(dds_entity_t participant, const dds_topic_descriptor_t* descriptor,
                           const std::string& dds_topic, AgentChannel channel)
{
    const dds_entity_t topic = create_topic(participant, descriptor, dds_topic);
    if (topic < 0)
    {
        return topic;
    }
    const auto qos = create_dds_qos(qos_for(channel, EndpointRole::READER), EndpointRole::READER);
    const dds_entity_t reader = dds_create_reader(participant, topic, qos.get(), nullptr);
    if (reader < 0)
    {
        std::cerr << "建立 reader " << dds_topic << " 失敗：" << dds_strretcode(reader) << "\n";
    }
    return reader;
}

/**
 * @brief 依 QoS 表建立 writer
 * @return writer 實體；失敗時為負值
 */
dds_entity_t create_writer(dds_entity_t participant, const dds_topic_descriptor_t* descriptor,
                           const std::string& dds_topic, AgentChannel channel)
{
    const dds_entity_t topic = create_topic(participant, descriptor, dds_topic);
    if (topic < 0)
    {
        return topic;
    }
    const auto qos = create_dds_qos(qos_for(channel, EndpointRole::WRITER), EndpointRole::WRITER);
    const dds_entity_t writer = dds_create_writer(participant, topic, qos.get(), nullptr);
    if (writer < 0)
    {
        std::cerr << "建立 writer " << dds_topic << " 失敗：" << dds_strretcode(writer) << "\n";
    }
    return writer;
}

/**
 * @brief 等待 writer 與至少一個遠端 reader 配對
 * @param writer writer 實體
 * @param timeout_ms 最長等待時間
 * @return 配對數量；逾時為 0
 */
uint32_t wait_publication_matched(dds_entity_t writer, int timeout_ms)
{
    for (int waited = 0; waited < timeout_ms && g_is_running; waited += POLL_INTERVAL_MS)
    {
        dds_publication_matched_status_t status{};
        if (dds_get_publication_matched_status(writer, &status) == DDS_RETCODE_OK && status.current_count > 0)
        {
            return status.current_count;
        }
        dds_sleepfor(DDS_MSECS(POLL_INTERVAL_MS));
    }
    return 0;
}

/**
 * @brief 等待 reader 與至少一個遠端 writer 配對
 * @param reader reader 實體
 * @param timeout_ms 最長等待時間
 * @return 配對數量；逾時為 0
 */
uint32_t wait_subscription_matched(dds_entity_t reader, int timeout_ms)
{
    for (int waited = 0; waited < timeout_ms && g_is_running; waited += POLL_INTERVAL_MS)
    {
        dds_subscription_matched_status_t status{};
        if (dds_get_subscription_matched_status(reader, &status) == DDS_RETCODE_OK && status.current_count > 0)
        {
            return status.current_count;
        }
        dds_sleepfor(DDS_MSECS(POLL_INTERVAL_MS));
    }
    return 0;
}

/**
 * @brief 以借用緩衝取出樣本並逐一處理有效資料
 * @param reader reader 實體
 * @param handler 處理函式，參數為樣本指標
 * @return 處理的有效樣本數
 */
template <typename Sample, typename Handler>
int take_valid(dds_entity_t reader, Handler handler)
{
    void* samples[MAX_SAMPLES] = {};
    dds_sample_info_t infos[MAX_SAMPLES];
    const dds_return_t count = dds_take(reader, samples, infos, MAX_SAMPLES, MAX_SAMPLES);
    if (count <= 0)
    {
        return 0;
    }
    int valid_count = 0;
    for (dds_return_t i = 0; i < count; ++i)
    {
        // dispose／unregister 樣本沒有資料內容（CODE_CONSTRAINT L1-03）。
        if (!infos[i].valid_data || samples[i] == nullptr)
        {
            continue;
        }
        handler(*static_cast<const Sample*>(samples[i]));
        ++valid_count;
    }
    dds_return_loan(reader, samples, count);
    return valid_count;
}

/**
 * @brief 建立開啟詳細追蹤的 participant（只供排除網路問題）
 * @param network 網路設定
 * @param trace_file 追蹤輸出檔
 * @param error 失敗時的錯誤訊息
 * @return participant 實體；失敗時為負值
 */
dds_entity_t create_traced_participant(const DdsNetworkConfig& network, const std::string& trace_file,
                                       std::string* error)
{
    auto xml = build_cyclonedds_xml(network, error);
    if (!xml.has_value())
    {
        return DDS_RETCODE_BAD_PARAMETER;
    }
    const std::string default_tracing = "<Verbosity>warning</Verbosity><OutputFile>stderr</OutputFile>";
    const auto position = xml->find(default_tracing);
    if (position == std::string::npos)
    {
        *error = "找不到預設追蹤設定";
        return DDS_RETCODE_ERROR;
    }
    xml->replace(position, default_tracing.size(),
                 "<Verbosity>finest</Verbosity><OutputFile>" + trace_file + "</OutputFile>");
    const dds_entity_t domain = dds_create_domain(network.domain_id, xml->c_str());
    if (domain < 0)
    {
        *error = std::string("建立 DDS domain 失敗：") + dds_strretcode(domain);
        return domain;
    }
    return dds_create_participant(network.domain_id, nullptr, nullptr);
}

/** @brief 目前時間轉為 ROS builtin_interfaces/Time */
builtin_interfaces_msg_dds__Time_ now_stamp()
{
    const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
    const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(since_epoch).count();
    builtin_interfaces_msg_dds__Time_ stamp{};
    stamp.sec = static_cast<int32_t>(nanoseconds / 1000000000LL);
    stamp.nanosec = static_cast<uint32_t>(nanoseconds % 1000000000LL);
    return stamp;
}

// ============================================================================
// list：DCPSPublication 發現（項目 3）
// ============================================================================
int run_list(dds_entity_t participant, const ProbeOptions& options)
{
    const dds_entity_t reader = dds_create_reader(participant, DDS_BUILTIN_TOPIC_DCPSPUBLICATION, nullptr, nullptr);
    if (reader < 0)
    {
        std::cerr << "建立 DCPSPublication reader 失敗：" << dds_strretcode(reader) << "\n";
        return EXIT_FAILURE;
    }

    std::map<dds_instance_handle_t, std::string> topics_by_instance;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(options.duration_s);
    while (g_is_running && std::chrono::steady_clock::now() < deadline)
    {
        void* samples[MAX_SAMPLES] = {};
        dds_sample_info_t infos[MAX_SAMPLES];
        const dds_return_t count = dds_take(reader, samples, infos, MAX_SAMPLES, MAX_SAMPLES);
        for (dds_return_t i = 0; i < count; ++i)
        {
            const auto handle = infos[i].instance_handle;
            if (infos[i].valid_data && samples[i] != nullptr)
            {
                const auto* endpoint = static_cast<const dds_builtintopic_endpoint_t*>(samples[i]);
                const std::string name = endpoint->topic_name != nullptr ? endpoint->topic_name : "";
                if (topics_by_instance.emplace(handle, name).second)
                {
                    std::cout << "+ " << name << "  [" << (endpoint->type_name ? endpoint->type_name : "?")
                              << "]\n";
                }
            }
            if (infos[i].instance_state != DDS_IST_ALIVE)
            {
                const auto it = topics_by_instance.find(handle);
                if (it != topics_by_instance.end())
                {
                    std::cout << "- " << it->second << "\n";
                    topics_by_instance.erase(it);
                }
            }
        }
        if (count > 0)
        {
            dds_return_loan(reader, samples, count);
        }
        dds_sleepfor(DDS_MSECS(POLL_INTERVAL_MS));
    }

    std::set<std::string> agents;
    for (const auto& [handle, dds_topic] : topics_by_instance)
    {
        (void)handle;
        const auto ros_topic = from_dds_topic(dds_topic);
        if (!ros_topic.has_value())
        {
            continue;
        }
        const auto agent = agent_namespace_from_state_topic(*ros_topic);
        if (agent.has_value())
        {
            agents.insert(*agent);
        }
    }
    std::cout << "\n目前偵測到 " << agents.size() << " 台 Agent：";
    for (const auto& agent : agents)
    {
        std::cout << " " << agent;
    }
    std::cout << "\n";
    return EXIT_SUCCESS;
}

// ============================================================================
// echo：訂閱與解碼（項目 1、5）
// ============================================================================
int run_echo(dds_entity_t participant, const ProbeOptions& options)
{
    if (!is_agent_namespace(options.target))
    {
        std::cerr << "請指定單一 Agent，例如 uav_1_agent\n";
        return EXIT_FAILURE;
    }
    const auto state_reader = create_reader(participant, &taiphoon_agent_msg_dds__AgentState__desc,
                                            *to_dds_topic(agent_topic(options.target, "state")), AgentChannel::STATE);
    const auto attitude_reader =
        create_reader(participant, &taiphoon_agent_msg_dds__AgentAttitude__desc,
                      *to_dds_topic(agent_topic(options.target, "attitude")), AgentChannel::ATTITUDE);
    const auto trajectory_reader = create_reader(
        participant, &taiphoon_agent_msg_dds__AgentTrajectory__desc,
        *to_dds_topic(agent_topic(options.target, "planning/trajectory")), AgentChannel::PLANNING_TRAJECTORY);
    if (state_reader < 0 || attitude_reader < 0 || trajectory_reader < 0)
    {
        return EXIT_FAILURE;
    }

    int state_count = 0;
    int attitude_count = 0;
    int trajectory_count = 0;
    auto last_state_print = std::chrono::steady_clock::time_point{};
    auto last_attitude_print = last_state_print;
    const auto print_interval = std::chrono::duration<double>(ECHO_PRINT_INTERVAL_S);
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::duration<double>(options.duration_s);

    std::cout << std::fixed;
    while (g_is_running && std::chrono::steady_clock::now() < deadline)
    {
        const auto now = std::chrono::steady_clock::now();
        state_count += take_valid<taiphoon_agent_msg_dds__AgentState_>(
            state_reader, [&](const taiphoon_agent_msg_dds__AgentState_& state)
            {
                if (now - last_state_print < print_interval)
                {
                    return;
                }
                last_state_print = now;
                std::cout << std::setprecision(7) << "[state] stamp=" << state.stamp.sec << "." << state.stamp.nanosec
                          << " mode=" << static_cast<int>(state.flight_mode) << " armed=" << state.armed
                          << " landed=" << state.landed << " nav=" << static_cast<int>(state.px4_nav_state)
                          << " px4_valid=" << state.px4_state_valid << " online=" << state.online
                          << " gps=(" << state.position_global[0] << ", " << state.position_global[1] << ", "
                          << std::setprecision(2) << state.position_global[2] << ")"
                          << " ned=(" << state.position_ned[0] << ", " << state.position_ned[1] << ", "
                          << state.position_ned[2] << ")"
                          << " batt=" << state.battery_remaining << " " << state.battery_voltage_v << "V "
                          << state.battery_current_a << "A\n";
            });
        attitude_count += take_valid<taiphoon_agent_msg_dds__AgentAttitude_>(
            attitude_reader, [&](const taiphoon_agent_msg_dds__AgentAttitude_& attitude)
            {
                if (now - last_attitude_print < print_interval)
                {
                    return;
                }
                last_attitude_print = now;
                std::cout << std::setprecision(4) << "[attitude] q=(" << attitude.q[0] << ", " << attitude.q[1]
                          << ", " << attitude.q[2] << ", " << attitude.q[3] << ")\n";
            });
        trajectory_count += take_valid<taiphoon_agent_msg_dds__AgentTrajectory_>(
            trajectory_reader, [&](const taiphoon_agent_msg_dds__AgentTrajectory_& trajectory)
            {
                std::cout << "[trajectory] id=" << trajectory.trajectory_id
                          << " agent_index=" << trajectory.agent_index
                          << " points=" << trajectory.control_points._length / 3
                          << " duration=" << std::setprecision(2) << trajectory.duration_s
                          << " hold=" << trajectory.terminal_hold << " valid=" << trajectory.valid << "\n";
            });
        dds_sleepfor(DDS_MSECS(POLL_INTERVAL_MS));
    }

    const double elapsed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << std::setprecision(1) << "\n收到 state " << state_count << " 筆（" << state_count / elapsed_s
              << " Hz）、attitude " << attitude_count << " 筆（" << attitude_count / elapsed_s
              << " Hz）、trajectory " << trajectory_count << " 筆\n";
    return state_count > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

// ============================================================================
// noop-command：命令發布（項目 2）
// ============================================================================
int run_noop_command(dds_entity_t participant, const ProbeOptions& options)
{
    std::string ros_topic;
    if (options.target == "all")
    {
        ros_topic = std::string(ALL_AGENTS_COMMAND_TOPIC);
    }
    else if (is_agent_namespace(options.target))
    {
        ros_topic = agent_topic(options.target, "command");
    }
    else
    {
        std::cerr << "目標必須是 uav_N_agent 或 all\n";
        return EXIT_FAILURE;
    }

    const auto writer =
        create_writer(participant, &std_msgs_msg_dds__String__desc, *to_dds_topic(ros_topic), AgentChannel::COMMAND);
    if (writer < 0)
    {
        return EXIT_FAILURE;
    }
    const uint32_t matched = wait_publication_matched(writer, MATCH_TIMEOUT_MS);
    if (matched == 0)
    {
        std::cerr << ros_topic << " 在 " << MATCH_TIMEOUT_MS << " ms 內沒有任何訂閱者，未發布\n";
        return EXIT_FAILURE;
    }

    std_msgs_msg_dds__String_ message{};
    message.data = const_cast<char*>(NOOP_COMMAND);
    const dds_return_t result = dds_write(writer, &message);
    if (result != DDS_RETCODE_OK)
    {
        std::cerr << "發布失敗：" << dds_strretcode(result) << "\n";
        return EXIT_FAILURE;
    }
    // reliable writer 需要時間完成傳送與重送，過早刪除 participant 會遺失樣本。
    dds_wait_for_acks(writer, DDS_SECS(2));
    std::cout << "已發布 '" << NOOP_COMMAND << "' 至 " << ros_topic << "（訂閱者 " << matched << " 個）\n"
              << "請在 Agent 日誌確認 \"Received agent command\" 與 \"Unknown command received\"。\n";
    return EXIT_SUCCESS;
}

// ============================================================================
// shutdown：service 呼叫（項目 4）
// ============================================================================
int run_shutdown(dds_entity_t participant, const ProbeOptions& options)
{
    if (!is_agent_namespace(options.target))
    {
        std::cerr << "shutdown 只能指定單一 Agent，例如 uav_1_agent\n";
        return EXIT_FAILURE;
    }
    if (!options.is_confirmed)
    {
        std::cerr << "此操作會立即終止 Agent，不會先 RTL 或降落。確認目標已安全落地後加上 --confirm 再執行。\n";
        return EXIT_FAILURE;
    }

    const std::string service = agent_topic(options.target, "shutdown");
    const auto request_writer = create_writer(participant, &std_srvs_srv_dds__SetBool_Request__desc,
                                              *to_service_request_topic(service), AgentChannel::SHUTDOWN_SERVICE);
    const auto reply_reader = create_reader(participant, &std_srvs_srv_dds__SetBool_Response__desc,
                                            *to_service_reply_topic(service), AgentChannel::SHUTDOWN_SERVICE);
    if (request_writer < 0 || reply_reader < 0)
    {
        return EXIT_FAILURE;
    }

    // 回應為 volatile：必須等兩個方向都配對，否則回應可能在 reader 配對前送出而遺失。
    if (wait_publication_matched(request_writer, MATCH_TIMEOUT_MS) == 0
        || wait_subscription_matched(reply_reader, MATCH_TIMEOUT_MS) == 0)
    {
        std::cerr << service << " service 尚未就緒（請求或回應端點未配對）\n";
        return EXIT_FAILURE;
    }

    std::random_device random_device;
    std::mt19937_64 generator(random_device());
    std_srvs_srv_dds__SetBool_Request_ request{};
    request.client_guid = generator();
    request.sequence_number = 1;
    request.data = true;

    const dds_return_t result = dds_write(request_writer, &request);
    if (result != DDS_RETCODE_OK)
    {
        std::cerr << "送出 shutdown 請求失敗：" << dds_strretcode(result) << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "已送出 shutdown 請求（client_guid=" << std::hex << request.client_guid << std::dec
              << " seq=" << request.sequence_number << "）\n";

    bool has_reply = false;
    for (int waited = 0; waited < SHUTDOWN_REPLY_TIMEOUT_MS && !has_reply && g_is_running;
         waited += POLL_INTERVAL_MS)
    {
        take_valid<std_srvs_srv_dds__SetBool_Response_>(
            reply_reader, [&](const std_srvs_srv_dds__SetBool_Response_& reply)
            {
                // 回應會送給所有 client，只接受標頭與本次請求相同者。
                if (reply.client_guid != request.client_guid || reply.sequence_number != request.sequence_number)
                {
                    return;
                }
                has_reply = true;
                std::cout << "收到回應：success=" << reply.success
                          << " message=" << (reply.message != nullptr ? reply.message : "") << "\n";
            });
        dds_sleepfor(DDS_MSECS(POLL_INTERVAL_MS));
    }
    if (!has_reply)
    {
        std::cerr << "等待 shutdown 回應逾時\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

// ============================================================================
// fake-agent：本機自我測試
// ============================================================================
int run_fake_agent(dds_entity_t participant, const ProbeOptions& options)
{
    if (!is_agent_namespace(options.target))
    {
        std::cerr << "請指定 Agent namespace，例如 uav_1_agent\n";
        return EXIT_FAILURE;
    }
    const std::string service = agent_topic(options.target, "shutdown");
    const auto state_writer = create_writer(participant, &taiphoon_agent_msg_dds__AgentState__desc,
                                            *to_dds_topic(agent_topic(options.target, "state")), AgentChannel::STATE);
    const auto command_reader =
        create_reader(participant, &std_msgs_msg_dds__String__desc,
                      *to_dds_topic(agent_topic(options.target, "command")), AgentChannel::COMMAND);
    const auto all_command_reader = create_reader(participant, &std_msgs_msg_dds__String__desc,
                                                  *to_dds_topic(std::string(ALL_AGENTS_COMMAND_TOPIC)),
                                                  AgentChannel::COMMAND);
    const auto request_reader = create_reader(participant, &std_srvs_srv_dds__SetBool_Request__desc,
                                              *to_service_request_topic(service), AgentChannel::SHUTDOWN_SERVICE);
    const auto reply_writer = create_writer(participant, &std_srvs_srv_dds__SetBool_Response__desc,
                                            *to_service_reply_topic(service), AgentChannel::SHUTDOWN_SERVICE);
    if (state_writer < 0 || command_reader < 0 || all_command_reader < 0 || request_reader < 0 || reply_writer < 0)
    {
        return EXIT_FAILURE;
    }

    std::cout << "模擬 " << options.target << "，按 Ctrl+C 結束\n";
    const auto period = std::chrono::milliseconds(1000 / FAKE_AGENT_RATE_HZ);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(options.duration_s);
    bool is_shutdown_accepted = false;
    while (g_is_running && !is_shutdown_accepted && std::chrono::steady_clock::now() < deadline)
    {
        taiphoon_agent_msg_dds__AgentState_ state{};
        state.stamp = now_stamp();
        state.position_global[0] = FAKE_LATITUDE_DEG;
        state.position_global[1] = FAKE_LONGITUDE_DEG;
        state.position_global[2] = 10.0;
        state.position_ned[2] = -5.0F;
        state.flight_mode = FLIGHT_STATE_HOVER;
        state.battery_remaining = 0.85F;
        state.battery_voltage_v = 16.2F;
        state.battery_current_a = 3.5F;
        state.online = true;
        state.armed = true;
        state.landed = false;
        state.px4_nav_state = PX4_NAV_STATE_OFFBOARD;
        state.px4_state_valid = true;
        dds_write(state_writer, &state);

        const auto print_command = [](const std_msgs_msg_dds__String_& message)
        {
            std::cout << "[command] " << (message.data != nullptr ? message.data : "") << "\n";
        };
        take_valid<std_msgs_msg_dds__String_>(command_reader, print_command);
        take_valid<std_msgs_msg_dds__String_>(all_command_reader, print_command);

        take_valid<std_srvs_srv_dds__SetBool_Request_>(
            request_reader, [&](const std_srvs_srv_dds__SetBool_Request_& request)
            {
                std_srvs_srv_dds__SetBool_Response_ reply{};
                reply.client_guid = request.client_guid;
                reply.sequence_number = request.sequence_number;
                reply.success = request.data;
                reply.message = const_cast<char*>(request.data ? "fake agent 接受關閉" : "data 必須為 true");
                dds_write(reply_writer, &reply);
                std::cout << "[shutdown] 收到請求 seq=" << request.sequence_number << "，已回應\n";
                is_shutdown_accepted = request.data;
            });
        std::this_thread::sleep_for(period);
    }
    if (is_shutdown_accepted)
    {
        dds_wait_for_acks(reply_writer, DDS_SECS(1));
        std::cout << "模擬 Agent 結束（相當於 Agent 容器 PID 1 終止）\n";
    }
    return EXIT_SUCCESS;
}

}  // namespace

/**
 * @brief 程式入口
 * @param argc 參數數量
 * @param argv 參數內容
 * @return 程式結束碼
 */
int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::signal(SIGINT, handle_signal);

    const auto options = parse_options(argc, argv);
    if (!options.has_value())
    {
        print_usage();
        return EXIT_FAILURE;
    }

    std::string error;
    dds_entity_t participant = 0;
    if (options->trace_file.empty())
    {
        participant = create_participant(options->network, &error);
    }
    else
    {
        participant = create_traced_participant(options->network, options->trace_file, &error);
    }
    if (participant < 0)
    {
        std::cerr << error << "\n";
        return EXIT_FAILURE;
    }

    int result = EXIT_FAILURE;
    if (options->command == "list")
    {
        result = run_list(participant, *options);
    }
    else if (options->command == "echo")
    {
        result = run_echo(participant, *options);
    }
    else if (options->command == "noop-command")
    {
        result = run_noop_command(participant, *options);
    }
    else if (options->command == "shutdown")
    {
        result = run_shutdown(participant, *options);
    }
    else if (options->command == "fake-agent")
    {
        result = run_fake_agent(participant, *options);
    }
    else
    {
        print_usage();
    }

    dds_delete(DDS_CYCLONEDDS_HANDLE);
    return result;
}
