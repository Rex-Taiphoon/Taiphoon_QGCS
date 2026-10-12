/**
 * @file gcs_module.hpp
 * @brief GcsModule 抽象基底類別 — 所有功能模組必須繼承此類別
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 模組之間禁止直接 include；跨模組通訊一律使用 signal／slot，
 * 連線集中在組裝根 src/app/gcs_wiring.cpp（ADR-0015）。
 */
#pragma once

#include <QObject>
#include <QString>

namespace gcs
{

/**
 * @class GcsModule
 * @brief 地面控制站功能模組的抽象基底類別
 *
 * 模組在核心執行緒執行；生命週期由 ModuleManager 依註冊順序初始化、反向關閉。
 */
class GcsModule : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~GcsModule() override = default;

    /**
     * @brief 模組初始化
     * @return 初始化是否成功
     */
    virtual bool init() = 0;

    /** @brief 模組關閉，釋放資源 */
    virtual void shutdown() = 0;

    /**
     * @brief 回報模組健康狀態
     * @return 模組是否正常運行
     */
    virtual bool health_check() const = 0;

    /**
     * @brief 取得模組名稱（用於日誌）
     * @return 模組名稱
     */
    virtual QString module_name() const = 0;
};

}  // namespace gcs
