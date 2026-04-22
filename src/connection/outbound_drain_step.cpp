#include "connection/outbound_drain_step.h"

#include "broker/broker.h"
#include "client_session/client_session.h"
#include "connection/connection_session.h"

namespace mqtt {

void drain_outbound_to_write_buffer(ConnectionSession &session, Broker &broker) {
  (void)broker;
  ClientSession *client_session = session.client_session();
  if (client_session == nullptr) {
    return;
  }

  std::vector<WriteBuffer> frames = client_session->drain_outbound();
  for (WriteBuffer &frame : frames) {
    session.pending_write_frames().push_back(std::move(frame));
  }
}

} // namespace mqtt
