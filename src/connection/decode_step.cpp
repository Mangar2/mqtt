#include "connection/decode_step.h"

#include "codec/codec_error.h"
#include "connection/connection_error.h"
#include "connection/connection_flow_support.h"
#include "connection/connection_session.h"
#include "connection/handshake_step.h"
#include "connection/runtime_step.h"

namespace mqtt {

DecodeOutcome decode_one_packet(ConnectionSession &session, Broker &broker) {
  try {
    std::optional<AnyPacket> packet = try_decode_packet(session.stream_buffer());
    if (!packet.has_value()) {
      return DecodeOutcome::NeedMore;
    }

    if (session.phase() == ConnectionSession::Phase::Handshake) {
      const HandshakeOutcome outcome =
          process_handshake_packet(session, broker, *packet);
      return (outcome == HandshakeOutcome::Rejected) ? DecodeOutcome::ProtocolError
                                                     : DecodeOutcome::Processed;
    }

    if (session.phase() != ConnectionSession::Phase::Connected) {
      return DecodeOutcome::Disconnected;
    }

    const RuntimeOutcome outcome = process_runtime_packet(session, broker, *packet);
    if (outcome == RuntimeOutcome::Continuing) {
      return DecodeOutcome::Processed;
    }
    return DecodeOutcome::Disconnected;
  } catch (const CodecException &codec_exception) {
    session.disconnect_state().clean_disconnect = true;
    session.disconnect_state().reason_code =
        (session.phase() == ConnectionSession::Phase::Handshake)
            ? map_codec_error_to_connect_reason(codec_exception.error())
            : map_codec_error_to_runtime_reason(codec_exception.error());
    return DecodeOutcome::ProtocolError;
  } catch (...) {
    session.disconnect_state().clean_disconnect = true;
    session.disconnect_state().reason_code = ReasonCode::ProtocolError;
    return DecodeOutcome::ProtocolError;
  }
}

} // namespace mqtt
