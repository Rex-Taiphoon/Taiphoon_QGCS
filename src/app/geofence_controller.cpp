/**
 * @file geofence_controller.cpp
 * @brief 地理圍籬介面狀態實作
 * @author Taiphoon GCS Team
 * @date 2026-11-06
 */
#include "app/geofence_controller.hpp"

#include "core/geofence_geometry.hpp"

#include <QLoggingCategory>

#include <algorithm>
#include <cmath>

namespace
{
Q_LOGGING_CATEGORY(lc_geofence_ui, "gcs.geofence.ui")
}  // namespace

namespace gcs
{

namespace
{

/// 虛線段長度的範圍（公尺）
constexpr double MIN_DASH_M = 0.3;
constexpr double MAX_DASH_M = 25.0;
/// margin 顯示到公分
constexpr double MARGIN_SCALE = 100.0;

/**
 * @brief 該列是否已套用指定設定
 * @param row 一列
 * @param overview 總覽
 * @return 是否已套用
 */
bool is_row_applied(const GeofenceAgentRow& row, const GeofenceOverview& overview)
{
    if (overview.requested_action == GeofenceAction::CLEAR)
    {
        return row.phase == GeofenceAgentPhase::NONE;
    }
    return (row.phase == GeofenceAgentPhase::ACTIVE || row.phase == GeofenceAgentPhase::PENDING)
           && row.fence_id == overview.requested_fence_id;
}

}  // namespace

GeofenceController::GeofenceController(int clear_confirm_timeout_ms, QObject* parent)
    : QObject(parent), clear_confirm_timer_(this)
{
    clear_confirm_timer_.setSingleShot(true);
    clear_confirm_timer_.setInterval(clear_confirm_timeout_ms);
    connect(&clear_confirm_timer_, &QTimer::timeout, this, [this]() {
        qCInfo(lc_geofence_ui) << "清除圍籬未確認，已取消";
        emit changed();
    });
}

QVariantMap GeofenceController::display_of(const GeofenceVertexData& vertex) const
{
    const auto position = ui::to_display_position({vertex.latitude_deg, vertex.longitude_deg, 0.0}, display_origin_);
    return QVariantMap{{QStringLiteral("latitude"), position[0]}, {QStringLiteral("longitude"), position[1]}};
}

bool GeofenceController::reported_of(double latitude, double longitude, GeofenceVertexData* vertex) const
{
    const auto reported = ui::to_reported_position({latitude, longitude, 0.0}, display_origin_);
    if (!ui::has_valid_global_position(reported))
    {
        return false;
    }
    *vertex = GeofenceVertexData{reported[0], reported[1]};
    return true;
}

QVariantList GeofenceController::draft_points() const
{
    QVariantList list;
    for (std::size_t i = 0; i < draft_.size(); ++i)
    {
        QVariantMap point = display_of(draft_.at(i));
        point.insert(QStringLiteral("index"), static_cast<int>(i));
        list.append(point);
    }
    return list;
}

QVariantList GeofenceController::draft_path() const
{
    QVariantList list;
    for (const GeofenceVertexData& vertex : draft_)
    {
        list.append(display_of(vertex));
    }
    return list;
}

QString GeofenceController::draft_error() const
{
    return geofence_geometry::validate(draft_, static_cast<float>(margin_));
}

bool GeofenceController::is_draft_self_intersecting() const
{
    return geofence_geometry::is_self_intersecting(draft_);
}

QVariantList GeofenceController::fence_path() const
{
    QVariantList list;
    if (!has_fence())
    {
        return list;
    }
    for (const GeofenceVertexData& vertex : overview_.vertices)
    {
        list.append(display_of(vertex));
    }
    return list;
}

QVariantList GeofenceController::fence_inset_dashes() const
{
    QVariantList dashes;
    if (!has_fence())
    {
        return dashes;
    }
    const std::vector<GeofenceVertexData> inset = geofence_geometry::inset(overview_.vertices, overview_.margin_m);
    if (inset.size() < 3)
    {
        return dashes;
    }
    // 以局部平面沿封閉邊界走，交替畫出等長的實線段與空白
    const GeofenceVertexData origin = inset.front();
    std::vector<geofence_geometry::LocalPoint> points;
    double perimeter = 0.0;
    for (std::size_t i = 0; i < inset.size(); ++i)
    {
        points.push_back(geofence_geometry::to_local(inset.at(i), origin));
    }
    points.push_back(points.front());
    for (std::size_t i = 1; i < points.size(); ++i)
    {
        perimeter += std::hypot(points[i].east_m - points[i - 1].east_m, points[i].north_m - points[i - 1].north_m);
    }
    const double dash = std::clamp(perimeter / (2.0 * INSET_DASH_COUNT), MIN_DASH_M, MAX_DASH_M);
    bool is_drawing = true;
    double remaining = dash;
    QVariantList current{display_of(inset.front())};
    for (std::size_t i = 1; i < points.size(); ++i)
    {
        geofence_geometry::LocalPoint from = points[i - 1];
        const geofence_geometry::LocalPoint& to = points[i];
        double length = std::hypot(to.east_m - from.east_m, to.north_m - from.north_m);
        while (length > 0.0)
        {
            const double step = std::min(remaining, length);
            const double ratio = step / length;
            from = {from.east_m + (to.east_m - from.east_m) * ratio, from.north_m + (to.north_m - from.north_m) * ratio};
            length -= step;
            remaining -= step;
            if (is_drawing)
            {
                current.append(display_of(geofence_geometry::to_global(from, origin)));
            }
            if (remaining <= 0.0)
            {
                if (is_drawing && current.size() >= 2)
                {
                    dashes.append(QVariantMap{{QStringLiteral("path"), current}});
                }
                is_drawing = !is_drawing;
                remaining = dash;
                current = is_drawing ? QVariantList{display_of(geofence_geometry::to_global(from, origin))}
                                     : QVariantList{};
            }
        }
    }
    if (is_drawing && current.size() >= 2)
    {
        dashes.append(QVariantMap{{QStringLiteral("path"), current}});
    }
    return dashes;
}

QVariantList GeofenceController::rows() const
{
    QVariantList list;
    for (const GeofenceAgentRow& row : overview_.rows)
    {
        QString detail;
        switch (row.phase)
        {
        case GeofenceAgentPhase::REJECTED:
            detail = ui::geofence_result_text(row.last_result, row.detail) + QStringLiteral("；保留原本的圍籬");
            break;
        case GeofenceAgentPhase::NO_REPORT:
            detail = QStringLiteral("可能離線或 Agent 版本過舊");
            break;
        case GeofenceAgentPhase::PENDING:
            detail = QStringLiteral("Home 建立後自動生效");
            break;
        case GeofenceAgentPhase::MISMATCH:
            detail = overview_.fence_id == 0 ? QStringLiteral("多數機沒有圍籬")
                                             : QStringLiteral("多數機為 #%1").arg(overview_.fence_id);
            break;
        default:
            break;
        }
        if (!row.is_position_inside)
        {
            detail = detail.isEmpty() ? QStringLiteral("目前在圍籬外")
                                      : QStringLiteral("目前在圍籬外；") + detail;
        }
        list.append(QVariantMap{
            {QStringLiteral("agent"), row.agent},
            {QStringLiteral("shortName"), ui::agent_short_name(row.agent)},
            {QStringLiteral("phaseText"), ui::geofence_phase_label(row.phase)},
            {QStringLiteral("level"),
             static_cast<int>(row.is_position_inside ? ui::geofence_phase_level(row.phase)
                                                     : std::max(ui::geofence_phase_level(row.phase),
                                                                ui::StatusLevel::CAUTION))},
            {QStringLiteral("fenceText"),
             row.fence_id == 0 ? QStringLiteral("—") : QStringLiteral("#%1").arg(row.fence_id)},
            {QStringLiteral("detail"), detail},
            {QStringLiteral("isOutside"), !row.is_position_inside},
        });
    }
    return list;
}

QString GeofenceController::summary_text() const
{
    if (overview_.rows.empty())
    {
        return has_fence() ? QStringLiteral("#%1 · margin %2 m · 沒有線上 Agent")
                                 .arg(overview_.fence_id)
                                 .arg(overview_.margin_m, 0, 'f', 1)
                           : QStringLiteral("沒有線上 Agent");
    }
    if (!has_fence())
    {
        return QStringLiteral("未設定圍籬");
    }
    const auto active = std::count_if(overview_.rows.cbegin(), overview_.rows.cend(), [this](const GeofenceAgentRow& row) {
        return row.phase == GeofenceAgentPhase::ACTIVE && row.fence_id == overview_.fence_id;
    });
    return QStringLiteral("#%1 · margin %2 m · 生效 %3/%4 機")
        .arg(overview_.fence_id)
        .arg(overview_.margin_m, 0, 'f', 1)
        .arg(active)
        .arg(overview_.rows.size());
}

QString GeofenceController::map_warning_text() const
{
    QStringList parts;
    if (!overview_.is_consistent && !overview_.rows.empty())
    {
        parts.append(QStringLiteral("各機圍籬不一致，顯示多數機的版本"));
    }
    QStringList outside;
    for (const GeofenceAgentRow& row : overview_.rows)
    {
        if (!row.is_position_inside)
        {
            outside.append(ui::agent_short_name(row.agent));
        }
    }
    if (!outside.isEmpty())
    {
        parts.append(QStringLiteral("%1 在圍籬外").arg(outside.join(QStringLiteral("、"))));
    }
    return parts.join(QStringLiteral("；"));
}

void GeofenceController::setMargin(double margin_m)
{
    if (!std::isfinite(margin_m))
    {
        return;
    }
    const double clamped = std::clamp(margin_m, min_margin(), max_margin());
    if (clamped == margin_)
    {
        return;
    }
    margin_ = clamped;
    emit changed();
}

void GeofenceController::set_display_origin(const ui::DisplayOrigin& origin)
{
    display_origin_ = origin;
    emit changed();
}

void GeofenceController::startEditing()
{
    if (is_editing_)
    {
        return;
    }
    is_editing_ = true;
    draft_ = has_fence() ? overview_.vertices : std::vector<GeofenceVertexData>{};
    // float32 的 margin 轉回十進位時取到公分，輸入框才不會出現 0.30000001
    margin_ = has_fence() ? std::round(static_cast<double>(overview_.margin_m) * MARGIN_SCALE) / MARGIN_SCALE
                          : geofence_limits::DEFAULT_MARGIN_M;
    clear_confirm_timer_.stop();
    emit changed();
}

void GeofenceController::cancelEditing()
{
    if (!is_editing_)
    {
        return;
    }
    is_editing_ = false;
    draft_.clear();
    emit changed();
}

void GeofenceController::addVertex(double latitude, double longitude)
{
    if (!is_editing_)
    {
        return;
    }
    if (draft_.size() >= static_cast<std::size_t>(geofence_limits::MAX_VERTICES))
    {
        notify(ToastModel::Kind::WARNING, QStringLiteral("無法新增頂點"),
               QStringLiteral("圍籬最多 %1 個頂點").arg(geofence_limits::MAX_VERTICES));
        return;
    }
    GeofenceVertexData vertex;
    if (!reported_of(latitude, longitude, &vertex))
    {
        return;
    }
    draft_.push_back(vertex);
    emit changed();
}

void GeofenceController::moveVertex(int index, double latitude, double longitude)
{
    if (!is_editing_ || index < 0 || index >= draft_count())
    {
        return;
    }
    GeofenceVertexData vertex;
    if (!reported_of(latitude, longitude, &vertex))
    {
        return;
    }
    draft_[static_cast<std::size_t>(index)] = vertex;
    emit changed();
}

void GeofenceController::removeVertex(int index)
{
    if (!is_editing_ || index < 0 || index >= draft_count())
    {
        return;
    }
    draft_.erase(draft_.begin() + index);
    emit changed();
}

void GeofenceController::clearDraft()
{
    if (!is_editing_ || draft_.empty())
    {
        return;
    }
    draft_.clear();
    emit changed();
}

void GeofenceController::apply()
{
    if (!is_editing_)
    {
        return;
    }
    const QString error = draft_error();
    if (!error.isEmpty())
    {
        notify(ToastModel::Kind::WARNING, QStringLiteral("無法套用圍籬"), error);
        return;
    }
    GeofenceData fence;
    fence.action = GeofenceAction::SET;
    fence.margin_m = static_cast<float>(margin_);
    fence.vertices = draft_;
    emit set_requested(fence);
}

void GeofenceController::requestClear()
{
    if (!has_fence() && overview_.rows.empty())
    {
        return;
    }
    clear_confirm_timer_.start();
    emit changed();
}

void GeofenceController::confirmClear()
{
    if (!clear_confirm_timer_.isActive())
    {
        return;
    }
    clear_confirm_timer_.stop();
    emit clear_requested();
    emit changed();
}

void GeofenceController::cancelClear()
{
    if (!clear_confirm_timer_.isActive())
    {
        return;
    }
    clear_confirm_timer_.stop();
    emit changed();
}

void GeofenceController::on_overview_changed(const GeofenceOverview& overview)
{
    const GeofenceOverview previous = overview_;
    overview_ = overview;
    announce_changes(previous);
    emit changed();
}

void GeofenceController::on_request_acked(const CommandAck& ack)
{
    if (ack.command != GEOFENCE_ACK_NAME)
    {
        return;
    }
    if (!ack.is_ok)
    {
        notify(ToastModel::Kind::ERROR, QStringLiteral("圍籬未送出"), ack.message);
        return;
    }
    notify(ToastModel::Kind::SUCCESS, QStringLiteral("圍籬已送出"), QStringLiteral("等待各機回報"));
    if (is_editing_)
    {
        is_editing_ = false;
        draft_.clear();
        emit changed();
    }
}

void GeofenceController::announce_changes(const GeofenceOverview& previous)
{
    for (const GeofenceAgentRow& row : overview_.rows)
    {
        const auto old = std::find_if(previous.rows.cbegin(), previous.rows.cend(),
                                      [&row](const GeofenceAgentRow& item) { return item.agent == row.agent; });
        const bool is_new = old == previous.rows.cend();
        const QString name = ui::agent_short_name(row.agent);
        if ((is_new || old->is_position_inside) && !row.is_position_inside)
        {
            notify(ToastModel::Kind::WARNING, QStringLiteral("%1 在圍籬外").arg(name),
                   QStringLiteral("目前位置在圍籬（含 margin）外；Agent 不會自動飛回，之後只接受往內的移動"));
        }
        if (row.phase == GeofenceAgentPhase::REJECTED && (is_new || old->phase != GeofenceAgentPhase::REJECTED))
        {
            notify(ToastModel::Kind::ERROR, QStringLiteral("%1 拒絕圍籬").arg(name),
                   ui::geofence_result_text(row.last_result, row.detail) + QStringLiteral("；保留原本的圍籬"));
        }
        if (row.phase == GeofenceAgentPhase::NO_REPORT && !is_new && old->phase == GeofenceAgentPhase::WAITING)
        {
            notify(ToastModel::Kind::WARNING, QStringLiteral("%1 未回報圍籬").arg(name),
                   QStringLiteral("可能離線或 Agent 版本過舊"));
        }
    }
    const bool is_all_applied =
        overview_.requested_fence_id != 0 && !overview_.rows.empty()
        && std::all_of(overview_.rows.cbegin(), overview_.rows.cend(),
                       [this](const GeofenceAgentRow& row) { return is_row_applied(row, overview_); });
    if (is_all_applied && announced_fence_id_ != overview_.requested_fence_id)
    {
        announced_fence_id_ = overview_.requested_fence_id;
        notify(ToastModel::Kind::SUCCESS,
               overview_.requested_action == GeofenceAction::CLEAR ? QStringLiteral("圍籬已清除")
                                                                     : QStringLiteral("圍籬已生效"),
               QStringLiteral("#%1，%2 機").arg(overview_.requested_fence_id).arg(overview_.rows.size()));
    }
}

void GeofenceController::notify(ToastModel::Kind kind, const QString& title, const QString& message)
{
    const QString text = title + QStringLiteral("：") + message;
    if (kind == ToastModel::Kind::SUCCESS)
    {
        qCInfo(lc_geofence_ui).noquote() << text;
        return;
    }
    qCWarning(lc_geofence_ui).noquote() << text;
    if (toasts_ != nullptr)
    {
        toasts_->add(kind, title, message);
    }
}

}  // namespace gcs
