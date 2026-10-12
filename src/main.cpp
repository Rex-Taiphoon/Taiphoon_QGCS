/**
 * @file main.cpp
 * @brief Taiphoon_GCS 主程式入口點
 */
#include "core/module_manager.hpp"
#include "core/ws_server.hpp"
#include "core/ws_protocol.hpp"
#include "modules/fleet/fleet_module.hpp"
#include "modules/telemetry/telemetry_module.hpp"
#include "modules/guard/guard_module.hpp"
#include "modules/command/command_module.hpp"
#include "modules/lifecycle/agent_lifecycle_module.hpp"
#include "modules/map/map_module.hpp"
#include "modules/debug/debug_terminal_module.hpp"
#include <rclcpp/rclcpp.hpp>
#include <csignal>
#include <cstdlib>
#include <regex>
#include <thread>
#include <chrono>
#include <iostream>
#include <atomic>
#include <string>
#include <exception>

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_shutdown_requested{false};
// 留出廣播通知的傳送時間；不在 WebSocket 執行緒停止或等待服務。
static constexpr auto SHUTDOWN_NOTICE_GRACE = std::chrono::milliseconds(500);
static void signalHandler(int) { g_running = false; }

int main(int argc, char* argv[]) {
  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);
  rclcpp::init(argc, argv);

  std::cout << "\n╔";
  std::cout << std::string(46, '=') << "╗" << std::endl;
    const char* version = std::getenv("GCS_VERSION");
    if (version == nullptr)
    {
        version = "dev";
    }
    std::cout << "║     Taiphoon_GCS 地面控制站 " << version << "   ";
  std::cout << "║" << std::endl;
  std::cout << "╚" << std::string(46, '=') << "╝\n" << std::endl;

  ModuleManager manager;

  auto fleetMod     = std::make_unique<FleetModule>();
  auto telemetryMod = std::make_unique<TelemetryModule>();
  auto guardMod     = std::make_unique<GuardModule>();
  auto commandMod   = std::make_unique<CommandModule>();
  auto lifecycleMod = std::make_unique<AgentLifecycleModule>();
  auto mapMod       = std::make_unique<MapModule>();
  auto debugTerminalMod = std::make_unique<DebugTerminalModule>();

  auto* fleetPtr     = fleetMod.get();
  auto* telemetryPtr = telemetryMod.get();
  auto* commandPtr   = commandMod.get();
  auto* lifecyclePtr = lifecycleMod.get();
  auto* mapPtr       = mapMod.get();

  manager.registerModule(std::move(fleetMod));
  manager.registerModule(std::move(telemetryMod));
  manager.registerModule(std::move(guardMod));
  manager.registerModule(std::move(commandMod));
  manager.registerModule(std::move(lifecycleMod));
  manager.registerModule(std::move(mapMod));
  manager.registerModule(std::move(debugTerminalMod));

  if (!manager.initAll()) {
    rclcpp::shutdown();
    return 1;
  }

  auto bus = manager.getEventBus();

  // 設定 WebSocket 與圖磚端點
  WsServer wsServer;
  const char* dir = std::getenv("GCS_FRONTEND_DIR");
  std::string staticDir = dir ? dir : "share/taiphoon_gcs/frontend";
  uint16_t port = 8080;
  if (auto* e = std::getenv("GCS_WS_PORT")) {
    port = static_cast<uint16_t>(std::stoi(e));
  }

  mapPtr->setCacheDir("/tmp/taiphoon_gcs_tiles");
  wsServer.registerRoute("/tiles/", [mapPtr](const std::string& path) -> std::string {
    // 解析 /tiles/{z}/{x}/{y}.png
    std::regex re("/tiles/(\\d+)/(\\d+)/(\\d+)\\.png");
    std::smatch m;
    if (std::regex_match(path, m, re)) {
      return mapPtr->getTile(std::stoi(m[1]), std::stoi(m[2]), std::stoi(m[3]));
    }
    return "";
  });

  // EventBus → Telemetry：在 executor 啟動前完成訂閱，避免漏接 agent_online
  bus->subscribe("agent_online", [telemetryPtr](const std::string&, const std::any& data) {
    try {
      telemetryPtr->subscribeAgent(std::any_cast<std::string>(data));
    } catch (const std::exception& e) {
      std::cerr << "[主程式] agent_online 處理失敗: " << e.what() << std::endl;
    }
  });
  bus->subscribe("agent_offline", [telemetryPtr](const std::string&, const std::any& data) {
    try {
      telemetryPtr->unsubscribeAgent(std::any_cast<std::string>(data));
    } catch (const std::exception& e) {
      std::cerr << "[主程式] agent_offline 處理失敗: " << e.what() << std::endl;
    }
  });

  // WS → EventBus routing
  bus->subscribe("ws_message", [bus](const std::string&, const std::any& data) {
    try {
      const auto raw = std::any_cast<std::string>(data);
      if (ws_protocol::is_confirmed_gcs_shutdown(raw)) {
        bus->publish("gcs_shutdown_request", raw);
      } else if (ws_protocol::isCmdRequest(raw)) {
        bus->publish("cmd_request", raw);
      } else if (ws_protocol::isAgentShutdownRequest(raw)) {
        bus->publish("agent_shutdown_request", raw);
      } else if (ws_protocol::isRos2ExecRequest(raw)) {
        bus->publish("ros2_exec_request", raw);
      }
    } catch (const std::exception& e) {
      std::cerr << "[主程式] ws_message 處理失敗: " << e.what() << std::endl;
    }
  });

  // EventBus → WebSocket
  auto* ws = &wsServer;
  bus->subscribe("gcs_shutdown_request", [ws](const std::string&, const std::any&) {
    if (!g_shutdown_requested.exchange(true))
    {
        ws->broadcast(R"({"type":"gcs_shutdown_notice"})");
    }
  });
  auto fwd = [ws](const std::string&, const std::any& data) {
    try {
      ws->broadcast(std::any_cast<std::string>(data));
    } catch (const std::exception& e) {
      std::cerr << "[主程式] WebSocket 推送失敗: " << e.what() << std::endl;
    }
  };
  bus->subscribe("state_update", fwd);
  bus->subscribe("attitude_update", fwd);
  bus->subscribe("ned_target", fwd);
  bus->subscribe("waypoints", fwd);
  bus->subscribe("trajectory_update", fwd);
  bus->subscribe("fleet_update", fwd);
  bus->subscribe("cmd_ack", fwd);
  bus->subscribe("ros2_exec_response", fwd);

  if (!wsServer.start(port, staticDir, bus)) {
    manager.shutdownAll();
    rclcpp::shutdown();
    return 1;
  }

  // 所有節點在 spin 前加入 executor，Telemetry 節點也預先建立
  auto executor = std::make_shared<rclcpp::executors::MultiThreadedExecutor>(
    rclcpp::ExecutorOptions(), 2);
  executor->add_node(fleetPtr->getNode());
  executor->add_node(telemetryPtr->getNode());
  executor->add_node(commandPtr->getNode());
  executor->add_node(lifecyclePtr->getNode());

  std::thread spinThread([executor]() {
    while (g_running && rclcpp::ok()) {
      try {
        executor->spin_some(std::chrono::milliseconds(100));
      } catch (const std::exception& e) {
        std::cerr << "[主程式] executor 例外: " << e.what() << std::endl;
        g_running = false;
      }
    }
  });

  std::cout << "[主程式] 全部服務已啟動，請開啟 http://localhost:" << port << std::endl;

  while (g_running && rclcpp::ok()) {
    if (g_shutdown_requested)
    {
        std::this_thread::sleep_for(SHUTDOWN_NOTICE_GRACE);
        g_running = false;
        break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  std::cout << "\n[主程式] 正在關閉..." << std::endl;
  executor->cancel();
  if (spinThread.joinable()) {
    spinThread.join();
  }
  wsServer.stop();
  manager.shutdownAll();
  rclcpp::shutdown();
  return 0;
}
