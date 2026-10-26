/**
 * @file fleet_model.hpp
 * @brief 機群卡片與地圖標記的資料模型（取代網頁版 fleetStore + telemetryStore 的顯示部分）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#pragma once

#include "app/ui_format.hpp"
#include "core/agent_types.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QStringList>

namespace gcs
{

/// 單一 Agent 在介面上的最新資料
struct AgentView
{
    AgentStateData state;
    bool has_state = false;
    AgentAttitudeData attitude;
    bool has_attitude = false;
    AgentTrajectoryData trajectory;
    bool has_trajectory = false;
};

/**
 * @class FleetModel
 * @brief 線上 Agent 清單；每列為一台 Agent
 */
class FleetModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    /// QML 使用的資料角色
    enum Role
    {
        AgentRole = Qt::UserRole + 1,
        ShortNameRole,
        IsSelectedRole,
        IsWaitingForFlightControllerRole,
        ModeLabelRole,
        Px4ModeLabelRole,
        BatteryTextRole,
        VoltageTextRole,
        CurrentTextRole,
        AltitudeTextRole,
        HasPositionRole,
        LatitudeRole,
        LongitudeRole,
        HasHeadingRole,
        HeadingRole,
        HasAttitudeRole,
        RollRole,
        PitchRole
    };

    explicit FleetModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief 以最新資料更新模型；只在清單改變時重設，其餘以 dataChanged 通知
     * @param online 線上 Agent（已排序）
     * @param views Agent → 資料
     * @param selected_agent 目前選取
     * @param display_origin 地圖顯示原點，只套用在經緯度角色
     */
    void update(const QStringList& online, const QHash<QString, AgentView>& views, const QString& selected_agent,
                const ui::DisplayOrigin& display_origin);

signals:
    void countChanged();

private:
    QStringList agents_;
    QHash<QString, AgentView> views_;
    QString selected_agent_;
    ui::DisplayOrigin display_origin_;
};

}  // namespace gcs
