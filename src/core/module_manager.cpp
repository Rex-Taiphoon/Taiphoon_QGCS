/**
 * @file module_manager.cpp
 * @brief ModuleManager 實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "core/module_manager.hpp"

#include <QLoggingCategory>
#include <QThread>

namespace
{
Q_LOGGING_CATEGORY(lc_modules, "gcs.modules")
}  // namespace

namespace gcs
{

bool ModuleManager::init_all()
{
    for (const auto& module : modules_)
    {
        qCInfo(lc_modules) << "初始化模組：" << module->module_name();
        if (!module->init())
        {
            qCCritical(lc_modules) << "模組初始化失敗：" << module->module_name();
            return false;
        }
    }
    qCInfo(lc_modules) << "全部模組初始化完成";
    return true;
}

void ModuleManager::shutdown_all()
{
    // 後初始化的模組可能依賴先初始化者，因此反向關閉。
    for (auto it = modules_.rbegin(); it != modules_.rend(); ++it)
    {
        qCInfo(lc_modules) << "關閉模組：" << (*it)->module_name();
        (*it)->shutdown();
    }
}

bool ModuleManager::health_check_all() const
{
    bool is_all_healthy = true;
    for (const auto& module : modules_)
    {
        if (!module->health_check())
        {
            qCWarning(lc_modules) << "模組異常：" << module->module_name();
            is_all_healthy = false;
        }
    }
    return is_all_healthy;
}

bool ModuleManager::move_to_thread(QThread* thread)
{
    if (thread == nullptr)
    {
        qCCritical(lc_modules) << "核心執行緒為空，無法移動模組";
        return false;
    }
    for (const auto& module : modules_)
    {
        module->moveToThread(thread);
    }
    return true;
}

}  // namespace gcs
