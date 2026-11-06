/**
 * @file test_geofence_controller.cpp
 * @brief GeofenceController：草稿編輯、顯示原點換算、套用與清除確認、各機狀況提示
 * @author Taiphoon GCS Team
 * @date 2026-11-06
 */
#include "app/geofence_controller.hpp"
#include "core/command_protocol.hpp"
#include "fakes/geofence_samples.hpp"

#include <QSignalSpy>
#include <QTest>

#include <gtest/gtest.h>

#include <cmath>

using namespace gcs;
using gcs::test::offset_from_center;
using gcs::test::square_fence;

namespace
{

const QString AGENT_1 = QStringLiteral("uav_1_agent");
const QString AGENT_2 = QStringLiteral("uav_2_agent");
constexpr double EPSILON = 1e-9;

/**
 * @brief 在地圖顯示座標新增頂點
 * @param controller 控制器
 * @param vertex 顯示座標
 */
void add(GeofenceController& controller, const GeofenceVertexData& vertex)
{
    controller.addVertex(vertex.latitude_deg, vertex.longitude_deg);
}

/**
 * @brief 只有一列的總覽
 * @param phase 狀況
 * @param fence_id 該機版本
 * @return 總覽
 */
GeofenceAgentRow make_row(const QString& agent, GeofenceAgentPhase phase, uint32_t fence_id = 0)
{
    GeofenceAgentRow row;
    row.agent = agent;
    row.phase = phase;
    row.fence_id = fence_id;
    return row;
}

/**
 * @brief 生效中的總覽
 * @param fence_id 版本
 * @param margin_m margin
 * @return 總覽
 */
GeofenceOverview active_overview(uint32_t fence_id, float margin_m = 1.0F)
{
    GeofenceOverview overview;
    overview.rows = {make_row(AGENT_1, GeofenceAgentPhase::ACTIVE, fence_id),
                     make_row(AGENT_2, GeofenceAgentPhase::ACTIVE, fence_id)};
    overview.fence_id = fence_id;
    overview.margin_m = margin_m;
    overview.vertices = square_fence();
    return overview;
}

/**
 * @class GeofenceControllerTest
 * @brief 控制器與提示模型
 */
class GeofenceControllerTest : public ::testing::Test
{
protected:
    void SetUp() override { controller_.set_toast_model(&toasts_); }

    ToastModel toasts_;
    GeofenceController controller_{100};
};

}  // namespace

TEST_F(GeofenceControllerTest, DraftEditingAndApply)
{
    QSignalSpy set_requested(&controller_, &GeofenceController::set_requested);
    // 未編輯時點地圖不新增
    add(controller_, offset_from_center(0, 0));
    EXPECT_EQ(controller_.draft_count(), 0);

    controller_.startEditing();
    EXPECT_TRUE(controller_.is_editing());
    EXPECT_DOUBLE_EQ(controller_.margin(), geofence_limits::DEFAULT_MARGIN_M);
    for (const auto& vertex : square_fence())
    {
        add(controller_, vertex);
        EXPECT_FALSE(controller_.can_apply() && controller_.draft_count() < 3);
    }
    EXPECT_EQ(controller_.draft_count(), 4);
    EXPECT_TRUE(controller_.can_apply());
    EXPECT_EQ(controller_.draft_points().size(), 4);
    EXPECT_EQ(controller_.draft_points().at(3).toMap().value(QStringLiteral("index")).toInt(), 3);

    controller_.setMargin(2.0);
    controller_.setMargin(50.0);
    EXPECT_DOUBLE_EQ(controller_.margin(), geofence_limits::MAX_MARGIN_M);
    controller_.setMargin(1.25);
    controller_.apply();
    ASSERT_EQ(set_requested.size(), 1);
    const auto fence = set_requested.at(0).at(0).value<GeofenceData>();
    EXPECT_EQ(fence.action, GeofenceAction::SET);
    EXPECT_FLOAT_EQ(fence.margin_m, 1.25F);
    ASSERT_EQ(fence.vertices.size(), 4U);
    EXPECT_NEAR(fence.vertices[2].latitude_deg, square_fence()[2].latitude_deg, EPSILON);

    // Guard 拒絕：維持編輯並彈出錯誤；發布成功：結束編輯
    controller_.on_request_acked(command_protocol::make_ack(ALL_AGENTS_TARGET, GEOFENCE_ACK_NAME, false, "拒絕"));
    EXPECT_TRUE(controller_.is_editing());
    EXPECT_EQ(toasts_.rowCount(), 1);
    controller_.on_request_acked(command_protocol::make_ack(ALL_AGENTS_TARGET, GEOFENCE_ACK_NAME, true));
    EXPECT_FALSE(controller_.is_editing());
    EXPECT_EQ(controller_.draft_count(), 0);
    EXPECT_EQ(toasts_.rowCount(), 1);
}

TEST_F(GeofenceControllerTest, SelfIntersectingDraftCannotApply)
{
    QSignalSpy set_requested(&controller_, &GeofenceController::set_requested);
    controller_.startEditing();
    auto bowtie = square_fence();
    std::swap(bowtie[1], bowtie[2]);
    for (const auto& vertex : bowtie)
    {
        add(controller_, vertex);
    }
    EXPECT_TRUE(controller_.is_draft_self_intersecting());
    EXPECT_FALSE(controller_.can_apply());
    EXPECT_FALSE(controller_.draft_error().isEmpty());
    controller_.apply();
    EXPECT_TRUE(set_requested.isEmpty());
    EXPECT_EQ(toasts_.rowCount(), 1);

    // 拖曳修正後即可套用
    const auto corner = square_fence()[1];
    controller_.moveVertex(1, corner.latitude_deg, corner.longitude_deg);
    const auto opposite = square_fence()[2];
    controller_.moveVertex(2, opposite.latitude_deg, opposite.longitude_deg);
    EXPECT_FALSE(controller_.is_draft_self_intersecting());
    EXPECT_TRUE(controller_.can_apply());

    controller_.removeVertex(0);
    controller_.removeVertex(0);
    EXPECT_EQ(controller_.draft_count(), 2);
    EXPECT_FALSE(controller_.can_apply());
    controller_.removeVertex(5);
    EXPECT_EQ(controller_.draft_count(), 2);
    controller_.clearDraft();
    EXPECT_EQ(controller_.draft_count(), 0);
}

TEST_F(GeofenceControllerTest, VertexLimitAndDisplayOrigin)
{
    controller_.startEditing();
    for (int i = 0; i < geofence_limits::MAX_VERTICES; ++i)
    {
        const double angle = 2.0 * 3.14159265358979323846 * i / geofence_limits::MAX_VERTICES;
        add(controller_, offset_from_center(30.0 * std::cos(angle), 30.0 * std::sin(angle)));
    }
    EXPECT_EQ(controller_.draft_count(), geofence_limits::MAX_VERTICES);
    EXPECT_TRUE(controller_.can_apply());
    add(controller_, offset_from_center(0, 0));
    EXPECT_EQ(controller_.draft_count(), geofence_limits::MAX_VERTICES);
    EXPECT_EQ(toasts_.rowCount(), 1);
    controller_.cancelEditing();
    EXPECT_EQ(controller_.draft_count(), 0);

    // 顯示原點：地圖點選的位置換回 Agent 回報座標再發布
    ui::DisplayOrigin origin;
    origin.is_enabled = true;
    origin.source_latitude = 25.0;
    origin.source_longitude = 121.5;
    origin.display_latitude = 24.0;
    origin.display_longitude = 120.0;
    controller_.set_display_origin(origin);
    QSignalSpy set_requested(&controller_, &GeofenceController::set_requested);
    controller_.startEditing();
    for (const auto& vertex : square_fence())
    {
        const auto display = ui::to_display_position({vertex.latitude_deg, vertex.longitude_deg, 0.0}, origin);
        controller_.addVertex(display[0], display[1]);
    }
    EXPECT_NEAR(controller_.draft_path().at(0).toMap().value(QStringLiteral("latitude")).toDouble(),
                ui::to_display_position({square_fence()[0].latitude_deg, square_fence()[0].longitude_deg, 0.0},
                                        origin)[0],
                EPSILON);
    controller_.apply();
    ASSERT_EQ(set_requested.size(), 1);
    const auto fence = set_requested.at(0).at(0).value<GeofenceData>();
    EXPECT_NEAR(fence.vertices[0].latitude_deg, square_fence()[0].latitude_deg, 1e-7);
    EXPECT_NEAR(fence.vertices[0].longitude_deg, square_fence()[0].longitude_deg, 1e-7);
}

TEST_F(GeofenceControllerTest, EditingStartsFromActiveFenceAndShowsMargin)
{
    EXPECT_FALSE(controller_.has_fence());
    EXPECT_TRUE(controller_.fence_path().isEmpty());
    controller_.on_overview_changed(active_overview(4, 0.3F));
    EXPECT_TRUE(controller_.has_fence());
    EXPECT_EQ(controller_.fence_id(), 4);
    EXPECT_EQ(controller_.fence_path().size(), 4);
    EXPECT_FALSE(controller_.fence_inset_dashes().isEmpty());
    EXPECT_TRUE(controller_.summary_text().contains(QStringLiteral("2/2")));
    EXPECT_TRUE(controller_.map_warning_text().isEmpty());

    controller_.startEditing();
    EXPECT_EQ(controller_.draft_count(), 4);
    // float32 的 0.3 取到公分
    EXPECT_DOUBLE_EQ(controller_.margin(), 0.3);
}

TEST_F(GeofenceControllerTest, ClearNeedsConfirmationWithinTimeout)
{
    QSignalSpy cleared(&controller_, &GeofenceController::clear_requested);
    controller_.on_overview_changed(active_overview(2));
    controller_.confirmClear();
    EXPECT_TRUE(cleared.isEmpty());

    controller_.requestClear();
    EXPECT_TRUE(controller_.is_clear_confirm_pending());
    controller_.cancelClear();
    EXPECT_FALSE(controller_.is_clear_confirm_pending());

    controller_.requestClear();
    controller_.confirmClear();
    EXPECT_EQ(cleared.size(), 1);
    EXPECT_FALSE(controller_.is_clear_confirm_pending());

    // 逾時自動取消
    controller_.requestClear();
    QTRY_VERIFY_WITH_TIMEOUT(!controller_.is_clear_confirm_pending(), 1000);
    controller_.confirmClear();
    EXPECT_EQ(cleared.size(), 1);
}

TEST_F(GeofenceControllerTest, OverviewChangesRaiseNotices)
{
    GeofenceOverview overview = active_overview(3);
    overview.requested_fence_id = 4;
    overview.rows[0].phase = GeofenceAgentPhase::WAITING;
    overview.rows[1].phase = GeofenceAgentPhase::WAITING;
    controller_.on_overview_changed(overview);
    EXPECT_EQ(toasts_.rowCount(), 0);

    // 被拒、未回報、在圍籬外各提示一次
    overview.rows[0].phase = GeofenceAgentPhase::REJECTED;
    overview.rows[0].last_result = static_cast<uint8_t>(GeofenceResult::REJECTED_TOO_FAR);
    overview.rows[1].phase = GeofenceAgentPhase::NO_REPORT;
    overview.rows[1].is_position_inside = false;
    overview.is_consistent = false;
    controller_.on_overview_changed(overview);
    EXPECT_EQ(toasts_.rowCount(), 3);
    controller_.on_overview_changed(overview);
    EXPECT_EQ(toasts_.rowCount(), 3);

    const QVariantList rows = controller_.rows();
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("phaseText")).toString(), QStringLiteral("被拒"));
    EXPECT_TRUE(rows.at(0).toMap().value(QStringLiteral("detail")).toString().contains(QStringLiteral("5 km")));
    EXPECT_TRUE(rows.at(1).toMap().value(QStringLiteral("isOutside")).toBool());
    EXPECT_EQ(rows.at(1).toMap().value(QStringLiteral("level")).toInt(), static_cast<int>(ui::StatusLevel::CAUTION));
    EXPECT_TRUE(controller_.map_warning_text().contains(QStringLiteral("不一致")));
    EXPECT_TRUE(controller_.map_warning_text().contains(QStringLiteral("UAV_2")));

    // 全部生效只寫日誌，不彈出
    overview.rows[0] = make_row(AGENT_1, GeofenceAgentPhase::ACTIVE, 4);
    overview.rows[1] = make_row(AGENT_2, GeofenceAgentPhase::ACTIVE, 4);
    overview.fence_id = 4;
    overview.is_consistent = true;
    controller_.on_overview_changed(overview);
    EXPECT_EQ(toasts_.rowCount(), 3);
}
