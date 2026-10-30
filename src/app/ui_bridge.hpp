/**
 * @file ui_bridge.hpp
 * @brief QML 與核心模組之間的唯一通道（取代網頁版 useWebSocket 與各 store）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * UiBridge 位於主執行緒，不持有任何模組或 IAgentLink 指標；
 * 對外只發出 command_requested 與 shutdown_requested 兩種請求，
 * 由 ui_wiring.cpp 分別連到 GuardModule 與 AgentLifecycleModule（AGENTS.md）。
 * 高頻遙測先寫入快取，再由計時器以固定頻率刷新介面（N-02）。
 * Agent 事件只把警告與錯誤彈出提示（同一事件短時間內只顯示一次），一般資訊只寫入日誌。
 */
#pragma once

#include "app/fleet_model.hpp"
#include "app/toast_model.hpp"
#include "app/ui_format.hpp"
#include "core/agent_types.hpp"
#include "core/command_protocol.hpp"
#include "modules/guard/guard_rules.hpp"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace gcs
{

/**
 * @class UiBridge
 * @brief 介面狀態與操作入口
 */
class UiBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList onlineAgents READ online_agents NOTIFY fleetChanged)
    Q_PROPERTY(int onlineCount READ online_count NOTIFY fleetChanged)
    Q_PROPERTY(QString selectedAgent READ selected_agent WRITE selectAgent NOTIFY selectionChanged)
    Q_PROPERTY(bool isSingleSelected READ is_single_selected NOTIFY selectionChanged)
    Q_PROPERTY(bool isAllSelected READ is_all_selected NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedSummary READ selected_summary NOTIFY telemetryChanged)
    Q_PROPERTY(QVariantList trajectories READ trajectories NOTIFY telemetryChanged)
    Q_PROPERTY(QObject* fleetModel READ fleet_model CONSTANT)
    Q_PROPERTY(QObject* toastModel READ toast_model CONSTANT)
    Q_PROPERTY(bool isLinkConnected READ is_link_connected NOTIFY linkStatusChanged)
    Q_PROPERTY(QString linkMessage READ link_message NOTIFY linkStatusChanged)
    Q_PROPERTY(bool canArm READ can_arm NOTIFY availabilityChanged)
    Q_PROPERTY(bool canDisarm READ can_disarm NOTIFY availabilityChanged)
    Q_PROPERTY(bool canOffboard READ can_offboard NOTIFY availabilityChanged)
    Q_PROPERTY(bool canTakeoff READ can_takeoff NOTIFY availabilityChanged)
    Q_PROPERTY(bool canMove READ can_move NOTIFY availabilityChanged)
    Q_PROPERTY(bool canYaw READ can_yaw NOTIFY availabilityChanged)
    Q_PROPERTY(bool canRtl READ can_rtl NOTIFY availabilityChanged)
    Q_PROPERTY(bool canLand READ can_land NOTIFY availabilityChanged)
    Q_PROPERTY(bool canFormation READ can_formation NOTIFY availabilityChanged)
    Q_PROPERTY(bool canShutdownAgent READ can_shutdown_agent NOTIFY availabilityChanged)
    Q_PROPERTY(bool canRebootFc READ can_reboot_fc NOTIFY availabilityChanged)
    Q_PROPERTY(bool canForceRebootFc READ can_force_reboot_fc NOTIFY availabilityChanged)
    Q_PROPERTY(bool isRebootForceMode READ is_reboot_force_mode NOTIFY availabilityChanged)
    Q_PROPERTY(QString rebootBlockedReason READ reboot_blocked_reason NOTIFY availabilityChanged)

public:
    /// 介面刷新週期（毫秒），約 20 Hz
    static constexpr int REFRESH_INTERVAL_MS = 50;
    /// 命令待回應與 ARM／DISARM 解鎖確認的逾時（毫秒），與網頁版預設值相同
    static constexpr int DEFAULT_COMMAND_TIMEOUT_MS = 3000;
    /// 同一 Agent、事件代碼與命令在此時間內（毫秒）只彈出一次提示
    static constexpr qint64 EVENT_DEDUP_MS = 10000;
    /// 預檢未通過事件（303）等待健康狀態確認的時間（毫秒）：health 約 1 Hz，至少涵蓋兩筆
    static constexpr int DEFAULT_PREFLIGHT_CONFIRM_MS = 2500;

    /**
     * @brief 建立 UiBridge
     * @param command_timeout_ms 命令待回應與解鎖確認的逾時（毫秒）
     * @param preflight_confirm_ms 預檢未通過事件等待健康狀態確認的時間（毫秒）
     * @param parent 父物件
     */
    explicit UiBridge(int command_timeout_ms = DEFAULT_COMMAND_TIMEOUT_MS,
                      int preflight_confirm_ms = DEFAULT_PREFLIGHT_CONFIRM_MS, QObject* parent = nullptr);

    QStringList online_agents() const { return online_agents_; }
    int online_count() const { return static_cast<int>(online_agents_.size()); }
    QString selected_agent() const { return selected_agent_; }
    bool is_single_selected() const;
    bool is_all_selected() const;
    QVariantMap selected_summary() const { return selected_summary_; }
    QVariantList trajectories() const { return trajectories_; }
    QObject* fleet_model() { return &fleet_model_; }
    QObject* toast_model() { return &toast_model_; }
    ToastModel& toasts() { return toast_model_; }
    bool is_link_connected() const { return is_link_connected_; }
    QString link_message() const { return link_message_; }

    bool can_arm() const { return availability_.value(CmdType::ARM); }
    bool can_disarm() const { return availability_.value(CmdType::DISARM); }
    bool can_offboard() const { return availability_.value(CmdType::MODE_OFFBOARD); }
    bool can_takeoff() const { return availability_.value(CmdType::TAKEOFF); }
    bool can_move() const { return availability_.value(CmdType::MOVE_FORWARD); }
    bool can_yaw() const { return availability_.value(CmdType::YAW); }
    bool can_rtl() const { return availability_.value(CmdType::MODE_RTL); }
    bool can_land() const { return availability_.value(CmdType::MODE_LAND); }
    bool can_formation() const { return availability_.value(CmdType::FORMATION_CIRCLE); }
    bool can_shutdown_agent() const;
    bool can_reboot_fc() const { return can_reboot_fc_; }
    bool can_force_reboot_fc() const { return can_force_reboot_fc_; }
    /// 選取單機的 PX4 狀態不明，重新啟動飛控只能強制
    bool is_reboot_force_mode() const { return is_reboot_force_mode_; }
    /// 目前方式（一般或強制）不可用的原因；可用時為空字串
    QString reboot_blocked_reason() const { return reboot_blocked_reason_; }

    /**
     * @brief 選取目標
     * @param agent 空字串為未選取、"all" 為全體，或單一 Agent namespace
     */
    Q_INVOKABLE void selectAgent(const QString& agent);

    /**
     * @brief 對目前選取目標送出命令（經 Guard 驗證）；同一目標與命令待回應時不重複送出
     * @param command 命令名稱
     * @param params 數值參數
     */
    Q_INVOKABLE void sendCommand(const QString& command, const QVariantList& params = QVariantList());

    /** @brief 要求關閉目前選取的單一 Agent（L1 確認後由 QML 呼叫） */
    Q_INVOKABLE void requestAgentShutdown();

    /**
     * @brief 任務階段中文名稱
     * @param flight_mode AgentState.flight_mode
     * @return 中文名稱
     */
    Q_INVOKABLE QString flightModeLabel(int flight_mode) const;

    /**
     * @brief 立即刷新介面（測試用；正常運作由計時器刷新）
     */
    void refresh_now();

    /**
     * @brief 設定地圖顯示原點；只改變地圖、機群與軌跡的經緯度，快取的遙測與命令不受影響
     * @param origin 顯示原點
     */
    void set_display_origin(const ui::DisplayOrigin& origin);

public slots:
    void on_state_updated(const QString& agent_ns, const gcs::AgentStateData& state);
    void on_attitude_updated(const QString& agent_ns, const gcs::AgentAttitudeData& attitude);
    void on_trajectory_updated(const QString& agent_ns, const gcs::AgentTrajectoryData& trajectory);
    void on_fleet_changed(const gcs::FleetSnapshot& snapshot);
    void on_command_acked(const gcs::CommandAck& ack);

    /**
     * @brief 更新 Agent 健康狀態（PX4 連線、預檢與強制重新啟動飛控的判斷）
     * @param agent_ns Agent namespace
     * @param health 最新 AgentHealth
     */
    void on_health_updated(const QString& agent_ns, const gcs::AgentHealthData& health);

    /**
     * @brief Agent 事件：錯誤紅色提示、警告黃色提示、一般資訊只寫日誌
     *
     * 預檢未通過（303）在正常降落自動上鎖時可能短暫出現，先等健康狀態確認仍未通過才提示（介面文件事件代碼表）。
     * @param agent_ns Agent namespace
     * @param event Agent 事件
     */
    void on_agent_event(const QString& agent_ns, const gcs::AgentEventData& event);

    /**
     * @brief 更新 DDS 連線狀態
     * @param is_connected DDS 是否已啟動
     * @param message 狀態說明
     */
    void set_link_status(bool is_connected, const QString& message);

signals:
    /** @brief 命令請求；只能連到 GuardModule::request_command */
    void command_requested(const gcs::CommandRequest& request);
    /** @brief 關閉請求；只能連到 AgentLifecycleModule::request_shutdown */
    void shutdown_requested(const QString& agent_ns);

    void fleetChanged();
    void selectionChanged();
    void telemetryChanged();
    void availabilityChanged();
    void linkStatusChanged();

private:
    void mark_dirty();
    void refresh();
    void recompute_availability();
    void rebuild_summary();
    void rebuild_trajectories();

    /**
     * @brief 開始追蹤待回應命令；已有相同待回應時回傳 false
     * @param agent 目標
     * @param command 命令名稱
     * @param timeout_ms 逾時（毫秒）
     * @return 是否開始追蹤
     */
    bool begin_pending(const QString& agent, const QString& command, int timeout_ms);

    /**
     * @brief ARM／DISARM 已送出後，開始等待 AgentState 的解鎖狀態符合預期
     *
     * 命令協定沒有飛控回報，送出成功不代表 PX4 已執行；
     * 逾時仍未觀察到預期狀態時提示使用者，只影響提示，不重送命令。
     * @param agent 單機 namespace 或全體目標
     * @param is_expected_armed 預期是否解鎖
     */
    void begin_arm_check(const QString& agent, bool is_expected_armed);

    /**
     * @brief 收到狀態時檢查是否已符合預期解鎖狀態
     * @param agent_ns Agent namespace
     * @param state 最新狀態
     */
    void resolve_arm_check(const QString& agent_ns, const AgentStateData& state);

    /**
     * @brief 回報命令結果：成功只寫日誌；失敗與警告寫日誌並彈出提示
     * @param kind 種類
     * @param title 標題
     * @param message 內容
     */
    void notify(ToastModel::Kind kind, const QString& title, const QString& message);

    /**
     * @brief 依嚴重程度彈出 Agent 事件提示；同一事件在 EVENT_DEDUP_MS 內重複時只寫日誌
     * @param agent_ns Agent namespace
     * @param event Agent 事件（警告或錯誤）
     */
    void show_agent_event(const QString& agent_ns, const AgentEventData& event);

    /**
     * @brief 開始以健康狀態確認預檢未通過事件；逾時仍未恢復才提示
     * @param agent_ns Agent namespace
     * @param event 預檢未通過事件
     */
    void begin_preflight_check(const QString& agent_ns, const AgentEventData& event);

    /**
     * @brief 收到健康狀態時檢查預檢是否已恢復
     * @param agent_ns Agent namespace
     * @param health 最新 AgentHealth
     */
    void resolve_preflight_check(const QString& agent_ns, const AgentHealthData& health);

    /**
     * @brief 依介面快取整理重新啟動飛控的判斷資訊（與 GuardModule 使用同一份規則）
     * @param agent_ns Agent namespace
     * @return 已知資訊
     */
    guard_rules::RebootFacts reboot_facts(const QString& agent_ns) const;

    /**
     * @brief 重新計算重新啟動飛控的可用狀態
     * @return 是否有變動
     */
    bool recompute_reboot_availability();

    /// 等待解鎖狀態確認的項目
    struct ArmCheck
    {
        quint64 id = 0;
        bool is_expected_armed = false;
    };

    /// 等待健康狀態確認的預檢未通過事件
    struct PreflightCheck
    {
        quint64 id = 0;
        AgentEventData event;
        /// 事件之後是否收到健康狀態（沒有收到時無法確認，逾時照樣提示）
        bool has_health_after = false;
    };

    int command_timeout_ms_ = DEFAULT_COMMAND_TIMEOUT_MS;
    int preflight_confirm_ms_ = DEFAULT_PREFLIGHT_CONFIRM_MS;
    FleetModel fleet_model_;
    ToastModel toast_model_;
    QTimer refresh_timer_;
    bool is_dirty_ = false;

    QStringList online_agents_;
    QString selected_agent_;
    QHash<QString, AgentView> views_;
    QVariantMap selected_summary_;
    QVariantList trajectories_;
    QHash<CmdType, bool> availability_;
    bool can_reboot_fc_ = false;
    bool can_force_reboot_fc_ = false;
    bool is_reboot_force_mode_ = false;
    QString reboot_blocked_reason_;
    /// Agent → 最後一次 PX4 狀態有效時的 AgentState；Agent 離線後保留（與 GuardModule 相同）
    QHash<QString, AgentStateData> last_valid_states_;
    ui::DisplayOrigin display_origin_;
    bool is_link_connected_ = false;
    QString link_message_;

    /// "agent:command" → 識別碼；逾時計時器以識別碼確認仍是同一筆請求
    QHash<QString, quint64> pending_commands_;
    quint64 next_pending_id_ = 1;

    /// Agent namespace → 等待中的解鎖狀態確認；逾時計時器以識別碼確認仍是同一筆
    QHash<QString, ArmCheck> arm_checks_;

    /// Agent namespace → 等待中的預檢確認；逾時計時器以識別碼確認仍是同一筆
    QHash<QString, PreflightCheck> preflight_checks_;
    /// "agent:code:command" → 上次彈出提示的時間（event_clock_ 毫秒）
    QHash<QString, qint64> event_shown_ms_;
    QElapsedTimer event_clock_;
};

}  // namespace gcs
