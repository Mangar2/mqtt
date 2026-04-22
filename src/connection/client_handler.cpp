/**
 * @file client_handler.cpp
 * @brief Stateless connection job processors.
 */

#include "connection/client_handler.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "broker/broker.h"
#include "broker/broker_config.h"
#include "client_session/client_session.h"
#include "connection/close_step.h"
#include "connection/connection_flow_support.h"
#include "connection/connection_session.h"
#include "connection/decode_step.h"
#include "connection/outbound_drain_step.h"
#include "executor/job_scheduler.h"
#include "network/connection_slot.h"
#include "network/connection_table.h"
#include "network/io_reactor.h"
#include "network/socket_ops.h"
#include "network/tcp_connection.h"
#include "transport/websocket_transport.h"

namespace mqtt {

namespace {

constexpr std::size_t k_decode_read_chunk_size = 4096U;
constexpr std::size_t k_decode_read_budget_bytes = 64U * 1024U;
constexpr std::size_t k_decode_packet_budget = 32U;

bool append_frame_to_slot(ConnectionSlot &slot, const WriteBuffer &frame,
                          bool is_websocket) {
  if (!is_websocket) {
    return slot.push_write_bytes(
        std::span<const uint8_t>(frame.data(), frame.size()));
  }

  const std::vector<uint8_t> ws_frame = WebSocketTransport::encode_frame(
      std::span<const uint8_t>(frame.data(), frame.size()));
  return slot.push_write_bytes(
      std::span<const uint8_t>(ws_frame.data(), ws_frame.size()));
}

bool move_pending_frames_to_slot(ConnectionSession &session, ConnectionSlot &slot) {
  for (const WriteBuffer &frame : session.pending_write_frames()) {
    if (!append_frame_to_slot(slot, frame, session.is_websocket())) {
      return false;
    }
  }
  session.clear_pending_write_frames();
  return true;
}

bool drain_socket_write_buffer(ConnectionSlot &slot) {
  while (slot.write_size() > 0U) {
    const std::span<const uint8_t> chunk = slot.write_contiguous_bytes();
    if (chunk.empty()) {
      return true;
    }

    std::size_t bytes_written = 0U;
    const IoResult write_result = nb_write(slot.fd(), chunk, &bytes_written);
    if (write_result == IoResult::Ok) {
      if (bytes_written == 0U) {
        return false;
      }
      (void)slot.pop_write_bytes(bytes_written);
      continue;
    }
    if (write_result == IoResult::WouldBlock) {
      return true;
    }
    return false;
  }
  return true;
}

void submit_close(JobScheduler &scheduler, int fd) {
  scheduler.submit(ConnectionJob{.type = JobType::Close,
                                 .connection_fd = fd,
                                 .payload = CloseJobPayload{.immediate = false}});
}

bool prepare_session_takeover_close(ConnectionSession &session) {
  if (!session.consume_session_takeover_request()) {
    return false;
  }

  session.disconnect_state().clean_disconnect = true;
  session.disconnect_state().reason_code = ReasonCode::SessionTakenOver;
  session.pending_write_frames().push_back(
      encode_disconnect_packet(ReasonCode::SessionTakenOver));
  session.set_phase(ConnectionSession::Phase::Closing);
  return true;
}

} // namespace

namespace client_handler {

void process_accept_job(const AcceptJobPayload &payload, ConnectionTable &table,
                        IoReactor &reactor, JobScheduler &scheduler,
                        Broker &broker, const BrokerConfig &config) {
  if (payload.socket_handle == k_invalid_socket) {
    return;
  }

  auto connection = std::make_unique<TcpConnection>(payload.socket_handle);
  if (set_nonblocking(payload.socket_handle) != IoResult::Ok) {
    connection->close();
    return;
  }

  std::unique_ptr<WebSocketTransport> ws_transport;
  if (payload.websocket_connection) {
    try {
      ws_transport = std::make_unique<WebSocketTransport>(*connection);
    } catch (...) {
      connection->close();
      return;
    }
  }

  auto session = std::make_unique<ConnectionSession>(
      std::move(connection), std::move(ws_transport),
      payload.websocket_connection, config);

  const int fd = static_cast<int>(payload.socket_handle);
  if (!table.add(fd, ConnectionSlot(payload.socket_handle), std::move(session))) {
    return;
  }

  reactor.register_connection(
      fd,
      [&scheduler](int active_fd) {
        scheduler.submit(ConnectionJob{.type = JobType::Decode,
                                       .connection_fd = active_fd,
                                       .payload = DecodeJobPayload{}});
      },
      [&scheduler](int active_fd) {
        scheduler.submit(ConnectionJob{.type = JobType::Drain,
                                       .connection_fd = active_fd,
                                       .payload = DrainJobPayload{}});
      });

  scheduler.submit(ConnectionJob{.type = JobType::Decode,
                                 .connection_fd = fd,
                                 .payload = DecodeJobPayload{}});
  (void)broker;
}

void process_decode_job(int fd, ConnectionTable &table, IoReactor &reactor,
                        JobScheduler &scheduler, Broker &broker) {
  ConnectionTable::Entry *entry = table.find(fd);
  if (entry == nullptr || entry->session == nullptr) {
    return;
  }

  ConnectionSession &session = *entry->session;
  ConnectionSlot &slot = entry->slot;

  if (session.phase() == ConnectionSession::Phase::Connected &&
      session.client_session() != nullptr) {
    auto &keep_alive_timer = session.client_session()->keep_alive_timer();
    if (keep_alive_timer.is_enabled() && keep_alive_timer.is_expired()) {
      session.disconnect_state().clean_disconnect = false;
      session.disconnect_state().reason_code = ReasonCode::KeepAliveTimeout;
      submit_close(scheduler, fd);
      return;
    }
  }

  bool close_after_flush = prepare_session_takeover_close(session);

  std::array<uint8_t, k_decode_read_chunk_size> read_chunk{};
  std::size_t total_read = 0U;
  while (!close_after_flush && total_read < k_decode_read_budget_bytes) {
    std::size_t bytes_read = 0U;
    const IoResult read_result = nb_read(
        slot.fd(), std::span<uint8_t>(read_chunk.data(), read_chunk.size()),
        &bytes_read);
    if (read_result == IoResult::Ok) {
      if (bytes_read == 0U) {
        submit_close(scheduler, fd);
        return;
      }
      session.stream_buffer().append(
          std::span<const uint8_t>(read_chunk.data(), bytes_read));
      total_read += bytes_read;
      continue;
    }
    if (read_result == IoResult::WouldBlock) {
      break;
    }

    submit_close(scheduler, fd);
    return;
  }

  std::size_t packets_processed = 0U;
  while (!close_after_flush && packets_processed < k_decode_packet_budget) {
    const DecodeOutcome outcome = decode_one_packet(session, broker);
    if (outcome == DecodeOutcome::NeedMore) {
      break;
    }
    if (outcome == DecodeOutcome::ProtocolError ||
        outcome == DecodeOutcome::Disconnected) {
      close_after_flush = true;
      break;
    }
    ++packets_processed;
  }

  drain_outbound_to_write_buffer(session, broker);
  if (!move_pending_frames_to_slot(session, slot)) {
    submit_close(scheduler, fd);
    return;
  }

  if (close_after_flush && slot.write_size() == 0U) {
    submit_close(scheduler, fd);
    return;
  }

  if (close_after_flush) {
    session.set_phase(ConnectionSession::Phase::Closing);
  }

  if (slot.write_size() > 0U) {
    reactor.arm_write(fd);
    scheduler.submit(ConnectionJob{.type = JobType::Drain,
                                   .connection_fd = fd,
                                   .payload = DrainJobPayload{}});
  }
}

void process_drain_job(int fd, ConnectionTable &table, IoReactor &reactor,
                       Broker &broker) {
  ConnectionTable::Entry *entry = table.find(fd);
  if (entry == nullptr || entry->session == nullptr) {
    return;
  }

  ConnectionSession &session = *entry->session;
  ConnectionSlot &slot = entry->slot;

  const bool close_after_flush = prepare_session_takeover_close(session);

  drain_outbound_to_write_buffer(session, broker);
  if (!move_pending_frames_to_slot(session, slot)) {
    process_close_job(fd, table, reactor, broker);
    return;
  }

  if (!drain_socket_write_buffer(slot)) {
    process_close_job(fd, table, reactor, broker);
    return;
  }

  if (slot.write_size() > 0U) {
    reactor.arm_write(fd);
    return;
  }
  reactor.disarm_write(fd);

  if (close_after_flush || session.phase() == ConnectionSession::Phase::Closing) {
    process_close_job(fd, table, reactor, broker);
  }
}

void process_close_job(int fd, ConnectionTable &table, IoReactor &reactor,
                       Broker &broker) {
  ConnectionTable::Entry *entry = table.find(fd);
  if (entry == nullptr || entry->session == nullptr) {
    reactor.unregister(fd);
    (void)table.remove(fd);
    return;
  }

  ConnectionSession &session = *entry->session;
  finalize_close(session, broker);
  session.connection().close();
  reactor.unregister(fd);
  (void)table.remove(fd);
}

} // namespace client_handler

} // namespace mqtt
