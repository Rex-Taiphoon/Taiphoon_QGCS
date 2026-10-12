/**
 * @file network_controller.cpp
 * @brief 網路設定與 DDS 生命週期實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "app/network_controller.hpp"

#include "app/app_settings.hpp"
#include "app/core_runtime.hpp"
#include "app/ui_bridge.hpp"

#include <QLoggingCategory>
#include <QNetworkInterface>
#include <QRegularExpression>

namespace
{
Q_LOGGING_CATEGORY(lc_network, "gcs.network")

/**
 * @brief 連線燈號提示文字
 * @param config 網路設定
 * @return 例如「domain 0、multicast、探索入口 192.168.0.101」
 */
QString describe(const gcs::link::DdsNetworkConfig& config)
{
    QStringList parts;
    parts << QStringLiteral("domain %1").arg(config.domain_id);
    if (!config.network_interface.empty())
    {
        parts << QStringLiteral("網卡 %1").arg(QString::fromStdString(config.network_interface));
    }
    parts << (config.is_multicast_allowed ? QStringLiteral("multicast") : QStringLiteral("單播"));
    if (!config.peers.empty())
    {
        QStringList peers;
        for (const auto& peer : config.peers)
        {
            peers << QString::fromStdString(peer);
        }
        parts << QStringLiteral("探索入口 %1").arg(peers.join(QStringLiteral("、")));
    }
    return parts.join(QStringLiteral("、"));
}

}  // namespace

namespace gcs
{

NetworkController::NetworkController(AppSettings* settings, CoreRuntime* runtime, UiBridge* bridge, QObject* parent)
    : QObject(parent), settings_(settings), runtime_(runtime), bridge_(bridge), address_timer_(this)
{
    refreshInterfaces();
    known_addresses_ = local_ipv4_addresses();
    address_timer_.setInterval(ADDRESS_CHECK_INTERVAL_MS);
    connect(&address_timer_, &QTimer::timeout, this, &NetworkController::check_addresses);
}

void NetworkController::set_session_override(const link::DdsNetworkConfig& config)
{
    session_override_ = config;
}

link::DdsNetworkConfig NetworkController::current_config() const
{
    if (session_override_.has_value())
    {
        return *session_override_;
    }
    return settings_ != nullptr ? settings_->network_config() : link::DdsNetworkConfig{};
}

bool NetworkController::start()
{
    if (runtime_ == nullptr)
    {
        return false;
    }
    QString error;
    const bool is_running = runtime_->start(current_config(), &error);
    report_status(is_running, error);
    address_timer_.start();
    if (is_running)
    {
        emit runtimeStarted();
    }
    return is_running;
}

bool NetworkController::restart_with(const link::DdsNetworkConfig& config)
{
    if (runtime_ == nullptr)
    {
        return false;
    }
    const bool was_running = runtime_->is_running();
    QString error;
    const bool is_running = runtime_->restart_network(config, &error);
    report_status(is_running, error);
    if (!was_running && is_running)
    {
        emit runtimeStarted();
    }
    return is_running;
}

void NetworkController::report_status(bool is_running, const QString& error)
{
    const QString message = is_running ? describe(current_config()) : error;
    qCInfo(lc_network) << (is_running ? "DDS 已啟動：" : "DDS 啟動失敗：") << message;
    if (bridge_ != nullptr)
    {
        bridge_->set_link_status(is_running, message);
    }
}

void NetworkController::refreshInterfaces()
{
    QVariantList list;
    for (const QNetworkInterface& network_interface : QNetworkInterface::allInterfaces())
    {
        const auto flags = network_interface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || flags.testFlag(QNetworkInterface::IsLoopBack))
        {
            continue;
        }
        for (const QNetworkAddressEntry& entry : network_interface.addressEntries())
        {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
            {
                continue;
            }
            const QString address = entry.ip().toString();
            list.append(QVariantMap{
                {QStringLiteral("name"), network_interface.humanReadableName()},
                {QStringLiteral("address"), address},
                {QStringLiteral("label"), QStringLiteral("%1（%2）").arg(network_interface.humanReadableName(), address)},
            });
        }
    }
    interfaces_ = list;
    emit interfacesChanged();
}

QString NetworkController::applySite(int domain_id, const QString& network_interface, const QString& peers_text,
                                     bool is_multicast_allowed, int participant_index)
{
    if (settings_ == nullptr)
    {
        return QStringLiteral("設定尚未初始化");
    }
    static const QRegularExpression SEPARATORS(QStringLiteral("[\\s,，、]+"));
    const QStringList peers = peers_text.split(SEPARATORS, Qt::SkipEmptyParts);
    const QString error =
        settings_->saveSite(domain_id, network_interface, peers, is_multicast_allowed, participant_index);
    if (!error.isEmpty())
    {
        return error;
    }
    // 使用者主動保存的設定優先於命令列的暫時設定
    session_override_.reset();
    if (!restart_with(current_config()))
    {
        return bridge_ != nullptr ? bridge_->link_message() : QStringLiteral("DDS 啟動失敗");
    }
    return QString();
}

QSet<QString> NetworkController::local_ipv4_addresses()
{
    QSet<QString> addresses;
    for (const QHostAddress& address : QNetworkInterface::allAddresses())
    {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback())
        {
            addresses.insert(address.toString());
        }
    }
    return addresses;
}

void NetworkController::check_addresses()
{
    const QSet<QString> current = local_ipv4_addresses();
    if (current == known_addresses_)
    {
        return;
    }
    known_addresses_ = current;
    refreshInterfaces();
    // DDS 公告的位址在建立 participant 時決定；位址改變後必須重建，否則 Agent 會送往舊位址
    qCWarning(lc_network) << "本機網路位址改變，重建 DDS";
    const bool is_running = restart_with(current_config());
    if (bridge_ != nullptr)
    {
        bridge_->toasts().add(is_running ? ToastModel::Kind::WARNING : ToastModel::Kind::ERROR,
                              QStringLiteral("網路位址已改變"),
                              is_running ? QStringLiteral("已以新位址重新建立 DDS 連線")
                                         : QStringLiteral("重新建立 DDS 連線失敗，請檢查網路設定"));
    }
}

}  // namespace gcs
