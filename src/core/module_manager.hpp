/**
 * @file module_manager.hpp
 * @brief 模組管理器 — 管理所有 GcsModule 的生命週期
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#pragma once

#include "core/gcs_module.hpp"

#include <memory>
#include <vector>

class QThread;

namespace gcs
{

/**
 * @class ModuleManager
 * @brief 依註冊順序初始化模組、反向順序關閉
 */
class ModuleManager
{
public:
    ModuleManager() = default;
    ~ModuleManager() = default;

    ModuleManager(const ModuleManager&) = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;

    /**
     * @brief 註冊模組（在 init_all 之前呼叫）
     * @param module 模組實例，所有權轉移給管理器
     * @return 模組指標，供組裝根建立連線；module 為空時回傳 nullptr
     */
    template <typename Module>
    Module* register_module(std::unique_ptr<Module> module)
    {
        Module* raw = module.get();
        if (raw != nullptr)
        {
            modules_.push_back(std::move(module));
        }
        return raw;
    }

    /**
     * @brief 依序初始化所有模組
     * @return 是否全部成功；任一失敗即停止並回傳 false
     */
    bool init_all();

    /** @brief 反向關閉所有模組 */
    void shutdown_all();

    /**
     * @brief 檢查所有模組健康狀態
     * @return 是否全部健康
     */
    bool health_check_all() const;

    /**
     * @brief 將所有模組移到指定執行緒（核心執行緒）
     * @param thread 目標執行緒
     * @return 是否成功；thread 為空時回傳 false
     */
    bool move_to_thread(QThread* thread);

private:
    std::vector<std::unique_ptr<GcsModule>> modules_;
};

}  // namespace gcs
