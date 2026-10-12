/**
 * @file network_controller.hpp
 * @brief 網路設定與 DDS 生命週期：套用站點設定、偵測位址變化並重建 DDS（F-13）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 取代 Go 啟動器的設定頁與「Wi-Fi 位址改變需重新啟動」流程；使用者不需編輯任何檔案。
 */
#pragma once

#include "link/dds_config.hpp"

#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>

#include <optional>

namespace gcs
{

class AppSettings;
class CoreRuntime;
class UiBridge;

/**
 * @class NetworkController
 * @brief 網路設定控制
 */
class NetworkController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList interfaces READ interfaces NOTIFY interfacesChanged)

public:
    /// 本機位址檢查週期（毫秒）
    static constexpr int ADDRESS_CHECK_INTERVAL_MS = 5000;

    /**
     * @brief 建立網路控制
     * @param settings 設定
     * @param runtime 核心執行環境
     * @param bridge 用於更新連線燈號與提示
     * @param parent 父物件
     */
    NetworkController(AppSettings* settings, CoreRuntime* runtime, UiBridge* bridge, QObject* parent = nullptr);

    /**
     * @brief 本次執行改用指定網路設定（命令列參數），不寫入設定檔
     * @param config 網路設定
     */
    void set_session_override(const link::DdsNetworkConfig& config);

    /**
     * @brief 以目前設定啟動核心與 DDS
     * @return 是否成功；失敗時介面仍可開啟網路設定修正
     */
    bool start();

    /**
     * @brief 可選的網卡（含 IPv4），供網路設定選單使用
     * @return 每項為 {name, label, address}
     */
    QVariantList interfaces() const { return interfaces_; }

    /** @brief 重新列出網卡 */
    Q_INVOKABLE void refreshInterfaces();

    /**
     * @brief 保存站點設定並以新設定重建 DDS
     * @param domain_id ROS_DOMAIN_ID
     * @param network_interface 網卡名稱或 IPv4，空字串為自動
     * @param peers_text Agent 位址，以換行、逗號或空白分隔
     * @param is_multicast_allowed 是否允許 multicast
     * @param participant_index 固定 index，-1 為自動
     * @return 錯誤訊息；空字串表示成功
     */
    Q_INVOKABLE QString applySite(int domain_id, const QString& network_interface, const QString& peers_text,
                                  bool is_multicast_allowed, int participant_index);

signals:
    void interfacesChanged();
    /** @brief 核心由停止變為運作（含首次啟動）；模組為新建立，需重新組裝 UI 連線 */
    void runtimeStarted();

private slots:
    void check_addresses();

private:
    link::DdsNetworkConfig current_config() const;
    bool restart_with(const link::DdsNetworkConfig& config);
    void report_status(bool is_running, const QString& error);
    static QSet<QString> local_ipv4_addresses();

    AppSettings* settings_ = nullptr;
    CoreRuntime* runtime_ = nullptr;
    UiBridge* bridge_ = nullptr;
    QTimer address_timer_;
    QSet<QString> known_addresses_;
    QVariantList interfaces_;
    std::optional<link::DdsNetworkConfig> session_override_;
};

}  // namespace gcs
