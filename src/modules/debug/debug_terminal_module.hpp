/**
 * @file debug_terminal_module.hpp
 * @brief 除錯終端機模組 — 在 GCS 容器內執行 ros2 CLI 並回傳輸出
 *
 * 前端除錯終端機送出的指令僅允許以 ros2 開頭；執行環境固定 source
 * ROS2 Humble 與 GCS 工作空間，確保指令可用。執行在背景執行緒進行，
 * 並以 timeout 限制執行時間，避免長期阻塞 WebSocket 事件迴圈。
 * @author Taiphoon GCS Team
 * @date 2026-08-17
 */
#pragma once

#include "core/event_bus.hpp"
#include "core/gcs_module.hpp"
#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>

/**
 * @brief 提供前端除錯終端機的 ROS2 指令執行能力
 */
class DebugTerminalModule : public GcsModule
{
public:
    DebugTerminalModule() = default;

    /** @brief 初始化 EventBus 訂閱 */
    bool init(EventBus& bus) override;

    /** @brief 停止接受新請求並等待執行中的指令結束 */
    void shutdown() override;

    /** @brief 檢查模組是否已初始化 */
    bool healthCheck() override;

    /** @brief 取得模組名稱 */
    std::string getName() const override { return "DebugTerminalModule"; }

private:
    /**
     * @brief 處理前端送出的 ros2_exec_request
     * @param data EventBus 承載的 WebSocket JSON 字串
     */
    void onRos2ExecRequest(const std::any& data);

    /**
     * @brief 在背景執行緒執行指令並發布 ros2_exec_response
     * @param rawJson 已驗證的 ros2_exec_request JSON 字串
     */
    void executeRequest(const std::string& rawJson);

    /**
     * @brief 將指令寫入暫存腳本並以 timeout 執行
     * @param command 已驗證的指令文字
     * @param output 合併輸出（含 stdout 與 stderr）
     * @return 執行結果（ok / error / timeout）
     */
    std::string runRos2Command(const std::string& command, std::string& output);

    EventBus* bus_ = nullptr;
    std::atomic<bool> running_{false};

    /// 執行中的請求數量；shutdown 時據此等待背景執行緒結束
    std::atomic<std::size_t> activeRequests_{0};
    std::mutex cvMutex_;
    std::condition_variable cv_;

    /// 指令執行逾時秒數（可由環境變數 GCS_ROS2_EXEC_TIMEOUT_S 覆寫）
    static constexpr int DEFAULT_EXEC_TIMEOUT_S = 10;
    /// 輸出上限（位元組），避免過量輸出癱瘓 WebSocket
    static constexpr std::size_t MAX_OUTPUT_BYTES = 64 * 1024;
    /// 等待執行中指令結束的總時間（秒），需大於逾時 + kill 寬限
    static constexpr int SHUTDOWN_WAIT_S = 20;
};
