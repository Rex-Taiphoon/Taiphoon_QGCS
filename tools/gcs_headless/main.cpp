/**
 * @file main.cpp
 * @brief gcs_headless：無介面整合驗證工具（M3，開發用，不隨產品發布）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 以與桌面程式相同的 CoreRuntime（CycloneAgentLink + 全部模組 + 組裝根）連線 Agent，
 * 以日誌輸出機隊、遙測摘要與命令回報。命令一律經 GuardModule.request_command 送出，
 * 預設不送出任何命令；--command 必須搭配 --allow-commands 才會執行。
 * 航點任務（--mission／--mission-relative）在命令之後開始；加上 --voice-wav 時改由語音檔觸發，
 * 與桌面程式相同經 MissionController 的倒數流程；語音的解鎖、起飛、降落與任務中的命令同樣經 Guard。
 * 地理圍籬（--geofence／--geofence-relative／--geofence-clear）在命令之前送出，經 GeofenceModule 與 Guard；
 * 各機圍籬狀態不論是否設定都以 [geofence] 輸出。
 */
#include "app/core_runtime.hpp"
#include "app/mission_controller.hpp"
#include "app/toast_model.hpp"
#include "app/ui_format.hpp"
#include "app/ui_wiring.hpp"
#include "core/agent_types.hpp"
#include "core/geofence_geometry.hpp"
#include "modules/command/command_module.hpp"
#include "modules/fleet/fleet_module.hpp"
#include "modules/geofence/geofence_module.hpp"
#include "modules/guard/guard_module.hpp"
#include "modules/lifecycle/agent_lifecycle_module.hpp"
#include "modules/mission/mission_module.hpp"
#include "modules/telemetry/telemetry_module.hpp"
#include "voice/keyword_spotter.hpp"
#include "voice/wav_file.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QTimer>

#include <cmath>
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
constexpr int MS_PER_SECOND = 1000;
/// 每度緯度對應的距離（公尺，球體近似），只用於 --mission-relative 換算
constexpr double METERS_PER_DEGREE = 111319.49;
constexpr double DEGREES_TO_RADIANS = 3.14159265358979323846 / 180.0;
/// 航點三個欄位：緯度（或北向公尺）、經度（或東向公尺）、高度
constexpr int WAYPOINT_FIELDS = 3;
/// 圍籬頂點兩個欄位：緯度（或北向公尺）、經度（或東向公尺）
constexpr int VERTEX_FIELDS = 2;

/// --geofence 或 --geofence-relative 的內容
struct GeofenceSpec
{
    /// 相對模式的原點 Agent
    QString agent;
    /// 每筆 [緯度, 經度]；相對模式為 [北向公尺, 東向公尺]
    QVector<std::array<double, 2>> points;
    bool is_relative = false;
};

/**
 * @brief 解析圍籬頂點 "a,b;a,b;..."（相對模式前面加 "agent:"）
 * @param text 文字
 * @param is_relative 是否為相對模式
 * @param spec 解析結果
 * @return 是否成功
 */
bool parse_geofence_spec(const QString& text, bool is_relative, GeofenceSpec* spec)
{
    QString body = text;
    spec->is_relative = is_relative;
    if (is_relative)
    {
        const qsizetype colon = text.indexOf(QLatin1Char(':'));
        if (colon <= 0)
        {
            return false;
        }
        spec->agent = text.left(colon);
        body = text.mid(colon + 1);
    }
    spec->points.clear();
    for (const QString& item : body.split(QLatin1Char(';'), Qt::SkipEmptyParts))
    {
        const QStringList values = item.split(QLatin1Char(','));
        if (values.size() != VERTEX_FIELDS)
        {
            return false;
        }
        std::array<double, 2> point{};
        for (int i = 0; i < VERTEX_FIELDS; ++i)
        {
            bool is_number = false;
            point.at(i) = values.at(i).trimmed().toDouble(&is_number);
            if (!is_number || !std::isfinite(point.at(i)))
            {
                return false;
            }
        }
        spec->points.push_back(point);
    }
    return !spec->points.isEmpty();
}

/**
 * @brief 圍籬頂點換算為 WGS84；相對模式以原點 Agent 目前位置為基準
 * @param spec 圍籬內容
 * @param origin_state 原點 Agent 的最新狀態（相對模式使用）
 * @param vertices 換算結果
 * @return 是否成功（相對模式需要原點 Agent 的有效位置）
 */
bool make_geofence_vertices(const GeofenceSpec& spec, const AgentStateData& origin_state,
                            std::vector<GeofenceVertexData>* vertices)
{
    vertices->clear();
    if (spec.is_relative && !ui::has_valid_global_position(origin_state.position_global))
    {
        return false;
    }
    const GeofenceVertexData origin{origin_state.position_global[0], origin_state.position_global[1]};
    for (const auto& point : spec.points)
    {
        vertices->push_back(spec.is_relative ? geofence_geometry::to_global({point[1], point[0]}, origin)
                                             : GeofenceVertexData{point[0], point[1]});
    }
    return true;
}

/// --mission 或 --mission-relative 的內容
struct MissionSpec
{
    QString agent;
    /// 每筆 [緯度, 經度, 高度]；相對模式為 [北向公尺, 東向公尺, 高度]
    QVector<std::array<double, 3>> points;
    bool is_relative = false;
};

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

/**
 * @brief 解析 "agent:a,b,c[;a,b,c...]" 格式的航點任務
 * @param text 任務文字
 * @param spec 解析結果（is_relative 由呼叫端設定）
 * @return 是否成功
 */
bool parse_mission_spec(const QString& text, MissionSpec* spec)
{
    const qsizetype colon = text.indexOf(QLatin1Char(':'));
    if (colon <= 0)
    {
        return false;
    }
    spec->agent = text.left(colon);
    spec->points.clear();
    for (const QString& item : text.mid(colon + 1).split(QLatin1Char(';'), Qt::SkipEmptyParts))
    {
        const QStringList values = item.split(QLatin1Char(','));
        if (values.size() != WAYPOINT_FIELDS)
        {
            return false;
        }
        std::array<double, 3> point{};
        for (int i = 0; i < WAYPOINT_FIELDS; ++i)
        {
            bool is_number = false;
            point.at(i) = values.at(i).toDouble(&is_number);
            if (!is_number || !std::isfinite(point.at(i)))
            {
                return false;
            }
        }
        spec->points.append(point);
    }
    return !spec->points.isEmpty();
}

/**
 * @brief 依目前位置把任務內容換成 WGS84 航點
 * @param spec 任務內容
 * @param state Agent 目前狀態（相對模式的基準）
 * @return 任務
 */
MissionPlan make_plan(const MissionSpec& spec, const AgentStateData& state)
{
    MissionPlan plan;
    plan.agent = spec.agent;
    const double latitude = state.position_global[0];
    const double longitude = state.position_global[1];
    for (const auto& point : spec.points)
    {
        if (!spec.is_relative)
        {
            plan.waypoints.append({point[0], point[1], point[2]});
            continue;
        }
        plan.waypoints.append({latitude + point[0] / METERS_PER_DEGREE,
                               longitude + point[1] / (METERS_PER_DEGREE * std::cos(latitude * DEGREES_TO_RADIANS)),
                               point[2]});
    }
    return plan;
}

/**
 * @brief 任務階段名稱（輸出用）
 * @param phase 階段
 * @return 名稱
 */
QString phase_name(MissionPhase phase)
{
    switch (phase)
    {
    case MissionPhase::STARTING: return QStringLiteral("STARTING");
    case MissionPhase::MOVING: return QStringLiteral("MOVING");
    case MissionPhase::DWELLING: return QStringLiteral("DWELLING");
    case MissionPhase::COMPLETED: return QStringLiteral("COMPLETED");
    case MissionPhase::ABORTED: return QStringLiteral("ABORTED");
    case MissionPhase::REJECTED: return QStringLiteral("REJECTED");
    case MissionPhase::LOST: return QStringLiteral("LOST");
    default: return QStringLiteral("IDLE");
    }
}

/**
 * @brief Agent 事件嚴重度名稱（輸出用）
 * @param severity AgentEvent.severity
 * @return 名稱；未知值輸出數值
 */
QString severity_name(uint8_t severity)
{
    switch (static_cast<AgentEventSeverity>(severity))
    {
    case AgentEventSeverity::INFO: return QStringLiteral("INFO");
    case AgentEventSeverity::WARNING: return QStringLiteral("WARNING");
    case AgentEventSeverity::ERROR: return QStringLiteral("ERROR");
    default: return QString::number(severity);
    }
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
    const QCommandLineOption mission_option(
        QStringLiteral("mission"), QStringLiteral("命令之後開始航點任務 agent:緯度,經度,高度[;...]（需 --allow-commands）"),
        QStringLiteral("spec"));
    const QCommandLineOption mission_relative_option(
        QStringLiteral("mission-relative"),
        QStringLiteral("同 --mission，航點改以開始時位置的北向公尺,東向公尺,高度表示"), QStringLiteral("spec"));
    const QCommandLineOption voice_wav_option(
        QStringLiteral("voice-wav"),
        QStringLiteral("改由語音檔觸發航點任務（巡邏）或解鎖、起飛、降落：依序每 3 秒辨識一個 16 位元 PCM WAV，可重複"), QStringLiteral("file"));
    const QCommandLineOption geofence_option(
        QStringLiteral("geofence"),
        QStringLiteral("命令之前設定全體地理圍籬 緯度,經度;緯度,經度;...（3～64 點，需 --allow-commands）"),
        QStringLiteral("vertices"));
    const QCommandLineOption geofence_relative_option(
        QStringLiteral("geofence-relative"),
        QStringLiteral("同 --geofence，頂點改以 agent 目前位置的北向公尺,東向公尺表示 agent:n,e;n,e;..."),
        QStringLiteral("spec"));
    const QCommandLineOption geofence_margin_option(QStringLiteral("geofence-margin"),
                                                    QStringLiteral("圍籬 margin（公尺，0～10，預設 0.5）"),
                                                    QStringLiteral("meters"), QStringLiteral("0.5"));
    const QCommandLineOption geofence_clear_option(QStringLiteral("geofence-clear"),
                                                   QStringLiteral("命令之前清除全體地理圍籬（需 --allow-commands）"));
    parser.addOptions({domain_option, interface_option, peer_option, no_multicast_option, participant_index_option,
                       external_address_option, duration_option, warmup_option, command_option, allow_commands_option,
                       shutdown_option, mission_option, mission_relative_option, voice_wav_option, geofence_option,
                       geofence_relative_option, geofence_margin_option, geofence_clear_option});
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
    MissionSpec mission_spec;
    const bool has_mission = parser.isSet(mission_option) || parser.isSet(mission_relative_option);
    if (parser.isSet(mission_option) && parser.isSet(mission_relative_option))
    {
        print_line(QStringLiteral("--mission 與 --mission-relative 只能擇一"));
        return EXIT_FAILURE;
    }
    if (has_mission)
    {
        mission_spec.is_relative = parser.isSet(mission_relative_option);
        const QString text = parser.value(mission_spec.is_relative ? mission_relative_option : mission_option);
        if (!parse_mission_spec(text, &mission_spec))
        {
            print_line(QStringLiteral("航點任務格式錯誤：%1（應為 agent:a,b,c[;a,b,c...]）").arg(text));
            return EXIT_FAILURE;
        }
    }
    const QStringList voice_files = parser.values(voice_wav_option);
    if (!voice_files.isEmpty() && !has_mission)
    {
        print_line(QStringLiteral("--voice-wav 需搭配 --mission 或 --mission-relative 提供航點"));
        return EXIT_FAILURE;
    }
    GeofenceSpec geofence_spec;
    const bool has_geofence_set = parser.isSet(geofence_option) || parser.isSet(geofence_relative_option);
    const bool has_geofence_clear = parser.isSet(geofence_clear_option);
    if (static_cast<int>(parser.isSet(geofence_option)) + static_cast<int>(parser.isSet(geofence_relative_option))
            + static_cast<int>(has_geofence_clear)
        > 1)
    {
        print_line(QStringLiteral("--geofence、--geofence-relative 與 --geofence-clear 只能擇一"));
        return EXIT_FAILURE;
    }
    if (has_geofence_set)
    {
        const bool is_relative = parser.isSet(geofence_relative_option);
        const QString text = parser.value(is_relative ? geofence_relative_option : geofence_option);
        if (!parse_geofence_spec(text, is_relative, &geofence_spec))
        {
            print_line(QStringLiteral("圍籬格式錯誤：%1（應為 a,b;a,b;...，相對模式前加 agent:）").arg(text));
            return EXIT_FAILURE;
        }
    }
    bool is_margin_number = false;
    const double geofence_margin = parser.value(geofence_margin_option).toDouble(&is_margin_number);
    if (!is_margin_number || !std::isfinite(geofence_margin))
    {
        print_line(QStringLiteral("--geofence-margin 必須是數字"));
        return EXIT_FAILURE;
    }
    const bool has_geofence = has_geofence_set || has_geofence_clear;
    if ((!commands.empty() || !shutdown_target.isEmpty() || has_mission || has_geofence)
        && !parser.isSet(allow_commands_option))
    {
        print_line(QStringLiteral("有指定命令、航點任務、地理圍籬或 shutdown，但未加 --allow-commands，拒絕執行"));
        return EXIT_FAILURE;
    }
    voice::KeywordSpotter spotter;
    if (!voice_files.isEmpty())
    {
        QString voice_error;
        if (!spotter.load(voice::KeywordSpotter::default_voice_dir(), &voice_error))
        {
            print_line(QStringLiteral("語音辨識無法載入：%1").arg(voice_error));
            return EXIT_FAILURE;
        }
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
    // Agent 事件發生時立即輸出；health 隨 [state] 摘要週期輸出
    QObject::connect(runtime.telemetry(), &TelemetryModule::agent_event_received, &receiver,
                     [](const QString& agent, const AgentEventData& event)
                     {
                         print_line(QStringLiteral("[event] %1 %2 code=%3 command=%4 px4_result=%5 %6")
                                        .arg(agent, severity_name(event.severity))
                                        .arg(event.code)
                                        .arg(event.command.isEmpty() ? QStringLiteral("-") : event.command)
                                        .arg(event.px4_result)
                                        .arg(event.detail));
                     });
    QHash<QString, AgentHealthData> latest_health;
    QObject::connect(runtime.telemetry(), &TelemetryModule::health_updated, &receiver,
                     [&latest_health](const QString& agent, const AgentHealthData& health)
                     { latest_health.insert(agent, health); });
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
    QObject::connect(runtime.geofence(), &GeofenceModule::request_acked, &receiver, print_ack);
    // 各機圍籬狀態：列有變化才輸出
    QHash<QString, GeofenceAgentRow> printed_fence_rows;
    QObject::connect(
        runtime.geofence(), &GeofenceModule::overview_changed, &receiver,
        [&printed_fence_rows](const GeofenceOverview& overview)
        {
            for (const GeofenceAgentRow& row : overview.rows)
            {
                const auto it = printed_fence_rows.constFind(row.agent);
                if (it != printed_fence_rows.cend() && *it == row)
                {
                    continue;
                }
                printed_fence_rows.insert(row.agent, row);
                print_line(QStringLiteral("[geofence] %1 %2 fence_id=%3 result=%4 inside=%5 %6")
                               .arg(row.agent, ui::geofence_phase_label(row.phase))
                               .arg(row.fence_id)
                               .arg(row.last_result)
                               .arg(row.is_position_inside ? 1 : 0)
                               .arg(row.detail));
            }
            print_line(QStringLiteral("[geofence] 顯示 fence_id=%1 頂點 %2 個 margin=%3 一致=%4 已送出=%5")
                           .arg(overview.fence_id)
                           .arg(overview.vertices.size())
                           .arg(static_cast<double>(overview.margin_m))
                           .arg(overview.is_consistent ? 1 : 0)
                           .arg(overview.requested_fence_id));
        });

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
                         for (auto it = latest_health.cbegin(); it != latest_health.cend(); ++it)
                         {
                             const AgentHealthData& h = it.value();
                             print_line(QStringLiteral("[health] %1 status_age=%2 odometry_age=%3 global_age=%4 "
                                                       "preflight=%5 failsafe=%6 arming=%7 arming_reason=%8 "
                                                       "flags_valid=%9 flags=0x%10 offboard=%11")
                                            .arg(it.key())
                                            .arg(h.px4_status_age_s, 0, 'f', 1)
                                            .arg(h.px4_odometry_age_s, 0, 'f', 1)
                                            .arg(h.px4_global_position_age_s, 0, 'f', 1)
                                            .arg(h.is_px4_pre_flight_checks_pass)
                                            .arg(h.is_px4_failsafe)
                                            .arg(h.px4_arming_state)
                                            .arg(h.px4_latest_arming_reason)
                                            .arg(h.is_failsafe_flags_valid)
                                            .arg(h.failsafe_flags, 0, 16)
                                            .arg(h.is_offboard_session_active));
                         }
                     });
    summary_timer.start(SUMMARY_INTERVAL_MS);

    // 依序送出命令；一律經 Guard
    const int warmup_ms = parser.value(warmup_option).toInt() * 1000;
    int delay_ms = warmup_ms;
    // 地理圍籬在命令之前：經 GeofenceModule 指定版本號，再由 Guard 驗證
    if (has_geofence)
    {
        QTimer::singleShot(
            delay_ms, &receiver,
            [&runtime, &latest_states, geofence_spec, geofence_margin, has_geofence_clear]()
            {
                GeofenceModule* geofence = runtime.geofence();
                if (has_geofence_clear)
                {
                    print_line(QStringLiteral("[request] geofence clear"));
                    QMetaObject::invokeMethod(geofence, [geofence]() { geofence->request_clear(); });
                    return;
                }
                GeofenceData fence;
                fence.margin_m = static_cast<float>(geofence_margin);
                if (!make_geofence_vertices(geofence_spec, latest_states.value(geofence_spec.agent), &fence.vertices))
                {
                    print_line(QStringLiteral("[geofence] %1 沒有有效位置，無法換算相對圍籬").arg(geofence_spec.agent));
                    return;
                }
                QStringList points;
                for (const GeofenceVertexData& vertex : fence.vertices)
                {
                    points.append(QStringLiteral("%1,%2")
                                      .arg(vertex.latitude_deg, 0, 'f', 7)
                                      .arg(vertex.longitude_deg, 0, 'f', 7));
                }
                print_line(QStringLiteral("[request] geofence %1 個頂點 margin=%2 %3")
                               .arg(fence.vertices.size())
                               .arg(geofence_margin)
                               .arg(points.join(QLatin1Char(';'))));
                QMetaObject::invokeMethod(geofence, [geofence, fence]() { geofence->request_set(fence); });
            });
        delay_ms += COMMAND_INTERVAL_MS;
    }
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

    // 航點任務：與桌面程式相同的 MissionController（語音確認流程）與 MissionModule（命令經 Guard）
    MissionController mission_controller;
    ToastModel mission_notices;
    if (has_mission)
    {
        // 桌面程式彈出的警告（例如不在懸停、沒有待確認的任務）在此輸出
        mission_controller.set_toast_model(&mission_notices);
        QObject::connect(&mission_notices, &QAbstractItemModel::rowsInserted, &receiver,
                         [&mission_notices](const QModelIndex& /*parent*/, int first, int /*last*/)
                         {
                             const QModelIndex index = mission_notices.index(first);
                             print_line(QStringLiteral("[notice] %1：%2")
                                            .arg(mission_notices.data(index, ToastModel::TitleRole).toString(),
                                                 mission_notices.data(index, ToastModel::MessageRole).toString()));
                         });
        const CoreModules modules{runtime.fleet(),   runtime.telemetry(), runtime.guard(),
                                  runtime.command(), runtime.lifecycle(), runtime.mission(),
                                  runtime.geofence()};
        wire_mission(modules, &mission_controller);
        mission_controller.set_selected_agent(mission_spec.agent);
        // 語音「解鎖」「起飛」「降落」倒數結束：與桌面程式的 UiBridge 相同，一律經 Guard 送出
        QObject::connect(&mission_controller, &MissionController::flight_command_requested, &receiver,
                         [&runtime, agent = mission_spec.agent](const QString& command, const QVariantList& params)
                         {
                             CommandRequest request;
                             request.agent = agent;
                             request.command = command;
                             for (const QVariant& value : params)
                             {
                                 bool is_number = false;
                                 const double number = value.toDouble(&is_number);
                                 request.params.push_back(is_number ? number : std::nan(""));
                             }
                             print_line(QStringLiteral("[request] %1 %2").arg(request.agent, request.command));
                             GuardModule* guard = runtime.guard();
                             QMetaObject::invokeMethod(guard, [guard, request]() { guard->request_command(request); });
                         });
        QObject::connect(runtime.mission(), &MissionModule::mission_status_changed, &receiver,
                         [](const MissionStatus& status)
                         {
                             print_line(QStringLiteral("[mission] %1 %2 %3/%4 %5")
                                            .arg(status.agent, phase_name(status.phase))
                                            .arg(status.waypoint_index + 1)
                                            .arg(status.waypoint_count)
                                            .arg(status.message));
                         });
        QTimer::singleShot(
            delay_ms, &receiver,
            [&runtime, &mission_controller, &latest_states, mission_spec, is_voice = !voice_files.isEmpty()]()
            {
                const MissionPlan plan = make_plan(mission_spec, latest_states.value(mission_spec.agent));
                if (is_voice)
                {
                    // 經介面相同的路徑載入航點，之後由語音口令開始
                    for (int i = 0; i < plan.waypoints.size(); ++i)
                    {
                        mission_controller.addWaypoint(plan.waypoints.at(i).latitude, plan.waypoints.at(i).longitude);
                        mission_controller.setWaypointAltitude(i, plan.waypoints.at(i).altitude_m);
                    }
                    print_line(QStringLiteral("[mission] %1 已載入 %2 個航點，等待語音口令")
                                   .arg(plan.agent)
                                   .arg(mission_controller.waypoint_count()));
                    return;
                }
                print_line(QStringLiteral("[request] mission %1 %2 個航點").arg(plan.agent).arg(plan.waypoints.size()));
                MissionModule* mission = runtime.mission();
                QMetaObject::invokeMethod(mission, [mission, plan]() { mission->start_mission(plan); });
            });
        for (const QString& file : voice_files)
        {
            delay_ms += COMMAND_INTERVAL_MS;
            QTimer::singleShot(delay_ms, &receiver,
                               [&spotter, &mission_controller, file]()
                               {
                                   voice::MonoAudio audio;
                                   QString wav_error;
                                   if (!voice::read_wav_file(file, &audio, &wav_error))
                                   {
                                       print_line(QStringLiteral("[voice] %1").arg(wav_error));
                                       return;
                                   }
                                   const QStringList keywords = spotter.spot(audio.samples, audio.sample_rate);
                                   print_line(QStringLiteral("[voice] %1 → %2")
                                                  .arg(QFileInfo(file).fileName(),
                                                       keywords.isEmpty() ? QStringLiteral("（未辨識到口令）")
                                                                          : keywords.join(QLatin1Char(','))));
                                   for (const QString& keyword : keywords)
                                   {
                                       mission_controller.on_voice_keyword(keyword);
                                   }
                               });
        }
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

    QTimer::singleShot(parser.value(duration_option).toInt() * MS_PER_SECOND, &app, &QCoreApplication::quit);
    const int result = QCoreApplication::exec();
    summary_timer.stop();
    runtime.stop();

    print_line(QStringLiteral("結束：收到 %1 台 Agent 的狀態，失敗回報 %2 筆").arg(latest_states.size()).arg(failed_acks));
    return result == 0 && !latest_states.isEmpty() ? EXIT_SUCCESS : EXIT_FAILURE;
}
