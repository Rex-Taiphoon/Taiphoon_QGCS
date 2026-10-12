/**
 * @file agent_lifecycle_module.hpp
 * @brief Agent 生命週期模組 — 單機 shutdown service 橋接
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 此操作不會先 RTL 或降落，只允許單一 Agent，且不要求授權碼（介面文件第 5 節）。
 */
#pragma once

#include "core/agent_types.hpp"
#include "core/gcs_module.hpp"

#include <QHash>

#include <chrono>

namespace gcs
{

class IAgentLink;

/**
 * @class AgentLifecycleModule
 * @brief 將單機關閉請求轉送至 /{agent_namespace}/shutdown
 */
class AgentLifecycleModule : public GcsModule
{
    Q_OBJECT

public:
    /// 回報結果使用的命令名稱
    static inline const QString SHUTDOWN_COMMAND = QStringLiteral("AGENT_SHUTDOWN");

    /// 預設回應逾時；AgentSupervisor 應在容器結束前回應
    static constexpr std::chrono::milliseconds DEFAULT_RESPONSE_TIMEOUT{5000};

    /**
     * @brief 建立生命週期模組
     * @param link Agent 通訊介面，生命週期需長於本模組
     * @param parent 父物件
     */
    explicit AgentLifecycleModule(IAgentLink* link, QObject* parent = nullptr);

    bool init() override;
    void shutdown() override;
    bool health_check() const override;
    QString module_name() const override { return QStringLiteral("AgentLifecycleModule"); }

    /**
     * @brief 設定回應逾時（測試用）
     * @param timeout 逾時時間
     */
    void set_response_timeout(std::chrono::milliseconds timeout);

public slots:
    /**
     * @brief 請求關閉單一 Agent
     * @param agent_ns Agent namespace（不可為 all）
     */
    void request_shutdown(const QString& agent_ns);

signals:
    /** @brief Agent 已接受關閉；組裝根連到 FleetModule 立即隔離 */
    void shutdown_accepted(const QString& agent_ns);
    /** @brief 關閉請求結果 */
    void command_acked(const gcs::CommandAck& ack);

private slots:
    void on_shutdown_replied(const QString& agent_ns, quint64 request_id, bool is_success, const QString& message);

private:
    /**
     * @brief 結束待處理請求並回報結果
     * @param agent_ns Agent namespace
     * @param request_id 請求識別碼；與待處理請求不同時忽略
     * @param is_ok 是否成功
     * @param message 結果訊息
     */
    void complete(const QString& agent_ns, quint64 request_id, bool is_ok, const QString& message);

    IAgentLink* link_ = nullptr;
    bool is_initialized_ = false;
    std::chrono::milliseconds response_timeout_ = DEFAULT_RESPONSE_TIMEOUT;
    quint64 next_request_id_ = 1;
    /// Agent → 等待回應中的請求識別碼；防止同一 Agent 重複關閉
    QHash<QString, quint64> pending_requests_;
};

}  // namespace gcs
