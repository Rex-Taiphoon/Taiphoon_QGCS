/**
 * @file ws_server.cpp
 * @brief WebSocket 伺服器實作（C++17 相容）
 */
#include "core/ws_server.hpp"
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <utility>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

static bool endsWith(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string getMimeType(const std::string& path) {
    if (endsWith(path, ".html")) {
        return "text/html; charset=utf-8";
    }
    if (endsWith(path, ".css")) {
        return "text/css; charset=utf-8";
    }
    if (endsWith(path, ".js")) {
        return "application/javascript; charset=utf-8";
    }
    if (endsWith(path, ".json")) {
        return "application/json";
    }
    if (endsWith(path, ".png")) {
        return "image/png";
    }
    if (endsWith(path, ".jpg") || endsWith(path, ".jpeg")) {
        return "image/jpeg";
    }
    if (endsWith(path, ".svg")) {
        return "image/svg+xml";
    }
    if (endsWith(path, ".ico")) {
        return "image/x-icon";
    }
    if (endsWith(path, ".wasm")) {
        return "application/wasm";
    }
    return "application/octet-stream";
}

// ============================================================================
// 路由表
// ============================================================================

void WsServer::registerRoute(const std::string& prefix, RouteHandler handler) {
    std::lock_guard<std::mutex> lock(routesMutex_);
    routes_.push_back({prefix, handler});
}

// ============================================================================
WsServer::WsServer() = default;
WsServer::~WsServer() {
    stop();
}

bool WsServer::start(
    uint16_t port, const std::string& staticDir, std::shared_ptr<EventBus> bus) {
    port_ = port;
    staticDir_ = staticDir;
    bus_ = bus;
    try {
        tcp::endpoint ep(tcp::v4(), port);
        acceptor_ = std::make_unique<tcp::acceptor>(ioc_, ep);
        acceptor_->set_option(net::socket_base::reuse_address(true));
        doAccept();
        thread_ = std::make_unique<std::thread>([this]() {
            running_ = true;
            std::cout << "[WsServer] 啟動於埠號 " << port_ << std::endl;
            ioc_.run();
            running_ = false;
        });
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[WsServer] 啟動失敗: " << e.what() << std::endl;
        return false;
    }
}

void WsServer::stop() {
    if (running_) {
        ioc_.stop();
        if (thread_ && thread_->joinable()) {
            thread_->join();
        }
        running_ = false;
    }
}

void WsServer::broadcast(const std::string& message) {
    // 使用 post 排入 io_context，讓 broadcast 可從其他執行緒呼叫；
    // 實際入隊與 async_write 都在 ioc 執行緒上依序進行。
    auto payload = std::make_shared<std::string>(message);
    net::post(ioc_, [this, payload]() {
        std::vector<std::shared_ptr<WsClient>> clients;
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            clients.assign(clients_.begin(), clients_.end());
        }
        for (const auto& client : clients) {
            enqueueWrite(client, payload);
        }
    });
}

void WsServer::doAccept() {
    acceptor_->async_accept(
        net::make_strand(ioc_),
        [this](beast::error_code ec, tcp::socket socket) {
            if (!ec) {
                handleSession(std::move(socket));
            }
            if (running_) {
                doAccept();
            }
        });
}

void WsServer::handleSession(tcp::socket socket) {
    auto buffer = std::make_shared<beast::flat_buffer>();
    auto req = std::make_shared<http::request<http::string_body>>();
    auto socketPtr = std::make_shared<tcp::socket>(std::move(socket));

    http::async_read(
        *socketPtr, *buffer, *req,
        [this, socketPtr, buffer, req](beast::error_code ec, std::size_t) {
            if (ec) {
                return;
            }
            if (websocket::is_upgrade(*req)) {
                auto ws = std::make_shared<websocket::stream<tcp::socket>>(
                    std::move(*socketPtr));
                ws->set_option(
                    websocket::stream_base::timeout::suggested(beast::role_type::server));
                ws->set_option(websocket::stream_base::decorator(
                    [](websocket::response_type& res) {
                        res.set(http::field::server, "Taiphoon_GCS");
                    }));
                ws->async_accept(*req, [this, ws](beast::error_code ec) {
                    if (ec) {
                        return;
                    }
                    auto client = addClient(ws);
                    if (!client) {
                        return;
                    }
                    auto buf = std::make_shared<beast::flat_buffer>();
                    readLoop(client, buf);
                });
            } else {
                handleHttpRequest(*socketPtr, *req);
            }
        });
}

void WsServer::readLoop(
    std::shared_ptr<WsClient> client,
    std::shared_ptr<beast::flat_buffer> buffer) {
    if (!client || !client->ws_ || !buffer) {
        return;
    }

    client->ws_->async_read(
        *buffer,
        [this, client, buffer](beast::error_code ec, std::size_t) {
            if (ec) {
                removeClient(client);
                return;
            }
            std::string msg = beast::buffers_to_string(buffer->data());
            buffer->consume(buffer->size());
            if (bus_) {
                bus_->publish("ws_message", msg);
            }
            readLoop(client, buffer);
        });
}

void WsServer::handleHttpRequest(
    tcp::socket& socket, http::request<http::string_body>& req) {
    beast::error_code ec;
    std::string target(req.target());

    // 檢查自訂路由
    {
        std::lock_guard<std::mutex> lock(routesMutex_);
        for (const auto& [prefix, handler] : routes_) {
            if (target.find(prefix) == 0) {
                std::string body = handler(target);
                if (!body.empty()) {
                    http::response<http::string_body> res;
                    res.result(http::status::ok);
                    res.set(http::field::content_type, getMimeType(target));
                    res.set(http::field::server, "Taiphoon_GCS");
                    res.set(http::field::access_control_allow_origin, "*");
                    res.body() = body;
                    res.prepare_payload();
                    http::write(socket, res, ec);
                    socket.shutdown(tcp::socket::shutdown_send, ec);
                    return;
                }
            }
        }
    }

    // 靜態檔案
    if (target == "/") {
        target = "/index.html";
    }
    std::string filePath = staticDir_ + target;
    if (filePath.find("..") != std::string::npos) {
        filePath = staticDir_ + "/index.html";
    }

    http::response<http::string_body> res;
    std::ifstream file(filePath, std::ios::binary);
    if (file.is_open()) {
        std::ostringstream ss;
        ss << file.rdbuf();
        res.result(http::status::ok);
        res.set(http::field::content_type, getMimeType(filePath));
        res.body() = ss.str();
    } else {
        res.result(http::status::not_found);
        res.set(http::field::content_type, "text/plain");
        res.body() = "404 Not Found";
    }
    res.set(http::field::server, "Taiphoon_GCS");
    res.set(http::field::access_control_allow_origin, "*");
    res.prepare_payload();
    http::write(socket, res, ec);
    socket.shutdown(tcp::socket::shutdown_send, ec);
}

WsServer::WsClient::WsClient(
    std::shared_ptr<websocket::stream<tcp::socket>> ws)
    : ws_(std::move(ws)) {
}

std::shared_ptr<WsServer::WsClient> WsServer::addClient(
    std::shared_ptr<websocket::stream<tcp::socket>> ws) {
    if (!ws) {
        return nullptr;
    }

    auto client = std::make_shared<WsClient>(std::move(ws));
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto result = clients_.insert(client);
    if (!result.second) {
        return *result.first;
    }
    return client;
}

void WsServer::removeClient(std::shared_ptr<WsClient> client) {
    if (!client) {
        return;
    }

    std::lock_guard<std::mutex> lock(clientsMutex_);
    if (client->is_removed_) {
        return;
    }
    client->is_removed_ = true;
    client->writeQueue_.clear();
    clients_.erase(client);
}

void WsServer::enqueueWrite(
    std::shared_ptr<WsClient> client, std::shared_ptr<std::string> payload) {
    if (!client || !payload) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        if (client->is_removed_) {
            return;
        }
        client->writeQueue_.push_back(std::move(payload));
    }

    startWrite(client);
}

void WsServer::startWrite(std::shared_ptr<WsClient> client) {
    if (!client || !client->ws_) {
        return;
    }

    std::shared_ptr<std::string> payload;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        if (client->is_removed_) {
            return;
        }
        if (client->writeQueue_.empty()) {
            return;
        }
        if (client->is_writing_) {
            return;
        }
        payload = client->writeQueue_.front();
        client->is_writing_ = true;
    }

    client->ws_->async_write(
        net::buffer(*payload),
        [this, client, payload](beast::error_code ec, std::size_t) {
            bool shouldRemove = false;
            {
                std::lock_guard<std::mutex> lock(clientsMutex_);
                client->is_writing_ = false;
                if (client->is_removed_) {
                    return;
                }
                if (ec) {
                    shouldRemove = true;
                } else if (!client->writeQueue_.empty()) {
                    client->writeQueue_.pop_front();
                }
            }

            if (shouldRemove) {
                removeClient(client);
                return;
            }
            startWrite(client);
        });
}
