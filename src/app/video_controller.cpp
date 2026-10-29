/**
 * @file video_controller.cpp
 * @brief 影像串流設定實作
 * @author Taiphoon GCS Team
 * @date 2026-10-29
 */
#include "app/video_controller.hpp"

#include "core/command_protocol.hpp"

#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>


namespace
{
Q_LOGGING_CATEGORY(lc_video, "gcs.ui.video")
}  // namespace

namespace gcs
{

namespace
{

const QString KEY_URL_TEMPLATE = QStringLiteral("site/video/url_template");
/// 單機覆寫保存為「uav_N_agent=URL」字串清單
const QString KEY_OVERRIDES = QStringLiteral("site/video/overrides");
const QString KEY_PANEL_OPEN = QStringLiteral("ui/video_panel_open");
const QString PLACEHOLDER_NUMBER = QStringLiteral("{N}");
const QString PLACEHOLDER_AGENT = QStringLiteral("{agent}");
/// 驗證範本時代入的 namespace
const QString SAMPLE_AGENT = QStringLiteral("uav_1_agent");

/// FFmpeg 可播放且適合即時影像的協定；file 等本機來源不接受，避免誤設成錄影檔而以為是即時畫面
const QStringList SUPPORTED_SCHEMES = {
    QStringLiteral("rtsp"), QStringLiteral("rtsps"), QStringLiteral("rtmp"), QStringLiteral("rtmps"),
    QStringLiteral("srt"),  QStringLiteral("udp"),   QStringLiteral("rtp"),  QStringLiteral("tcp"),
    QStringLiteral("http"), QStringLiteral("https"),
};
/// 可只指定埠號、由本機接收的協定（例如 udp://@:5600）
const QStringList LISTEN_SCHEMES = {QStringLiteral("udp"), QStringLiteral("rtp"), QStringLiteral("srt"),
                                    QStringLiteral("tcp")};

}  // namespace

VideoController::VideoController(bool is_available, const QString& file_path, QObject* parent)
    : QObject(parent),
      is_available_(is_available),
      settings_(file_path.isEmpty() ? std::make_unique<QSettings>()
                                    : std::make_unique<QSettings>(file_path, QSettings::IniFormat))
{
    load();
}

std::optional<int> VideoController::agent_number(const QString& agent)
{
    static const QRegularExpression PATTERN(QStringLiteral("^uav_([1-9][0-9]*)_agent$"));
    const auto match = PATTERN.match(agent);
    if (!match.hasMatch() || !command_protocol::is_single_agent(agent))
    {
        return std::nullopt;
    }
    bool is_number = false;
    const int number = match.captured(1).toInt(&is_number);
    if (!is_number)
    {
        return std::nullopt;
    }
    return number;
}

QString VideoController::validate_url(const QString& url)
{
    const QString text = url.trimmed();
    if (text.isEmpty())
    {
        return QStringLiteral("URL 不可為空");
    }
    const QUrl parsed(text, QUrl::StrictMode);
    if (!parsed.isValid())
    {
        return QStringLiteral("URL 格式錯誤：%1").arg(redacted(text));
    }
    const QString scheme = parsed.scheme().toLower();
    if (!SUPPORTED_SCHEMES.contains(scheme))
    {
        return QStringLiteral("不支援的協定「%1」，請使用 rtsp、udp、srt、rtmp 或 http").arg(scheme);
    }
    const bool has_host = !parsed.host().isEmpty();
    const bool has_port = parsed.port() > 0;
    if (!has_host && !(LISTEN_SCHEMES.contains(scheme) && has_port))
    {
        return QStringLiteral("URL 缺少主機位址：%1").arg(redacted(text));
    }
    return QString();
}

QString VideoController::redacted(const QString& url)
{
    const QUrl parsed(url.trimmed());
    if (!parsed.isValid() || parsed.userInfo().isEmpty())
    {
        return url.trimmed();
    }
    return parsed.toString(QUrl::RemoveUserInfo);
}

QString VideoController::expand(const QString& url_template, const QString& agent)
{
    const auto number = agent_number(agent);
    if (!number.has_value() || url_template.isEmpty())
    {
        return QString();
    }
    QString url = url_template;
    url.replace(PLACEHOLDER_NUMBER, QString::number(*number));
    url.replace(PLACEHOLDER_AGENT, agent);
    return url;
}

void VideoController::load()
{
    const QString url_template = settings_->value(KEY_URL_TEMPLATE).toString().trimmed();
    if (url_template.isEmpty() || validate_url(expand(url_template, SAMPLE_AGENT)).isEmpty())
    {
        url_template_ = url_template;
    }
    else
    {
        qCWarning(lc_video).noquote() << "忽略不合法的影像機群網址：" << redacted(url_template);
    }
    overrides_.clear();
    const QStringList entries = settings_->value(KEY_OVERRIDES).toStringList();
    for (const QString& entry : entries)
    {
        // 只以第一個「=」分隔：URL 的查詢字串也可能含有「=」
        const qsizetype separator = entry.indexOf(QLatin1Char('='));
        const QString agent = entry.left(separator).trimmed();
        const QString url = entry.mid(separator + 1).trimmed();
        if (separator <= 0 || !agent_number(agent).has_value() || !validate_url(url).isEmpty())
        {
            qCWarning(lc_video).noquote() << "忽略不合法的影像單機網址：" << redacted(entry);
            continue;
        }
        overrides_.insert(agent, url);
    }
}

void VideoController::save_overrides()
{
    QStringList entries;
    for (auto it = overrides_.cbegin(); it != overrides_.cend(); ++it)
    {
        entries.append(it.key() + QLatin1Char('=') + it.value());
    }
    entries.sort();
    settings_->setValue(KEY_OVERRIDES, entries);
}

bool VideoController::is_panel_open() const
{
    return settings_->value(KEY_PANEL_OPEN, false).toBool();
}

void VideoController::set_panel_open(bool is_open)
{
    if (is_open == is_panel_open())
    {
        return;
    }
    settings_->setValue(KEY_PANEL_OPEN, is_open);
    emit panelChanged();
}

QString VideoController::urlFor(const QString& agent) const
{
    if (!agent_number(agent).has_value())
    {
        return QString();
    }
    const auto it = overrides_.constFind(agent);
    if (it != overrides_.cend())
    {
        return it.value();
    }
    return expand(url_template_, agent);
}

QString VideoController::setUrlTemplate(const QString& url_template)
{
    const QString trimmed = url_template.trimmed();
    if (!trimmed.isEmpty())
    {
        // 沒有編號的範本會讓每一格顯示同一路影像，容易把畫面誤認成別台無人機
        if (!trimmed.contains(PLACEHOLDER_NUMBER) && !trimmed.contains(PLACEHOLDER_AGENT))
        {
            return QStringLiteral("機群網址需包含 {N} 或 {agent}，否則每台無人機都會顯示同一路影像");
        }
        const QString error = validate_url(expand(trimmed, SAMPLE_AGENT));
        if (!error.isEmpty())
        {
            return error;
        }
    }
    if (trimmed == url_template_)
    {
        return QString();
    }
    settings_->setValue(KEY_URL_TEMPLATE, trimmed);
    url_template_ = trimmed;
    qCInfo(lc_video).noquote() << "影像機群網址："
                               << (trimmed.isEmpty() ? QStringLiteral("（未設定）") : redacted(trimmed));
    emit changed();
    return QString();
}

QString VideoController::setAgentUrl(const QString& agent, const QString& url)
{
    if (!agent_number(agent).has_value())
    {
        return QStringLiteral("無人機「%1」不合法").arg(agent);
    }
    const QString trimmed = url.trimmed();
    // 清空或與機群網址相同時改用機群網址，日後修改範本時這台會一起更新
    if (trimmed.isEmpty() || trimmed == expand(url_template_, agent))
    {
        if (!overrides_.remove(agent))
        {
            return QString();
        }
        save_overrides();
        qCInfo(lc_video).noquote() << "影像來源：" << agent << "改用機群網址";
        emit changed();
        return QString();
    }
    const QString error = validate_url(trimmed);
    if (!error.isEmpty())
    {
        return error;
    }
    if (overrides_.value(agent) == trimmed)
    {
        return QString();
    }
    overrides_.insert(agent, trimmed);
    save_overrides();
    qCInfo(lc_video).noquote() << "影像來源：" << agent << "使用單機網址" << redacted(trimmed);
    emit changed();
    return QString();
}

}  // namespace gcs
