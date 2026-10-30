/**
 * @file test_cyclone_agent_link.cpp
 * @brief CycloneAgentLink：以同一程序內的原生 DDS 端點模擬 Agent，驗證探索、遙測、命令與 shutdown
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 模擬 Agent 與 link 位於同一 domain 的不同 participant，走 CycloneDDS 本地傳遞，
 * 不需要網路，也不會觸發防火牆提示。與實際 rmw_cyclonedds 的互通已於 M1 驗證。
 */
#include "link/cyclone_agent_link.hpp"
#include "link/qos_profile.hpp"
#include "link/ros_naming.hpp"

#include "ros_common.h"
#include "taiphoon_agent.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

using namespace gcs;
using namespace gcs::link;

namespace
{

/// 測試專用 domain
constexpr uint32_t TEST_DOMAIN_ID = 44;
/// 等待非同步事件的上限（毫秒）
constexpr int WAIT_LIMIT_MS = 5000;
/// 事件輪詢間隔（毫秒）
constexpr int POLL_INTERVAL_MS = 10;

const QString AGENT_1 = QStringLiteral("uav_1_agent");

/**
 * @brief 處理事件直到條件成立或逾時
 * @param condition 條件
 * @return 條件是否成立
 */
bool wait_until(const std::function<bool()>& condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition())
    {
        if (timer.elapsed() > WAIT_LIMIT_MS)
        {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, POLL_INTERVAL_MS);
        dds_sleepfor(DDS_MSECS(POLL_INTERVAL_MS));
    }
    return true;
}

/**
 * @class CycloneAgentLinkTest
 * @brief link 與模擬 Agent participant
 */
class CycloneAgentLinkTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        DdsNetworkConfig config;
        config.domain_id = TEST_DOMAIN_ID;
        QString error;
        ASSERT_TRUE(link_.start(config, &error)) << error.toStdString();
        // link 已建立 domain，模擬 Agent 在同一 domain 再建立一個 participant
        agent_participant_ = dds_create_participant(TEST_DOMAIN_ID, nullptr, nullptr);
        ASSERT_GT(agent_participant_, 0);

        // 以主執行緒的接收者承接 DDS 執行緒發出的 signal（自動排入佇列）
        QObject::connect(&link_, &IAgentLink::state_received, &receiver_,
                         [this](const QString& agent, const AgentStateData& state)
                         {
                             last_state_agent_ = agent;
                             last_state_ = state;
                             ++state_count_;
                         });
        QObject::connect(&link_, &IAgentLink::shutdown_replied, &receiver_,
                         [this](const QString& agent, quint64 request_id, bool is_success, const QString& message)
                         {
                             reply_agent_ = agent;
                             reply_id_ = request_id;
                             is_reply_success_ = is_success;
                             reply_message_ = message;
                             ++reply_count_;
                         });
    }

    void TearDown() override
    {
        dds_delete(agent_participant_);
        link_.stop();
    }

    /**
     * @brief 在模擬 Agent 端建立 writer
     * @param descriptor 型別
     * @param name DDS topic 名稱
     * @param channel QoS 通道
     * @return writer
     */
    dds_entity_t agent_writer(const dds_topic_descriptor_t* descriptor, const std::string& name, AgentChannel channel)
    {
        const dds_entity_t topic = dds_create_topic(agent_participant_, descriptor, name.c_str(), nullptr, nullptr);
        const auto qos = create_dds_qos(qos_for(channel, EndpointRole::WRITER), EndpointRole::WRITER);
        return dds_create_writer(agent_participant_, topic, qos.get(), nullptr);
    }

    /**
     * @brief 在模擬 Agent 端建立 reader
     * @return reader
     */
    dds_entity_t agent_reader(const dds_topic_descriptor_t* descriptor, const std::string& name, AgentChannel channel)
    {
        const dds_entity_t topic = dds_create_topic(agent_participant_, descriptor, name.c_str(), nullptr, nullptr);
        const auto qos = create_dds_qos(qos_for(channel, EndpointRole::READER), EndpointRole::READER);
        return dds_create_reader(agent_participant_, topic, qos.get(), nullptr);
    }

    CycloneAgentLink link_;
    QObject receiver_;
    dds_entity_t agent_participant_ = 0;
    QString last_state_agent_;
    AgentStateData last_state_;
    int state_count_ = 0;
    QString reply_agent_;
    quint64 reply_id_ = 0;
    bool is_reply_success_ = false;
    QString reply_message_;
    int reply_count_ = 0;
};

}  // namespace

TEST_F(CycloneAgentLinkTest, DiscoversStateWriterAndForgetsItWhenDeleted)
{
    const dds_entity_t writer = agent_writer(&taiphoon_agent_msg_dds__AgentState__desc,
                                             "rt/uav_1_agent/state", AgentChannel::STATE);
    ASSERT_GT(writer, 0);
    // 不符合介面規格的名稱不得被當成 Agent
    const dds_entity_t legacy = agent_writer(&taiphoon_agent_msg_dds__AgentState__desc,
                                             "rt/fmu1_agent/state", AgentChannel::STATE);
    ASSERT_GT(legacy, 0);

    ASSERT_TRUE(wait_until([this]() { return link_.discovered_state_agents() == QStringList{AGENT_1}; }));

    dds_delete(writer);
    EXPECT_TRUE(wait_until([this]() { return link_.discovered_state_agents().isEmpty(); }));
}

TEST_F(CycloneAgentLinkTest, SubscribedAgentStateIsDecoded)
{
    link_.subscribe_agent(AGENT_1);
    const dds_entity_t writer = agent_writer(&taiphoon_agent_msg_dds__AgentState__desc,
                                             "rt/uav_1_agent/state", AgentChannel::STATE);
    ASSERT_GT(writer, 0);

    taiphoon_agent_msg_dds__AgentState_ sample{};
    sample.position_global[0] = 25.0399700;
    sample.flight_mode = 5;
    sample.online = true;
    sample.px4_state_valid = true;
    ASSERT_TRUE(wait_until(
        [&]()
        {
            dds_write(writer, &sample);
            return state_count_ > 0;
        }));
    EXPECT_EQ(last_state_agent_, AGENT_1);
    EXPECT_EQ(last_state_.flight_mode, 5);
    EXPECT_NEAR(last_state_.position_global[0], 25.03997, 1e-9);

    // 取消訂閱後不再收到
    link_.unsubscribe_agent(AGENT_1);
    QCoreApplication::processEvents();
    const int count_after_unsubscribe = state_count_;
    dds_write(writer, &sample);
    dds_sleepfor(DDS_MSECS(200));
    QCoreApplication::processEvents();
    EXPECT_EQ(state_count_, count_after_unsubscribe);
}

TEST_F(CycloneAgentLinkTest, CommandRequiresSubscriberThenIsDelivered)
{
    link_.subscribe_agent(AGENT_1);
    QString error;
    EXPECT_FALSE(link_.publish_command(AGENT_1, QStringLiteral("HOLD"), &error));
    EXPECT_TRUE(error.contains(QStringLiteral("尚無訂閱者")));
    EXPECT_FALSE(link_.publish_command(QStringLiteral("uav_2_agent"), QStringLiteral("HOLD"), &error));

    const dds_entity_t reader =
        agent_reader(&std_msgs_msg_dds__String__desc, "rt/uav_1_agent/command", AgentChannel::COMMAND);
    ASSERT_GT(reader, 0);
    ASSERT_TRUE(wait_until([&]() { return link_.publish_command(AGENT_1, QStringLiteral("TAKEOFF 3"), &error); }));

    std::string received;
    ASSERT_TRUE(wait_until(
        [&]()
        {
            void* samples[1] = {nullptr};
            dds_sample_info_t info{};
            if (dds_take(reader, samples, &info, 1, 1) == 1 && info.valid_data)
            {
                received = static_cast<const std_msgs_msg_dds__String_*>(samples[0])->data;
                dds_return_loan(reader, samples, 1);
            }
            return !received.empty();
        }));
    EXPECT_EQ(received, "TAKEOFF 3");
}

TEST_F(CycloneAgentLinkTest, ShutdownServiceRoundTrip)
{
    link_.subscribe_agent(AGENT_1);
    QString error;
    EXPECT_FALSE(link_.request_shutdown(AGENT_1, 1, &error));

    // 模擬 agent_supervisor 的 service 端點
    const dds_entity_t request_reader = agent_reader(&std_srvs_srv_dds__SetBool_Request__desc,
                                                     "rq/uav_1_agent/shutdownRequest", AgentChannel::SHUTDOWN_SERVICE);
    const dds_entity_t reply_writer = agent_writer(&std_srvs_srv_dds__SetBool_Response__desc,
                                                   "rr/uav_1_agent/shutdownReply", AgentChannel::SHUTDOWN_SERVICE);
    ASSERT_GT(request_reader, 0);
    ASSERT_GT(reply_writer, 0);

    constexpr quint64 REQUEST_ID = 42;
    ASSERT_TRUE(wait_until([&]() { return link_.request_shutdown(AGENT_1, REQUEST_ID, &error); }));

    std_srvs_srv_dds__SetBool_Request_ request{};
    ASSERT_TRUE(wait_until(
        [&]()
        {
            void* samples[1] = {nullptr};
            dds_sample_info_t info{};
            if (dds_take(request_reader, samples, &info, 1, 1) == 1 && info.valid_data)
            {
                request = *static_cast<const std_srvs_srv_dds__SetBool_Request_*>(samples[0]);
                dds_return_loan(request_reader, samples, 1);
                return true;
            }
            return false;
        }));
    EXPECT_TRUE(request.data);
    EXPECT_EQ(request.sequence_number, static_cast<int64_t>(REQUEST_ID));

    // 其他 client 的回應必須被忽略
    std_srvs_srv_dds__SetBool_Response_ other{};
    other.client_guid = request.client_guid + 1;
    other.sequence_number = request.sequence_number;
    other.success = true;
    other.message = const_cast<char*>("其他 client");
    dds_write(reply_writer, &other);

    std_srvs_srv_dds__SetBool_Response_ reply{};
    reply.client_guid = request.client_guid;
    reply.sequence_number = request.sequence_number;
    reply.success = true;
    reply.message = const_cast<char*>("關閉請求已接受");
    dds_write(reply_writer, &reply);

    ASSERT_TRUE(wait_until([this]() { return reply_count_ > 0; }));
    dds_sleepfor(DDS_MSECS(100));
    QCoreApplication::processEvents();
    EXPECT_EQ(reply_count_, 1);
    EXPECT_EQ(reply_agent_, AGENT_1);
    EXPECT_EQ(reply_id_, REQUEST_ID);
    EXPECT_TRUE(is_reply_success_);
    EXPECT_EQ(reply_message_, QStringLiteral("關閉請求已接受"));
}

TEST_F(CycloneAgentLinkTest, RestartKeepsSubscriptions)
{
    link_.subscribe_agent(AGENT_1);
    dds_delete(agent_participant_);

    DdsNetworkConfig config;
    config.domain_id = TEST_DOMAIN_ID;
    QString error;
    ASSERT_TRUE(link_.start(config, &error)) << error.toStdString();
    agent_participant_ = dds_create_participant(TEST_DOMAIN_ID, nullptr, nullptr);
    ASSERT_GT(agent_participant_, 0);

    const dds_entity_t writer = agent_writer(&taiphoon_agent_msg_dds__AgentState__desc,
                                             "rt/uav_1_agent/state", AgentChannel::STATE);
    taiphoon_agent_msg_dds__AgentState_ sample{};
    sample.online = true;
    EXPECT_TRUE(wait_until(
        [&]()
        {
            dds_write(writer, &sample);
            return state_count_ > 0;
        }));
}

TEST_F(CycloneAgentLinkTest, MissionPlanRequiresSubscriberThenIsDelivered)
{
    WaypointMissionData mission;
    mission.mission_id = 77;
    mission.altitude_frame = MISSION_ALTITUDE_PX4_LOCAL;
    MissionWaypointData waypoint;
    waypoint.latitude_deg = 25.0399700;
    waypoint.longitude_deg = 121.5650000;
    waypoint.altitude_m = 12.0F;
    waypoint.dwell_s = 3.0F;
    mission.waypoints = {waypoint, waypoint};
    mission.waypoints[1].altitude_m = 18.0F;

    QString error;
    EXPECT_FALSE(link_.publish_mission_plan(AGENT_1, mission, &error));
    link_.subscribe_agent(AGENT_1);
    EXPECT_FALSE(link_.publish_mission_plan(AGENT_1, mission, &error));
    EXPECT_TRUE(error.contains(QStringLiteral("沒有訂閱航點任務")));

    const dds_entity_t reader = agent_reader(&taiphoon_agent_msg_dds__WaypointMission__desc,
                                             "rt/uav_1_agent/mission/plan", AgentChannel::MISSION_PLAN);
    ASSERT_GT(reader, 0);
    ASSERT_TRUE(wait_until([&]() { return link_.publish_mission_plan(AGENT_1, mission, &error); }));

    uint32_t received_id = 0;
    std::vector<float> altitudes;
    ASSERT_TRUE(wait_until(
        [&]()
        {
            void* samples[1] = {nullptr};
            dds_sample_info_t info{};
            if (dds_take(reader, samples, &info, 1, 1) == 1 && info.valid_data)
            {
                const auto* plan = static_cast<const taiphoon_agent_msg_dds__WaypointMission_*>(samples[0]);
                received_id = plan->mission_id;
                for (uint32_t i = 0; i < plan->waypoints._length; ++i)
                {
                    altitudes.push_back(plan->waypoints._buffer[i].altitude_m);
                }
                dds_return_loan(reader, samples, 1);
            }
            return received_id != 0;
        }));
    EXPECT_EQ(received_id, 77U);
    ASSERT_EQ(altitudes.size(), 2U);
    EXPECT_FLOAT_EQ(altitudes[1], 18.0F);
}

TEST_F(CycloneAgentLinkTest, MissionStatusEventsAndHealthAreDecoded)
{
    WaypointMissionStatusData last_status;
    AgentEventData last_event;
    AgentHealthData last_health;
    int status_count = 0;
    int event_count = 0;
    int health_count = 0;
    // 接收者在區域變數之後建立、之前解構，lambda 不會存取已解構的變數
    QObject receiver;
    QObject::connect(&link_, &IAgentLink::mission_status_received, &receiver,
                     [&](const QString&, const WaypointMissionStatusData& status)
                     {
                         last_status = status;
                         ++status_count;
                     });
    QObject::connect(&link_, &IAgentLink::event_received, &receiver,
                     [&](const QString&, const AgentEventData& event)
                     {
                         last_event = event;
                         ++event_count;
                     });
    QObject::connect(&link_, &IAgentLink::health_received, &receiver,
                     [&](const QString&, const AgentHealthData& health)
                     {
                         last_health = health;
                         ++health_count;
                     });

    // mission/status 為 transient_local：先寫入再訂閱也要收到最後一筆
    const dds_entity_t status_writer = agent_writer(&taiphoon_agent_msg_dds__WaypointMissionStatus__desc,
                                                    "rt/uav_1_agent/mission/status", AgentChannel::MISSION_STATUS);
    ASSERT_GT(status_writer, 0);
    char status_detail[] = "";
    taiphoon_agent_msg_dds__WaypointMissionStatus_ status{};
    status.mission_id = 77;
    status.state = static_cast<uint8_t>(AgentMissionState::RUNNING);
    status.waypoint_count = 2;
    status.detail = status_detail;
    ASSERT_EQ(dds_write(status_writer, &status), DDS_RETCODE_OK);
    link_.subscribe_agent(AGENT_1);
    ASSERT_TRUE(wait_until([&]() { return status_count > 0; }));
    EXPECT_EQ(last_status.mission_id, 77U);
    EXPECT_EQ(last_status.state, static_cast<uint8_t>(AgentMissionState::RUNNING));

    const dds_entity_t event_writer =
        agent_writer(&taiphoon_agent_msg_dds__AgentEvent__desc, "rt/uav_1_agent/events", AgentChannel::EVENTS);
    const dds_entity_t health_writer =
        agent_writer(&taiphoon_agent_msg_dds__AgentHealth__desc, "rt/uav_1_agent/health", AgentChannel::HEALTH);
    ASSERT_GT(event_writer, 0);
    ASSERT_GT(health_writer, 0);
    char command[] = "ARM";
    char detail[] = "preflight fail";
    taiphoon_agent_msg_dds__AgentEvent_ event{};
    event.sequence_ = 5;
    event.code = agent_event_code::PX4_COMMAND_REJECTED;
    event.command = command;
    event.px4_result = 1;
    event.detail = detail;
    taiphoon_agent_msg_dds__AgentHealth_ health{};
    health.px4_status_age_s = 30.0F;
    ASSERT_TRUE(wait_until(
        [&]()
        {
            if (event_count == 0)
            {
                dds_write(event_writer, &event);
            }
            if (health_count == 0)
            {
                dds_write(health_writer, &health);
            }
            return event_count > 0 && health_count > 0;
        }));
    EXPECT_EQ(last_event.sequence, 5U);
    EXPECT_EQ(last_event.command, QStringLiteral("ARM"));
    EXPECT_EQ(last_event.detail, QStringLiteral("preflight fail"));
    EXPECT_FLOAT_EQ(last_health.px4_status_age_s, 30.0F);
}
