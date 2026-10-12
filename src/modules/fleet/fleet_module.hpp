/**
 * @file fleet_module.hpp
 * @brief 機隊管理模組 — 依 DDS 發現與實際資料流維護線上／離線機隊
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 行為沿用網頁版：每 3 秒掃描；topic 首次出現視為上線、消失視為離線；
 * 超過 5 秒沒有狀態資料流則從顯示清單移除但保留訂閱，以利復原；
 * shutdown 成功的 Agent 在 topic 消失前不重新加入。
 */
#pragma once

#include "core/agent_types.hpp"
#include "core/gcs_module.hpp"

#include <QHash>
#include <QSet>
#include <QTimer>

#include <chrono>

namespace gcs
{

class IAgentLink;

/**
 * @class FleetModule
 * @brief 機隊成員管理
 */
class FleetModule : public GcsModule
{
    Q_OBJECT

public:
    /// 時間來源；測試可注入任意時間點
    using Clock = std::chrono::steady_clock;

    /// 掃描週期（毫秒）
    static constexpr std::chrono::milliseconds SCAN_INTERVAL{3000};
    /// 超過此時間未收到狀態資料流即視為停滯（DDS 發現快取可能殘留）
    static constexpr std::chrono::seconds STALE_TIMEOUT{5};

    /**
     * @brief 建立機隊模組
     * @param link Agent 通訊介面，生命週期需長於本模組
     * @param parent 父物件
     */
    explicit FleetModule(IAgentLink* link, QObject* parent = nullptr);

    bool init() override;
    void shutdown() override;
    bool health_check() const override;
    QString module_name() const override { return QStringLiteral("FleetModule"); }

    /**
     * @brief 以指定時間執行一次掃描（計時器與測試共用）
     * @param now 目前時間
     */
    void scan_at(Clock::time_point now);

    /**
     * @brief 以指定時間記錄 Agent 的狀態資料
     * @param agent_ns Agent namespace
     * @param now 收到資料的時間
     */
    void record_state_at(const QString& agent_ns, Clock::time_point now);

    /**
     * @brief 取得目前線上（topic 存在）的 Agent
     * @return Agent namespace 集合
     */
    QSet<QString> online_agents() const;

public slots:
    /** @brief 立即掃描一次 */
    void scan_now();

    /**
     * @brief 收到 Agent 狀態資料流，更新最後可見時間
     * @param agent_ns Agent namespace
     * @param state AgentState（只用於確認資料流存在）
     */
    void on_state_updated(const QString& agent_ns, const gcs::AgentStateData& state);

    /**
     * @brief Agent 已接受 shutdown：立即隔離並抑制重新加入
     * @param agent_ns Agent namespace
     */
    void on_shutdown_accepted(const QString& agent_ns);

signals:
    /** @brief Agent 上線（topic 首次出現） */
    void agent_online(const QString& agent_ns);
    /** @brief Agent 離線（topic 消失或 shutdown 成功） */
    void agent_offline(const QString& agent_ns);
    /** @brief 機隊顯示清單更新（每次掃描都會發出） */
    void fleet_changed(const gcs::FleetSnapshot& snapshot);

private:
    IAgentLink* link_ = nullptr;
    QTimer scan_timer_;
    bool is_initialized_ = false;
    QSet<QString> online_agents_;
    /// Agent → 最後一次收到狀態的時間
    QHash<QString, Clock::time_point> last_seen_;
    /// 已接受 shutdown 的 Agent；直到 topic 消失前不重新加入機隊
    QSet<QString> shutdown_suppressed_agents_;
};

}  // namespace gcs
