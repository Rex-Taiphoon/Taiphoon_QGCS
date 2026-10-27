/**
 * @file mission_module.hpp
 * @brief 航點任務模組 — 單機依序飛往預設航點，每段 MOVE_POINT 都經 GuardModule 驗證
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 *
 * Agent 命令協定沒有航點清單命令，任務由 QGCS 逐段送出 MOVE_POINT：
 * 到達航點後停留，再送下一段；最後一個航點到達後不再送命令，由 Agent 懸停。
 *
 * MOVE_POINT 的座標是 Agent planner 以啟動時位置為原點的 local NED，該原點不對外發布；
 * 本模組以「目前 position_ned + 目前位置到航點的北／東位移」換算，假設 planner 原點與 PX4 local
 * 原點相同（Agent 在起飛點啟動時成立）。兩者有偏差時，到達判斷改以「已離開起點且停止移動」為準。
 * SITL 實測 EGO-Swarm 執行 MOVE_POINT 時 flight_mode 維持 HOVER、不會轉為 MOVING，因此不能只靠任務階段判斷。
 * 到達判斷只用持續、連續的觀測：接近需持續一段時間；狀態未更新時（最後一筆位置不再變動，看起來像已停止）
 * 暫停判斷與下一段命令；相鄰兩筆位置換算的速度不合理時視為定位跳動，重新計時且跳動距離不算離開起點。
 *
 * 任務中只要收到其他成功送出的命令（操作人員介入）、狀態離開懸停／移動、Agent 離線（topic 消失或資料停滯）
 * 或逾時，即中止任務；
 * 中止時不自動送出任何命令。操作人員主動停止時送出「MOVE_POINT 目前位置」讓 Agent 停下：
 * SITL 實測 HOLD 不會取消 EGO-Swarm 已規劃的軌跡。
 */
#pragma once

#include "core/agent_types.hpp"
#include "core/gcs_module.hpp"

#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QTimer>

#include <array>
#include <optional>

namespace gcs
{

/// 航點任務的時間與距離參數；測試可縮短
struct MissionTiming
{
    /// 每個航點的停留時間（毫秒）
    int dwell_ms = 3000;
    /// 檢查停留與逾時的週期（毫秒）
    int tick_ms = 200;
    /// 與預期目標的距離在此範圍內即視為到達（公尺），與 Agent 的到達容許誤差同級
    double arrival_tolerance_m = 1.0;
    /// 已離開起點後，位置在 settle_radius_m 內持續此時間即視為 Agent 已完成該段（毫秒）
    int hover_settle_ms = 1500;
    /// 停止移動的判斷半徑（公尺）
    double settle_radius_m = 0.3;
    /// 與該段起點距離超過此值才算「已離開起點」（公尺）；避免命令剛送出、尚未起動時誤判為已停止
    double departure_distance_m = 2.0;
    /// 每段逾時的基本時間（毫秒）
    int leg_base_timeout_ms = 30000;
    /// 估算逾時用的最低速度（公尺／秒）；低於此速度仍未到達即視為異常
    double min_expected_speed_mps = 0.3;
    /// 狀態超過此時間未更新即暫停到達判斷與下一段命令（毫秒）；需小於 hover_settle_ms，凍結的位置才不會被當成已停止
    int state_fresh_ms = 1000;
    /// 與預期目標的距離需持續在 arrival_tolerance_m 內這麼久才算到達（毫秒）；單筆定位跳動不算
    int near_hold_ms = 1000;
    /// 相鄰兩筆位置換算的速度超過此值即視為定位跳動（公尺／秒）；Agent 預設最高速度 2.5 公尺／秒
    double max_plausible_speed_mps = 10.0;
};

/**
 * @class MissionModule
 * @brief 航點任務執行器
 */
class MissionModule : public GcsModule
{
    Q_OBJECT

public:
    /// 單一任務的航點上限
    static constexpr int MAX_WAYPOINTS = 50;
    /// 航點高度範圍（公尺）
    static constexpr double MIN_ALTITUDE_M = 1.0;
    static constexpr double MAX_ALTITUDE_M = 120.0;
    /// 航點與目前位置的水平距離上限（公尺）：防止顯示原點設錯時把航點放到極遠處
    static constexpr double MAX_RANGE_M = 1000.0;

    /**
     * @brief 建立航點任務模組
     * @param timing 時間與距離參數
     * @param parent 父物件
     */
    explicit MissionModule(const MissionTiming& timing = MissionTiming(), QObject* parent = nullptr);

    bool init() override;
    void shutdown() override;
    bool health_check() const override;
    QString module_name() const override { return QStringLiteral("MissionModule"); }

    /**
     * @brief 指定 Agent 是否正在執行任務
     * @param agent_ns Agent namespace
     * @return 是否執行中
     */
    bool is_running(const QString& agent_ns) const;

public slots:
    /**
     * @brief 開始航點任務；條件不符時回報 REJECTED，不送出任何命令
     * @param plan 任務
     */
    void start_mission(const gcs::MissionPlan& plan);

    /**
     * @brief 操作人員停止任務：中止並要求停在目前位置（MOVE_POINT 目前位置，經 Guard）
     * @param agent_ns Agent namespace
     */
    void stop_mission(const QString& agent_ns);

    /** @brief 更新 Agent 狀態 @param agent_ns Agent namespace @param state 最新 AgentState */
    void on_state_updated(const QString& agent_ns, const gcs::AgentStateData& state);

    /** @brief 更新 Agent 姿態 @param agent_ns Agent namespace @param attitude 最新 AgentAttitude */
    void on_attitude_updated(const QString& agent_ns, const gcs::AgentAttitudeData& attitude);

    /**
     * @brief 命令結果：判斷自己的航點命令是否送出，或操作人員是否介入
     * @param ack Guard 或 CommandModule 的回報
     */
    void on_command_acked(const gcs::CommandAck& ack);

    /** @brief Agent 離線：中止其任務 @param agent_ns Agent namespace */
    void on_agent_offline(const QString& agent_ns);

    /**
     * @brief 機隊清單更新：列為離線（含資料停滯）的 Agent 中止任務，與畫面的離線判定一致
     * @param snapshot FleetModule 回報
     */
    void on_fleet_changed(const gcs::FleetSnapshot& snapshot);

signals:
    /** @brief 航點命令請求；只能連到 GuardModule::request_command */
    void command_requested(const gcs::CommandRequest& request);

    /** @brief 任務狀態改變 */
    void mission_status_changed(const gcs::MissionStatus& status);

private:
    /// 單段航點的進度
    enum class LegPhase
    {
        /// 已送出 MOVE_POINT，等待 Guard／CommandModule 回報
        AWAITING_ACK,
        MOVING,
        DWELLING
    };

    /// 執行中的任務
    struct ActiveMission
    {
        MissionPlan plan;
        int index = 0;
        LegPhase phase = LegPhase::AWAITING_ACK;
        /// 起始機頭方向（度，北為 0、順時針），每段 MOVE_POINT 都維持此方向
        double heading_deg = 0.0;
        /// 本段預期目標與起點（PX4 local NED，公尺）
        std::array<double, 3> target_ned{};
        std::array<double, 3> start_ned{};
        /// 已轉為 MOVING 或已離開起點
        bool has_departed = false;
        /// 最近一次位置變動超過 settle_radius_m 的位置與時間
        std::array<double, 3> settle_anchor_ned{};
        qint64 settle_since_ms = 0;
        /// 開始持續位於到達容許範圍內的時間；不在範圍內時為空
        std::optional<qint64> near_since_ms;
        /// 狀態未更新而暫停判斷中（只用於避免重複記錄）
        bool is_paused_for_stale = false;
        qint64 leg_started_ms = 0;
        qint64 leg_timeout_ms = 0;
        qint64 dwell_started_ms = 0;
    };

    /**
     * @brief 檢查開始條件
     * @param plan 任務
     * @param heading_deg 輸出目前機頭方向
     * @return 錯誤訊息；空字串表示可以開始
     */
    QString validate_start(const MissionPlan& plan, double* heading_deg) const;

    /**
     * @brief 送出目前航點的 MOVE_POINT
     * @param agent_ns Agent namespace
     */
    void send_leg(const QString& agent_ns);

    /**
     * @brief 依最新狀態判斷是否已到達目前航點
     * @param agent_ns Agent namespace
     */
    void evaluate_arrival(const QString& agent_ns);

    /**
     * @brief 新狀態與上一筆不連續（資料中斷後恢復或定位跳動）時重新計時
     * @param agent_ns Agent namespace
     * @param previous_ned 上一筆位置
     * @param elapsed_ms 與上一筆的接收間隔（毫秒）
     */
    void check_continuity(const QString& agent_ns, const std::array<double, 3>& previous_ned, qint64 elapsed_ms);

    /**
     * @brief 最新狀態是否在 state_fresh_ms 內收到
     * @param agent_ns Agent namespace
     * @param now 目前時間（毫秒）
     * @return 是否仍然有效
     */
    bool is_state_fresh(const QString& agent_ns, qint64 now) const;

    /**
     * @brief 狀態未更新：暫停判斷，第一次時記錄
     * @param agent_ns Agent namespace
     * @param mission 執行中的任務
     */
    void pause_for_stale(const QString& agent_ns, ActiveMission* mission) const;

    /**
     * @brief 接近與停止的持續時間從目前重新計算
     * @param mission 執行中的任務
     * @param position 目前位置
     * @param now 目前時間（毫秒）
     */
    static void restart_arrival_timing(ActiveMission* mission, const std::array<double, 3>& position, qint64 now);

    /** @brief 週期檢查停留結束與逾時 */
    void on_tick();

    /**
     * @brief 結束任務並回報
     * @param agent_ns Agent namespace
     * @param phase COMPLETED 或 ABORTED
     * @param message 說明
     */
    void finish(const QString& agent_ns, MissionPhase phase, const QString& message);

    /**
     * @brief 回報目前進度
     * @param agent_ns Agent namespace
     * @param phase 階段
     * @param message 說明
     */
    void report(const QString& agent_ns, MissionPhase phase, const QString& message);

    /** @brief 目前時間（毫秒，單調遞增） @return 毫秒 */
    qint64 now_ms() const;

    MissionTiming timing_;
    bool is_initialized_ = false;
    QElapsedTimer clock_;
    QTimer tick_timer_;
    QHash<QString, AgentStateData> states_;
    /// 各 Agent 最新狀態的接收時間（毫秒）
    QHash<QString, qint64> state_received_ms_;
    QHash<QString, AgentAttitudeData> attitudes_;
    QHash<QString, ActiveMission> missions_;
};

}  // namespace gcs
