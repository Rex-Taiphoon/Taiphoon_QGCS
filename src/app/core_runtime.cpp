/**
 * @file core_runtime.cpp
 * @brief 核心執行環境實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "app/core_runtime.hpp"

#include "app/gcs_wiring.hpp"
#include "core/agent_types.hpp"
#include "core/module_manager.hpp"
#include "link/cyclone_agent_link.hpp"
#include "modules/command/command_module.hpp"
#include "modules/fleet/fleet_module.hpp"
#include "modules/guard/guard_module.hpp"
#include "modules/lifecycle/agent_lifecycle_module.hpp"
#include "modules/telemetry/telemetry_module.hpp"

#include <QLoggingCategory>
#include <QThread>

namespace
{
Q_LOGGING_CATEGORY(lc_runtime, "gcs.runtime")
}  // namespace

namespace gcs
{

CoreRuntime::CoreRuntime(QObject* parent) : QObject(parent)
{
    register_core_metatypes();
}

CoreRuntime::~CoreRuntime()
{
    stop();
}

template <typename Function>
void CoreRuntime::run_in_core(Function function)
{
    // 以 link 為 context：函式在其所屬的核心執行緒執行，主執行緒等待完成。
    QMetaObject::invokeMethod(link_.get(), function, Qt::BlockingQueuedConnection);
}

bool CoreRuntime::start(const link::DdsNetworkConfig& config, QString* error)
{
    if (is_running())
    {
        return restart_network(config, error);
    }

    link_ = std::make_unique<link::CycloneAgentLink>();
    manager_ = std::make_unique<ModuleManager>();
    // 註冊順序即初始化順序；關閉時反向
    fleet_ = manager_->register_module(std::make_unique<FleetModule>(link_.get()));
    telemetry_ = manager_->register_module(std::make_unique<TelemetryModule>(link_.get()));
    guard_ = manager_->register_module(std::make_unique<GuardModule>());
    command_ = manager_->register_module(std::make_unique<CommandModule>(link_.get()));
    lifecycle_ = manager_->register_module(std::make_unique<AgentLifecycleModule>(link_.get()));
    if (!wire_core_modules({fleet_, telemetry_, guard_, command_, lifecycle_}))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("核心模組組裝失敗");
        }
        stop();
        return false;
    }

    core_thread_ = std::make_unique<QThread>();
    core_thread_->setObjectName(QStringLiteral("gcs-core"));
    link_->moveToThread(core_thread_.get());
    manager_->move_to_thread(core_thread_.get());
    core_thread_->start();

    bool is_started = false;
    QString start_error;
    run_in_core(
        [this, &config, &is_started, &start_error]()
        {
            is_started = link_->start(config, &start_error);
            if (is_started && !manager_->init_all())
            {
                start_error = QStringLiteral("核心模組初始化失敗");
                is_started = false;
            }
        });
    if (!is_started)
    {
        qCCritical(lc_runtime) << "核心啟動失敗：" << start_error;
        if (error != nullptr)
        {
            *error = start_error;
        }
        stop();
        return false;
    }
    qCInfo(lc_runtime) << "核心已啟動";
    return true;
}

bool CoreRuntime::restart_network(const link::DdsNetworkConfig& config, QString* error)
{
    if (!is_running())
    {
        return start(config, error);
    }
    bool is_started = false;
    QString start_error;
    run_in_core([this, &config, &is_started, &start_error]() { is_started = link_->start(config, &start_error); });
    if (!is_started && error != nullptr)
    {
        *error = start_error;
    }
    qCInfo(lc_runtime) << "DDS 以新網路設定重建：" << (is_started ? "成功" : "失敗");
    return is_started;
}

void CoreRuntime::stop()
{
    if (core_thread_ && core_thread_->isRunning())
    {
        QThread* owner_thread = thread();
        run_in_core(
            [this, owner_thread]()
            {
                manager_->shutdown_all();
                link_->stop();
                // QObject 只能由所屬執行緒移動；移回後才能在主執行緒安全釋放（含子物件 QTimer）。
                manager_->move_to_thread(owner_thread);
                link_->moveToThread(owner_thread);
            });
        core_thread_->quit();
        core_thread_->wait();
        qCInfo(lc_runtime) << "核心已停止";
    }
    // 物件已回到主執行緒；模組先於 link 釋放。
    fleet_ = nullptr;
    telemetry_ = nullptr;
    guard_ = nullptr;
    command_ = nullptr;
    lifecycle_ = nullptr;
    manager_.reset();
    link_.reset();
    core_thread_.reset();
}

bool CoreRuntime::is_running() const
{
    return core_thread_ != nullptr && core_thread_->isRunning();
}

}  // namespace gcs
