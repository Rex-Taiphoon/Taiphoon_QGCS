/**
 * @file main.cpp
 * @brief Taiphoon_QGCS 主程式入口
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * M0 階段只載入空白主視窗；核心模組、UiBridge 與組裝根於 M2 之後加入。
 */
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>

#include <cstdlib>

Q_LOGGING_CATEGORY(lc_app, "gcs.app")

namespace
{

/// 煙霧測試：主視窗建立後立即結束，供 CI 在無畫面環境確認 QML 可載入
constexpr const char* SMOKE_TEST_OPTION = "smoke-test";

}  // namespace

/**
 * @brief 程式入口
 * @param argc 參數數量
 * @param argv 參數內容
 * @return 程式結束碼
 */
int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Taiphoon"));
    QGuiApplication::setApplicationName(QStringLiteral("Taiphoon_QGCS"));
    QGuiApplication::setApplicationVersion(QStringLiteral(GCS_VERSION_FULL));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Taiphoon Qt 原生地面控制站"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(QString::fromLatin1(SMOKE_TEST_OPTION),
                                        QStringLiteral("載入主視窗後立即結束（CI 使用）")));
    parser.process(app);

    // 使用 Basic 樣式，避免各平台原生樣式造成外觀與控制項尺寸不一致。
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []()
        {
            qCCritical(lc_app) << "主視窗載入失敗";
            QCoreApplication::exit(EXIT_FAILURE);
        },
        Qt::QueuedConnection);
    engine.loadFromModule("TaiphoonQgcs", "Main");

    if (parser.isSet(QString::fromLatin1(SMOKE_TEST_OPTION)))
    {
        const bool is_loaded = !engine.rootObjects().isEmpty();
        qCInfo(lc_app) << "煙霧測試結果：" << (is_loaded ? "主視窗已載入" : "主視窗未載入");
        return is_loaded ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    qCInfo(lc_app) << "Taiphoon_QGCS" << GCS_VERSION_FULL << "已啟動";
    return QGuiApplication::exec();
}
