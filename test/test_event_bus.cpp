/**
 * @file test_event_bus.cpp
 * @brief EventBus 單元測試 — 巢狀發布與命令回執鏈
 * @author Taiphoon GCS Team
 * @date 2026-08-05
 */
#include "core/event_bus.hpp"
#include <gtest/gtest.h>
#include <any>
#include <string>
#include <stdexcept>

TEST(EventBusTest, SubscriberExceptionDoesNotStopLaterSubscribers) {
  EventBus bus;
  bool laterCalled = false;

  bus.subscribe("test_topic", [](const std::string&, const std::any&) {
    throw std::runtime_error("first subscriber failure");
  });
  bus.subscribe("test_topic", [&laterCalled](const std::string&, const std::any&) {
    laterCalled = true;
  });

  EXPECT_NO_THROW(bus.publish("test_topic", std::string("data")));
  EXPECT_TRUE(laterCalled);
}

TEST(EventBusTest, NestedPublishDoesNotDeadlock) {
  EventBus bus;
  bool nestedCalled = false;

  bus.subscribe("level1", [&bus](const std::string&, const std::any&) {
    bus.publish("level2", std::string("nested"));
  });
  bus.subscribe("level2", [&nestedCalled](const std::string&, const std::any& data) {
    nestedCalled = std::any_cast<std::string>(data) == "nested";
  });

  bus.publish("level1", std::string("start"));
  EXPECT_TRUE(nestedCalled);
}

TEST(EventBusTest, CommandAckChainWorks) {
  EventBus bus;
  std::string receivedAck;

  bus.subscribe("cmd_ack", [&receivedAck](const std::string&, const std::any& data) {
    receivedAck = std::any_cast<std::string>(data);
  });
  bus.subscribe("cmd_validated", [&bus](const std::string&, const std::any&) {
    bus.publish("cmd_ack", std::string("ack"));
  });
  bus.subscribe("cmd_request", [&bus](const std::string&, const std::any&) {
    bus.publish("cmd_validated", std::string("validated"));
  });

  bus.publish("cmd_request", std::string("request"));
  EXPECT_EQ(receivedAck, "ack");
}
