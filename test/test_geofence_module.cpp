/**
 * @file test_geofence_module.cpp
 * @brief GeofenceModule：版本號接續、請求流程與各機圍籬狀況整理
 * @author Taiphoon GCS Team
 * @date 2026-11-06
 */
#include "core/command_protocol.hpp"
#include "fakes/geofence_samples.hpp"
#include "modules/geofence/geofence_module.hpp"

#include <QSignalSpy>
#include <QTest>

#include <gtest/gtest.h>

using namespace gcs;
using gcs::test::make_fence_status;
using gcs::test::square_fence;

namespace
{

const QString AGENT_1 = QStringLiteral("uav_1_agent");
const QString AGENT_2 = QStringLiteral("uav_2_agent");
const QString AGENT_3 = QStringLiteral("uav_3_agent");
const QString AGENT_10 = QStringLiteral("uav_10_agent");

/**
 * @brief 縮短等待時間的參數
 * @return 時間參數
 */
GeofenceTiming fast_timing()
{
    GeofenceTiming timing;
    timing.report_timeout_ms = 80;
    timing.tick_ms = 10;
    return timing;
}

/**
 * @brief 機隊清單
 * @param online 線上
 * @param offline 離線（資料停滯）
 * @return 快照
 */
FleetSnapshot fleet(const QStringList& online, const QStringList& offline = {})
{
    FleetSnapshot snapshot;
    snapshot.online = online;
    snapshot.offline = offline;
    return snapshot;
}

/**
 * @brief 依機號找總覽中的一列
 * @param overview 總覽
 * @param agent Agent namespace
 * @return 該列；找不到時 agent 為空
 */
GeofenceAgentRow row_of(const GeofenceOverview& overview, const QString& agent)
{
    for (const GeofenceAgentRow& row : overview.rows)
    {
        if (row.agent == agent)
        {
            return row;
        }
    }
    return {};
}

/**
 * @class GeofenceModuleTest
 * @brief 已初始化的 GeofenceModule；送出的請求一律由測試回報結果
 */
class GeofenceModuleTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(module_.init());
        QObject::connect(&module_, &GeofenceModule::geofence_requested, &module_,
                         [this](const GeofenceData& fence) { requests_.push_back(fence); });
    }

    /**
     * @brief 送出設定並回報發布成功
     * @return 送出的版本號
     */
    uint32_t apply_square()
    {
        GeofenceData draft;
        draft.vertices = square_fence();
        module_.request_set(draft);
        EXPECT_FALSE(requests_.empty());
        module_.on_geofence_acked(command_protocol::make_ack(ALL_AGENTS_TARGET, GEOFENCE_ACK_NAME, true));
        return requests_.back().fence_id;
    }

    GeofenceModule module_{fast_timing()};
    std::vector<GeofenceData> requests_;
};

}  // namespace

TEST_F(GeofenceModuleTest, FenceIdContinuesFromAgentReports)
{
    EXPECT_EQ(module_.next_fence_id(), 1U);
    module_.on_fleet_changed(fleet({AGENT_1, AGENT_2}));
    module_.on_geofence_status_updated(AGENT_1, make_fence_status(GeofenceState::ACTIVE, 5, 5));
    // 被拒的請求也會占用版本號
    module_.on_geofence_status_updated(AGENT_2,
                                       make_fence_status(GeofenceState::ACTIVE, 5, 7, GeofenceResult::REJECTED_INVALID));
    EXPECT_EQ(module_.next_fence_id(), 8U);

    GeofenceData draft;
    draft.vertices = square_fence();
    draft.fence_id = 99;
    draft.action = GeofenceAction::CLEAR;
    module_.request_set(draft);
    ASSERT_EQ(requests_.size(), 1U);
    EXPECT_EQ(requests_[0].fence_id, 8U);
    EXPECT_EQ(requests_[0].action, GeofenceAction::SET);
    EXPECT_EQ(requests_[0].vertices.size(), 4U);

    // Guard 拒絕也不重複使用版本號
    module_.on_geofence_acked(command_protocol::make_ack(ALL_AGENTS_TARGET, GEOFENCE_ACK_NAME, false, "x"));
    module_.request_clear();
    ASSERT_EQ(requests_.size(), 2U);
    EXPECT_EQ(requests_[1].fence_id, 9U);
    EXPECT_EQ(requests_[1].action, GeofenceAction::CLEAR);
}

TEST_F(GeofenceModuleTest, AckForwardedAndOnlyPublishedRequestTracked)
{
    QSignalSpy acked(&module_, &GeofenceModule::request_acked);
    GeofenceData draft;
    draft.vertices = square_fence();
    module_.request_set(draft);
    module_.on_geofence_acked(command_protocol::make_ack(ALL_AGENTS_TARGET, GEOFENCE_ACK_NAME, false, "拒絕"));
    ASSERT_EQ(acked.size(), 1);
    EXPECT_FALSE(acked.at(0).at(0).value<CommandAck>().is_ok);
    EXPECT_EQ(module_.overview().requested_fence_id, 0U);

    // 其他命令的回報與沒有等待中的請求時都忽略
    module_.on_geofence_acked(command_protocol::make_ack(AGENT_1, QStringLiteral("ARM"), true));
    module_.on_geofence_acked(command_protocol::make_ack(ALL_AGENTS_TARGET, GEOFENCE_ACK_NAME, true));
    EXPECT_EQ(acked.size(), 1);

    const uint32_t id = apply_square();
    EXPECT_EQ(acked.size(), 2);
    EXPECT_EQ(module_.overview().requested_fence_id, id);
}

TEST_F(GeofenceModuleTest, RowsFollowRequestAndReports)
{
    module_.on_fleet_changed(fleet({AGENT_10, AGENT_2, AGENT_1}));
    const uint32_t id = apply_square();

    // 依機號排序；送出後尚未回報為等待回報
    const GeofenceOverview waiting = module_.overview();
    ASSERT_EQ(waiting.rows.size(), 3U);
    EXPECT_EQ(waiting.rows[0].agent, AGENT_1);
    EXPECT_EQ(waiting.rows[2].agent, AGENT_10);
    EXPECT_EQ(waiting.rows[0].phase, GeofenceAgentPhase::WAITING);

    module_.on_geofence_status_updated(AGENT_1, make_fence_status(GeofenceState::ACTIVE, id, id));
    module_.on_geofence_status_updated(AGENT_2, make_fence_status(GeofenceState::PENDING, id, id));
    auto status = make_fence_status(GeofenceState::DISABLED, 0, id, GeofenceResult::REJECTED_TOO_FAR);
    status.detail = QStringLiteral("vertex too far from home");
    module_.on_geofence_status_updated(AGENT_10, status);

    const GeofenceOverview reported = module_.overview();
    EXPECT_EQ(row_of(reported, AGENT_1).phase, GeofenceAgentPhase::ACTIVE);
    EXPECT_EQ(row_of(reported, AGENT_1).fence_id, id);
    EXPECT_EQ(row_of(reported, AGENT_2).phase, GeofenceAgentPhase::PENDING);
    EXPECT_EQ(row_of(reported, AGENT_10).phase, GeofenceAgentPhase::REJECTED);
    EXPECT_EQ(row_of(reported, AGENT_10).fence_id, 0U);
    EXPECT_EQ(row_of(reported, AGENT_10).detail, status.detail);
    // 多數機的版本為新圍籬；拒絕的機仍是無圍籬，不一致
    EXPECT_EQ(reported.fence_id, id);
    EXPECT_EQ(reported.vertices.size(), 4U);
    EXPECT_FALSE(reported.is_consistent);
}

TEST_F(GeofenceModuleTest, MissingReportsTimeOut)
{
    module_.on_fleet_changed(fleet({AGENT_1, AGENT_2}));
    // 上線後還沒有任何回報：等待，逾時後未回報
    EXPECT_EQ(row_of(module_.overview(), AGENT_1).phase, GeofenceAgentPhase::WAITING);
    QSignalSpy changed(&module_, &GeofenceModule::overview_changed);
    EXPECT_TRUE(changed.wait(1000));
    EXPECT_EQ(row_of(module_.overview(), AGENT_1).phase, GeofenceAgentPhase::NO_REPORT);

    // 有舊圍籬但沒有處理新設定：先等待，逾時後未回報（列中保留目前版本）
    module_.on_geofence_status_updated(AGENT_1, make_fence_status(GeofenceState::ACTIVE, 1, 1));
    module_.on_geofence_status_updated(AGENT_2, make_fence_status(GeofenceState::ACTIVE, 1, 1));
    const uint32_t id = apply_square();
    module_.on_geofence_status_updated(AGENT_1, make_fence_status(GeofenceState::ACTIVE, id, id));
    EXPECT_EQ(row_of(module_.overview(), AGENT_2).phase, GeofenceAgentPhase::WAITING);
    QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(row_of(module_.overview(), AGENT_2).phase),
                              static_cast<int>(GeofenceAgentPhase::NO_REPORT), 1000);
    EXPECT_EQ(row_of(module_.overview(), AGENT_2).fence_id, 1U);
    EXPECT_EQ(row_of(module_.overview(), AGENT_1).phase, GeofenceAgentPhase::ACTIVE);
}

TEST_F(GeofenceModuleTest, MajorityVersionShownAndMinorityMarked)
{
    module_.on_fleet_changed(fleet({AGENT_1, AGENT_2, AGENT_3}));
    const auto small = square_fence(5.0);
    module_.on_geofence_status_updated(AGENT_1, make_fence_status(GeofenceState::ACTIVE, 3, 3));
    module_.on_geofence_status_updated(AGENT_2, make_fence_status(GeofenceState::ACTIVE, 3, 3));
    module_.on_geofence_status_updated(
        AGENT_3, make_fence_status(GeofenceState::ACTIVE, 2, 2, GeofenceResult::ACCEPTED, small, 1.0F));

    GeofenceOverview overview = module_.overview();
    EXPECT_EQ(overview.fence_id, 3U);
    EXPECT_EQ(overview.vertices, square_fence());
    EXPECT_FLOAT_EQ(overview.margin_m, geofence_limits::DEFAULT_MARGIN_M);
    EXPECT_FALSE(overview.is_consistent);
    EXPECT_EQ(row_of(overview, AGENT_1).phase, GeofenceAgentPhase::ACTIVE);
    EXPECT_EQ(row_of(overview, AGENT_3).phase, GeofenceAgentPhase::MISMATCH);

    // 票數相同時取較新的版本
    module_.on_fleet_changed(fleet({AGENT_2, AGENT_3}));
    overview = module_.overview();
    EXPECT_EQ(overview.rows.size(), 2U);
    EXPECT_EQ(overview.fence_id, 3U);

    // 多數機沒有圍籬時不顯示圍籬
    module_.on_fleet_changed(fleet({AGENT_1, AGENT_2, AGENT_3}));
    module_.on_geofence_status_updated(AGENT_1, make_fence_status(GeofenceState::DISABLED, 0, 4));
    module_.on_geofence_status_updated(AGENT_2, make_fence_status(GeofenceState::DISABLED, 0, 4));
    overview = module_.overview();
    EXPECT_EQ(overview.fence_id, 0U);
    EXPECT_TRUE(overview.vertices.empty());
    EXPECT_EQ(row_of(overview, AGENT_1).phase, GeofenceAgentPhase::NONE);
    EXPECT_EQ(row_of(overview, AGENT_3).phase, GeofenceAgentPhase::MISMATCH);
}

TEST_F(GeofenceModuleTest, RequestedVersionNeverMarkedMismatch)
{
    module_.on_fleet_changed(fleet({AGENT_1, AGENT_2, AGENT_3}));
    for (const QString& agent : {AGENT_1, AGENT_2, AGENT_3})
    {
        module_.on_geofence_status_updated(agent, make_fence_status(GeofenceState::ACTIVE, 1, 1));
    }
    const uint32_t id = apply_square();
    module_.on_geofence_status_updated(AGENT_1, make_fence_status(GeofenceState::ACTIVE, id, id));
    module_.on_geofence_status_updated(AGENT_2,
                                       make_fence_status(GeofenceState::ACTIVE, 1, id, GeofenceResult::REJECTED_INVALID));
    module_.on_geofence_status_updated(AGENT_3,
                                       make_fence_status(GeofenceState::ACTIVE, 1, id, GeofenceResult::REJECTED_INVALID));
    const GeofenceOverview overview = module_.overview();
    // 多數機保留舊圍籬；套用新版本的機標示生效，拒絕的機標示被拒
    EXPECT_EQ(overview.fence_id, 1U);
    EXPECT_EQ(row_of(overview, AGENT_1).phase, GeofenceAgentPhase::ACTIVE);
    EXPECT_EQ(row_of(overview, AGENT_2).phase, GeofenceAgentPhase::REJECTED);
}

TEST_F(GeofenceModuleTest, ClearRequestSettlesAsNone)
{
    module_.on_fleet_changed(fleet({AGENT_1}));
    module_.on_geofence_status_updated(AGENT_1, make_fence_status(GeofenceState::ACTIVE, 2, 2));
    module_.request_clear();
    module_.on_geofence_acked(command_protocol::make_ack(ALL_AGENTS_TARGET, GEOFENCE_ACK_NAME, true));
    const uint32_t id = requests_.back().fence_id;
    EXPECT_EQ(module_.overview().requested_action, GeofenceAction::CLEAR);
    EXPECT_EQ(row_of(module_.overview(), AGENT_1).phase, GeofenceAgentPhase::WAITING);
    module_.on_geofence_status_updated(AGENT_1, make_fence_status(GeofenceState::DISABLED, 0, id));
    EXPECT_EQ(row_of(module_.overview(), AGENT_1).phase, GeofenceAgentPhase::NONE);
    EXPECT_TRUE(module_.overview().is_consistent);
    EXPECT_EQ(module_.overview().fence_id, 0U);
}

TEST_F(GeofenceModuleTest, PositionOutsideAndFleetChanges)
{
    module_.on_fleet_changed(fleet({AGENT_1, AGENT_2}));
    auto outside = make_fence_status(GeofenceState::ACTIVE, 1, 1);
    outside.is_position_inside = false;
    module_.on_geofence_status_updated(AGENT_1, outside);
    EXPECT_FALSE(row_of(module_.overview(), AGENT_1).is_position_inside);

    // 資料停滯：列移除但保留狀態，恢復時不必等待重送
    module_.on_fleet_changed(fleet({AGENT_2}, {AGENT_1}));
    EXPECT_EQ(module_.overview().rows.size(), 1U);
    module_.on_fleet_changed(fleet({AGENT_1, AGENT_2}));
    EXPECT_EQ(row_of(module_.overview(), AGENT_1).phase, GeofenceAgentPhase::ACTIVE);

    // 完全離線：狀態一併清除
    module_.on_fleet_changed(fleet({AGENT_2}));
    module_.on_fleet_changed(fleet({AGENT_1, AGENT_2}));
    EXPECT_EQ(row_of(module_.overview(), AGENT_1).phase, GeofenceAgentPhase::WAITING);

    // 上線前收到的狀態（transient_local 先到）保留
    module_.on_geofence_status_updated(AGENT_3, make_fence_status(GeofenceState::ACTIVE, 1, 1));
    module_.on_fleet_changed(fleet({AGENT_1, AGENT_2, AGENT_3}));
    EXPECT_EQ(row_of(module_.overview(), AGENT_3).phase, GeofenceAgentPhase::ACTIVE);
}

TEST_F(GeofenceModuleTest, NotInitializedRejectsRequests)
{
    module_.shutdown();
    QSignalSpy acked(&module_, &GeofenceModule::request_acked);
    GeofenceData draft;
    draft.vertices = square_fence();
    module_.request_set(draft);
    EXPECT_TRUE(requests_.empty());
    ASSERT_EQ(acked.size(), 1);
    EXPECT_FALSE(acked.at(0).at(0).value<CommandAck>().is_ok);
}
