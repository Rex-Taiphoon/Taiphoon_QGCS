/**
 * @file main.cpp
 * @brief gcs_headless：無介面整合驗證工具（M3，開發用，不隨產品發布）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 以與桌面程式相同的 CoreRuntime（CycloneAgentLink + 全部模組 + 組裝根）連線 Agent，
 * 以日誌輸出機隊、遙測摘要與命令回報。命令一律經 GuardModule.request_command 送出，
 * 預設不送出任何命令；--command 必須搭配 --allow-commands 才會執行。
 */
#include "app/core_runtime.hpp"
#include "core/agent_types.hpp"
#include "modules/command/command_module.hpp"
#include "modules/fleet/fleet_module.hpp"
#include "modules/guard/guard_module.hpp"
#include "modules/lifecycle/agent_lifecycle_module.hpp"
#include "modules/telemetry/telemetry_module.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QHash>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace gcs;

namespace
{

/// 每台 Agent 遙測摘要的輸出間隔（毫秒）
constexpr int SUMMARY_INTERVAL_MS = 2000;
/// 依序送出命令的間隔（毫秒），讓 Agent 有時間回報新狀態
constexpr int COMMAND_INTERVAL_MS = 3000;

/**
 * @brief 輸出一行帶時間的日誌
 * @param text 內容
 */
void print_line(const QString& text)
{
    const QString line = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz ")) + text + '\n';
    std::fputs(line.toUtf8().constData(), stdout);
    std::fflush(stdout);
}

/**
 * @brief 解析 "agent:COMMAND[:p1,p2]" 格式的命令
 * @param text 命令文字
 * @param request 解析結果
 * @return 是否成功
 */
bool parse_command_spec(const QString& text, CommandRequest* request)
{
    const QStringList parts = text.split(QLatin1Char(':'));
    if (parts.size() < 2 || parts.size() > 3)
    {
        return false;
    }
    request->agent = parts.at(0);
    request->command = parts.at(1);
    request->params.clear();
    if (parts.size() == 3)
    {
        for (const QString& value : parts.at(2).split(QLatin1Char(','), Qt::SkipEmptyParts))
        {
            bool is_number = false;
            const double number = value.toDouble(&is_number);
            if (!is_number)
            {
                return false;
            }
            request->params.push_back(number);
        }
    }
    return true;
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
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Taiphoon_QGCS 無介面整合驗證"));
    parser.addHelpOption();
    const QCommandLineOption domain_option(QStringLiteral("domain"), QStringLiteral("ROS_DOMAIN_ID"),
                                           QStringLiteral("id"), QStringLiteral("0"));
    const QCommandLineOption interface_option(QStringLiteral("interface"), QStringLiteral("網卡名稱或 IPv4"),
                                              QStringLiteral("value"));
    const QCommandLineOption peer_option(QStringLiteral("peer"), QStringLiteral("Agent 探索入口，可重複"),
                                         QStringLiteral("address"));
    const QCommandLineOption no_multicast_option(QStringLiteral("no-multicast"), QStringLiteral("關閉 multicast"));
    const QCommandLineOption participant_index_option(QStringLiteral("participant-index"),
                                                      QStringLiteral("固定 participant index"), QStringLiteral("index"));
    const QCommandLineOption external_address_option(QStringLiteral("external-address"),
                                                     QStringLiteral("對外公告的位址（位於 NAT 之後時使用）"),
                                                     QStringLiteral("ipv4"));
    const QCommandLineOption duration_option(QStringLiteral("duration"), QStringLiteral("執行秒數"),
                                             QStringLiteral("seconds"), QStringLiteral("20"));
    const QCommandLineOption warmup_option(QStringLiteral("warmup"), QStringLiteral("送出命令前等待秒數"),
                                           QStringLiteral("seconds"), QStringLiteral("8"));
    const QCommandLineOption command_option(QStringLiteral("command"),
                                            QStringLiteral("經 Guard 送出的命令 agent:COMMAND[:p1,p2]，可重複"),
                                            QStringLiteral("spec"));
    const QCommandLineOption allow_commands_option(QStringLiteral("allow-commands"),
                                                   QStringLiteral("確認允許送出 --command 指定的命令"));
    const QCommandLineOption shutdown_option(QStringLiteral("shutdown"),
                                             QStringLiteral("最後關閉指定 Agent（需 --allow-commands）"),
                                             QStringLiteral("agent"));
    parser.addOptions({domain_option, interface_option, peer_option, no_multicast_option, participant_index_option,
                       external_address_option, duration_option, warmup_option, command_option, allow_commands_option,
                       shutdown_option});
    parser.process(app);

    link::DdsNetworkConfig config;
    config.domain_id = parser.value(domain_option).toUInt();
    config.network_interface = parser.value(interface_option).toStdString();
    config.is_multicast_allowed = !parser.isSet(no_multicast_option);
    for (const QString& peer : parser.values(peer_option))
    {
        config.peers.push_back(peer.toStdString());
    }
    config.external_address = parser.value(external_address_option).toStdString();
    if (parser.isSet(participant_index_option))
    {
        config.participant_index = parser.value(participant_index_option).toUInt();
    }

    std::vector<CommandRequest> commands;
    for (const QString& spec : parser.values(command_option))
    {
        CommandRequest request;
        if (!parse_command_spec(spec, &request))
        {
            print_line(QStringLiteral("命令格式錯誤：%1（應為 agent:COMMAND[:p1,p2]）").arg(spec));
            return EXIT_FAILURE;
        }
        commands.push_back(request);
    }
    const QString shutdown_target = parser.value(shutdown_option);
    if ((!commands.empty() || !shutdown_target.isEmpty()) && !parser.isSet(allow_commands_option))
    {
        print_line(QStringLiteral("有指定命令或 shutdown，但未加 --allow-commands，拒絕執行"));
        return EXIT_FAILURE;
    }

    CoreRuntime runtime;
    QString error;
    if (!runtime.start(config, &error))
    {
        print_line(QStringLiteral("核心啟動失敗：%1").arg(error));
        return EXIT_FAILURE;
    }

    // 主執行緒的接收者；與核心執行緒之間自動以佇列傳遞
    QObject receiver;
    QHash<QString, AgentStateData> latest_states;
    QHash<QString, int> state_counts;
    int failed_acks = 0;
    QObject::connect(runtime.fleet(), &FleetModule::fleet_changed, &receiver,
                     [](const FleetSnapshot& snapshot)
                     {
                         print_line(QStringLiteral("[fleet] online=%1 offline=%2")
                                        .arg(snapshot.online.join(QLatin1Char(',')),
                                             snapshot.offline.join(QLatin1Char(','))));
                     });
    QObject::connect(runtime.fleet(), &FleetModule::agent_online, &receiver,
                     [](const QString& agent) { print_line(QStringLiteral("[fleet] 上線 %1").arg(agent)); });
    QObject::connect(runtime.fleet(), &FleetModule::agent_offline, &receiver,
                     [](const QString& agent) { print_line(QStringLiteral("[fleet] 離線 %1").arg(agent)); });
    QObject::connect(runtime.telemetry(), &TelemetryModule::state_updated, &receiver,
                     [&](const QString& agent, const AgentStateData& state)
                     {
                         latest_states.insert(agent, state);
                         ++state_counts[agent];
                     });
    const auto print_ack = [&failed_acks](const CommandAck& ack)
    {
        failed_acks += ack.is_ok ? 0 : 1;
        print_line(QStringLiteral("[ack] %1 %2 %3 %4")
                       .arg(ack.agent, ack.command, ack.is_ok ? QStringLiteral("成功") : QStringLiteral("失敗"),
                            ack.message));
    };
    QObject::connect(runtime.guard(), &GuardModule::command_acked, &receiver, print_ack);
    QObject::connect(runtime.command(), &CommandModule::command_acked, &receiver, print_ack);
    QObject::connect(runtime.lifecycle(), &AgentLifecycleModule::command_acked, &receiver, print_ack);

    QTimer summary_timer;
    QObject::connect(&summary_timer, &QTimer::timeout, &receiver,
                     [&]()
                     {
                         for (auto it = latest_states.cbegin(); it != latest_states.cend(); ++it)
                         {
                             const AgentStateData& s = it.value();
                             print_line(QStringLiteral("[state] %1 n=%2 mode=%3 armed=%4 landed=%5 nav=%6 valid=%7 "
                                                       "gps=(%8, %9, %10) alt=%11")
                                            .arg(it.key())
                                            .arg(state_counts.value(it.key()))
                                            .arg(s.flight_mode)
                                            .arg(s.is_armed)
                                            .arg(s.is_landed)
                                            .arg(s.px4_nav_state)
                                            .arg(s.is_px4_state_valid)
                                            .arg(s.position_global[0], 0, 'f', 7)
                                            .arg(s.position_global[1], 0, 'f', 7)
                                            .arg(s.position_global[2], 0, 'f', 2)
                                            .arg(-s.position_ned[2], 0, 'f', 2));
                         }
                     });
    summary_timer.start(SUMMARY_INTERVAL_MS);

    // 依序送出命令；一律經 Guard
    const int warmup_ms = parser.value(warmup_option).toInt() * 1000;
    int delay_ms = warmup_ms;
    for (const CommandRequest& request : commands)
    {
        QTimer::singleShot(delay_ms, &receiver,
                           [&runtime, request]()
                           {
                               print_line(QStringLiteral("[request] %1 %2").arg(request.agent, request.command));
                               GuardModule* guard = runtime.guard();
                               QMetaObject::invokeMethod(guard, [guard, request]() { guard->request_command(request); });
                           });
        delay_ms += COMMAND_INTERVAL_MS;
    }
    if (!shutdown_target.isEmpty())
    {
        QTimer::singleShot(delay_ms, &receiver,
                           [&runtime, shutdown_target]()
                           {
                               print_line(QStringLiteral("[request] shutdown %1").arg(shutdown_target));
                               AgentLifecycleModule* lifecycle = runtime.lifecycle();
                               QMetaObject::invokeMethod(lifecycle, [lifecycle, shutdown_target]()
                                                         { lifecycle->request_shutdown(shutdown_target); });
                           });
    }

    QTimer::singleShot(parser.value(duration_option).toInt() * 1000, &app, &QCoreApplication::quit);
    const int result = QCoreApplication::exec();
    summary_timer.stop();
    runtime.stop();

    print_line(QStringLiteral("結束：收到 %1 台 Agent 的狀態，失敗回報 %2 筆").arg(latest_states.size()).arg(failed_acks));
    return result == 0 && !latest_states.isEmpty() ? EXIT_SUCCESS : EXIT_FAILURE;
}
