/**
 * @file test_telemetry_serialization.cpp
 * @brief 驗證 WGS84 座標的 JSON 精度與無資料語意
 * @author Codex
 * @date 2026-10-06
 */
#include "modules/telemetry/telemetry_module.hpp"
#include <gtest/gtest.h>
#include <limits>

TEST(TelemetrySerializationTest, PreservesSmallWgs84Movement)
{
    taiphoon_agent::msg::AgentState state;
    state.position_global = {25.033964321, 121.564468123, 123.456789012};
    const auto first = nlohmann::json::parse(TelemetryModule::serialize_state(state, "uav_1_agent").dump());
    for (std::size_t index = 0; index < state.position_global.size(); ++index)
    {
        EXPECT_NEAR(first["state"]["position_global"][index].get<double>(),
            state.position_global.at(index), 1e-12);
    }
    // 低於 float32 的量化間隔，仍需保留厘米尺度的移動。
    state.position_global.at(1) += 0.0000001;
    const auto second = nlohmann::json::parse(TelemetryModule::serialize_state(state, "uav_1_agent").dump());
    const double delta = second["state"]["position_global"][1].get<double>()
        - first["state"]["position_global"][1].get<double>();
    EXPECT_NEAR(delta, 0.0000001, 1e-12);
}

TEST(TelemetrySerializationTest, ConvertsNonFiniteGlobalPositionToNull)
{
    taiphoon_agent::msg::AgentState state;
    state.position_global = {std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    const auto message = TelemetryModule::serialize_state(state, "uav_1_agent");
    for (const auto& value : message["state"]["position_global"])
    {
        EXPECT_TRUE(value.is_null());
    }
}
