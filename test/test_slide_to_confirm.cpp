/**
 * @file test_slide_to_confirm.cpp
 * @brief L2 滑桿（SlideToConfirm.qml）互動測試：放在可捲動面板內時，斜向拖曳也必須能確認
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 飛行控制面板是 Flickable；拖曳帶有垂直分量時，Flickable 會搶走滑桿的拖曳而使其彈回。
 * 以真實的 QQuickWindow 事件傳遞重現此情境。
 */
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <gtest/gtest.h>

namespace
{

/// 拖曳分段數：模擬實際滑鼠移動事件
constexpr int DRAG_STEPS = 30;
/// 斜向拖曳的總垂直偏移（像素），超過 Flickable 的拖曳門檻
constexpr int VERTICAL_DRIFT_PX = 40;

/**
 * @brief 把元件與最小 qmldir 複製到暫存目錄，讓測試不依賴主程式的 QML 模組
 * @param dir 暫存目錄
 * @return 是否成功
 */
bool prepare_qml(const QTemporaryDir& dir)
{
    const QDir source(QStringLiteral(GCS_QML_SOURCE_DIR));
    for (const QString& name : {QStringLiteral("Theme.qml"), QStringLiteral("SlideToConfirm.qml")})
    {
        if (!QFile::copy(source.filePath(name), dir.filePath(name)))
        {
            return false;
        }
    }
    QFile qmldir(dir.filePath(QStringLiteral("qmldir")));
    if (!qmldir.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }
    qmldir.write("singleton Theme 1.0 Theme.qml\nSlideToConfirm 1.0 SlideToConfirm.qml\n");
    QFile scene(dir.filePath(QStringLiteral("Scene.qml")));
    if (!scene.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }
    // 與 CommandPanel 相同：滑桿位於可垂直捲動的 Flickable 內
    scene.write(R"(
import QtQuick
Flickable {
    width: 420
    height: 200
    contentHeight: 600
    property alias slider: slider
    SlideToConfirm {
        id: slider
        objectName: "slider"
        x: 20
        y: 40
        width: 360
        label: "Arm"
    }
}
)");
    return true;
}

/**
 * @class SlideToConfirmTest
 * @brief 載入測試場景
 */
class SlideToConfirmTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(dir_.isValid());
        ASSERT_TRUE(prepare_qml(dir_));
        view_.setSource(QUrl::fromLocalFile(dir_.filePath(QStringLiteral("Scene.qml"))));
        ASSERT_EQ(view_.status(), QQuickView::Ready);
        view_.show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(&view_));
        slider_ = view_.rootObject()->findChild<QQuickItem*>(QStringLiteral("slider"));
        ASSERT_NE(slider_, nullptr);
    }

    /**
     * @brief 從滑塊中心拖到軌道右端
     * @param vertical_drift 拖曳過程的總垂直偏移
     */
    void drag_to_end(int vertical_drift)
    {
        const QPointF knob = slider_->mapToScene(QPointF(25, slider_->height() / 2));
        const QPointF end = slider_->mapToScene(QPointF(slider_->width() - 5, slider_->height() / 2));
        QTest::mousePress(&view_, Qt::LeftButton, Qt::NoModifier, knob.toPoint());
        for (int step = 1; step <= DRAG_STEPS; ++step)
        {
            const double t = static_cast<double>(step) / DRAG_STEPS;
            const QPointF point(knob.x() + (end.x() - knob.x()) * t, knob.y() + vertical_drift * t);
            QTest::mouseMove(&view_, point.toPoint());
        }
        QTest::mouseRelease(&view_, Qt::LeftButton, Qt::NoModifier,
                            QPointF(end.x(), knob.y() + vertical_drift).toPoint());
    }

    QTemporaryDir dir_;
    QQuickView view_;
    QQuickItem* slider_ = nullptr;
};

}  // namespace

TEST_F(SlideToConfirmTest, StraightDragConfirms)
{
    QSignalSpy confirmed(slider_, SIGNAL(confirmed()));
    drag_to_end(0);
    EXPECT_EQ(confirmed.size(), 1);
}

TEST_F(SlideToConfirmTest, DiagonalDragInsideFlickableConfirms)
{
    QSignalSpy confirmed(slider_, SIGNAL(confirmed()));
    drag_to_end(VERTICAL_DRIFT_PX);
    EXPECT_EQ(confirmed.size(), 1);
}

TEST_F(SlideToConfirmTest, ReleaseBeforeEndDoesNotConfirm)
{
    QSignalSpy confirmed(slider_, SIGNAL(confirmed()));
    const QPointF knob = slider_->mapToScene(QPointF(25, slider_->height() / 2));
    QTest::mousePress(&view_, Qt::LeftButton, Qt::NoModifier, knob.toPoint());
    QTest::mouseMove(&view_, (knob + QPointF(150, 0)).toPoint());
    QTest::mouseRelease(&view_, Qt::LeftButton, Qt::NoModifier, (knob + QPointF(150, 0)).toPoint());
    EXPECT_TRUE(confirmed.isEmpty());
    EXPECT_NEAR(slider_->property("progress").toDouble(), 0.0, 1e-9);
}

TEST_F(SlideToConfirmTest, PressingTrackEndDoesNotJumpToConfirm)
{
    // 必須從滑塊開始拖曳，直接按在終點附近不得觸發（L2 防誤觸）
    QSignalSpy confirmed(slider_, SIGNAL(confirmed()));
    const QPointF end = slider_->mapToScene(QPointF(slider_->width() - 5, slider_->height() / 2));
    QTest::mousePress(&view_, Qt::LeftButton, Qt::NoModifier, end.toPoint());
    QTest::mouseMove(&view_, (end + QPointF(2, 0)).toPoint());
    QTest::mouseRelease(&view_, Qt::LeftButton, Qt::NoModifier, (end + QPointF(2, 0)).toPoint());
    EXPECT_TRUE(confirmed.isEmpty());
}

/**
 * @brief 測試程式入口：Qt Quick 需要 QGuiApplication
 * @param argc 參數數量
 * @param argv 參數內容
 * @return 測試結果
 */
int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
