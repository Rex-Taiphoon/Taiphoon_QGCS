/**
 * @file cyclone_agent_link.cpp
 * @brief CycloneDDS IAgentLink 實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "link/cyclone_agent_link.hpp"

#include "link/message_conversion.hpp"
#include "link/qos_profile.hpp"
#include "link/ros_naming.hpp"

#include "ros_common.h"
#include "taiphoon_agent.h"

#include <QLoggingCategory>

#include <algorithm>
#include <random>

namespace
{
Q_LOGGING_CATEGORY(lc_link, "gcs.link")

/// 單次 take 的樣本上限
constexpr uint32_t MAX_SAMPLES = 32;

/**
 * @brief 以借用緩衝取出樣本並逐一處理有效資料
 * @param reader reader 實體
 * @param handler 處理函式
 */
template <typename Sample, typename Handler>
void take_valid(dds_entity_t reader, Handler handler)
{
    void* samples[MAX_SAMPLES] = {};
    dds_sample_info_t infos[MAX_SAMPLES];
    const dds_return_t count = dds_take(reader, samples, infos, MAX_SAMPLES, MAX_SAMPLES);
    if (count <= 0)
    {
        return;
    }
    for (dds_return_t i = 0; i < count; ++i)
    {
        // dispose／unregister 樣本沒有資料內容（CODE_CONSTRAINT L1-03）
        if (infos[i].valid_data && samples[i] != nullptr)
        {
            handler(*static_cast<const Sample*>(samples[i]));
        }
    }
    dds_return_loan(reader, samples, count);
}

/**
 * @brief 寫入錯誤訊息
 * @param error 輸出位置（可為 nullptr）
 * @param message 錯誤訊息
 */
void set_error(QString* error, const QString& message)
{
    if (error != nullptr)
    {
        *error = message;
    }
}

/**
 * @brief 取得 writer 目前配對的遠端 reader 數量
 * @param writer writer 實體
 * @return 配對數量；查詢失敗為 0
 */
uint32_t matched_readers(dds_entity_t writer)
{
    dds_publication_matched_status_t status{};
    return dds_get_publication_matched_status(writer, &status) == DDS_RETCODE_OK ? status.current_count : 0;
}

/**
 * @brief 取得 reader 目前配對的遠端 writer 數量
 * @param reader reader 實體
 * @return 配對數量；查詢失敗為 0
 */
uint32_t matched_writers(dds_entity_t reader)
{
    dds_subscription_matched_status_t status{};
    return dds_get_subscription_matched_status(reader, &status) == DDS_RETCODE_OK ? status.current_count : 0;
}

}  // namespace

namespace gcs::link
{

CycloneAgentLink::CycloneAgentLink(QObject* parent) : IAgentLink(parent)
{
}

CycloneAgentLink::~CycloneAgentLink()
{
    // 必須在成員釋放前刪除 DDS 實體，確保 listener 不再使用本物件。
    stop();
}

bool CycloneAgentLink::start(const DdsNetworkConfig& config, QString* error)
{
    stop();

    std::string config_error;
    const auto xml = build_cyclonedds_xml(config, &config_error);
    if (!xml.has_value())
    {
        set_error(error, QString::fromStdString(config_error));
        return false;
    }
    domain_ = dds_create_domain(config.domain_id, xml->c_str());
    if (domain_ < 0)
    {
        set_error(error, QStringLiteral("建立 DDS domain 失敗：%1").arg(QString::fromUtf8(dds_strretcode(domain_))));
        domain_ = 0;
        return false;
    }
    domain_id_ = config.domain_id;
    participant_ = dds_create_participant(config.domain_id, nullptr, nullptr);
    if (participant_ < 0)
    {
        set_error(error,
                  QStringLiteral("建立 DDS participant 失敗：%1").arg(QString::fromUtf8(dds_strretcode(participant_))));
        participant_ = 0;
        stop();
        return false;
    }

    std::random_device random_device;
    client_guid_ = (static_cast<uint64_t>(random_device()) << 32U) | random_device();

    discovery_context_ = std::make_unique<ReaderContext>();
    discovery_context_->link = this;
    discovery_context_->channel = Channel::DISCOVERY;
    dds_listener_t* listener = dds_create_listener(discovery_context_.get());
    dds_lset_data_available(listener, &CycloneAgentLink::data_available_cb);
    discovery_reader_ = dds_create_reader(participant_, DDS_BUILTIN_TOPIC_DCPSPUBLICATION, nullptr, listener);
    dds_delete_listener(listener);

    const auto command_qos = create_dds_qos(qos_for(AgentChannel::COMMAND, EndpointRole::WRITER), EndpointRole::WRITER);
    const dds_entity_t all_topic = find_or_create_topic(&std_msgs_msg_dds__String__desc,
                                                        *to_dds_topic(std::string(ALL_AGENTS_COMMAND_TOPIC)));
    all_command_writer_ = all_topic > 0 ? dds_create_writer(participant_, all_topic, command_qos.get(), nullptr) : all_topic;
    if (discovery_reader_ < 0 || all_command_writer_ < 0)
    {
        set_error(error, QStringLiteral("建立探索 reader 或全體命令 writer 失敗"));
        stop();
        return false;
    }

    // 網路設定改變後重新啟動時，保留核心模組已訂閱的 Agent。
    for (const QString& agent : subscribed_agents_)
    {
        create_agent_endpoints(agent);
    }
    qCInfo(lc_link) << "DDS 已啟動：domain" << config.domain_id << "peers" << config.peers.size();
    return true;
}

void CycloneAgentLink::stop()
{
    if (domain_ > 0)
    {
        // 刪除 domain 會一併刪除所有子實體，並等待執行中的 listener 結束。
        dds_delete(domain_);
        qCInfo(lc_link) << "DDS 已停止";
    }
    domain_ = 0;
    participant_ = 0;
    discovery_reader_ = 0;
    all_command_writer_ = 0;
    topics_.clear();
    agents_.clear();
    discovery_context_.reset();
    std::lock_guard<std::mutex> lock(discovery_mutex_);
    publications_.clear();
}

bool CycloneAgentLink::is_running() const
{
    return participant_ > 0;
}

dds_entity_t CycloneAgentLink::find_or_create_topic(const dds_topic_descriptor_t* descriptor, const std::string& name)
{
    const auto it = topics_.find(name);
    if (it != topics_.end())
    {
        return it->second;
    }
    const dds_entity_t topic = dds_create_topic(participant_, descriptor, name.c_str(), nullptr, nullptr);
    if (topic < 0)
    {
        qCWarning(lc_link) << "建立 topic 失敗：" << QString::fromStdString(name) << dds_strretcode(topic);
        return topic;
    }
    topics_.emplace(name, topic);
    return topic;
}

dds_entity_t CycloneAgentLink::create_listened_reader(const dds_topic_descriptor_t* descriptor,
                                                      const std::string& dds_topic, const dds_qos_t* qos,
                                                      ReaderContext* context)
{
    const dds_entity_t topic = find_or_create_topic(descriptor, dds_topic);
    if (topic < 0)
    {
        return topic;
    }
    dds_listener_t* listener = dds_create_listener(context);
    dds_lset_data_available(listener, &CycloneAgentLink::data_available_cb);
    const dds_entity_t reader = dds_create_reader(participant_, topic, qos, listener);
    dds_delete_listener(listener);
    if (reader < 0)
    {
        qCWarning(lc_link) << "建立 reader 失敗：" << QString::fromStdString(dds_topic) << dds_strretcode(reader);
    }
    return reader;
}

bool CycloneAgentLink::create_agent_endpoints(const QString& agent_ns)
{
    if (!is_running() || agents_.count(agent_ns) > 0)
    {
        return is_running();
    }
    const std::string ns = agent_ns.toStdString();
    AgentEndpoints endpoints;

    struct ReaderSpec
    {
        const dds_topic_descriptor_t* descriptor;
        const char* suffix;
        AgentChannel qos_channel;
        Channel channel;
    };
    const ReaderSpec specs[] = {
        {&taiphoon_agent_msg_dds__AgentState__desc, "state", AgentChannel::STATE, Channel::STATE},
        {&taiphoon_agent_msg_dds__AgentAttitude__desc, "attitude", AgentChannel::ATTITUDE, Channel::ATTITUDE},
        {&taiphoon_agent_msg_dds__NedTarget__desc, "ned_target", AgentChannel::NED_TARGET, Channel::NED_TARGET},
        {&taiphoon_agent_msg_dds__WaypointSequence__desc, "waypoints", AgentChannel::WAYPOINTS, Channel::WAYPOINTS},
        {&taiphoon_agent_msg_dds__AgentTrajectory__desc, "planning/trajectory", AgentChannel::PLANNING_TRAJECTORY,
         Channel::TRAJECTORY},
    };
    bool is_ok = true;
    for (const auto& spec : specs)
    {
        auto context = std::make_unique<ReaderContext>(ReaderContext{this, agent_ns, spec.channel});
        const auto qos = create_dds_qos(qos_for(spec.qos_channel, EndpointRole::READER), EndpointRole::READER);
        const dds_entity_t reader = create_listened_reader(
            spec.descriptor, *to_dds_topic(agent_topic(ns, spec.suffix)), qos.get(), context.get());
        is_ok = is_ok && reader > 0;
        if (reader > 0)
        {
            endpoints.entities.push_back(reader);
            endpoints.contexts.push_back(std::move(context));
        }
    }

    // 上線時就建立命令 writer，讓 DDS 有時間完成配對，避免第一筆命令遺失。
    const auto command_qos = create_dds_qos(qos_for(AgentChannel::COMMAND, EndpointRole::WRITER), EndpointRole::WRITER);
    const dds_entity_t command_topic =
        find_or_create_topic(&std_msgs_msg_dds__String__desc, *to_dds_topic(agent_topic(ns, "command")));
    endpoints.command_writer =
        command_topic > 0 ? dds_create_writer(participant_, command_topic, command_qos.get(), nullptr) : command_topic;

    const std::string service = agent_topic(ns, "shutdown");
    const auto service_writer_qos =
        create_dds_qos(qos_for(AgentChannel::SHUTDOWN_SERVICE, EndpointRole::WRITER), EndpointRole::WRITER);
    const auto service_reader_qos =
        create_dds_qos(qos_for(AgentChannel::SHUTDOWN_SERVICE, EndpointRole::READER), EndpointRole::READER);
    const dds_entity_t request_topic =
        find_or_create_topic(&std_srvs_srv_dds__SetBool_Request__desc, *to_service_request_topic(service));
    endpoints.shutdown_request_writer =
        request_topic > 0 ? dds_create_writer(participant_, request_topic, service_writer_qos.get(), nullptr)
                          : request_topic;
    auto reply_context = std::make_unique<ReaderContext>(ReaderContext{this, agent_ns, Channel::SHUTDOWN_REPLY});
    endpoints.shutdown_reply_reader =
        create_listened_reader(&std_srvs_srv_dds__SetBool_Response__desc, *to_service_reply_topic(service),
                               service_reader_qos.get(), reply_context.get());
    endpoints.contexts.push_back(std::move(reply_context));

    for (const dds_entity_t entity :
         {endpoints.command_writer, endpoints.shutdown_request_writer, endpoints.shutdown_reply_reader})
    {
        is_ok = is_ok && entity > 0;
        if (entity > 0)
        {
            endpoints.entities.push_back(entity);
        }
    }
    agents_.emplace(agent_ns, std::move(endpoints));
    if (!is_ok)
    {
        qCWarning(lc_link) << "部分端點建立失敗：" << agent_ns;
    }
    return is_ok;
}

void CycloneAgentLink::delete_agent_endpoints(const QString& agent_ns)
{
    const auto it = agents_.find(agent_ns);
    if (it == agents_.end())
    {
        return;
    }
    // 先刪除 reader（會等待 listener 結束），再釋放其 context。
    for (const dds_entity_t entity : it->second.entities)
    {
        dds_delete(entity);
    }
    agents_.erase(it);
}

void CycloneAgentLink::subscribe_agent(const QString& agent_ns)
{
    if (!is_agent_namespace(agent_ns.toStdString()))
    {
        qCWarning(lc_link) << "拒絕訂閱不合法的 Agent namespace：" << agent_ns;
        return;
    }
    subscribed_agents_.insert(agent_ns);
    create_agent_endpoints(agent_ns);
}

void CycloneAgentLink::unsubscribe_agent(const QString& agent_ns)
{
    subscribed_agents_.erase(agent_ns);
    delete_agent_endpoints(agent_ns);
}

QStringList CycloneAgentLink::discovered_state_agents() const
{
    std::set<QString> agents;
    {
        std::lock_guard<std::mutex> lock(discovery_mutex_);
        for (const auto& [handle, discovered] : publications_)
        {
            Q_UNUSED(handle)
            if (discovered.type != QLatin1String(TYPE_AGENT_STATE.data(), static_cast<int>(TYPE_AGENT_STATE.size())))
            {
                continue;
            }
            const auto ros_topic = from_dds_topic(discovered.topic.toStdString());
            if (!ros_topic.has_value())
            {
                continue;
            }
            const auto agent = agent_namespace_from_state_topic(*ros_topic);
            if (agent.has_value())
            {
                agents.insert(QString::fromStdString(*agent));
            }
        }
    }
    return QStringList(agents.cbegin(), agents.cend());
}

std::vector<CycloneAgentLink::DiscoveredTopic> CycloneAgentLink::discovered_topics() const
{
    std::vector<DiscoveredTopic> topics;
    {
        std::lock_guard<std::mutex> lock(discovery_mutex_);
        for (const auto& [handle, discovered] : publications_)
        {
            Q_UNUSED(handle)
            topics.push_back(discovered);
        }
    }
    std::sort(topics.begin(), topics.end(),
              [](const DiscoveredTopic& a, const DiscoveredTopic& b)
              { return a.topic < b.topic || (a.topic == b.topic && a.type < b.type); });
    topics.erase(std::unique(topics.begin(), topics.end(),
                             [](const DiscoveredTopic& a, const DiscoveredTopic& b)
                             { return a.topic == b.topic && a.type == b.type; }),
                 topics.end());
    return topics;
}

bool CycloneAgentLink::publish_command(const QString& target, const QString& command_line, QString* error)
{
    if (!is_running())
    {
        set_error(error, QStringLiteral("DDS 尚未啟動"));
        return false;
    }
    dds_entity_t writer = 0;
    if (target == ALL_AGENTS_TARGET)
    {
        writer = all_command_writer_;
    }
    else
    {
        const auto it = agents_.find(target);
        writer = it != agents_.end() ? it->second.command_writer : 0;
    }
    if (writer <= 0)
    {
        set_error(error, QStringLiteral("Agent 尚未上線，沒有命令通道"));
        return false;
    }
    // command topic 為 volatile：沒有訂閱者時寫入的命令會直接遺失。
    if (matched_readers(writer) == 0)
    {
        set_error(error, QStringLiteral("尚無訂閱者，Agent 可能尚未完成連線"));
        return false;
    }

    const QByteArray utf8 = command_line.toUtf8();
    std_msgs_msg_dds__String_ message{};
    message.data = const_cast<char*>(utf8.constData());
    const dds_return_t result = dds_write(writer, &message);
    if (result != DDS_RETCODE_OK)
    {
        set_error(error, QStringLiteral("DDS 寫入失敗：%1").arg(QString::fromUtf8(dds_strretcode(result))));
        return false;
    }
    return true;
}

bool CycloneAgentLink::request_shutdown(const QString& agent_ns, quint64 request_id, QString* error)
{
    const auto it = agents_.find(agent_ns);
    if (!is_running() || it == agents_.end() || it->second.shutdown_request_writer <= 0
        || it->second.shutdown_reply_reader <= 0)
    {
        set_error(error, QStringLiteral("Agent 尚未上線，沒有 shutdown 通道"));
        return false;
    }
    // 回應為 volatile：請求與回應兩個方向都必須已配對，否則回應可能遺失。
    if (matched_readers(it->second.shutdown_request_writer) == 0
        || matched_writers(it->second.shutdown_reply_reader) == 0)
    {
        set_error(error, QStringLiteral("shutdown service 尚未配對"));
        return false;
    }

    std_srvs_srv_dds__SetBool_Request_ request{};
    request.client_guid = client_guid_;
    request.sequence_number = static_cast<int64_t>(request_id);
    request.data = true;
    const dds_return_t result = dds_write(it->second.shutdown_request_writer, &request);
    if (result != DDS_RETCODE_OK)
    {
        set_error(error, QStringLiteral("DDS 寫入失敗：%1").arg(QString::fromUtf8(dds_strretcode(result))));
        return false;
    }
    return true;
}

void CycloneAgentLink::data_available_cb(dds_entity_t reader, void* arg)
{
    const auto* context = static_cast<const ReaderContext*>(arg);
    if (context == nullptr || context->link == nullptr)
    {
        return;
    }
    if (context->channel == Channel::DISCOVERY)
    {
        context->link->handle_discovery(reader);
    }
    else
    {
        context->link->handle_agent_data(reader, *context);
    }
}

void CycloneAgentLink::handle_discovery(dds_entity_t reader)
{
    void* samples[MAX_SAMPLES] = {};
    dds_sample_info_t infos[MAX_SAMPLES];
    const dds_return_t count = dds_take(reader, samples, infos, MAX_SAMPLES, MAX_SAMPLES);
    if (count <= 0)
    {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(discovery_mutex_);
        for (dds_return_t i = 0; i < count; ++i)
        {
            const dds_instance_handle_t handle = infos[i].instance_handle;
            if (infos[i].instance_state != DDS_IST_ALIVE)
            {
                // writer 刪除或其 participant 離開：視為 topic 消失
                publications_.erase(handle);
                continue;
            }
            if (!infos[i].valid_data || samples[i] == nullptr)
            {
                continue;
            }
            const auto* endpoint = static_cast<const dds_builtintopic_endpoint_t*>(samples[i]);
            DiscoveredTopic discovered;
            discovered.topic = QString::fromUtf8(endpoint->topic_name != nullptr ? endpoint->topic_name : "");
            discovered.type = QString::fromUtf8(endpoint->type_name != nullptr ? endpoint->type_name : "");
            publications_[handle] = discovered;
        }
    }
    dds_return_loan(reader, samples, count);
}

void CycloneAgentLink::handle_agent_data(dds_entity_t reader, const ReaderContext& context)
{
    const QString& agent = context.agent_ns;
    switch (context.channel)
    {
    case Channel::STATE:
        take_valid<taiphoon_agent_msg_dds__AgentState_>(
            reader, [&](const auto& sample) { emit state_received(agent, to_core(sample)); });
        break;
    case Channel::ATTITUDE:
        take_valid<taiphoon_agent_msg_dds__AgentAttitude_>(
            reader, [&](const auto& sample) { emit attitude_received(agent, to_core(sample)); });
        break;
    case Channel::NED_TARGET:
        take_valid<taiphoon_agent_msg_dds__NedTarget_>(
            reader, [&](const auto& sample) { emit ned_target_received(agent, to_core(sample)); });
        break;
    case Channel::WAYPOINTS:
        take_valid<taiphoon_agent_msg_dds__WaypointSequence_>(
            reader, [&](const auto& sample) { emit waypoints_received(agent, to_core(sample)); });
        break;
    case Channel::TRAJECTORY:
        take_valid<taiphoon_agent_msg_dds__AgentTrajectory_>(
            reader, [&](const auto& sample) { emit trajectory_received(agent, to_core(sample)); });
        break;
    case Channel::SHUTDOWN_REPLY:
        take_valid<std_srvs_srv_dds__SetBool_Response_>(
            reader,
            [&](const auto& reply)
            {
                // 回應會送給所有 client，只處理本程式送出的請求
                if (reply.client_guid != client_guid_)
                {
                    return;
                }
                emit shutdown_replied(agent, static_cast<quint64>(reply.sequence_number), reply.success,
                                      QString::fromUtf8(reply.message != nullptr ? reply.message : ""));
            });
        break;
    case Channel::DISCOVERY:
        break;
    }
}

}  // namespace gcs::link
