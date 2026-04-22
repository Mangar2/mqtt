#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "auth/authenticator.h"
#include "broker/broker_config.h"
#include "client_session/client_session.h"
#include "connection/connection_session.h"
#include "network/tcp_connection.h"
#include "outbound_queue/outbound_queue.h"
#include "store/inflight_store.h"

using namespace mqtt;

TEST_CASE("session_owns_subobjects_and_supports_phase_transition", "[connection]") {
  BrokerConfig config;
  config.topic_alias_maximum = 17U;
  config.receive_maximum = 13U;

  auto connection =
      std::make_unique<TcpConnection>(static_cast<SocketHandle>(k_invalid_socket));

  ConnectionSession session(std::move(connection), nullptr, false, config);

  CHECK_FALSE(session.is_websocket());
  CHECK(session.phase() == ConnectionSession::Phase::Handshake);
  CHECK(session.topic_alias_table().max_aliases() == 17U);
  CHECK(session.inbound_receive_window().max() == 13U);
  CHECK(session.ws_transport() == nullptr);

  session.set_phase(ConnectionSession::Phase::Connected);
  CHECK(session.phase() == ConnectionSession::Phase::Connected);

  // Accessors are expected to expose usable owned objects for per-fd jobs.
  CHECK(session.connection().fd() == k_invalid_socket);
  CHECK(session.stream_buffer().is_empty());
  CHECK_FALSE(session.disconnect_state().clean_disconnect);

  auto outbound_queue = std::make_shared<OutboundQueue>(16U);
  InflightStore inflight_store;
  auto authenticator = std::make_shared<CallbackAuthenticator>(
      [](const ConnectPacket & /*unused*/) {
        return AuthResult{.status = AuthStatus::Success,
                          .reason_code = ReasonCode::Success,
                          .auth_data = std::nullopt};
      });

  auto client_session = std::make_unique<ClientSession>(
      "cid", "", authenticator, outbound_queue, inflight_store,
      30U, 10U, config.topic_alias_maximum);
  session.install_client_session(std::move(client_session));

  REQUIRE(session.client_session() != nullptr);
  REQUIRE(session.outbound_queue() != nullptr);
  CHECK(session.outbound_queue() == outbound_queue);
}
