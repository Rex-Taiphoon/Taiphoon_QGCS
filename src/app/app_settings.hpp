/**
 * @file app_settings.hpp
 * @brief 使用者設定與站點設定（取代網頁版 localStorage，CODE_CONSTRAINT L2-22）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 所有數值都有預設值與合法範圍；讀到超出範圍或格式錯誤的值時使用預設值。
 * 提供給 QML 的屬性採 lowerCamelCase（CODE_CONSTRAINT L2-21）。
 */
#pragma once

#include "link/dds_config.hpp"

#include <QObject>
#include <QSettings>
#include <QStringList>

#include <memory>

namespace gcs
{

/**
 * @class AppSettings
 * @brief 以 QSettings 保存的設定
 */
class AppSettings : public QObject
{
    Q_OBJECT
    // 介面設定（網頁版 settingsStore）
    Q_PROPERTY(bool autoFollow READ auto_follow WRITE set_auto_follow NOTIFY changed)
    Q_PROPERTY(bool showAttitude READ show_attitude WRITE set_show_attitude NOTIFY changed)
    // 飛行控制記憶值（網頁版 CommandPanel 的 localStorage）
    Q_PROPERTY(double takeoffHeight READ takeoff_height WRITE set_takeoff_height NOTIFY changed)
    Q_PROPERTY(double yawDegrees READ yaw_degrees WRITE set_yaw_degrees NOTIFY changed)
    Q_PROPERTY(double formCircleRadius READ form_circle_radius WRITE set_form_circle_radius NOTIFY changed)
    Q_PROPERTY(double formCircleHeight READ form_circle_height WRITE set_form_circle_height NOTIFY changed)
    Q_PROPERTY(double formLinearSpacing READ form_linear_spacing WRITE set_form_linear_spacing NOTIFY changed)
    Q_PROPERTY(double formLinearHeight READ form_linear_height WRITE set_form_linear_height NOTIFY changed)
    Q_PROPERTY(double formVertSpacing READ form_vert_spacing WRITE set_form_vert_spacing NOTIFY changed)
    // 站點設定（取代 Go 啟動器的設定頁）
    Q_PROPERTY(bool isSiteConfigured READ is_site_configured NOTIFY siteChanged)
    Q_PROPERTY(int domainId READ domain_id NOTIFY siteChanged)
    Q_PROPERTY(QString networkInterface READ network_interface NOTIFY siteChanged)
    Q_PROPERTY(QStringList peers READ peers NOTIFY siteChanged)
    Q_PROPERTY(bool isMulticastAllowed READ is_multicast_allowed NOTIFY siteChanged)
    Q_PROPERTY(int participantIndex READ participant_index NOTIFY siteChanged)

public:
    /// participant index 使用 auto
    static constexpr int AUTO_PARTICIPANT_INDEX = -1;

    /**
     * @brief 建立設定
     * @param file_path 設定檔路徑；空字串時使用作業系統預設位置（測試可指定暫存檔）
     * @param parent 父物件
     */
    explicit AppSettings(const QString& file_path = QString(), QObject* parent = nullptr);

    bool auto_follow() const;
    void set_auto_follow(bool is_enabled);
    bool show_attitude() const;
    void set_show_attitude(bool is_enabled);

    double takeoff_height() const;
    void set_takeoff_height(double value);
    double yaw_degrees() const;
    void set_yaw_degrees(double value);
    double form_circle_radius() const;
    void set_form_circle_radius(double value);
    double form_circle_height() const;
    void set_form_circle_height(double value);
    double form_linear_spacing() const;
    void set_form_linear_spacing(double value);
    double form_linear_height() const;
    void set_form_linear_height(double value);
    double form_vert_spacing() const;
    void set_form_vert_spacing(double value);

    bool is_site_configured() const;
    int domain_id() const;
    QString network_interface() const;
    QStringList peers() const;
    bool is_multicast_allowed() const;
    int participant_index() const;

    /**
     * @brief 保存站點設定並標記為已設定
     * @param domain_id ROS_DOMAIN_ID
     * @param network_interface 網卡名稱或 IPv4，空字串為自動
     * @param peers Agent 探索入口
     * @param is_multicast_allowed 是否允許 multicast
     * @param participant_index 固定 index，AUTO_PARTICIPANT_INDEX 為自動
     * @return 錯誤訊息；空字串表示成功
     */
    Q_INVOKABLE QString saveSite(int domain_id, const QString& network_interface, const QStringList& peers,
                                 bool is_multicast_allowed, int participant_index);

    /**
     * @brief 依站點設定產生 DDS 網路設定
     * @return 網路設定
     */
    link::DdsNetworkConfig network_config() const;

    /** @brief 介面設定恢復預設值（網頁版「恢復預設值」，不含站點設定） */
    Q_INVOKABLE void resetUiSettings();

signals:
    /** @brief 介面設定改變 */
    void changed();
    /** @brief 站點設定改變 */
    void siteChanged();

private:
    double read_double(const QString& key, double fallback, double min, double max) const;
    void write_double(const QString& key, double value, double min, double max);

    std::unique_ptr<QSettings> settings_;
};

}  // namespace gcs
