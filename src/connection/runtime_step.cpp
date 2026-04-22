#include "connection/runtime_step.h"

#include <utility>

#include "broker/broker.h"
#include "client_session/client_session.h"
#include "connection/connection_flow_support.h"
#include "connection/connection_session.h"

namespace mqtt {

namespace {

void append_frame(ConnectionSession &session, WriteBuffer frame) {
  session.pending_write_frames().push_back(std::move(frame));
}

RuntimeOutcome protocol_error(ConnectionSession &session) {
  session.disconnect_state().clean_disconnect = true;
  session.disconnect_state().reason_code = ReasonCode::ProtocolError;
  append_frame(session, encode_disconnect_packet(ReasonCode::ProtocolError));
  return RuntimeOutcome::DisconnectError;
}

} // namespace

RuntimeOutcome process_runtime_packet(ConnectionSession &session, Broker &broker,
                                      const AnyPacket &packet) {
  ClientSession *client_session = session.client_session();
  if (client_session == nullptr) {
    return protocol_error(session);
  }

  if (std::holds_alternative<PublishPacket>(packet)) {
    InboundPublishResult publish_result =
        client_session->on_publish(std::get<PublishPacket>(packet));
    if (publish_result.routable_message.has_value()) {
      Message routable_message = std::move(*publish_result.routable_message);
      const ReasonCode publish_reason =
          broker.handle_publish(routable_message, client_session->client_id(),
                                client_session->username(),
                                client_session->topic_alias_table());
      if (publish_reason == ReasonCode::ProtocolError) {
        return protocol_error(session);
      }
    }
    for (WriteBuffer &frame : publish_result.response_frames) {
      append_frame(session, std::move(frame));
    }
    return RuntimeOutcome::Continuing;
  }

  if (std::holds_alternative<SubscribePacket>(packet)) {
    const SubackPacket suback =
        broker.handle_subscribe(client_session->client_id(),
                                std::get<SubscribePacket>(packet));
    append_frame(session, encode_suback_packet(suback));
    return RuntimeOutcome::Continuing;
  }

  if (std::holds_alternative<UnsubscribePacket>(packet)) {
    const UnsubackPacket unsuback =
        broker.handle_unsubscribe(client_session->client_id(),
                                  std::get<UnsubscribePacket>(packet));
    append_frame(session, encode_unsuback_packet(unsuback));
    return RuntimeOutcome::Continuing;
  }

  if (std::holds_alternative<PingreqPacket>(packet)) {
    append_frame(session, encode_pingresp_packet());
    return RuntimeOutcome::Continuing;
  }

  if (std::holds_alternative<DisconnectPacket>(packet)) {
    const DisconnectPacket &disconnect_packet = std::get<DisconnectPacket>(packet);
    const std::optional<uint32_t> expiry_override =
        find_session_expiry_override(disconnect_packet.properties);
    if (!broker.is_disconnect_expiry_override_valid(client_session->client_id(),
                                                    expiry_override)) {
      return protocol_error(session);
    }

    session.disconnect_state().clean_disconnect = true;
    session.disconnect_state().reason_code = disconnect_packet.reason_code;
    session.disconnect_state().expiry_override = expiry_override;
    return RuntimeOutcome::DisconnectClean;
  }

  if (std::holds_alternative<AuthPacket>(packet)) {
    try {
      const AuthResult auth_result =
          client_session->on_auth(std::get<AuthPacket>(packet));
      if (auth_result.status == AuthStatus::Failure) {
        session.disconnect_state().clean_disconnect = true;
        session.disconnect_state().reason_code = auth_result.reason_code;
        append_frame(session, encode_disconnect_packet(auth_result.reason_code));
        return RuntimeOutcome::DisconnectError;
      }

      append_frame(session,
                   encode_auth_packet(
                       auth_result.reason_code,
                       build_auth_properties(client_session->negotiated_auth_method(),
                                             auth_result.auth_data, true)));
      return RuntimeOutcome::Continuing;
    } catch (...) {
      return protocol_error(session);
    }
  }

  return protocol_error(session);
}

} // namespace mqtt
