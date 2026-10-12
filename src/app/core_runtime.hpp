/**
 * @file core_runtime.hpp
 * @brief 核心執行環境 — 建立核心執行緒、CycloneAgentLink 與全部模組並完成組裝
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 桌面程式與無介面工具共用。模組與 link 位於核心執行緒；呼叫端（主執行緒）
 * 與模組之間的連線由 Qt 自動以 queued connection 傳遞（ADR-0015）。
 */
#pragma once

#include "link/dds_config.hpp"

#include <QObject>
#include <QString>

#include <memory>

class QThread;

namespace gcs
{

class AgentLifecycleModule;
class CommandModule;
class FleetModule;
class GuardModule;
class ModuleManager;
class TelemetryModule;

namespace link
{
class CycloneAgentLink;
}

/**
 * @class CoreRuntime
 * @brief 核心執行環境
 */
class CoreRuntime : public QObject
{
    Q_OBJECT

public:
    explicit CoreRuntime(QObject* parent = nullptr);
    ~CoreRuntime() override;

    CoreRuntime(const CoreRuntime&) = delete;
    CoreRuntime& operator=(const CoreRuntime&) = delete;

    /**
     * @brief 建立核心並啟動 DDS
     * @param config 網路設定
     * @param error 失敗原因（可為 nullptr）
     * @return 是否成功；失敗時已釋放所有資源
     */
    bool start(const link::DdsNetworkConfig& config, QString* error);

    /**
     * @brief 以新網路設定重建 DDS（Wi-Fi 位址改變時使用），模組與機隊狀態保留
     * @param config 新網路設定
     * @param error 失敗原因（可為 nullptr）
     * @return 是否成功
     */
    bool restart_network(const link::DdsNetworkConfig& config, QString* error);

    /** @brief 關閉模組、停止 DDS 與核心執行緒；不對 Agent 發送任何命令 */
    void stop();

    /** @brief 是否運作中 @return 是否運作中 */
    bool is_running() const;

    FleetModule* fleet() const { return fleet_; }
    TelemetryModule* telemetry() const { return telemetry_; }
    GuardModule* guard() const { return guard_; }
    CommandModule* command() const { return command_; }
    AgentLifecycleModule* lifecycle() const { return lifecycle_; }
    link::CycloneAgentLink* agent_link() const { return link_.get(); }

private:
    /**
     * @brief 在核心執行緒同步執行函式
     * @param function 要執行的函式
     */
    template <typename Function>
    void run_in_core(Function function);

    std::unique_ptr<QThread> core_thread_;
    /// link 必須比模組晚釋放（模組關閉時仍會呼叫 link）；宣告順序決定釋放順序
    std::unique_ptr<link::CycloneAgentLink> link_;
    std::unique_ptr<ModuleManager> manager_;
    FleetModule* fleet_ = nullptr;
    TelemetryModule* telemetry_ = nullptr;
    GuardModule* guard_ = nullptr;
    CommandModule* command_ = nullptr;
    AgentLifecycleModule* lifecycle_ = nullptr;
};

}  // namespace gcs
