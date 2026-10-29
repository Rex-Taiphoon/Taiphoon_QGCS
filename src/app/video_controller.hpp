/**
 * @file video_controller.hpp
 * @brief 影像串流設定：各機的串流 URL（範本與單機覆寫）與影像面板狀態
 * @author Taiphoon GCS Team
 * @date 2026-10-29
 *
 * Agent 介面沒有影像規格，串流位置由地面站設定：一個含 {N}（UAV 編號）或 {agent}（namespace）的機群網址範本，
 * 套用到 DDS 發現的每一台無人機，個別機不同時再設定單機網址。兩者都從機群表的右鍵選單個別修改，
 * 修改介面關閉時套用。URL 屬站點設定，與介面設定分開保存。
 * 本類別只依賴 QtCore；實際播放由 VideoPlayer（Qt Multimedia）負責。
 */
#pragma once

#include <QHash>
#include <QObject>
#include <QSettings>
#include <QString>

#include <memory>
#include <optional>

namespace gcs
{

/**
 * @class VideoController
 * @brief 影像串流 URL 的設定、驗證與解析
 */
class VideoController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isAvailable READ is_available CONSTANT)
    Q_PROPERTY(QString urlTemplate READ url_template NOTIFY changed)
    Q_PROPERTY(bool isConfigured READ is_configured NOTIFY changed)
    Q_PROPERTY(bool isPanelOpen READ is_panel_open WRITE set_panel_open NOTIFY panelChanged)

public:
    /**
     * @brief 建立設定
     * @param is_available 此版本是否包含影像播放（Qt Multimedia）
     * @param file_path 設定檔路徑；空字串時使用作業系統預設位置（測試可指定暫存檔）
     * @param parent 父物件
     */
    explicit VideoController(bool is_available, const QString& file_path = QString(), QObject* parent = nullptr);

    bool is_available() const { return is_available_; }
    QString url_template() const { return url_template_; }

    /** @brief 是否有任何影像來源 @return 範本或單機網址不為空 */
    bool is_configured() const { return !url_template_.isEmpty() || !overrides_.isEmpty(); }
    bool is_panel_open() const;

    /**
     * @brief 展開或收合影像面板（介面設定）
     * @param is_open 是否展開
     */
    void set_panel_open(bool is_open);

    /**
     * @brief 某台無人機的串流 URL：單機網址優先，其次套用機群網址範本
     * @param agent uav_{N}_agent
     * @return URL；未設定或 namespace 不合法時為空字串
     */
    Q_INVOKABLE QString urlFor(const QString& agent) const;

    /**
     * @brief 某台無人機是否設定了單機網址
     * @param agent uav_{N}_agent
     * @return 有單機網址時為 true
     */
    Q_INVOKABLE bool hasAgentUrl(const QString& agent) const { return overrides_.contains(agent); }

    /**
     * @brief 以指定範本代入某台無人機，供編輯機群網址時預覽（不檢查範本是否合法）
     * @param url_template 範本
     * @param agent uav_{N}_agent
     * @return URL；範本為空或 namespace 不合法時為空字串
     */
    Q_INVOKABLE QString previewUrl(const QString& url_template, const QString& agent) const
    {
        return expand(url_template.trimmed(), agent);
    }

    /**
     * @brief 驗證並保存機群網址範本
     * @param url_template 範本，需含 {N} 或 {agent}；空字串表示不使用範本
     * @return 錯誤訊息；空字串表示成功或未變更
     */
    Q_INVOKABLE QString setUrlTemplate(const QString& url_template);

    /**
     * @brief 驗證並保存某台無人機的單機網址
     *
     * 空字串或與機群網址代入結果相同時移除單機網址，改用機群網址。
     * @param agent uav_{N}_agent
     * @param url URL
     * @return 錯誤訊息；空字串表示成功或未變更
     */
    Q_INVOKABLE QString setAgentUrl(const QString& agent, const QString& url);

    /**
     * @brief 從 namespace 取出 UAV 編號
     * @param agent uav_{N}_agent
     * @return 編號（1 起算）；格式不符時無值
     */
    static std::optional<int> agent_number(const QString& agent);

    /**
     * @brief 檢查串流 URL：需有主機或埠號，且為支援的串流協定
     * @param url URL
     * @return 錯誤訊息；空字串表示合法
     */
    static QString validate_url(const QString& url);

    /**
     * @brief 移除 URL 中的帳號密碼，供日誌與畫面顯示
     * @param url URL
     * @return 不含帳號密碼的 URL
     */
    static QString redacted(const QString& url);

signals:
    /** @brief 影像來源設定改變 */
    void changed();
    /** @brief 面板展開狀態改變 */
    void panelChanged();

private:
    /**
     * @brief 套用範本
     * @param url_template 範本
     * @param agent uav_{N}_agent
     * @return URL；namespace 不合法時為空字串
     */
    static QString expand(const QString& url_template, const QString& agent);

    /** @brief 讀取保存的設定；不合法的項目個別略過 */
    void load();

    /** @brief 保存單機網址 */
    void save_overrides();

    bool is_available_ = false;
    std::unique_ptr<QSettings> settings_;
    QString url_template_;
    /// 單機網址（namespace → URL）
    QHash<QString, QString> overrides_;
};

}  // namespace gcs
