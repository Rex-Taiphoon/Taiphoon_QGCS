/**
 * @file test_geofence_file.cpp
 * @brief 地理圍籬檔案：JSON 往返、格式與範圍檢查、檔案讀寫，以及 GeofenceController 的儲存與載入
 * @author Taiphoon GCS Team
 * @date 2026-11-18
 */
#include "app/geofence_controller.hpp"
#include "app/geofence_file.hpp"
#include "fakes/geofence_samples.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>

#include <gtest/gtest.h>

using namespace gcs;
using gcs::test::square_fence;

namespace
{

/**
 * @brief 合法檔案的 JSON 物件，供各測試改動單一欄位
 * @return JSON 物件
 */
QJsonObject valid_root()
{
    geofence_file::Content content;
    content.vertices = square_fence();
    content.margin_m = 1.25;
    return QJsonDocument::fromJson(geofence_file::to_json(content)).object();
}

/**
 * @brief 解析 JSON 物件
 * @param root JSON 物件
 * @param content 解析結果
 * @return 錯誤原因
 */
QString parse(const QJsonObject& root, geofence_file::Content* content)
{
    return geofence_file::from_json(QJsonDocument(root).toJson(), content);
}

/**
 * @brief 生效中的總覽（單機）
 * @param margin_m margin
 * @return 總覽
 */
GeofenceOverview active_overview(float margin_m)
{
    GeofenceAgentRow row;
    row.agent = QStringLiteral("uav_1_agent");
    row.phase = GeofenceAgentPhase::ACTIVE;
    row.fence_id = 7;
    GeofenceOverview overview;
    overview.rows = {row};
    overview.fence_id = 7;
    overview.margin_m = margin_m;
    overview.vertices = square_fence();
    return overview;
}

/**
 * @class GeofenceFileControllerTest
 * @brief 控制器、提示模型與暫存目錄
 */
class GeofenceFileControllerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(directory_.isValid());
        controller_.set_toast_model(&toasts_);
    }

    /**
     * @brief 暫存目錄內的檔案位置
     * @param name 檔名
     * @return 本機檔案的 URL
     */
    QUrl url_of(const QString& name) const { return QUrl::fromLocalFile(directory_.filePath(name)); }

    QTemporaryDir directory_;
    ToastModel toasts_;
    GeofenceController controller_{100};
};

}  // namespace

TEST(GeofenceFileTest, JsonRoundTripKeepsVerticesAndMargin)
{
    geofence_file::Content content;
    content.vertices = square_fence();
    content.margin_m = 0.3;

    geofence_file::Content parsed;
    EXPECT_TRUE(geofence_file::from_json(geofence_file::to_json(content), &parsed).isEmpty());
    EXPECT_DOUBLE_EQ(parsed.margin_m, 0.3);
    // 座標逐位元相同，載入後的圍籬與儲存時一致
    EXPECT_EQ(parsed.vertices, content.vertices);
}

TEST(GeofenceFileTest, RejectsForeignOrMalformedContent)
{
    geofence_file::Content content;
    content.margin_m = 9.0;
    EXPECT_FALSE(geofence_file::from_json("not json", &content).isEmpty());
    EXPECT_FALSE(geofence_file::from_json("[1, 2]", &content).isEmpty());

    QJsonObject root = valid_root();
    root[QStringLiteral("format")] = QStringLiteral("other");
    EXPECT_FALSE(parse(root, &content).isEmpty());

    root = valid_root();
    root[QStringLiteral("version")] = geofence_file::FORMAT_VERSION + 1;
    EXPECT_TRUE(parse(root, &content).contains(QStringLiteral("版本")));

    root = valid_root();
    root.remove(QStringLiteral("vertices"));
    EXPECT_FALSE(parse(root, &content).isEmpty());

    // 失敗時不修改輸出
    EXPECT_DOUBLE_EQ(content.margin_m, 9.0);
    EXPECT_TRUE(content.vertices.empty());
}

TEST(GeofenceFileTest, RejectsOutOfRangeValues)
{
    geofence_file::Content content;

    QJsonObject root = valid_root();
    root[QStringLiteral("margin_m")] = static_cast<double>(geofence_limits::MAX_MARGIN_M) + 0.5;
    EXPECT_FALSE(parse(root, &content).isEmpty());
    root[QStringLiteral("margin_m")] = -0.1;
    EXPECT_FALSE(parse(root, &content).isEmpty());
    root[QStringLiteral("margin_m")] = QStringLiteral("1");
    EXPECT_FALSE(parse(root, &content).isEmpty());

    // 頂點數：少於 3 或多於 64
    root = valid_root();
    QJsonArray vertices = root.value(QStringLiteral("vertices")).toArray();
    const QJsonValue first = vertices.at(0);
    vertices.removeLast();
    vertices.removeLast();
    root[QStringLiteral("vertices")] = vertices;
    EXPECT_FALSE(parse(root, &content).isEmpty());
    while (vertices.size() <= geofence_limits::MAX_VERTICES)
    {
        vertices.append(first);
    }
    root[QStringLiteral("vertices")] = vertices;
    EXPECT_FALSE(parse(root, &content).isEmpty());

    // 座標：超出範圍、缺欄位、不是數字
    const QList<QJsonValue> bad_vertices{
        QJsonObject{{QStringLiteral("latitude_deg"), 91.0}, {QStringLiteral("longitude_deg"), 121.5}},
        QJsonObject{{QStringLiteral("latitude_deg"), 25.0}, {QStringLiteral("longitude_deg"), -181.0}},
        QJsonObject{{QStringLiteral("latitude_deg"), 25.0}},
        QJsonObject{{QStringLiteral("latitude_deg"), QStringLiteral("25")}, {QStringLiteral("longitude_deg"), 121.5}},
        QJsonValue(5),
    };
    for (const QJsonValue& bad : bad_vertices)
    {
        root = valid_root();
        vertices = root.value(QStringLiteral("vertices")).toArray();
        vertices[1] = bad;
        root[QStringLiteral("vertices")] = vertices;
        EXPECT_TRUE(parse(root, &content).contains(QStringLiteral("第 2 個")));
    }
    EXPECT_TRUE(content.vertices.empty());
}

TEST(GeofenceFileTest, WriteAndReadFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("fence.json"));

    geofence_file::Content content;
    content.vertices = square_fence(25.0);
    content.margin_m = 2.0;
    EXPECT_TRUE(geofence_file::write(path, content).isEmpty());

    geofence_file::Content parsed;
    EXPECT_TRUE(geofence_file::read(path, &parsed).isEmpty());
    EXPECT_EQ(parsed.vertices, content.vertices);
    EXPECT_DOUBLE_EQ(parsed.margin_m, 2.0);

    EXPECT_FALSE(geofence_file::read(directory.filePath(QStringLiteral("missing.json")), &parsed).isEmpty());
    EXPECT_FALSE(geofence_file::write(directory.filePath(QStringLiteral("no_dir/fence.json")), content).isEmpty());
}

TEST_F(GeofenceFileControllerTest, SavesDraftWhileEditing)
{
    // 沒有圍籬也不在編輯：不能儲存，也不產生檔案
    EXPECT_FALSE(controller_.can_save());
    EXPECT_FALSE(controller_.saveToFile(url_of(QStringLiteral("none.json"))));
    EXPECT_FALSE(QFile::exists(directory_.filePath(QStringLiteral("none.json"))));
    EXPECT_EQ(toasts_.rowCount(), 1);

    // 草稿不足 3 點：不能儲存
    controller_.startEditing();
    const auto square = square_fence();
    controller_.addVertex(square[0].latitude_deg, square[0].longitude_deg);
    controller_.addVertex(square[1].latitude_deg, square[1].longitude_deg);
    EXPECT_FALSE(controller_.can_save());
    EXPECT_FALSE(controller_.saveToFile(url_of(QStringLiteral("short.json"))));
    EXPECT_FALSE(QFile::exists(directory_.filePath(QStringLiteral("short.json"))));

    controller_.addVertex(square[2].latitude_deg, square[2].longitude_deg);
    controller_.addVertex(square[3].latitude_deg, square[3].longitude_deg);
    controller_.setMargin(1.5);
    EXPECT_TRUE(controller_.can_save());
    EXPECT_TRUE(controller_.saveToFile(url_of(QStringLiteral("draft.json"))));
    // 儲存不結束編輯
    EXPECT_TRUE(controller_.is_editing());

    geofence_file::Content saved;
    ASSERT_TRUE(geofence_file::read(directory_.filePath(QStringLiteral("draft.json")), &saved).isEmpty());
    EXPECT_EQ(saved.vertices.size(), 4U);
    EXPECT_DOUBLE_EQ(saved.margin_m, 1.5);

    // 不是本機檔案
    EXPECT_FALSE(controller_.saveToFile(QUrl(QStringLiteral("https://example.com/fence.json"))));
}

TEST_F(GeofenceFileControllerTest, SavesActiveFenceWhenNotEditing)
{
    controller_.on_overview_changed(active_overview(0.3F));
    EXPECT_TRUE(controller_.can_save());
    EXPECT_TRUE(controller_.saveToFile(url_of(QStringLiteral("active.json"))));
    EXPECT_FALSE(controller_.is_editing());

    geofence_file::Content saved;
    ASSERT_TRUE(geofence_file::read(directory_.filePath(QStringLiteral("active.json")), &saved).isEmpty());
    EXPECT_EQ(saved.vertices, square_fence());
    // float32 的 0.3 取到公分
    EXPECT_DOUBLE_EQ(saved.margin_m, 0.3);
}

TEST_F(GeofenceFileControllerTest, LoadBecomesDraftAndNeverPublishes)
{
    QSignalSpy set_requested(&controller_, &GeofenceController::set_requested);
    QSignalSpy clear_requested(&controller_, &GeofenceController::clear_requested);
    geofence_file::Content content;
    content.vertices = square_fence(30.0);
    content.margin_m = 2.5;
    ASSERT_TRUE(geofence_file::write(directory_.filePath(QStringLiteral("site.json")), content).isEmpty());

    // 生效中的圍籬不受載入影響；載入只進入編輯
    controller_.on_overview_changed(active_overview(0.5F));
    EXPECT_TRUE(controller_.loadFromFile(url_of(QStringLiteral("site.json"))));
    EXPECT_TRUE(controller_.is_editing());
    EXPECT_EQ(controller_.draft_count(), 4);
    EXPECT_DOUBLE_EQ(controller_.margin(), 2.5);
    EXPECT_TRUE(controller_.can_apply());
    EXPECT_EQ(controller_.fence_id(), 7);
    EXPECT_TRUE(set_requested.isEmpty());
    EXPECT_TRUE(clear_requested.isEmpty());
    EXPECT_EQ(toasts_.rowCount(), 0);

    // 按套用才送出，內容與檔案相同
    controller_.apply();
    ASSERT_EQ(set_requested.size(), 1);
    const auto fence = set_requested.at(0).at(0).value<GeofenceData>();
    EXPECT_EQ(fence.vertices, content.vertices);
    EXPECT_FLOAT_EQ(fence.margin_m, 2.5F);
}

TEST_F(GeofenceFileControllerTest, FailedLoadKeepsDraft)
{
    controller_.startEditing();
    const auto square = square_fence();
    controller_.addVertex(square[0].latitude_deg, square[0].longitude_deg);
    controller_.setMargin(3.0);

    EXPECT_FALSE(controller_.loadFromFile(url_of(QStringLiteral("missing.json"))));
    QFile broken(directory_.filePath(QStringLiteral("broken.json")));
    ASSERT_TRUE(broken.open(QIODevice::WriteOnly));
    broken.write("{\"format\": \"taiphoon-geofence\"");
    broken.close();
    EXPECT_FALSE(controller_.loadFromFile(url_of(QStringLiteral("broken.json"))));

    EXPECT_TRUE(controller_.is_editing());
    EXPECT_EQ(controller_.draft_count(), 1);
    EXPECT_DOUBLE_EQ(controller_.margin(), 3.0);
    EXPECT_EQ(toasts_.rowCount(), 2);

    // 未編輯時載入失敗不進入編輯
    controller_.cancelEditing();
    EXPECT_FALSE(controller_.loadFromFile(url_of(QStringLiteral("broken.json"))));
    EXPECT_FALSE(controller_.is_editing());
}

TEST_F(GeofenceFileControllerTest, LoadedSelfIntersectingFenceCannotApply)
{
    geofence_file::Content content;
    content.vertices = square_fence();
    std::swap(content.vertices[1], content.vertices[2]);
    ASSERT_TRUE(geofence_file::write(directory_.filePath(QStringLiteral("bowtie.json")), content).isEmpty());

    // 幾何規則由草稿驗證把關：載入後可在地圖上修正，但不能直接套用
    EXPECT_TRUE(controller_.loadFromFile(url_of(QStringLiteral("bowtie.json"))));
    EXPECT_TRUE(controller_.is_draft_self_intersecting());
    EXPECT_FALSE(controller_.can_apply());
    EXPECT_FALSE(controller_.can_save());
}
