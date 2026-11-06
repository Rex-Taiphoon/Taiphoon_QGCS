/**
 * @file geofence_module.hpp
 * @brief 地理圍籬模組 — 分配圍籬版本號送交 Guard，並依各機 GeofenceStatus 整理機隊的圍籬總覽
 * @author Taiphoon GCS Team
 * @date 2026-11-06
 *
 * 圍籬由 GCS 發布到 /uav_all_agent/geofence，全體 Agent 共用（介面文件 4.4）。本模組只指定 fence_id
 * 並送出請求，驗證由 GuardModule 負責、發布由 CommandModule 負責，本模組不直接連到 CommandModule。
 *
 * fence_id 每次設定或清除都遞增；QGCS 重新啟動後，以 transient_local 取得的各機狀態中
 * fence_id 與 last_request_id 的最大值加 1 接續，避免 Agent 把新設定當成舊版本。
 *
 * GeofenceStatus 只在改變時發布：送出設定後 5 秒內沒有處理到新版本、或上線 5 秒仍沒有任何回報的 Agent
 * 列為未回報。各機版本不一致時不自動重送，由操作人員在 FENCE 分頁確認後重新套用。
 */
#pragma once

#include "core/agent_types.hpp"
#include "core/gcs_module.hpp"

#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <optional>

namespace gcs
{

/// 圍籬模組的時間參數；測試可縮短
struct GeofenceTiming
{
    /// 等待 Agent 回報的時間上限（毫秒）
    int report_timeout_ms = 5000;
    /// 檢查逾時的週期（毫秒）
    int tick_ms = 500;
};

/**
 * @class GeofenceModule
 * @brief 圍籬設定請求與機隊圍籬總覽
 */
class GeofenceModule : public GcsModule
{
    Q_OBJECT

public:
    /**
     * @brief 建立地理圍籬模組
     * @param timing 時間參數
     * @param parent 父物件
     */
    explicit GeofenceModule(const GeofenceTiming& timing = GeofenceTiming(), QObject* parent = nullptr);

    bool init() override;
    void shutdown() override;
    bool health_check() const override;
    QString module_name() const override { return QStringLiteral("GeofenceModule"); }

    /** @brief 目前的總覽 @return 總覽 */
    const GeofenceOverview& overview() const { return overview_; }

    /** @brief 下一次設定會使用的 fence_id @return 版本號 */
    uint32_t next_fence_id() const;

public slots:
    /**
     * @brief 設定圍籬：指定新的 fence_id 後送交 Guard 驗證
     * @param fence 頂點與 margin；fence_id 與 action 由本模組填入
     */
    void request_set(const gcs::GeofenceData& fence);

    /** @brief 清除圍籬：指定新的 fence_id 後送交 Guard 驗證 */
    void request_clear();

    /**
     * @brief Agent 回報的圍籬狀態
     * @param agent_ns Agent namespace
     * @param status GeofenceStatus
     */
    void on_geofence_status_updated(const QString& agent_ns, const gcs::GeofenceStatusData& status);

    /**
     * @brief 圍籬設定結果（Guard 拒絕或 CommandModule 發布結果）
     * @param ack 回報
     */
    void on_geofence_acked(const gcs::CommandAck& ack);

    /**
     * @brief 機隊清單更新：總覽只列線上 Agent
     * @param snapshot FleetModule 回報
     */
    void on_fleet_changed(const gcs::FleetSnapshot& snapshot);

signals:
    /** @brief 圍籬設定或清除請求；只能連到 GuardModule::request_geofence */
    void geofence_requested(const gcs::GeofenceData& fence);

    /** @brief 本模組送出的設定被拒絕或已發布（command 為 GEOFENCE_ACK_NAME） */
    void request_acked(const gcs::CommandAck& ack);

    /** @brief 總覽改變 */
    void overview_changed(const gcs::GeofenceOverview& overview);

private:
    /// 單一線上 Agent
    struct AgentFence
    {
        std::optional<GeofenceStatusData> status;
        /// 列入線上清單的時間（毫秒）
        qint64 online_since_ms = 0;
    };

    /**
     * @brief 送出請求並等待 Guard／CommandModule 回報
     * @param fence 已填入 fence_id 與 action 的設定
     */
    void submit(GeofenceData fence);

    /**
     * @brief 依目前資料推導單一 Agent 的圍籬狀況
     * @param agent_ns Agent namespace
     * @param agent 資料
     * @return 一列（尚未比對多數版本）
     */
    GeofenceAgentRow make_row(const QString& agent_ns, const AgentFence& agent) const;

    /** @brief 重新整理總覽，改變時發出 overview_changed 並寫日誌 */
    void refresh();

    /** @brief 是否有需要等待逾時的 Agent；有才啟動計時器 */
    void update_ticking();

    /** @brief 目前時間（毫秒，單調遞增） @return 毫秒 */
    qint64 now_ms() const;

    GeofenceTiming timing_;
    bool is_initialized_ = false;
    QElapsedTimer clock_;
    QTimer tick_timer_;
    QHash<QString, AgentFence> agents_;
    /// 已看過的最大版本號（各機的 fence_id、last_request_id 與本機送出的版本）
    uint32_t max_seen_id_ = 0;
    /// 已送出、等待 Guard／CommandModule 回報的設定
    std::optional<GeofenceData> pending_;
    /// 最近一次已發布的設定
    std::optional<GeofenceData> requested_;
    /// requested_ 發布的時間（毫秒）
    qint64 requested_ms_ = 0;
    GeofenceOverview overview_;
};

}  // namespace gcs
