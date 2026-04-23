#include "connection/handshake_step.h"

#include <chrono>
#include <memory>
#include <optional>

#include "auth/authenticator.h"
#include "broker/broker.h"
#include "client_session/client_session.h"
#include "connection/connection_flow_support.h"
#include "connection/connection_session.h"
#include "outbound_queue/outbound_queue.h"

namespace mqtt {

namespace {

constexpr uint16_t k_default_receive_maximum = 65535U;

std::optional<uint16_t> find_server_keep_alive(
    const std::vector<Property> &properties) {
  for (const Property &property : properties) {
    if (property.id != PropertyId::ServerKeepAlive) {
      continue;
    }
    if (const auto *keep_alive_ptr =
            std::get_if<TwoByteInteger>(&property.value)) {
      return *keep_alive_ptr;
    }
  }
  return std::nullopt;
}

void append_frame(ConnectionSession &session, WriteBuffer frame) {
  session.pending_write_frames().push_back(std::move(frame));
}

HandshakeOutcome finalize_connect_success(ConnectionSession &session,
                                         Broker &broker,
                                         const ConnectPacket &packet,
                                         const ConnectResult &connect_result) {
  auto outbound_queue = std::make_shared<OutboundQueue>();
  broker.register_connection(connect_result.client_id, outbound_queue,
                             static_cast<int>(session.connection().fd()));

  std::shared_ptr<IAuthenticator> authenticator(
      &broker.authenticator(), [](IAuthenticator * /*unused*/) {});
  const std::string username =
      packet.username.has_value() ? packet.username->value : "";
  const uint16_t outbound_receive_maximum =
      find_receive_maximum(packet.properties).value_or(k_default_receive_maximum);
    const uint16_t effective_keep_alive =
      find_server_keep_alive(connect_result.connack_properties)
        .value_or(packet.keep_alive);
  const uint32_t maximum_packet_size =
      find_maximum_packet_size(packet.properties).value_or(0U);

  auto client_session = std::make_unique<ClientSession>(
      connect_result.client_id, username, std::move(authenticator),
      outbound_queue, broker.session_manager().inflight_store(),
      effective_keep_alive, outbound_receive_maximum,
      session.topic_alias_table().max_aliases(), std::chrono::seconds(20),
      maximum_packet_size, connect_result.auth_method);

  if (connect_result.session_present) {
    client_session->mark_session_resumed();
  }
  client_session->keep_alive_timer().reset();
  session.install_client_session(std::move(client_session));

  if (connect_result.session_present) {
    broker.flush_offline_queue(connect_result.client_id);
  }

  append_frame(session, encode_connack_packet(ConnackPacket{
                        .session_present = connect_result.session_present,
                        .reason_code = ReasonCode::Success,
                        .properties = connect_result.connack_properties,
                      }));

  session.set_phase(ConnectionSession::Phase::Connected);
  return HandshakeOutcome::ConnectAccepted;
}

HandshakeOutcome on_connect_packet(ConnectionSession &session, Broker &broker,
                                   const ConnectPacket &packet) {
  session.connect_packet() = packet;
  session.connect_result() = broker.handle_connect(
      packet, [&session, &broker]() {
        session.request_session_takeover();
        if (!session.connect_result().client_id.empty()) {
          broker.wake_outbound(session.connect_result().client_id);
        }
      });

  ConnectResult &connect_result = session.connect_result();
  if (connect_result.auth_status == AuthStatus::Continue) {
    append_frame(session,
                 encode_auth_packet(connect_result.reason_code,
                                    build_auth_properties(
                                        connect_result.auth_method,
                                        connect_result.auth_data,
                                        !connect_result.auth_method.empty())));
    return HandshakeOutcome::Continuing;
  }

  if (connect_result.auth_status != AuthStatus::Success ||
      connect_result.reason_code != ReasonCode::Success) {
    append_frame(session, encode_connack_packet(ConnackPacket{
                          .session_present = false,
                          .reason_code = connect_result.reason_code,
                          .properties = connect_result.connack_properties,
                        }));
    session.disconnect_state().clean_disconnect = true;
    session.disconnect_state().reason_code = connect_result.reason_code;
    return HandshakeOutcome::Rejected;
  }

  return finalize_connect_success(session, broker, packet, connect_result);
}

HandshakeOutcome on_auth_packet(ConnectionSession &session, Broker &broker,
                                const AuthPacket &packet) {
  if (!session.connect_packet().has_value() ||
      session.connect_result().client_id.empty()) {
    append_frame(session, encode_connack_packet(ConnackPacket{
                          .session_present = false,
                          .reason_code = ReasonCode::ProtocolError,
                          .properties = {},
                        }));
    session.disconnect_state().clean_disconnect = true;
    session.disconnect_state().reason_code = ReasonCode::ProtocolError;
    return HandshakeOutcome::Rejected;
  }

  ConnectResult auth_result =
      broker.handle_auth_packet(session.connect_result().client_id, packet);
  session.connect_result() = auth_result;

  if (auth_result.auth_status == AuthStatus::Continue) {
    append_frame(session,
                 encode_auth_packet(auth_result.reason_code,
                                    build_auth_properties(
                                        auth_result.auth_method,
                                        auth_result.auth_data,
                                        !auth_result.auth_method.empty())));
    return HandshakeOutcome::Continuing;
  }

  if (auth_result.auth_status != AuthStatus::Success ||
      auth_result.reason_code != ReasonCode::Success) {
    append_frame(session, encode_connack_packet(ConnackPacket{
                          .session_present = false,
                          .reason_code = auth_result.reason_code,
                          .properties = auth_result.connack_properties,
                        }));
    session.disconnect_state().clean_disconnect = true;
    session.disconnect_state().reason_code = auth_result.reason_code;
    return HandshakeOutcome::Rejected;
  }

  return finalize_connect_success(session, broker, *session.connect_packet(),
                                  auth_result);
}

} // namespace

HandshakeOutcome process_handshake_packet(ConnectionSession &session,
                                          Broker &broker,
                                          const AnyPacket &packet) {
  if (std::holds_alternative<ConnectPacket>(packet)) {
    return on_connect_packet(session, broker, std::get<ConnectPacket>(packet));
  }

  if (std::holds_alternative<AuthPacket>(packet)) {
    return on_auth_packet(session, broker, std::get<AuthPacket>(packet));
  }

  append_frame(session, encode_connack_packet(ConnackPacket{
                        .session_present = false,
                        .reason_code = ReasonCode::ProtocolError,
                        .properties = {},
                      }));
  session.disconnect_state().clean_disconnect = true;
  session.disconnect_state().reason_code = ReasonCode::ProtocolError;
  return HandshakeOutcome::Rejected;
}

} // namespace mqtt
