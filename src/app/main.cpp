/**
 * @file main.cpp
 * @brief Taiphoon_QGCS 主程式入口
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 建立設定、核心執行環境、UiBridge、航點任務、語音口令與除錯工具，完成 UI 組裝後載入 QML。
 * 結束程式只停止 GCS，不對 Agent 發送任何命令（F-12）。
 */
#include "app/app_settings.hpp"
#include "app/core_runtime.hpp"
#include "app/diagnostics_controller.hpp"
#include "app/log_file.hpp"
#include "app/mission_controller.hpp"
#include "app/network_controller.hpp"
#include "app/ui_bridge.hpp"
#include "app/ui_wiring.hpp"
#include "app/voice_controller.hpp"
#include "link/cyclone_agent_link.hpp"
#include "voice/keyword_spotter.hpp"

#include <QCommandLineParser>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>

#include <cstdlib>

Q_LOGGING_CATEGORY(lc_app, "gcs.app")

namespace
{

/// 煙霧測試：主視窗建立後立即結束，供 CI 在無畫面環境確認 QML 可載入
constexpr const char* SMOKE_TEST_OPTION = "smoke-test";
/// 開發驗證：延遲後擷取主視窗畫面並結束
constexpr const char* SCREENSHOT_OPTION = "screenshot";
/// 擷取畫面前的預設等待時間（毫秒），讓探索與地圖圖磚完成載入
constexpr int DEFAULT_SCREENSHOT_DELAY_MS = 12000;

/**
 * @brief 解析命令列的網路設定（本次執行有效，不寫入設定檔；開發與測試使用）
 * @param parser 已處理的命令列
 * @param config 輸出設定
 * @return 是否有指定任何網路參數
 */
bool parse_network_override(const QCommandLineParser& parser, gcs::link::DdsNetworkConfig* config)
{
    const bool has_override = parser.isSet(QStringLiteral("peer")) || parser.isSet(QStringLiteral("interface"))
                              || parser.isSet(QStringLiteral("domain")) || parser.isSet(QStringLiteral("no-multicast"))
                              || parser.isSet(QStringLiteral("participant-index"))
                              || parser.isSet(QStringLiteral("external-address"));
    if (!has_override)
    {
        return false;
    }
    config->domain_id = parser.value(QStringLiteral("domain")).toUInt();
    config->network_interface = parser.value(QStringLiteral("interface")).toStdString();
    config->external_address = parser.value(QStringLiteral("external-address")).toStdString();
    config->is_multicast_allowed = !parser.isSet(QStringLiteral("no-multicast"));
    for (const QString& peer : parser.values(QStringLiteral("peer")))
    {
        config->peers.push_back(peer.toStdString());
    }
    if (parser.isSet(QStringLiteral("participant-index")))
    {
        config->participant_index = parser.value(QStringLiteral("participant-index")).toUInt();
    }
    return true;
}

/**
 * @brief 註冊內嵌的 B612／B612 Mono 與 Noto Sans TC 字型（皆為 OFL-1.1），供 QML 以字族名稱使用
 *
 * B612 不含中文字形，以字族替代讓中文一律改用內嵌的 Noto Sans TC，Windows 與 macOS 外觀一致；
 * 載入失敗時只記錄警告，介面改用系統字型。
 */
void register_fonts()
{
    static const char* const FONT_FILES[] = {"B612-Regular.ttf", "B612-Bold.ttf", "B612Mono-Regular.ttf",
                                             "B612Mono-Bold.ttf", "NotoSansTC-VF.ttf"};
    for (const char* file : FONT_FILES)
    {
        const QString path = QStringLiteral(":/qt/qml/TaiphoonQgcs/resources/fonts/") + QLatin1String(file);
        if (QFontDatabase::addApplicationFont(path) < 0)
        {
            qCWarning(lc_app) << "字型載入失敗：" << path;
        }
    }
    const QString cjk_family = QStringLiteral("Noto Sans TC");
    QFont::insertSubstitution(QStringLiteral("B612"), cjk_family);
    QFont::insertSubstitution(QStringLiteral("B612 Mono"), cjk_family);
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
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Taiphoon"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("taiphoon.com.tw"));
    QGuiApplication::setApplicationName(QStringLiteral("Taiphoon_QGCS"));
    QGuiApplication::setApplicationVersion(QStringLiteral(GCS_VERSION_FULL));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/TaiphoonQgcs/resources/taiphoon-icon.png")));

    // 日誌檔：命令結果等不再彈出的訊息改記錄於此（使用者資料夾 logs/）
    const QString log_directory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs");
    if (!gcs::log_file::install(log_directory))
    {
        qCWarning(lc_app) << "無法建立日誌目錄：" << log_directory;
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Taiphoon Qt 原生地面控制站"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({
        {QString::fromLatin1(SMOKE_TEST_OPTION), QStringLiteral("載入主視窗後立即結束（CI 使用）")},
        {QString::fromLatin1(SCREENSHOT_OPTION), QStringLiteral("延遲後將主視窗畫面存檔並結束（開發驗證）"),
         QStringLiteral("file")},
        {QStringLiteral("screenshot-delay"), QStringLiteral("擷取畫面前等待毫秒數"), QStringLiteral("ms")},
        {QStringLiteral("open-command-panel"), QStringLiteral("啟動時展開飛行控制（開發驗證）")},
        {QStringLiteral("select"), QStringLiteral("啟動後選取的目標（all 或 uav_N_agent，開發驗證）"),
         QStringLiteral("agent")},
        {QStringLiteral("domain"), QStringLiteral("本次使用的 ROS_DOMAIN_ID"), QStringLiteral("id"), QStringLiteral("0")},
        {QStringLiteral("interface"), QStringLiteral("本次使用的網卡名稱或 IPv4"), QStringLiteral("value")},
        {QStringLiteral("peer"), QStringLiteral("本次使用的 Agent 位址，可重複"), QStringLiteral("address")},
        {QStringLiteral("no-multicast"), QStringLiteral("本次關閉 multicast")},
        {QStringLiteral("participant-index"), QStringLiteral("本次使用固定 participant index"), QStringLiteral("index")},
        {QStringLiteral("external-address"), QStringLiteral("本次對外公告的位址（位於 NAT 之後時使用）"),
         QStringLiteral("ipv4")},
    });
    parser.process(app);

    // 使用 Basic 樣式，避免各平台原生樣式造成外觀與控制項尺寸不一致
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    register_fonts();

    gcs::AppSettings settings;
    gcs::CoreRuntime runtime;
    gcs::UiBridge bridge;
    gcs::DiagnosticsController diagnostics;
    gcs::NetworkController network(&settings, &runtime, &bridge);

    // 地圖顯示原點只影響畫面座標；除錯工具與核心模組仍使用 Agent 回報的原始位置
    gcs::MissionController mission;
    mission.set_toast_model(&bridge.toasts());
    bridge.set_display_origin(settings.display_origin());
    mission.set_display_origin(settings.display_origin());
    QObject::connect(&settings, &gcs::AppSettings::displayOriginChanged, &bridge,
                     [&settings, &bridge, &mission]()
                     {
                         bridge.set_display_origin(settings.display_origin());
                         mission.set_display_origin(settings.display_origin());
                     });
    QObject::connect(&bridge, &gcs::UiBridge::selectionChanged, &mission,
                     [&bridge, &mission]() { mission.set_selected_agent(bridge.selected_agent()); });

    // 語音口令：只在按住 F2 或畫面按鈕時收音，口令交給 MissionController 處理
    gcs::VoiceController voice;
    voice.initialize(gcs::voice::KeywordSpotter::default_voice_dir());
    app.installEventFilter(&voice);
    QObject::connect(&voice, &gcs::VoiceController::keyword_detected, &mission,
                     &gcs::MissionController::on_voice_keyword);

    diagnostics.set_topic_source(
        [&runtime]()
        {
            std::vector<gcs::DiagnosticsController::TopicEntry> entries;
            if (const auto* link = runtime.agent_link(); link != nullptr)
            {
                for (const auto& topic : link->discovered_topics())
                {
                    entries.push_back({topic.topic, topic.type});
                }
            }
            return entries;
        });

    // 核心每次（重新）建立時，模組都是新物件，必須重新組裝 UI 連線
    QObject::connect(&network, &gcs::NetworkController::runtimeStarted, &app,
                     [&runtime, &bridge, &diagnostics, &mission]()
                     {
                         const gcs::CoreModules modules{runtime.fleet(),   runtime.telemetry(), runtime.guard(),
                                                        runtime.command(), runtime.lifecycle(), runtime.mission()};
                         gcs::wire_ui(modules, &bridge);
                         gcs::wire_diagnostics(modules, &diagnostics);
                         gcs::wire_mission(modules, &mission);
                     });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&runtime]() { runtime.stop(); });

    gcs::link::DdsNetworkConfig override_config;
    const bool has_network_override = parse_network_override(parser, &override_config);
    if (has_network_override)
    {
        network.set_session_override(override_config);
    }
    if (!network.start())
    {
        // 介面仍開啟，使用者可在網路設定修正後重新連線
        qCWarning(lc_app) << "DDS 未能啟動：" << bridge.link_message();
    }

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("bridge"), QVariant::fromValue(&bridge)},
        {QStringLiteral("settings"), QVariant::fromValue(&settings)},
        {QStringLiteral("network"), QVariant::fromValue(&network)},
        {QStringLiteral("diagnostics"), QVariant::fromValue(&diagnostics)},
        {QStringLiteral("mission"), QVariant::fromValue(&mission)},
        {QStringLiteral("voice"), QVariant::fromValue(&voice)},
        // 命令列已指定網路時不顯示首次設定精靈
        {QStringLiteral("isFirstRunAllowed"), !has_network_override},
        {QStringLiteral("isCommandPanelInitiallyOpen"), parser.isSet(QStringLiteral("open-command-panel"))},
    });
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
        runtime.stop();
        return is_loaded ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (parser.isSet(QStringLiteral("select")))
    {
        const QString target = parser.value(QStringLiteral("select"));
        // 等機隊第一次掃描後再選取，否則單機選取會因尚未上線而被清除
        QObject::connect(&bridge, &gcs::UiBridge::fleetChanged, &bridge,
                         [&bridge, target]()
                         {
                             if (target == gcs::ALL_AGENTS_TARGET || bridge.online_agents().contains(target))
                             {
                                 bridge.selectAgent(target);
                             }
                         });
    }
    if (parser.isSet(QString::fromLatin1(SCREENSHOT_OPTION)))
    {
        const QString path = parser.value(QString::fromLatin1(SCREENSHOT_OPTION));
        bool is_number = false;
        const int requested_delay = parser.value(QStringLiteral("screenshot-delay")).toInt(&is_number);
        const int delay_ms = is_number && requested_delay > 0 ? requested_delay : DEFAULT_SCREENSHOT_DELAY_MS;
        QTimer::singleShot(delay_ms, &app,
                           [&engine, path]()
                           {
                               auto* window = engine.rootObjects().isEmpty()
                                                  ? nullptr
                                                  : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
                               const bool is_saved = window != nullptr && window->grabWindow().save(path);
                               qCInfo(lc_app) << "畫面擷取" << (is_saved ? "已保存：" : "失敗：") << path;
                               QCoreApplication::exit(is_saved ? EXIT_SUCCESS : EXIT_FAILURE);
                           });
    }

    qCInfo(lc_app) << "Taiphoon_QGCS" << GCS_VERSION_FULL << "已啟動，日誌目錄：" << log_directory;
    return QGuiApplication::exec();
}
