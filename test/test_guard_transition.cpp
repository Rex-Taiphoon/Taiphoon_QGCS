/**
 * @file test_guard_transition.cpp
 * @brief GuardModule 轉移表與全體模式驗證單元測試
 * @author Taiphoon GCS Team
 * @date 2026-08-05
 */
#include "modules/guard/guard_module.hpp"
#include "core/event_bus.hpp"
#include "core/ws_protocol.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <any>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

const std::array<FlightState, 11> kStates = {
  FlightState::IDLE, FlightState::OFFBOARD_INIT, FlightState::OFFBOARD_ACTIVE,
  FlightState::ARMED, FlightState::TAKEOFF, FlightState::HOVER,
  FlightState::RTL, FlightState::LAND, FlightState::MOVING,
  FlightState::POST_RTL, FlightState::ERROR
};

const std::array<CmdType, 20> kCommands = {
  CmdType::ARM, CmdType::DISARM, CmdType::TAKEOFF,
  CmdType::MOVE_FORWARD, CmdType::MOVE_BACKWARD, CmdType::MOVE_LEFT,
  CmdType::MOVE_RIGHT, CmdType::MOVE_UP, CmdType::MOVE_DOWN,
  CmdType::YAW, CmdType::MOVE_POINT, CmdType::HOLD,
  CmdType::MODE_RTL, CmdType::MODE_LAND, CmdType::MODE_OFFBOARD,
  CmdType::FORMATION_CIRCLE, CmdType::FORMATION_HORI_LINEAR,
  CmdType::FORMATION_VERT_LINEAR, CmdType::FORMATION_CANCEL,
  CmdType::UNKNOWN
};

const std::set<CmdType> kFormationCommands = {
  CmdType::FORMATION_CIRCLE, CmdType::FORMATION_HORI_LINEAR,
  CmdType::FORMATION_VERT_LINEAR, CmdType::FORMATION_CANCEL
};

std::map<FlightState, std::set<CmdType>> buildExpectedTable() {
  std::map<FlightState, std::set<CmdType>> table;
  for (const auto state : kStates) {
    table[state] = kFormationCommands;
  }

  table[FlightState::IDLE].insert(CmdType::MODE_OFFBOARD);
  table[FlightState::OFFBOARD_INIT].insert(CmdType::MODE_OFFBOARD);
  table[FlightState::ARMED].insert(CmdType::DISARM);
  table[FlightState::ERROR].insert(CmdType::DISARM);

  for (const auto cmd : {
         CmdType::MOVE_FORWARD, CmdType::MOVE_BACKWARD, CmdType::MOVE_LEFT,
         CmdType::MOVE_RIGHT, CmdType::MOVE_UP, CmdType::MOVE_DOWN
       }) {
    table[FlightState::TAKEOFF].insert(cmd);
  }

  for (const auto cmd : {
         CmdType::MOVE_FORWARD, CmdType::MOVE_BACKWARD, CmdType::MOVE_LEFT,
         CmdType::MOVE_RIGHT, CmdType::MOVE_UP, CmdType::MOVE_DOWN,
         CmdType::YAW, CmdType::MOVE_POINT, CmdType::HOLD,
         CmdType::MODE_RTL, CmdType::MODE_LAND
       }) {
    table[FlightState::HOVER].insert(cmd);
    table[FlightState::MOVING].insert(cmd);
  }
  table[FlightState::HOVER].insert(CmdType::DISARM);

  return table;
}

constexpr uint8_t PX4_NAV_STATE_OFFBOARD = 14;

std::string stateUpdateJson(const std::string& agent, int mode, bool online,
                            bool armed = false, bool landed = true,
                            uint8_t px4NavState = PX4_NAV_STATE_OFFBOARD,
                            bool px4StateValid = true) {
  json j;
  j["type"] = "state_update";
  j["agent"] = agent;
  j["state"]["flight_mode"] = mode;
  j["state"]["online"] = online;
  j["state"]["armed"] = armed;
  j["state"]["landed"] = landed;
  j["state"]["px4_nav_state"] = px4NavState;
  j["state"]["px4_state_valid"] = px4StateValid;
  return j.dump();
}

std::string cmdRequestJson(const std::string& agent, const std::string& command) {
  json j;
  j["type"] = "cmd_request";
  j["agent"] = agent;
  j["command"] = command;
  return j.dump();
}

std::string cmdRequestJson(const std::string& agent, const std::string& command,
                           const std::vector<json>& params) {
  json j;
  j["type"] = "cmd_request";
  j["agent"] = agent;
  j["command"] = command;
  if (!params.empty()) {
    j["params"] = params;
  }
  return j.dump();
}

}  // namespace

TEST(GuardModuleTest, TransitionTableCoversEveryStateAndCommand) {
  GuardModule guard;
  const auto expected = buildExpectedTable();

  for (const auto state : kStates) {
    for (const auto cmd : kCommands) {
      const bool shouldAllow = expected.at(state).count(cmd) > 0;
      EXPECT_EQ(guard.isCommandAllowed(state, cmd), shouldAllow)
        << "state=" << static_cast<int>(state) << " cmd=" << static_cast<int>(cmd);
    }
  }
}

TEST(GuardModuleTest, AllowsOffboardModeFromIdle) {
  EventBus bus;
  GuardModule guard;
  ASSERT_TRUE(guard.init(bus));

  bus.publish("state_update", stateUpdateJson("uav_1_agent", 0, true));

  bool validated = false;
  bus.subscribe("cmd_validated", [&validated](const std::string&, const std::any&) {
    validated = true;
  });

  bus.publish("cmd_request", cmdRequestJson("uav_1_agent", "MODE_OFFBOARD"));
  EXPECT_TRUE(validated);
}

TEST(GuardModuleTest, AllModeRequiresEveryOnlineAgentAllowed) {
  EventBus bus;
  GuardModule guard;
  ASSERT_TRUE(guard.init(bus));

  bus.publish("state_update", stateUpdateJson("uav_1_agent", 2, true));
  bus.publish("state_update", stateUpdateJson("uav_2_agent", 2, true));

  bool validated = false;
  std::string rejectedAck;
  bus.subscribe("cmd_validated", [&validated](const std::string&, const std::any&) {
    validated = true;
  });
  bus.subscribe("cmd_ack", [&rejectedAck](const std::string&, const std::any& data) {
    rejectedAck = std::any_cast<std::string>(data);
  });

  bus.publish("cmd_request", cmdRequestJson("all", "ARM"));
  EXPECT_TRUE(validated);
  EXPECT_TRUE(rejectedAck.empty());

  validated = false;
  bus.publish("state_update", stateUpdateJson("uav_2_agent", 2, true, true));
  bus.publish("cmd_request", cmdRequestJson("all", "ARM"));
  EXPECT_FALSE(validated);
  ASSERT_FALSE(rejectedAck.empty());
  const json ack = json::parse(rejectedAck);
  EXPECT_EQ(ack["status"], "error");
  EXPECT_NE(ack["message"].get<std::string>().find("uav_2_agent"), std::string::npos);
}

TEST(GuardModuleTest, AllModeRejectsWhenNoAgentOnline) {
  EventBus bus;
  GuardModule guard;
  ASSERT_TRUE(guard.init(bus));

  std::string rejectedAck;
  bus.subscribe("cmd_ack", [&rejectedAck](const std::string&, const std::any& data) {
    rejectedAck = std::any_cast<std::string>(data);
  });

  bus.publish("cmd_request", cmdRequestJson("all", "ARM"));
  ASSERT_FALSE(rejectedAck.empty());
  const json ack = json::parse(rejectedAck);
  EXPECT_EQ(ack["status"], "error");
}

TEST(GuardModuleTest, OfflineAgentRemovedFromAllModeValidation) {
  EventBus bus;
  GuardModule guard;
  ASSERT_TRUE(guard.init(bus));

  bus.publish("state_update", stateUpdateJson("uav_1_agent", 2, true));
  bus.publish("agent_offline", std::string("uav_1_agent"));

  bool validated = false;
  bus.subscribe("cmd_validated", [&validated](const std::string&, const std::any&) {
    validated = true;
  });

  bus.publish("cmd_request", cmdRequestJson("all", "ARM"));
  EXPECT_FALSE(validated);
}

TEST(GuardModuleTest, UnknownSingleAgentRejected) {
  EventBus bus;
  GuardModule guard;
  ASSERT_TRUE(guard.init(bus));

  bool validated = false;
  std::string rejectedAck;
  bus.subscribe("cmd_validated", [&validated](const std::string&, const std::any&) {
    validated = true;
  });
  bus.subscribe("cmd_ack", [&rejectedAck](const std::string&, const std::any& data) {
    rejectedAck = std::any_cast<std::string>(data);
  });

  bus.publish("cmd_request", cmdRequestJson("uav_9_agent", "MODE_OFFBOARD"));
  EXPECT_FALSE(validated);
  ASSERT_FALSE(rejectedAck.empty());
  const json ack = json::parse(rejectedAck);
  EXPECT_EQ(ack["status"], "error");
  EXPECT_NE(ack["message"].get<std::string>().find("不在線上"), std::string::npos);
}

TEST(GuardModuleTest, OfflineSingleAgentRejected) {
  EventBus bus;
  GuardModule guard;
  ASSERT_TRUE(guard.init(bus));

  bus.publish("state_update", stateUpdateJson("uav_1_agent", 0, true));
  bus.publish("agent_offline", std::string("uav_1_agent"));

  bool validated = false;
  std::string rejectedAck;
  bus.subscribe("cmd_validated", [&validated](const std::string&, const std::any&) {
    validated = true;
  });
  bus.subscribe("cmd_ack", [&rejectedAck](const std::string&, const std::any& data) {
    rejectedAck = std::any_cast<std::string>(data);
  });

  bus.publish("cmd_request", cmdRequestJson("uav_1_agent", "MODE_OFFBOARD"));
  EXPECT_FALSE(validated);
  ASSERT_FALSE(rejectedAck.empty());
  const json ack = json::parse(rejectedAck);
  EXPECT_EQ(ack["status"], "error");
}

TEST(GuardModuleTest, OnlineFalseClearsOldState) {
  EventBus bus;
  GuardModule guard;
  ASSERT_TRUE(guard.init(bus));

  bus.publish("state_update", stateUpdateJson("uav_1_agent", 2, true));
  bus.publish("state_update", stateUpdateJson("uav_1_agent", 2, false));

  bool validated = false;
  std::string rejectedAck;
  bus.subscribe("cmd_validated", [&validated](const std::string&, const std::any&) {
    validated = true;
  });
  bus.subscribe("cmd_ack", [&rejectedAck](const std::string&, const std::any& data) {
    rejectedAck = std::any_cast<std::string>(data);
  });

  bus.publish("cmd_request", cmdRequestJson("uav_1_agent", "ARM"));
  EXPECT_FALSE(validated);
  ASSERT_FALSE(rejectedAck.empty());
}

TEST(GuardModuleTest, FormationCommandRejectedForSingleAgent) {
  EventBus bus;
  GuardModule guard;
  ASSERT_TRUE(guard.init(bus));

  bool validated = false;
  std::string rejectedAck;
  bus.subscribe("cmd_validated", [&validated](const std::string&, const std::any&) {
    validated = true;
  });
  bus.subscribe("cmd_ack", [&rejectedAck](const std::string&, const std::any& data) {
    rejectedAck = std::any_cast<std::string>(data);
  });

  bus.publish("cmd_request", cmdRequestJson(
    "uav_1_agent", "FORMATION_CIRCLE", {json(10.0), json(20.0)}));
  EXPECT_FALSE(validated);
  ASSERT_FALSE(rejectedAck.empty());
  const json ack = json::parse(rejectedAck);
  EXPECT_NE(ack["message"].get<std::string>().find("FORMATION"), std::string::npos);
}

TEST(GuardModuleTest, FormationCommandAllowedForAll) {
  EventBus bus;
  GuardModule guard;
  ASSERT_TRUE(guard.init(bus));

  bool validated = false;
  bus.subscribe("cmd_validated", [&validated](const std::string&, const std::any&) {
    validated = true;
  });

  bus.publish("cmd_request", cmdRequestJson(
    "all", "FORMATION_CIRCLE", {json(10.0), json(20.0)}));
  EXPECT_TRUE(validated);
}

TEST(GuardModuleTest, RejectsMalformedAgentAndParams) {
  EventBus bus;
  GuardModule guard;
  ASSERT_TRUE(guard.init(bus));

  bool validated = false;
  std::string rejectedAck;
  bus.subscribe("cmd_validated", [&validated](const std::string&, const std::any&) {
    validated = true;
  });
  bus.subscribe("cmd_ack", [&rejectedAck](const std::string&, const std::any& data) {
    rejectedAck = std::any_cast<std::string>(data);
  });

  bus.publish("cmd_request", cmdRequestJson("uav_1", "MODE_OFFBOARD"));
  EXPECT_FALSE(validated);
  ASSERT_FALSE(rejectedAck.empty());
  const json firstAck = json::parse(rejectedAck);
  EXPECT_EQ(firstAck["status"], "error");

  rejectedAck.clear();
  bus.publish("cmd_request", cmdRequestJson("uav_1_agent", "MOVE_FORWARD"));
  EXPECT_FALSE(validated);
  ASSERT_FALSE(rejectedAck.empty());
  const json secondAck = json::parse(rejectedAck);
  EXPECT_EQ(secondAck["status"], "error");

  rejectedAck.clear();
  bus.publish("cmd_request", cmdRequestJson(
    "all", "FORMATION_CIRCLE", {json(10.0), json(true)}));
  EXPECT_FALSE(validated);
  ASSERT_FALSE(rejectedAck.empty());
  const json thirdAck = json::parse(rejectedAck);
  EXPECT_EQ(thirdAck["status"], "error");
}

TEST(GuardModuleTest, StateUpdateFromCmdValidatedSubscriberDoesNotDeadlock) {
  // Guard 在鎖內發布 cmd_validated，此測試確認 EventBus 訂閱者回呼再發布
  // state_update 時，同一執行緒可安全重入 Guard 的狀態更新路徑。
  // Guard 的原子範圍只到 EventBus 送出 cmd_validated 為止；CommandModule
  // 實際 ROS2 publish 不在 Guard 鎖內。
  EventBus bus;
  GuardModule guard;
  ASSERT_TRUE(guard.init(bus));

  bus.publish("state_update", stateUpdateJson("uav_1_agent", 0, true));

  bus.subscribe("cmd_validated", [&bus](const std::string&, const std::any&) {
    bus.publish("state_update", stateUpdateJson("uav_1_agent", 2, true));
  });

  bool validated = false;
  bus.subscribe("cmd_validated", [&validated](const std::string&, const std::any&) {
    validated = true;
  });

  bus.publish("cmd_request", cmdRequestJson("uav_1_agent", "MODE_OFFBOARD"));
  EXPECT_TRUE(validated);
  EXPECT_EQ(guard.getAgentState("uav_1_agent"), FlightState::OFFBOARD_ACTIVE);
}

TEST(GuardModuleTest, CommandEventsDoNotInferFlightState) {
    EventBus bus;
    GuardModule guard;
    ASSERT_TRUE(guard.init(bus));

    bus.publish("state_update", stateUpdateJson("uav_1_agent", 3, true, true));
    ASSERT_EQ(guard.getAgentState("uav_1_agent"), FlightState::ARMED);

    bool validated = false;
    bus.subscribe("cmd_validated", [&validated](const std::string&, const std::any&) {
        validated = true;
    });

    bus.publish("cmd_request", cmdRequestJson("uav_1_agent", "TAKEOFF"));
    ASSERT_TRUE(validated);
    EXPECT_EQ(guard.getAgentState("uav_1_agent"), FlightState::ARMED);

    bus.publish("cmd_ack", ws_protocol::makeCmdAck("uav_1_agent", "TAKEOFF", "ok"));
    EXPECT_EQ(guard.getAgentState("uav_1_agent"), FlightState::ARMED);

    bus.publish("state_update", stateUpdateJson("uav_1_agent", 4, true, true, false));
    EXPECT_EQ(guard.getAgentState("uav_1_agent"), FlightState::TAKEOFF);
}

TEST(GuardModuleTest, RejectsCommandsWhenPx4StateIsInvalid) {
  EventBus bus;
  GuardModule guard;
  ASSERT_TRUE(guard.init(bus));

  json state = json::parse(stateUpdateJson("uav_1_agent", 2, true));
  state["state"]["px4_state_valid"] = false;
  bus.publish("state_update", state.dump());

  std::string rejectedAck;
  bus.subscribe("cmd_ack", [&rejectedAck](const std::string&, const std::any& data) {
    rejectedAck = std::any_cast<std::string>(data);
  });

  bus.publish("cmd_request", cmdRequestJson("uav_1_agent", "ARM"));

  ASSERT_FALSE(rejectedAck.empty());
  const json ack = json::parse(rejectedAck);
  EXPECT_EQ(ack["status"], "error");
  EXPECT_NE(ack["message"].get<std::string>().find("PX4 狀態尚未有效"), std::string::npos);
}

TEST(GuardModuleTest, AllModeRejectsWhenAnyAgentPx4StateIsInvalid) {
  EventBus bus;
  GuardModule guard;
  ASSERT_TRUE(guard.init(bus));

  bus.publish("state_update", stateUpdateJson("uav_1_agent", 2, true));
  json invalidState = json::parse(stateUpdateJson("uav_2_agent", 2, true));
  invalidState["state"]["px4_state_valid"] = false;
  bus.publish("state_update", invalidState.dump());

  std::string rejectedAck;
  bus.subscribe("cmd_ack", [&rejectedAck](const std::string&, const std::any& data) {
    rejectedAck = std::any_cast<std::string>(data);
  });

  bus.publish("cmd_request", cmdRequestJson("all", "ARM"));

  ASSERT_FALSE(rejectedAck.empty());
  const json ack = json::parse(rejectedAck);
  EXPECT_EQ(ack["status"], "error");
  EXPECT_NE(ack["message"].get<std::string>().find("uav_2_agent"), std::string::npos);
  EXPECT_NE(ack["message"].get<std::string>().find("PX4 狀態尚未有效"), std::string::npos);
}

TEST(GuardModuleTest, AllowsSingleArmFromValidDisarmedLandedPx4Offboard) {
  EventBus bus;
  GuardModule guard;
  ASSERT_TRUE(guard.init(bus));

  bus.publish("state_update", stateUpdateJson(
    "uav_1_agent", static_cast<int>(FlightState::IDLE), true, false, true,
    PX4_NAV_STATE_OFFBOARD, true));

  bool validated = false;
  bus.subscribe("cmd_validated", [&validated](const std::string&, const std::any&) {
    validated = true;
  });

  bus.publish("cmd_request", cmdRequestJson("uav_1_agent", "ARM"));

  EXPECT_TRUE(validated);
}

TEST(GuardModuleTest, AllowsSingleTakeoffFromValidArmedLandedPx4Offboard) {
  EventBus bus;
  GuardModule guard;
  ASSERT_TRUE(guard.init(bus));

  bus.publish("state_update", stateUpdateJson(
    "uav_1_agent", static_cast<int>(FlightState::IDLE), true, true, true,
    PX4_NAV_STATE_OFFBOARD, true));

  bool validated = false;
  bus.subscribe("cmd_validated", [&validated](const std::string&, const std::any&) {
    validated = true;
  });

  bus.publish("cmd_request", cmdRequestJson("uav_1_agent", "TAKEOFF"));

  EXPECT_TRUE(validated);
}

TEST(GuardModuleTest, RejectsArmWhenAnyPx4PreconditionIsNotMet) {
  struct ArmCase {
    bool armed;
    bool landed;
    uint8_t navState;
  };
  const std::array<ArmCase, 3> cases = {{
    {true, true, PX4_NAV_STATE_OFFBOARD},
    {false, false, PX4_NAV_STATE_OFFBOARD},
    {false, true, 3},
  }};

  for (const auto& testCase : cases) {
    EventBus bus;
    GuardModule guard;
    ASSERT_TRUE(guard.init(bus));
    bus.publish("state_update", stateUpdateJson(
      "uav_1_agent", static_cast<int>(FlightState::IDLE), true, testCase.armed,
      testCase.landed, testCase.navState, true));

    std::string rejectedAck;
    bus.subscribe("cmd_ack", [&rejectedAck](const std::string&, const std::any& data) {
      rejectedAck = std::any_cast<std::string>(data);
    });
    bus.publish("cmd_request", cmdRequestJson("uav_1_agent", "ARM"));

    ASSERT_FALSE(rejectedAck.empty());
    EXPECT_EQ(json::parse(rejectedAck)["status"], "error");
  }
}

TEST(GuardModuleTest, RejectsTakeoffWhenAnyPx4PreconditionIsNotMet) {
  struct TakeoffCase {
    bool armed;
    bool landed;
    uint8_t navState;
  };
  const std::array<TakeoffCase, 3> cases = {{
    {false, true, PX4_NAV_STATE_OFFBOARD},
    {true, false, PX4_NAV_STATE_OFFBOARD},
    {true, true, 3},
  }};

  for (const auto& testCase : cases) {
    EventBus bus;
    GuardModule guard;
    ASSERT_TRUE(guard.init(bus));
    bus.publish("state_update", stateUpdateJson(
      "uav_1_agent", static_cast<int>(FlightState::IDLE), true, testCase.armed,
      testCase.landed, testCase.navState, true));

    std::string rejectedAck;
    bus.subscribe("cmd_ack", [&rejectedAck](const std::string&, const std::any& data) {
      rejectedAck = std::any_cast<std::string>(data);
    });
    bus.publish("cmd_request", cmdRequestJson("uav_1_agent", "TAKEOFF"));

    ASSERT_FALSE(rejectedAck.empty());
    EXPECT_EQ(json::parse(rejectedAck)["status"], "error");
  }
}

TEST(GuardModuleTest, AllModeRejectsTakeoffWhenAnyAgentIsNotPx4Armed) {
  EventBus bus;
  GuardModule guard;
  ASSERT_TRUE(guard.init(bus));

  bus.publish("state_update", stateUpdateJson(
    "uav_1_agent", static_cast<int>(FlightState::IDLE), true, true, true,
    PX4_NAV_STATE_OFFBOARD, true));
  bus.publish("state_update", stateUpdateJson(
    "uav_2_agent", static_cast<int>(FlightState::IDLE), true, false, true,
    PX4_NAV_STATE_OFFBOARD, true));

  std::string rejectedAck;
  bus.subscribe("cmd_ack", [&rejectedAck](const std::string&, const std::any& data) {
    rejectedAck = std::any_cast<std::string>(data);
  });

  bus.publish("cmd_request", cmdRequestJson("all", "TAKEOFF"));

  ASSERT_FALSE(rejectedAck.empty());
  const json ack = json::parse(rejectedAck);
  EXPECT_EQ(ack["status"], "error");
  EXPECT_NE(ack["message"].get<std::string>().find("uav_2_agent"), std::string::npos);
}
