/**
 * @file test_cdr_wire_format.cpp
 * @brief 驗證產生型別的 CDR 網路格式與 ROS2 Humble rmw_cyclonedds 相同
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * rmw_cyclonedds 以 XCDR1（CDR_LE）編解碼，8 位元組欄位對齊 8；XCDR2 只對齊 4，
 * 若誤用 XCDR2，AgentState.position_global 會錯位。本測試取出 writer 實際送出的序列化
 * 資料逐位元組核對，並驗證 service 請求標頭位於樣本最前面。
 */
#include "link/dds_config.hpp"
#include "link/qos_profile.hpp"

#include "ros_common.h"
#include "taiphoon_agent.h"

#include <dds/dds.h>
#include <dds/ddsi/ddsi_serdata.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace gcs::link;

namespace
{

/// 測試專用 domain
constexpr uint32_t TEST_DOMAIN_ID = 42;
/// RTPS 封裝標頭長度（encoding id 2 位元組 + options 2 位元組）
constexpr size_t ENCAPSULATION_HEADER_SIZE = 4;
/// CDR_LE（XCDR1 little endian）的 encoding id
constexpr uint8_t CDR_LE_ID[] = {0x00, 0x01};
/// 浮點比較容許誤差
constexpr double EPSILON = 1e-9;

/**
 * @brief 在單一 participant 內以本地傳遞取得 writer 序列化結果的測試夾具
 */
class CdrWireFormat : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        DdsNetworkConfig config;
        config.domain_id = TEST_DOMAIN_ID;
        std::string error;
        participant_ = create_participant(config, &error);
        ASSERT_GT(participant_, 0) << error;
    }

    static void TearDownTestSuite()
    {
        dds_delete(DDS_CYCLONEDDS_HANDLE);
    }

    /**
     * @brief 建立同名 topic 的 writer 與 reader
     * @param descriptor 型別描述
     * @param topic_name topic 名稱（各測試唯一）
     */
    void create_endpoints(const dds_topic_descriptor_t* descriptor, const std::string& topic_name)
    {
        const dds_entity_t topic = dds_create_topic(participant_, descriptor, topic_name.c_str(), nullptr, nullptr);
        ASSERT_GT(topic, 0);
        const auto writer_qos =
            create_dds_qos(qos_for(AgentChannel::COMMAND, EndpointRole::WRITER), EndpointRole::WRITER);
        const auto reader_qos =
            create_dds_qos(qos_for(AgentChannel::COMMAND, EndpointRole::READER), EndpointRole::READER);
        writer_ = dds_create_writer(participant_, topic, writer_qos.get(), nullptr);
        reader_ = dds_create_reader(participant_, topic, reader_qos.get(), nullptr);
        ASSERT_GT(writer_, 0);
        ASSERT_GT(reader_, 0);
    }

    /**
     * @brief 寫入樣本並取回 writer 產生的序列化位元組（含封裝標頭）
     * @param sample 樣本指標
     * @return 序列化位元組；失敗時為空
     */
    std::vector<uint8_t> write_and_take_bytes(const void* sample)
    {
        EXPECT_EQ(dds_write(writer_, sample), DDS_RETCODE_OK);
        struct ddsi_serdata* serdata = nullptr;
        dds_sample_info_t info{};
        const dds_return_t count = dds_takecdr(reader_, &serdata, 1, &info, DDS_ANY_STATE);
        EXPECT_EQ(count, 1);
        if (count != 1 || serdata == nullptr)
        {
            return {};
        }
        const uint32_t size = ddsi_serdata_size(serdata);
        std::vector<uint8_t> bytes(size);
        ddsi_serdata_to_ser(serdata, 0, size, bytes.data());
        ddsi_serdata_unref(serdata);
        return bytes;
    }

    static dds_entity_t participant_;
    dds_entity_t writer_ = 0;
    dds_entity_t reader_ = 0;
};

dds_entity_t CdrWireFormat::participant_ = 0;

/**
 * @brief 從位元組讀取 little endian 數值
 * @param bytes 位元組
 * @param offset 位移
 * @return 數值
 */
template <typename T>
T read_le(const std::vector<uint8_t>& bytes, size_t offset)
{
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

}  // namespace

TEST_F(CdrWireFormat, ServiceRequestHeaderPrecedesPayload)
{
    create_endpoints(&std_srvs_srv_dds__SetBool_Request__desc, "test_set_bool_request");

    std_srvs_srv_dds__SetBool_Request_ request{};
    request.client_guid = 0x0102030405060708ULL;
    request.sequence_number = 42;
    request.data = true;
    const auto bytes = write_and_take_bytes(&request);

    // 標頭（client_guid、sequence_number 各 8 位元組）+ data（1 位元組）
    const size_t payload_size = 8 + 8 + 1;
    ASSERT_GE(bytes.size(), ENCAPSULATION_HEADER_SIZE + payload_size);
    EXPECT_EQ(bytes[0], CDR_LE_ID[0]);
    EXPECT_EQ(bytes[1], CDR_LE_ID[1]);
    const size_t base = ENCAPSULATION_HEADER_SIZE;
    EXPECT_EQ(read_le<uint64_t>(bytes, base), request.client_guid);
    EXPECT_EQ(read_le<int64_t>(bytes, base + 8), request.sequence_number);
    EXPECT_EQ(bytes[base + 16], 1);
}

TEST_F(CdrWireFormat, AgentStateUsesXcdr1Alignment)
{
    create_endpoints(&taiphoon_agent_msg_dds__AgentState__desc, "test_agent_state");

    taiphoon_agent_msg_dds__AgentState_ state{};
    state.stamp.sec = 100;
    state.stamp.nanosec = 200;
    state.position_ned[0] = 1.5F;
    state.position_global[0] = 25.03997;
    state.position_global[1] = 121.51195;
    state.position_global[2] = 12.5;
    state.flight_mode = 5;
    state.battery_remaining = 0.75F;
    state.px4_nav_state = 14;
    state.px4_state_valid = true;
    const auto bytes = write_and_take_bytes(&state);

    // XCDR1 位移：stamp 0、position_ned 8、position_global 24（對齊 8）、flight_mode 48、
    // battery_remaining 52、px4_nav_state 67、px4_state_valid 68，總長 69。
    constexpr size_t PAYLOAD_SIZE = 69;
    const size_t base = ENCAPSULATION_HEADER_SIZE;
    ASSERT_GE(bytes.size(), base + PAYLOAD_SIZE);
    EXPECT_EQ(bytes[0], CDR_LE_ID[0]);
    EXPECT_EQ(bytes[1], CDR_LE_ID[1]);
    EXPECT_EQ(read_le<int32_t>(bytes, base + 0), 100);
    EXPECT_EQ(read_le<uint32_t>(bytes, base + 4), 200U);
    EXPECT_LT(std::abs(read_le<float>(bytes, base + 8) - 1.5F), EPSILON);
    EXPECT_LT(std::abs(read_le<double>(bytes, base + 24) - 25.03997), EPSILON);
    EXPECT_LT(std::abs(read_le<double>(bytes, base + 32) - 121.51195), EPSILON);
    EXPECT_LT(std::abs(read_le<double>(bytes, base + 40) - 12.5), EPSILON);
    EXPECT_EQ(bytes[base + 48], 5);
    EXPECT_LT(std::abs(read_le<float>(bytes, base + 52) - 0.75F), EPSILON);
    EXPECT_EQ(bytes[base + 67], 14);
    EXPECT_EQ(bytes[base + 68], 1);
}

TEST_F(CdrWireFormat, TrajectorySequenceRoundTrip)
{
    create_endpoints(&taiphoon_agent_msg_dds__AgentTrajectory__desc, "test_agent_trajectory");

    double points[] = {25.0, 121.5, 10.0, 25.1, 121.6, 11.0};
    taiphoon_agent_msg_dds__AgentTrajectory_ trajectory{};
    trajectory.trajectory_id = 7;
    trajectory.agent_index = 2;
    trajectory.coordinate_frame = taiphoon_agent_msg_dds__AgentTrajectory_Constants_COORDINATE_FRAME_WGS84_GEODETIC;
    trajectory.control_points._buffer = points;
    trajectory.control_points._length = 6;
    trajectory.control_points._maximum = 6;
    trajectory.control_points._release = false;
    trajectory.valid = true;
    ASSERT_EQ(dds_write(writer_, &trajectory), DDS_RETCODE_OK);

    void* samples[1] = {nullptr};
    dds_sample_info_t info{};
    ASSERT_EQ(dds_take(reader_, samples, &info, 1, 1), 1);
    ASSERT_TRUE(info.valid_data);
    const auto* received = static_cast<const taiphoon_agent_msg_dds__AgentTrajectory_*>(samples[0]);
    EXPECT_EQ(received->trajectory_id, 7U);
    EXPECT_EQ(received->agent_index, 2U);
    ASSERT_EQ(received->control_points._length, 6U);
    for (uint32_t i = 0; i < received->control_points._length; ++i)
    {
        EXPECT_LT(std::abs(received->control_points._buffer[i] - points[i]), EPSILON);
    }
    EXPECT_TRUE(received->valid);
    dds_return_loan(reader_, samples, 1);
}
