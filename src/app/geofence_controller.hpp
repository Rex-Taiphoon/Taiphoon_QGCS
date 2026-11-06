/**
 * @file geofence_controller.hpp
 * @brief 地理圍籬的介面狀態：地圖上的草稿編輯、各機套用狀況與生效中圍籬的顯示
 * @author Taiphoon GCS Team
 * @date 2026-11-06
 *
 * 位於主執行緒。草稿頂點以 Agent 回報的 WGS84 座標保存，提供給 QML 時才依地圖顯示原點換算；
 * 地圖點選或拖曳的位置先反向換算再保存。地圖上的圍籬以各機 GeofenceStatus 為準（GeofenceModule 選出多數機的版本），
 * 不使用本地保存的內容，QGCS 重新啟動後也能還原。
 * 套用與清除只送給 GeofenceModule，由其指定版本號並交給 GuardModule 驗證，本類別不直接發布。
 * 清除圍籬會失去場域保護，需在 10 秒內再次確認。
 */
#pragma once

#include "app/toast_model.hpp"
#include "app/ui_format.hpp"
#include "core/agent_types.hpp"

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <vector>

namespace gcs
{

/**
 * @class GeofenceController
 * @brief 地理圍籬的介面狀態與操作入口
 */
class GeofenceController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isEditing READ is_editing NOTIFY changed)
    Q_PROPERTY(QVariantList draftPoints READ draft_points NOTIFY changed)
    Q_PROPERTY(QVariantList draftPath READ draft_path NOTIFY changed)
    Q_PROPERTY(int draftCount READ draft_count NOTIFY changed)
    Q_PROPERTY(double margin READ margin WRITE setMargin NOTIFY changed)
    Q_PROPERTY(double minMargin READ min_margin CONSTANT)
    Q_PROPERTY(double maxMargin READ max_margin CONSTANT)
    Q_PROPERTY(int maxVertices READ max_vertices CONSTANT)
    Q_PROPERTY(QString draftError READ draft_error NOTIFY changed)
    Q_PROPERTY(bool isDraftSelfIntersecting READ is_draft_self_intersecting NOTIFY changed)
    Q_PROPERTY(bool canApply READ can_apply NOTIFY changed)
    Q_PROPERTY(bool hasFence READ has_fence NOTIFY changed)
    Q_PROPERTY(int fenceId READ fence_id NOTIFY changed)
    Q_PROPERTY(double fenceMargin READ fence_margin NOTIFY changed)
    Q_PROPERTY(QVariantList fencePath READ fence_path NOTIFY changed)
    Q_PROPERTY(QVariantList fenceInsetDashes READ fence_inset_dashes NOTIFY changed)
    Q_PROPERTY(bool isConsistent READ is_consistent NOTIFY changed)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(QString summaryText READ summary_text NOTIFY changed)
    Q_PROPERTY(QString mapWarningText READ map_warning_text NOTIFY changed)
    Q_PROPERTY(bool isClearConfirmPending READ is_clear_confirm_pending NOTIFY changed)

public:
    /// 清除圍籬的確認時限（毫秒）
    static constexpr int CLEAR_CONFIRM_TIMEOUT_MS = 10000;
    /// margin 虛線的段數（約略值，依周長平均分配）
    static constexpr int INSET_DASH_COUNT = 60;

    /**
     * @brief 建立控制器
     * @param clear_confirm_timeout_ms 清除確認時限（毫秒），測試可縮短
     * @param parent 父物件
     */
    explicit GeofenceController(int clear_confirm_timeout_ms = CLEAR_CONFIRM_TIMEOUT_MS, QObject* parent = nullptr);

    bool is_editing() const { return is_editing_; }
    QVariantList draft_points() const;
    QVariantList draft_path() const;
    int draft_count() const { return static_cast<int>(draft_.size()); }
    double margin() const { return margin_; }
    static double min_margin() { return geofence_limits::MIN_MARGIN_M; }
    static double max_margin() { return geofence_limits::MAX_MARGIN_M; }
    static int max_vertices() { return geofence_limits::MAX_VERTICES; }
    QString draft_error() const;
    bool is_draft_self_intersecting() const;
    bool can_apply() const { return is_editing_ && draft_error().isEmpty(); }
    bool has_fence() const { return overview_.fence_id != 0 && !overview_.vertices.empty(); }
    int fence_id() const { return static_cast<int>(overview_.fence_id); }
    double fence_margin() const { return overview_.margin_m; }
    QVariantList fence_path() const;
    /** @brief margin 內縮線的虛線段（每段為 {path: [...]}） @return 虛線段 */
    QVariantList fence_inset_dashes() const;
    bool is_consistent() const { return overview_.is_consistent; }
    QVariantList rows() const;
    QString summary_text() const;
    QString map_warning_text() const;
    bool is_clear_confirm_pending() const { return clear_confirm_timer_.isActive(); }

    /**
     * @brief 修改草稿的 margin
     * @param margin_m margin（公尺），超出範圍時夾住
     */
    void setMargin(double margin_m);

    /**
     * @brief 提示訊息的去處；成功訊息只寫日誌，失敗與警告另外彈出
     * @param toasts 提示模型（生命週期需長於本物件），可為 nullptr
     */
    void set_toast_model(ToastModel* toasts) { toasts_ = toasts; }

    /**
     * @brief 地圖顯示原點
     * @param origin 顯示原點
     */
    void set_display_origin(const ui::DisplayOrigin& origin);

    /** @brief 開始編輯：草稿從目前生效的圍籬複製（沒有圍籬時為空） */
    Q_INVOKABLE void startEditing();

    /** @brief 取消編輯並捨棄草稿 */
    Q_INVOKABLE void cancelEditing();

    /**
     * @brief 在最後一個頂點之後新增頂點
     * @param latitude 地圖顯示緯度
     * @param longitude 地圖顯示經度
     */
    Q_INVOKABLE void addVertex(double latitude, double longitude);

    /**
     * @brief 移動頂點（地圖拖曳）
     * @param index 0 起算
     * @param latitude 地圖顯示緯度
     * @param longitude 地圖顯示經度
     */
    Q_INVOKABLE void moveVertex(int index, double latitude, double longitude);

    /** @brief 刪除頂點 @param index 0 起算 */
    Q_INVOKABLE void removeVertex(int index);

    /** @brief 清空草稿的所有頂點 */
    Q_INVOKABLE void clearDraft();

    /** @brief 套用草稿：條件符合時送給 GeofenceModule */
    Q_INVOKABLE void apply();

    /** @brief 要求清除圍籬：進入確認狀態 */
    Q_INVOKABLE void requestClear();

    /** @brief 確認清除圍籬 */
    Q_INVOKABLE void confirmClear();

    /** @brief 取消清除圍籬 */
    Q_INVOKABLE void cancelClear();

public slots:
    /** @brief 機隊圍籬總覽 @param overview GeofenceModule 回報 */
    void on_overview_changed(const gcs::GeofenceOverview& overview);

    /** @brief 套用或清除的結果 @param ack Guard 拒絕或 CommandModule 發布結果 */
    void on_request_acked(const gcs::CommandAck& ack);

signals:
    void changed();
    /** @brief 套用圍籬；只連到 GeofenceModule::request_set */
    void set_requested(const gcs::GeofenceData& fence);
    /** @brief 清除圍籬；只連到 GeofenceModule::request_clear */
    void clear_requested();

private:
    /**
     * @brief 回報位置轉為 QML 的 {latitude, longitude}
     * @param vertex 回報座標
     * @return 地圖顯示位置
     */
    QVariantMap display_of(const GeofenceVertexData& vertex) const;

    /**
     * @brief 地圖顯示位置換回回報座標
     * @param latitude 地圖顯示緯度
     * @param longitude 地圖顯示經度
     * @param vertex 換算結果
     * @return 位置是否有效
     */
    bool reported_of(double latitude, double longitude, GeofenceVertexData* vertex) const;

    /**
     * @brief 比對前後兩次總覽，回報被拒、未回報、在圍籬外與全部生效
     * @param previous 前一次總覽
     */
    void announce_changes(const GeofenceOverview& previous);

    /**
     * @brief 回報提示
     * @param kind 種類
     * @param title 標題
     * @param message 內容
     */
    void notify(ToastModel::Kind kind, const QString& title, const QString& message);

    ToastModel* toasts_ = nullptr;
    ui::DisplayOrigin display_origin_;
    bool is_editing_ = false;
    std::vector<GeofenceVertexData> draft_;
    double margin_ = geofence_limits::DEFAULT_MARGIN_M;
    GeofenceOverview overview_;
    /// 已宣告全部生效的版本，避免重複寫日誌
    uint32_t announced_fence_id_ = 0;
    QTimer clear_confirm_timer_;
};

}  // namespace gcs
