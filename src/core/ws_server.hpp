/**
 * @file ws_server.hpp
 * @brief WebSocket 伺服器 — 內嵌 HTTP + WebSocket 雙協定
 */
#pragma once

#include "event_bus.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/http.hpp>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class WsServer {
public:
    /// 自訂路由處理器（path → response body）
    using RouteHandler = std::function<std::string(const std::string& path)>;

    WsServer();
    ~WsServer();

    bool start(uint16_t port, const std::string& staticDir, std::shared_ptr<EventBus> bus);
    void stop();
    void broadcast(const std::string& message);
    bool isRunning() const { return running_; }

    /// 註冊自訂路由（例如圖磚端點 /tiles/...）
    void registerRoute(const std::string& prefix, RouteHandler handler);

private:
    /**
     * @brief 單一 WebSocket client 的連線狀態與待寫佇列。
     *
     * 同一 stream 同時間只能有一個 async_write()，避免 Beast 的 soft_mutex 斷言。
     */
    struct WsClient {
        explicit WsClient(std::shared_ptr<websocket::stream<tcp::socket>> ws);

        std::shared_ptr<websocket::stream<tcp::socket>> ws_;
        std::deque<std::shared_ptr<std::string>> writeQueue_;
        bool is_writing_{false};
        bool is_removed_{false};
    };

    void doAccept();
    void handleSession(tcp::socket socket);
    void handleHttpRequest(tcp::socket& socket, http::request<http::string_body>& req);
    void readLoop(std::shared_ptr<WsClient> client, std::shared_ptr<beast::flat_buffer> buffer);

    /**
     * @brief 新增 WebSocket client 至連線集合。
     * @param ws 已完成的 WebSocket stream
     * @return 對應的 WsClient，供讀寫回呼持有
     */
    std::shared_ptr<WsClient> addClient(std::shared_ptr<websocket::stream<tcp::socket>> ws);

    /**
     * @brief 移除 WebSocket client，並使其後續寫入不再啟動。
     * @param client 要移除的 client
     */
    void removeClient(std::shared_ptr<WsClient> client);

    /**
     * @brief 將一筆訊息放入 client 的寫入佇列，必要時啟動寫入。
     * @param client 目標 client
     * @param payload 訊息內容，由非同步寫入期間持有
     */
    void enqueueWrite(std::shared_ptr<WsClient> client, std::shared_ptr<std::string> payload);

    /**
     * @brief 從 client 佇列取出一筆並啟動 async_write。
     * @param client 目標 client
     */
    void startWrite(std::shared_ptr<WsClient> client);

    net::io_context ioc_;
    std::unique_ptr<tcp::acceptor> acceptor_;
    std::unique_ptr<std::thread> thread_;
    std::string staticDir_;
    std::shared_ptr<EventBus> bus_;
    std::atomic<bool> running_{false};
    uint16_t port_{0};

    std::mutex clientsMutex_;
    std::set<std::shared_ptr<WsClient>> clients_;

    /// 自訂路由表
    std::mutex routesMutex_;
    std::vector<std::pair<std::string, RouteHandler>> routes_;
};
