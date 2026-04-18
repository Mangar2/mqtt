#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "broker/broker.h"
#include "broker/broker_config.h"
#include "connection/client_handler.h"
#include "connection/connection_error.h"
#include "connection/connection_state.h"
#include "connection/keep_alive_timer.h"
#include "connection/receive_maximum.h"
#include "connection/topic_alias_table.h"
#include "network/tcp_connection.h"

namespace mqtt {

//  ConnectionStateMachine
//

TEST_CASE("state_initial_is_connecting", "[connection]") {
  ConnectionStateMachine fsm;
  CHECK(fsm.state() == ConnectionState::Connecting);
}

TEST_CASE("on_connect_transitions_to_connected", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  CHECK(fsm.state() == ConnectionState::Connected);
}

TEST_CASE("on_connect_throws_duplicate", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  try {
    fsm.on_connect();
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::DuplicateConnect);
  }
}

TEST_CASE("on_connect_throws_in_disconnecting", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  fsm.on_disconnect();
  try {
    fsm.on_connect();
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::InvalidState);
  }
}

TEST_CASE("on_connect_throws_in_closed", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.close();
  try {
    fsm.on_connect();
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::InvalidState);
  }
}

TEST_CASE("on_disconnect_transitions_to_disconnecting", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  fsm.on_disconnect();
  CHECK(fsm.state() == ConnectionState::Disconnecting);
}

TEST_CASE("on_disconnect_throws_if_not_connected", "[connection]") {
  ConnectionStateMachine fsm;
  try {
    fsm.on_disconnect();
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::InvalidState);
  }
}

TEST_CASE("on_connection_lost_transitions_to_closed", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  fsm.on_connection_lost();
  CHECK(fsm.state() == ConnectionState::Closed);
}

TEST_CASE("on_connection_lost_from_connecting", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connection_lost();
  CHECK(fsm.state() == ConnectionState::Closed);
}

TEST_CASE("close_transitions_to_closed", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  fsm.close();
  CHECK(fsm.state() == ConnectionState::Closed);
}

TEST_CASE("is_connected_true_when_connected", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  CHECK(fsm.is_connected());
}

TEST_CASE("is_connected_false_when_not_connected", "[connection]") {
  ConnectionStateMachine fsm;
  CHECK_FALSE(fsm.is_connected());
}

TEST_CASE("enforce_not_connecting_passes_in_connected", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  CHECK_NOTHROW(fsm.enforce_not_connecting());
}

TEST_CASE("enforce_not_connecting_throws_in_connecting", "[connection]") {
  ConnectionStateMachine fsm;
  try {
    fsm.enforce_not_connecting();
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::ConnectRequired);
  }
}

TEST_CASE("enforce_not_connecting_throws_in_disconnecting", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  fsm.on_disconnect();
  try {
    fsm.enforce_not_connecting();
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::InvalidState);
  }
}

//  KeepAliveTimer
//

TEST_CASE("disabled_when_keep_alive_is_zero", "[connection]") {
  KeepAliveTimer timer(0U);
  CHECK_FALSE(timer.is_enabled());
}

TEST_CASE("not_expired_immediately_after_construction", "[connection]") {
  KeepAliveTimer timer(60U);
  CHECK_FALSE(timer.is_expired());
}

TEST_CASE("not_expired_after_reset", "[connection]") {
  KeepAliveTimer timer(60U);
  timer.reset();
  CHECK_FALSE(timer.is_expired());
}

TEST_CASE("disabled_timer_never_expires", "[connection]") {
  KeepAliveTimer timer(0U);
  CHECK_FALSE(timer.is_expired());
  timer.reset();
  CHECK_FALSE(timer.is_expired());
}

TEST_CASE("enabled_when_keep_alive_nonzero", "[connection]") {
  KeepAliveTimer timer(10U);
  CHECK(timer.is_enabled());
}

//  TopicAliasTable
//

TEST_CASE("max_aliases_returns_configured_value", "[connection]") {
  TopicAliasTable tab(10U);
  CHECK(tab.max_aliases() == 10U);
}

TEST_CASE("set_and_get_inbound_alias", "[connection]") {
  TopicAliasTable tab(10U);
  tab.set_inbound(1U, "a/b");
  CHECK(tab.get_inbound(1U) == "a/b");
}

TEST_CASE("set_and_get_outbound_alias", "[connection]") {
  TopicAliasTable tab(10U);
  tab.set_outbound("a/b", 1U);
  CHECK(tab.get_outbound("a/b") == 1U);
}

TEST_CASE("get_outbound_unknown_returns_nullopt", "[connection]") {
  TopicAliasTable tab(10U);
  CHECK(tab.get_outbound("x") == std::nullopt);
}

TEST_CASE("get_inbound_unknown_throws", "[connection]") {
  TopicAliasTable tab(10U);
  try {
    (void)tab.get_inbound(1U);
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::AliasNotFound);
  }
}

TEST_CASE("set_inbound_alias_zero_throws", "[connection]") {
  TopicAliasTable tab(10U);
  try {
    tab.set_inbound(0U, "a");
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::AliasOutOfRange);
  }
}

TEST_CASE("set_inbound_alias_exceeds_max_throws", "[connection]") {
  TopicAliasTable tab(5U);
  try {
    tab.set_inbound(6U, "a");
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::AliasOutOfRange);
  }
}

TEST_CASE("set_outbound_alias_zero_throws", "[connection]") {
  TopicAliasTable tab(10U);
  try {
    tab.set_outbound("a", 0U);
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::AliasOutOfRange);
  }
}

TEST_CASE("set_outbound_alias_exceeds_max_throws", "[connection]") {
  TopicAliasTable tab(5U);
  try {
    tab.set_outbound("a", 6U);
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::AliasOutOfRange);
  }
}

TEST_CASE("get_inbound_alias_exceeds_max_throws", "[connection]") {
  TopicAliasTable tab(5U);
  try {
    (void)tab.get_inbound(6U);
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::AliasOutOfRange);
  }
}

TEST_CASE("reset_clears_inbound_mappings", "[connection]") {
  TopicAliasTable tab(10U);
  tab.set_inbound(1U, "a");
  tab.reset();
  try {
    (void)tab.get_inbound(1U);
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::AliasNotFound);
  }
}

TEST_CASE("reset_clears_outbound_mappings", "[connection]") {
  TopicAliasTable tab(10U);
  tab.set_outbound("a", 1U);
  tab.reset();
  CHECK(tab.get_outbound("a") == std::nullopt);
}

TEST_CASE("overwrite_inbound_alias", "[connection]") {
  TopicAliasTable tab(10U);
  tab.set_inbound(1U, "a");
  tab.set_inbound(1U, "b");
  CHECK(tab.get_inbound(1U) == "b");
}

TEST_CASE("max_alias_zero_disables_inbound", "[connection]") {
  TopicAliasTable tab(0U);
  try {
    tab.set_inbound(1U, "a");
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::AliasOutOfRange);
  }
}

//  ReceiveMaximum
//

TEST_CASE("max_returns_configured_value", "[connection]") {
  ReceiveMaximum recv(10U);
  CHECK(recv.max() == 10U);
}

TEST_CASE("zero_max_defaults_to_65535", "[connection]") {
  ReceiveMaximum recv(0U);
  CHECK(recv.max() == 65535U);
}

TEST_CASE("acquire_succeeds_within_limit", "[connection]") {
  ReceiveMaximum recv(5U);
  CHECK(recv.acquire());
}

TEST_CASE("acquire_returns_false_at_limit", "[connection]") {
  ReceiveMaximum recv(2U);
  CHECK(recv.acquire());
  CHECK(recv.acquire());
  CHECK_FALSE(recv.acquire());
}

TEST_CASE("is_paused_true_when_limit_reached", "[connection]") {
  ReceiveMaximum recv(1U);
  (void)recv.acquire();
  CHECK(recv.is_paused());
}

TEST_CASE("is_paused_false_initially", "[connection]") {
  ReceiveMaximum recv(10U);
  CHECK_FALSE(recv.is_paused());
}

TEST_CASE("available_decreases_after_acquire", "[connection]") {
  ReceiveMaximum recv(5U);
  (void)recv.acquire();
  (void)recv.acquire();
  CHECK(recv.available() == 3U);
}

TEST_CASE("available_increases_after_release", "[connection]") {
  ReceiveMaximum recv(2U);
  (void)recv.acquire();
  (void)recv.acquire();
  recv.release();
  CHECK(recv.available() == 1U);
}

TEST_CASE("release_restores_capacity", "[connection]") {
  ReceiveMaximum recv(1U);
  (void)recv.acquire();
  recv.release();
  CHECK(recv.acquire());
}

TEST_CASE("release_throws_when_inflight_zero", "[connection]") {
  ReceiveMaximum recv(5U);
  try {
    recv.release();
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::InvalidState);
  }
}

//  ClientHandler placeholder (Module 17)
//

TEST_CASE("client_handler_run_with_connection_pointer", "[connection]") {
  BrokerConfig cfg;
  Broker broker(cfg);
  ClientHandler handler;

  auto conn = std::make_unique<TcpConnection>(k_invalid_socket);
  CHECK_NOTHROW(handler.run(std::move(conn), broker, cfg, false));
}

TEST_CASE("client_handler_run_with_null_connection", "[connection]") {
  BrokerConfig cfg;
  Broker broker(cfg);
  ClientHandler handler;

  std::unique_ptr<TcpConnection> conn;
  CHECK_NOTHROW(handler.run(std::move(conn), broker, cfg, true));
}

} // namespace mqtt
