/**
 * @file mission_module.hpp
 * @brief 航點任務模組 — 將航點上傳給 Agent 執行，並依 Agent 回報的任務狀態追蹤進度
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 *
 * 任務由 Agent 執行（ADR-0020）：本模組把航點上傳至 /{ns}/mission/plan，等 Agent 回報 LOADED 後
 * 送出 MISSION_START <mission_id>，之後進度完全依 /{ns}/mission/status 顯示。
 * 上傳與 MISSION_START／MISSION_STOP 都經 GuardModule 驗證，本模組不直接連到 CommandModule。
 *
 * 到達判斷、停留、逾時與中止（操作人員介入、飛行狀態改變、落地）都由 Agent 處理；QGCS 斷線時 Agent 照樣
 * 執行任務，因此 Agent 離線或任務狀態停止更新時本模組只回報 LOST，不視為中止，也不自動送出任何命令。
 * QGCS 重新啟動或晚加入時，以 transient_local 取得的最後狀態接手追蹤 Agent 執行中的任務。
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

/// 航點任務的時間參數；測試可縮短
struct MissionTiming
{
    /// 每個航點的停留時間（毫秒），隨任務上傳給 Agent
    int dwell_ms = 3000;
    /// 檢查逾時的週期（毫秒）
    int tick_ms = 200;
    /// 上傳後等待 Agent 載入、送出 MISSION_START 後等待 Agent 開始的時間上限（毫秒）
    int handshake_timeout_ms = 3000;
    /// 任務進行中超過此時間沒有任務狀態即回報失去聯繫（毫秒）；Agent 每秒重送狀態
    int status_timeout_ms = 5000;
};

/**
 * @class MissionModule
 * @brief 航點任務的上傳、開始、停止與進度追蹤
 */
class MissionModule : public GcsModule
{
    Q_OBJECT

public:
    /// 航點編輯限制（MissionController 使用）；與 GuardModule 的上傳檢查相同
    static constexpr int MAX_WAYPOINTS = mission_limits::MAX_WAYPOINTS;
    static constexpr double MIN_ALTITUDE_M = mission_limits::MIN_ALTITUDE_M;
    static constexpr double MAX_ALTITUDE_M = mission_limits::MAX_ALTITUDE_M;

    /**
     * @brief 建立航點任務模組
     * @param timing 時間參數
     * @param parent 父物件
     */
    explicit MissionModule(const MissionTiming& timing = MissionTiming(), QObject* parent = nullptr);

    bool init() override;
    void shutdown() override;
    bool health_check() const override;
    QString module_name() const override { return QStringLiteral("MissionModule"); }

    /**
     * @brief 指定 Agent 是否正在開始或執行任務
     * @param agent_ns Agent namespace
     * @return 是否開始中或執行中
     */
    bool is_running(const QString& agent_ns) const;

public slots:
    /**
     * @brief 開始航點任務：上傳航點，Agent 載入後送出 MISSION_START；條件不符時回報 REJECTED
     * @param plan 任務
     */
    void start_mission(const gcs::MissionPlan& plan);

    /**
     * @brief 操作人員停止任務：送出 MISSION_STOP（經 Guard），Agent 停在目前位置並回報 ABORTED
     * @param agent_ns Agent namespace
     */
    void stop_mission(const QString& agent_ns);

    /**
     * @brief Agent 回報的任務狀態
     * @param agent_ns Agent namespace
     * @param status WaypointMissionStatus
     */
    void on_mission_status_updated(const QString& agent_ns, const gcs::WaypointMissionStatusData& status);

    /**
     * @brief 航點任務上傳結果（Guard 拒絕或 CommandModule 發布結果）
     * @param ack 回報
     */
    void on_mission_upload_acked(const gcs::CommandAck& ack);

    /**
     * @brief 命令結果：只處理本模組送出的 MISSION_START
     * @param ack Guard 或 CommandModule 的回報
     */
    void on_command_acked(const gcs::CommandAck& ack);

    /** @brief Agent 離線：回報失去聯繫，Agent 會繼續執行任務 @param agent_ns Agent namespace */
    void on_agent_offline(const QString& agent_ns);

    /**
     * @brief 機隊清單更新：列為離線（含資料停滯）的 Agent 回報失去聯繫，與畫面的離線判定一致
     * @param snapshot FleetModule 回報
     */
    void on_fleet_changed(const gcs::FleetSnapshot& snapshot);

signals:
    /** @brief 航點任務上傳請求；只能連到 GuardModule::request_mission_upload */
    void mission_upload_requested(const QString& agent_ns, const gcs::WaypointMissionData& mission);

    /** @brief MISSION_START／MISSION_STOP 命令請求；只能連到 GuardModule::request_command */
    void command_requested(const gcs::CommandRequest& request);

    /** @brief 任務狀態改變（同一狀態的週期重送不會重複發出） */
    void mission_status_changed(const gcs::MissionStatus& status);

private:
    /// 本機發起、Agent 尚未開始的任務進度
    enum class StartStage
    {
        /// 已送出上傳，等待 Guard／CommandModule 回報
        AWAITING_UPLOAD_ACK,
        /// 已上傳，等待 Agent 回報 LOADED
        AWAITING_LOADED,
        /// 已送出 MISSION_START，等待 Guard／CommandModule 回報
        AWAITING_START_ACK,
        /// MISSION_START 已送出，等待 Agent 回報 RUNNING
        AWAITING_RUNNING
    };

    /// 開始中的任務
    struct PendingStart
    {
        uint32_t mission_id = 0;
        int waypoint_count = 0;
        StartStage stage = StartStage::AWAITING_UPLOAD_ACK;
        /// 目前階段開始的時間（毫秒）
        qint64 stage_started_ms = 0;
    };

    /// 單一 Agent 的任務追蹤
    struct AgentMission
    {
        std::optional<PendingStart> pending;
        /// 本機開始或接手追蹤的任務；COMPLETED／ABORTED 只對此任務回報
        uint32_t tracked_mission_id = 0;
        /// 最近一次收到任務狀態的時間（毫秒）
        qint64 status_received_ms = 0;
        /// 最近一次回報的狀態與所屬任務；只在改變時才再發出
        std::optional<MissionStatus> last_reported;
        uint32_t last_reported_mission_id = 0;
    };

    /**
     * @brief Agent 已載入任務：送出 MISSION_START（經 Guard）
     * @param agent_ns Agent namespace
     */
    void send_start(const QString& agent_ns);

    /**
     * @brief 依 Agent 狀態更新執行中任務的進度
     * @param agent_ns Agent namespace
     * @param status Agent 回報
     */
    void follow_agent_status(const QString& agent_ns, const WaypointMissionStatusData& status);

    /**
     * @brief 開始中的任務失敗：回報 REJECTED 並結束開始流程
     * @param agent_ns Agent namespace
     * @param message 原因
     */
    void reject_pending(const QString& agent_ns, const QString& message);

    /**
     * @brief 回報失去聯繫；開始中的任務一併結束開始流程
     * @param agent_ns Agent namespace
     * @param message 原因
     */
    void report_lost(const QString& agent_ns, const QString& message);

    /** @brief 週期檢查開始流程與任務狀態逾時 */
    void on_tick();

    /** @brief 需要週期檢查時啟動計時器 */
    void ensure_ticking();

    /**
     * @brief 回報任務狀態；與上一次相同（同任務、同階段、同航點）時不重複發出，REJECTED 與 STARTING 一律發出
     * @param agent_ns Agent namespace
     * @param mission_id 所屬任務
     * @param phase 階段
     * @param index 目前航點（0 起算），沒有時為 -1
     * @param count 航點數
     * @param message 說明
     */
    void report(const QString& agent_ns, uint32_t mission_id, MissionPhase phase, int index, int count,
                const QString& message);

    /**
     * @brief Agent 回報的 REJECTED／ABORTED 原因轉為說明
     * @param status Agent 回報
     * @return 說明
     */
    static QString reason_text(const WaypointMissionStatusData& status);

    /**
     * @brief 最近回報的階段是否表示 Agent 仍在執行（含失去聯繫）
     * @param mission 任務追蹤
     * @return 是否仍在執行
     */
    static bool is_following(const AgentMission& mission);

    /** @brief 產生非 0 且與目前追蹤不同的任務識別碼 @param previous 目前追蹤的識別碼 @return 識別碼 */
    static uint32_t new_mission_id(uint32_t previous);

    /** @brief 目前時間（毫秒，單調遞增） @return 毫秒 */
    qint64 now_ms() const;

    MissionTiming timing_;
    bool is_initialized_ = false;
    QElapsedTimer clock_;
    QTimer tick_timer_;
    QHash<QString, AgentMission> missions_;
};

}  // namespace gcs
