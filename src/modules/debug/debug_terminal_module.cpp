/**
 * @file debug_terminal_module.cpp
 * @brief 除錯終端機模組實作
 * @author Taiphoon GCS Team
 * @date 2026-08-17
 */
#include "modules/debug/debug_terminal_module.hpp"
#include "core/ws_protocol.hpp"
#include <rclcpp/rclcpp.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace
{

/// 執行指令用的暫存腳本前綴
constexpr const char* SCRIPT_PREFIX = "/tmp/gcs_ros2_exec_";

/// GCS 工作空間 setup.bash 的可能位置（優先 volume mount 版本）
constexpr const char* WORKSPACE_INSTALL = "/home/taiphoon/Taiphoon_GCS/install/setup.bash";
/// 映像內預編譯工作空間 setup.bash
constexpr const char* PREBUILT_INSTALL = "/home/taiphoon/.gcs_prebuilt_install/setup.bash";

/**
 * @brief 建立內含 ROS2 環境 source 的暫存執行腳本
 * @param command 使用者指令（已驗證以 ros2 開頭）
 * @param scriptPath 輸出的腳本路徑
 * @return 是否建立成功
 */
bool writeExecScript(const std::string& command, std::string& scriptPath)
{
    static std::atomic<unsigned long> sequence{0};
    const std::string path = std::string(SCRIPT_PREFIX) +
        std::to_string(static_cast<long>(::getpid())) + "_" +
        std::to_string(sequence.fetch_add(1)) + ".sh";

    std::ofstream script(path, std::ios::out | std::ios::trunc);
    if (!script.is_open())
    {
        return false;
    }

    script << "#!/bin/bash\n";
    script << "source /opt/ros/humble/setup.bash\n";
    script << "if [ -f " << WORKSPACE_INSTALL << " ]; then\n";
    script << "  source " << WORKSPACE_INSTALL << "\n";
    script << "elif [ -f " << PREBUILT_INSTALL << " ]; then\n";
    script << "  source " << PREBUILT_INSTALL << "\n";
    script << "fi\n";
    script << command << "\n";
    script.flush();
    if (!script.good())
    {
        script.close();
        std::remove(path.c_str());
        return false;
    }
    script.close();
    scriptPath = path;
    return true;
}

}  // namespace

bool DebugTerminalModule::init(EventBus& bus)
{
    bus_ = &bus;
    running_ = true;

    bus_->subscribe("ros2_exec_request", [this](const std::string&, const std::any& data)
    {
        onRos2ExecRequest(data);
    });

    RCLCPP_INFO(rclcpp::get_logger("DebugTerminalModule"), "除錯終端機已啟用，僅允許執行 ros2 指令");
    return true;
}

void DebugTerminalModule::shutdown()
{
    running_ = false;

    // 等待執行中的指令結束；每筆指令都有 timeout 上限，因此總等待有界
    std::unique_lock<std::mutex> lock(cvMutex_);
    cv_.wait_for(lock, std::chrono::seconds(SHUTDOWN_WAIT_S),
                 [this]() { return activeRequests_.load() == 0; });
}

bool DebugTerminalModule::healthCheck()
{
    return running_;
}

void DebugTerminalModule::onRos2ExecRequest(const std::any& data)
{
    if (!running_)
    {
        return;
    }

    try
    {
        const auto rawJson = std::any_cast<std::string>(data);
        ws_protocol::Ros2ExecRequest request;
        std::string protocolError;
        if (!ws_protocol::parseRos2ExecRequest(rawJson, request, &protocolError))
        {
            bus_->publish("ros2_exec_response",
                          ws_protocol::makeRos2ExecResponse(
                              request.request_id, request.command, "error", protocolError));
            return;
        }

        ++activeRequests_;
        std::thread([this, rawJson]()
        {
            try
            {
                executeRequest(rawJson);
            }
            catch (const std::exception& e)
            {
                std::cerr << "[DebugTerminalModule] 執行失敗: " << e.what() << std::endl;
            }
            if (activeRequests_.fetch_sub(1) == 1)
            {
                std::lock_guard<std::mutex> lock(cvMutex_);
                cv_.notify_all();
            }
        }).detach();
    }
    catch (const std::exception& e)
    {
        std::cerr << "[DebugTerminalModule] 請求解析失敗: " << e.what() << std::endl;
        bus_->publish("ros2_exec_response",
                      ws_protocol::makeRos2ExecResponse("", "", "error", "請求解析失敗"));
    }
}

void DebugTerminalModule::executeRequest(const std::string& rawJson)
{
    ws_protocol::Ros2ExecRequest request;
    if (!ws_protocol::parseRos2ExecRequest(rawJson, request, nullptr))
    {
        return;
    }

    std::string output;
    const std::string status = runRos2Command(request.command, output);
    if (bus_ != nullptr)
    {
        bus_->publish("ros2_exec_response",
                      ws_protocol::makeRos2ExecResponse(request.request_id, request.command,
                                                        status, output));
    }
}

std::string DebugTerminalModule::runRos2Command(const std::string& command, std::string& output)
{
    std::string scriptPath;
    if (!writeExecScript(command, scriptPath))
    {
        output = "無法建立暫存執行腳本";
        return "error";
    }

    int timeoutSeconds = DEFAULT_EXEC_TIMEOUT_S;
    if (const char* envTimeout = std::getenv("GCS_ROS2_EXEC_TIMEOUT_S"))
    {
        try
        {
            const int parsed = std::stoi(envTimeout);
            if (parsed > 0 && parsed <= 300)
            {
                timeoutSeconds = parsed;
            }
        }
        catch (const std::exception&)
        {
            // 環境變數格式錯誤時沿用預設值
        }
    }

    const std::string execLine = "timeout --signal=TERM --kill-after=2 " +
        std::to_string(timeoutSeconds) + " /bin/bash " + scriptPath + " 2>&1";
    FILE* pipe = popen(execLine.c_str(), "r");
    if (pipe == nullptr)
    {
        std::remove(scriptPath.c_str());
        output = "無法啟動指令執行程序";
        return "error";
    }

    char buffer[4096];
    std::size_t total = 0;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        const std::size_t length = std::strlen(buffer);
        const std::size_t remaining = MAX_OUTPUT_BYTES - total;
        if (remaining > 0)
        {
            const std::size_t chunk = length < remaining ? length : remaining;
            output.append(buffer, chunk);
            total += chunk;
        }
    }
    const int exitStatus = pclose(pipe);
    std::remove(scriptPath.c_str());

    if (total >= MAX_OUTPUT_BYTES)
    {
        output += "\n[輸出已達上限，其餘內容省略]";
    }

    if (exitStatus == 124)
    {
        output += "\n[指令執行逾時，已強制終止]";
        return "timeout";
    }
    if (exitStatus != 0)
    {
        return "error";
    }
    return "ok";
}
