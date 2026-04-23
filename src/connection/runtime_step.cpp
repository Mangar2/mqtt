#include "connection/runtime_step.h"

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "broker/broker.h"
#include "codec/packet/publish_codec.h"
#include "client_session/client_session.h"
#include "connection/connection_flow_support.h"
#include "connection/connection_error.h"
#include "connection/connection_session.h"
#include "monitoring/structured_tracer.h"
#include "qos/qos_error.h"

namespace mqtt {

namespace {

void append_frame(ConnectionSession &session, WriteBuffer frame) {
  session.pending_write_frames().push_back(std::move(frame));
}

std::string packet_name(const AnyPacket &packet) {
  return std::visit(
      [](const auto &typed_packet) -> std::string {
        using Packet = std::decay_t<decltype(typed_packet)>;
        if constexpr (std::is_same_v<Packet, ConnectPacket>) {
          return "CONNECT";
        }
        if constexpr (std::is_same_v<Packet, ConnackPacket>) {
          return "CONNACK";
        }
        if constexpr (std::is_same_v<Packet, PublishPacket>) {
          return "PUBLISH";
        }
        if constexpr (std::is_same_v<Packet, PubackPacket>) {
          return "PUBACK";
        }
        if constexpr (std::is_same_v<Packet, PubrecPacket>) {
          return "PUBREC";
        }
        if constexpr (std::is_same_v<Packet, PubrelPacket>) {
          return "PUBREL";
        }
        if constexpr (std::is_same_v<Packet, PubcompPacket>) {
          return "PUBCOMP";
        }
        if constexpr (std::is_same_v<Packet, SubscribePacket>) {
          return "SUBSCRIBE";
        }
        if constexpr (std::is_same_v<Packet, SubackPacket>) {
          return "SUBACK";
        }
        if constexpr (std::is_same_v<Packet, UnsubscribePacket>) {
          return "UNSUBSCRIBE";
        }
        if constexpr (std::is_same_v<Packet, UnsubackPacket>) {
          return "UNSUBACK";
        }
        if constexpr (std::is_same_v<Packet, PingreqPacket>) {
          return "PINGREQ";
        }
        if constexpr (std::is_same_v<Packet, PingrespPacket>) {
          return "PINGRESP";
        }
        if constexpr (std::is_same_v<Packet, DisconnectPacket>) {
          return "DISCONNECT";
        }
        if constexpr (std::is_same_v<Packet, AuthPacket>) {
          return "AUTH";
        }
        return "UNKNOWN";
      },
      packet);
}

void trace_connection_warning(Broker &broker, std::string_view client_id,
                              std::string_view info,
                              const AnyPacket *packet = nullptr,
                              std::string_view detail = {}) {
  TRACE_GUARD((&broker.structured_tracer()), TraceLevel::Warning, "connection") {
    TraceEvent event;
    event.level = TraceLevel::Warning;
    event.module = "connection";
    event.info = std::string(info);
    event.data.emplace_back("client_id", std::string(client_id));
    if (packet != nullptr) {
      event.data.emplace_back("packet_type", packet_name(*packet));
    }
    if (!detail.empty()) {
      event.detail = std::string(detail);
    }
    broker.structured_tracer().emit(event);
  }
}

void trace_connection_packet(Broker &broker, std::string_view client_id,
                             std::string_view info,
                             const AnyPacket &packet) {
  TRACE_GUARD((&broker.structured_tracer()), TraceLevel::Trace, "connection") {
    TraceEvent event;
    event.level = TraceLevel::Trace;
    event.module = "connection";
    event.info = std::string(info);
    event.data.emplace_back("client_id", std::string(client_id));
    event.data.emplace_back("packet_type", packet_name(packet));
    broker.structured_tracer().emit(event);
  }
}

RuntimeOutcome protocol_error(ConnectionSession &session, Broker &broker,
                              const AnyPacket *packet = nullptr,
                              std::string_view detail = {}) {
  std::string_view client_id = session.connect_result().client_id;
  if (const ClientSession *client_session = session.client_session();
      client_session != nullptr) {
    client_id = client_session->client_id();
  }

  trace_connection_warning(broker, client_id, "runtime_protocol_error", packet,
                           detail);

  session.disconnect_state().clean_disconnect = true;
  session.disconnect_state().reason_code = ReasonCode::ProtocolError;
  const std::string_view reason_text =
      detail.empty() ? std::string_view{"runtime_protocol_error"} : detail;
  std::vector<Property> properties;
  if (session.connect_packet().has_value()) {
    properties = build_protocol_error_disconnect_properties(
        *session.connect_packet(), reason_text);
  }
  append_frame(
      session,
      encode_disconnect_packet(ReasonCode::ProtocolError, properties));
  return RuntimeOutcome::DisconnectError;
}

} // namespace

RuntimeOutcome process_runtime_packet(ConnectionSession &session, Broker &broker,
                                      const AnyPacket &packet) {
  ClientSession *client_session = session.client_session();
  if (client_session == nullptr) {
    return protocol_error(session, broker, &packet, "missing_client_session");
  }

  trace_connection_packet(broker, client_session->client_id(),
                          "runtime_packet_received", packet);
  client_session->keep_alive_timer().reset();

  if (std::holds_alternative<PublishPacket>(packet)) {
    const PublishPacket &publish_packet = std::get<PublishPacket>(packet);
    const bool tracks_inbound_inflight = publish_packet.qos == QoS::ExactlyOnce;
    if (tracks_inbound_inflight && !session.inbound_receive_window().acquire()) {
      session.disconnect_state().clean_disconnect = true;
      session.disconnect_state().reason_code = ReasonCode::ReceiveMaximumExceeded;
      append_frame(session,
                   encode_disconnect_packet(ReasonCode::ReceiveMaximumExceeded));
      trace_connection_warning(broker, client_session->client_id(),
                               "runtime_receive_maximum_exceeded", &packet);
      return RuntimeOutcome::DisconnectError;
    }

    InboundPublishResult publish_result = client_session->on_publish(publish_packet);
    if (tracks_inbound_inflight && !publish_result.routable_message.has_value()) {
      session.inbound_receive_window().release();
    }

    if (publish_result.routable_message.has_value()) {
      Message routable_message = std::move(*publish_result.routable_message);
      const ReasonCode publish_reason =
          broker.handle_publish(routable_message, client_session->client_id(),
                                client_session->username(),
                                client_session->topic_alias_table());
      if (publish_reason == ReasonCode::ProtocolError) {
        if (tracks_inbound_inflight) {
          session.inbound_receive_window().release();
        }
        return protocol_error(session, broker, &packet,
                              "publish_facade_protocol_error");
      }

      if (publish_packet.qos == QoS::AtLeastOnce &&
          publish_packet.packet_id.has_value() &&
          !publish_result.response_frames.empty()) {
        WriteBuffer frame;
        encode_puback(frame, PubackPacket{.packet_id = publish_packet.packet_id.value(),
                                          .reason_code = publish_reason,
                                          .properties = {}});
        publish_result.response_frames[0] = std::move(frame);
      }

      if (publish_packet.qos == QoS::ExactlyOnce &&
          publish_packet.packet_id.has_value() &&
          !publish_result.response_frames.empty()) {
        ReasonCode pubrec_reason = publish_reason;
        if (pubrec_reason == ReasonCode::NoMatchingSubscribers) {
          pubrec_reason = ReasonCode::Success;
        }
        WriteBuffer frame;
        encode_pubrec(frame, PubrecPacket{.packet_id = publish_packet.packet_id.value(),
                                          .reason_code = pubrec_reason,
                                          .properties = {}});
        publish_result.response_frames[0] = std::move(frame);
      }

      if (publish_packet.qos == QoS::ExactlyOnce && is_error(publish_reason) &&
          publish_packet.packet_id.has_value()) {
        client_session->abort_inbound_qos2(publish_packet.packet_id.value());
        session.inbound_receive_window().release();
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

  if (std::holds_alternative<PubackPacket>(packet)) {
    client_session->on_puback(std::get<PubackPacket>(packet));
    return RuntimeOutcome::Continuing;
  }

  if (std::holds_alternative<PubrecPacket>(packet)) {
    append_frame(session, client_session->on_pubrec(std::get<PubrecPacket>(packet)));
    return RuntimeOutcome::Continuing;
  }

  if (std::holds_alternative<PubrelPacket>(packet)) {
    WriteBuffer pubcomp_frame;
    try {
      pubcomp_frame = client_session->on_pubrel(std::get<PubrelPacket>(packet));
    } catch (const QosException &exception) {
      if (exception.error() != QosError::UnexpectedPacketId) {
        return protocol_error(session, broker, &packet, exception.what());
      }

      const uint16_t packet_id = std::get<PubrelPacket>(packet).packet_id;
      encode_pubcomp(pubcomp_frame,
                     PubcompPacket{.packet_id = packet_id,
                                   .reason_code = ReasonCode::PacketIdentifierNotFound,
                                   .properties = {}});
    }

    try {
      session.inbound_receive_window().release();
    } catch (const ConnectionException &) {
    }

    append_frame(session, std::move(pubcomp_frame));
    return RuntimeOutcome::Continuing;
  }

  if (std::holds_alternative<PubcompPacket>(packet)) {
    client_session->on_pubcomp(std::get<PubcompPacket>(packet));
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
      return protocol_error(session, broker, &packet,
                            "invalid_disconnect_expiry_override");
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
    } catch (const std::exception &exception) {
      return protocol_error(session, broker, &packet, exception.what());
    } catch (...) {
      return protocol_error(session, broker, &packet, "auth_exception");
    }
  }

  return protocol_error(session, broker, &packet, "unexpected_packet_in_runtime");
}

} // namespace mqtt
