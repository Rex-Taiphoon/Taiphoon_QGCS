/**
 * @file mission_controller.hpp
 * @brief 航點任務的介面狀態：各機航點編輯、開始前確認、語音口令與任務進度
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 *
 * 位於主執行緒。航點以 Agent 回報的 WGS84 座標保存，提供給 QML 時才依地圖顯示原點換算；
 * 地圖點選的位置先反向換算再保存，任務不受顯示原點影響。
 * 按鈕開始任務需經確認：先暫存，10 秒內按確認或說「確認」才送給 MissionModule。
 * 語音「請執行任務」改為倒數：辨識後倒數 5 秒自動開始，期間可取消或按 EXEC 提前開始。
 * 只支援單機；航點不保存，關閉程式或該機離線即清除。
 */
#pragma once

#include "app/toast_model.hpp"
#include "app/ui_format.hpp"
#include "core/agent_types.hpp"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <optional>

namespace gcs
{

/**
 * @class MissionController
 * @brief 航點任務的介面狀態與操作入口
 */
class MissionController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList waypoints READ waypoints NOTIFY changed)
    Q_PROPERTY(QVariantList routes READ routes NOTIFY changed)
    Q_PROPERTY(QVariantList waypointMarkers READ waypoint_markers NOTIFY changed)
    Q_PROPERTY(int waypointCount READ waypoint_count NOTIFY changed)
    Q_PROPERTY(bool canEdit READ can_edit NOTIFY changed)
    Q_PROPERTY(bool isEditing READ is_editing WRITE setEditing NOTIFY changed)
    Q_PROPERTY(bool canStart READ can_start NOTIFY changed)
    Q_PROPERTY(QString startBlockReason READ start_block_reason NOTIFY changed)
    Q_PROPERTY(bool isRunning READ is_running NOTIFY changed)
    Q_PROPERTY(bool isStatusUnknown READ is_status_unknown NOTIFY changed)
    Q_PROPERTY(bool canStop READ can_stop NOTIFY changed)
    Q_PROPERTY(QString statusText READ status_text NOTIFY changed)
    Q_PROPERTY(bool isConfirmPending READ is_confirm_pending NOTIFY changed)
    Q_PROPERTY(QString confirmText READ confirm_text NOTIFY changed)
    Q_PROPERTY(bool isAutoStartPending READ is_auto_start_pending NOTIFY changed)
    Q_PROPERTY(int autoStartSeconds READ auto_start_seconds NOTIFY countdownChanged)

public:
    /// 開始任務的確認時限（毫秒）
    static constexpr int CONFIRM_TIMEOUT_MS = 10000;
    /// 語音「請執行任務」到自動開始的倒數（毫秒）
    static constexpr int AUTO_START_DELAY_MS = 5000;
    /// 倒數秒數的更新間隔（毫秒）
    static constexpr int COUNTDOWN_TICK_MS = 200;
    /// 語音「確認」需在確認提示出現後至少這麼久才接受（毫秒）：同一句話同時說出兩個口令不算確認
    static constexpr int VOICE_CONFIRM_MIN_DELAY_MS = 800;
    /// 沒有高度資料時新航點的高度（公尺）
    static constexpr double DEFAULT_ALTITUDE_M = 5.0;

    /**
     * @brief 建立控制器
     * @param confirm_timeout_ms 確認時限（毫秒），測試可縮短
     * @param auto_start_delay_ms 語音開始的倒數（毫秒），測試可縮短
     * @param parent 父物件
     */
    explicit MissionController(int confirm_timeout_ms = CONFIRM_TIMEOUT_MS,
                               int auto_start_delay_ms = AUTO_START_DELAY_MS, QObject* parent = nullptr);

    QVariantList waypoints() const;
    QVariantList routes() const;
    /** @brief 所有航點的地圖標記（攤平成一層，供 MapItemView 使用） @return 標記清單 */
    QVariantList waypoint_markers() const;
    int waypoint_count() const;
    bool can_edit() const;
    /** @brief 是否在地圖點選新增航點 @return 是否編輯中 */
    bool is_editing() const { return is_editing_ && can_edit(); }

    /**
     * @brief 開始或結束地圖編輯
     * @param is_editing 是否編輯
     */
    void setEditing(bool is_editing);
    bool can_start() const;
    QString start_block_reason() const;
    bool is_running() const;
    /** @brief 任務狀態不明（與 Agent 失去聯繫，Agent 會繼續執行） @return 是否不明 */
    bool is_status_unknown() const;
    /** @brief 是否可停止任務：執行中或狀態不明（不確定 Agent 是否仍在執行時也要能停止） @return 是否可停止 */
    bool can_stop() const;
    QString status_text() const;
    bool is_confirm_pending() const { return pending_.has_value(); }
    QString confirm_text() const;
    /** @brief 暫存的任務是否為語音倒數（時間到自動開始） @return 是否倒數中 */
    bool is_auto_start_pending() const { return pending_.has_value() && pending_->is_auto_start; }
    /** @brief 自動開始前剩餘的秒數（無條件進位） @return 秒數；未倒數時為 0 */
    int auto_start_seconds() const;

    /**
     * @brief 提示訊息的去處；成功訊息只寫日誌，失敗與警告另外彈出
     * @param toasts 提示模型（生命週期需長於本物件），可為 nullptr
     */
    void set_toast_model(ToastModel* toasts) { toasts_ = toasts; }

    /**
     * @brief 目前選取的目標；切換時取消待確認的任務
     * @param agent 空字串、all 或 uav_{N}_agent
     */
    void set_selected_agent(const QString& agent);

    /**
     * @brief 地圖顯示原點
     * @param origin 顯示原點
     */
    void set_display_origin(const ui::DisplayOrigin& origin);

    /**
     * @brief 在地圖點選處新增航點（選取單機時）；高度取該機目前高度
     * @param latitude 地圖顯示緯度
     * @param longitude 地圖顯示經度
     */
    Q_INVOKABLE void addWaypoint(double latitude, double longitude);

    /** @brief 刪除航點 @param index 0 起算 */
    Q_INVOKABLE void removeWaypoint(int index);

    /** @brief 清除選取單機的所有航點 */
    Q_INVOKABLE void clearWaypoints();

    /**
     * @brief 修改航點高度
     * @param index 0 起算
     * @param altitude_m 相對高度（公尺）
     */
    Q_INVOKABLE void setWaypointAltitude(int index, double altitude_m);

    /** @brief 要求開始任務：條件符合時暫存，等待確認 */
    Q_INVOKABLE void requestStart();

    /** @brief 確認開始暫存的任務（語音倒數中則提前開始） */
    Q_INVOKABLE void confirmStart();

    /** @brief 取消暫存的任務 */
    Q_INVOKABLE void cancelStart();

    /** @brief 停止選取單機的任務（要求停在目前位置） */
    Q_INVOKABLE void stopMission();

public slots:
    /** @brief 最新 AgentState @param agent_ns Agent namespace @param state 狀態 */
    void on_state_updated(const QString& agent_ns, const gcs::AgentStateData& state);

    /** @brief 任務狀態 @param status MissionModule 回報 */
    void on_mission_status(const gcs::MissionStatus& status);

    /**
     * @brief 機隊清單更新：離線（含資料停滯）的 Agent 清除航點、任務狀態與待確認的任務
     * @param snapshot FleetModule 回報
     */
    void on_fleet_changed(const gcs::FleetSnapshot& snapshot);

    /**
     * @brief 語音辨識到的口令
     * @param keyword voice_keywords.hpp 定義的口令名稱
     */
    void on_voice_keyword(const QString& keyword);

signals:
    void changed();
    /** @brief 自動開始的剩餘秒數改變 */
    void countdownChanged();
    /** @brief 確認提示或倒數出現（QML 可據此切到任務頁） */
    void confirmRequested();
    /** @brief 開始任務；只連到 MissionModule::start_mission */
    void mission_start_requested(const gcs::MissionPlan& plan);
    /** @brief 停止任務；只連到 MissionModule::stop_mission */
    void mission_stop_requested(const QString& agent_ns);

private:
    /// 等待確認的任務
    struct PendingStart
    {
        MissionPlan plan;
        qint64 requested_ms = 0;
        /// 語音開始：時間到自動開始，而不是取消
        bool is_auto_start = false;
    };

    /**
     * @brief 條件符合時暫存任務
     * @param is_auto_start 是否倒數後自動開始（語音口令）
     */
    void request_start(bool is_auto_start);

    /** @brief 清除暫存的任務並停止計時（不發出 changed） */
    void clear_pending();

    /** @brief 確認時限或倒數到期 */
    void on_confirm_timeout();

    /** @brief 倒數秒數改變時通知畫面 */
    void on_countdown_tick();

    /**
     * @brief 選取單機的開始條件
     * @return 不能開始的原因；空字串表示可以開始
     */
    QString block_reason() const;

    /** @brief 選取的是否為單一 Agent @return 是否為單機 */
    bool is_single_selected() const;

    /**
     * @brief 航點轉為地圖顯示位置
     * @param waypoint 航點
     * @return [緯度, 經度, 高度]
     */
    std::array<double, 3> display_of(const MissionWaypoint& waypoint) const;

    /**
     * @brief 回報提示
     * @param kind 種類
     * @param title 標題
     * @param message 內容
     */
    void notify(ToastModel::Kind kind, const QString& title, const QString& message);

    int confirm_timeout_ms_ = CONFIRM_TIMEOUT_MS;
    int auto_start_delay_ms_ = AUTO_START_DELAY_MS;
    ToastModel* toasts_ = nullptr;
    QString selected_agent_;
    bool is_editing_ = false;
    ui::DisplayOrigin display_origin_;
    QHash<QString, QVector<MissionWaypoint>> waypoints_;
    QHash<QString, AgentStateData> states_;
    QHash<QString, MissionStatus> statuses_;
    std::optional<PendingStart> pending_;
    QTimer confirm_timer_;
    QTimer countdown_timer_;
    int last_countdown_seconds_ = 0;
    QElapsedTimer clock_;
};

}  // namespace gcs
