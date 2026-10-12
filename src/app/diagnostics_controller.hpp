/**
 * @file diagnostics_controller.hpp
 * @brief 唯讀除錯工具（取代網頁版以 ros2 CLI 執行的除錯終端機，F-11）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 只能檢視：DDS topic 清單、Agent 最新資料、模組事件紀錄。
 * 刻意不提供發布 topic 或呼叫 service 的指令，避免繞過 GuardModule（AGENTS.md）。
 */
#pragma once

#include "core/agent_types.hpp"

#include <QHash>
#include <QObject>
#include <QStringList>

#include <functional>
#include <vector>

namespace gcs
{

/**
 * @class DiagnosticsController
 * @brief 終端機式唯讀除錯工具
 */
class DiagnosticsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString output READ output NOTIFY outputChanged)

public:
    /// 探索到的 topic（topic 名稱與型別）
    struct TopicEntry
    {
        QString topic;
        QString type;
    };
    /// 取得 topic 清單的函式；由組裝端提供，避免本類別依賴 DDS
    using TopicSource = std::function<std::vector<TopicEntry>()>;

    /// 事件紀錄保留筆數
    static constexpr int MAX_EVENTS = 200;
    /// 輸出保留行數
    static constexpr int MAX_OUTPUT_LINES = 2000;

    explicit DiagnosticsController(QObject* parent = nullptr);

    /** @brief 設定 topic 來源 @param source 取得 topic 清單的函式 */
    void set_topic_source(TopicSource source);

    QString output() const { return output_lines_.join(QLatin1Char('\n')); }

    /**
     * @brief 執行一行指令並把結果附加到輸出
     * @param line 指令，例如 "topic list"
     */
    Q_INVOKABLE void execute(const QString& line);

    /** @brief 清除輸出 */
    Q_INVOKABLE void clear();

public slots:
    void on_state_updated(const QString& agent_ns, const gcs::AgentStateData& state);
    void on_attitude_updated(const QString& agent_ns, const gcs::AgentAttitudeData& attitude);
    void on_trajectory_updated(const QString& agent_ns, const gcs::AgentTrajectoryData& trajectory);
    void on_agent_online(const QString& agent_ns);
    void on_agent_offline(const QString& agent_ns);
    void on_command_acked(const gcs::CommandAck& ack);

signals:
    void outputChanged();

private:
    void append(const QString& text);
    void record_event(const QString& text);
    QString help_text() const;
    QString topic_list() const;
    QString echo(const QString& agent_ns, const QString& channel) const;

    TopicSource topic_source_;
    QStringList output_lines_;
    QStringList events_;
    QHash<QString, AgentStateData> states_;
    QHash<QString, AgentAttitudeData> attitudes_;
    QHash<QString, AgentTrajectoryData> trajectories_;
};

}  // namespace gcs
