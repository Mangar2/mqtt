#include "connection/close_step.h"

#include <chrono>
#include <string>

#include "broker/broker.h"
#include "client_session/client_session.h"
#include "connection/connection_session.h"

namespace mqtt {

void finalize_close(ConnectionSession &session, Broker &broker) {
  std::string client_id;
  if (session.client_session() != nullptr) {
    client_id = std::string(session.client_session()->client_id());
  } else if (!session.connect_result().client_id.empty()) {
    client_id = session.connect_result().client_id;
  }

  if (client_id.empty()) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (session.disconnect_state().clean_disconnect) {
    broker.handle_disconnect(client_id, session.disconnect_state().reason_code,
                             session.disconnect_state().expiry_override, now,
                             session.outbound_queue());
    return;
  }

  broker.handle_connection_lost(client_id, now, session.outbound_queue());
}

} // namespace mqtt
