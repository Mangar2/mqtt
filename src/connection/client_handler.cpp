/**
 * @file client_handler.cpp
 * @brief Stateless connection job processors.
 */

#include "connection/client_handler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
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

class DecodeGuard {
public:
  explicit DecodeGuard(ConnectionSession &session) noexcept
      : session_(session), active_(session.try_begin_decode()) {}

  DecodeGuard(const DecodeGuard &) = delete;
  DecodeGuard &operator=(const DecodeGuard &) = delete;

  ~DecodeGuard() {
    if (active_) {
      session_.end_decode();
    }
  }

  [[nodiscard]] bool active() const noexcept { return active_; }

private:
  ConnectionSession &session_;
  bool active_;
};

constexpr std::size_t k_decode_read_chunk_size = 4096U;
constexpr std::size_t k_decode_read_budget_bytes = 64U * 1024U;
constexpr std::size_t k_decode_packet_budget = 32U;
constexpr std::size_t k_drain_write_budget_bytes = 64U * 1024U;
constexpr auto k_handshake_timeout = std::chrono::seconds(30);
constexpr auto k_session_takeover_grace = std::chrono::milliseconds(100);

std::atomic<std::uint64_t> g_socket_write_bytes_total{0U};
std::atomic<std::uint64_t> g_outbound_publish_total{0U};
std::atomic<std::uint64_t> g_inbound_socket_bytes_total{0U};
std::atomic<std::uint64_t> g_decode_rescheduled_total{0U};
std::atomic<std::uint64_t> g_decode_packet_budget_exhausted_total{0U};
std::atomic<std::uint64_t> g_decode_read_budget_exhausted_total{0U};
std::atomic<std::uint64_t> g_decode_streambuffer_pending_total{0U};
std::atomic<std::uint64_t> g_drain_rescheduled_total{0U};
std::atomic<std::uint64_t> g_drain_write_budget_exhausted_total{0U};

void note_decode_rescheduled_debug(bool packet_budget_exhausted,
                                   bool read_budget_exhausted,
                                   bool streambuffer_has_packet) {
  if (packet_budget_exhausted) {
    (void)g_decode_packet_budget_exhausted_total.fetch_add(1U);
  }
  if (read_budget_exhausted) {
    (void)g_decode_read_budget_exhausted_total.fetch_add(1U);
  }
  if (streambuffer_has_packet) {
    (void)g_decode_streambuffer_pending_total.fetch_add(1U);
  }

  const std::uint64_t decode_rescheduled_total =
      g_decode_rescheduled_total.fetch_add(1U) + 1U;
  if ((decode_rescheduled_total % 100U) == 0U) {
    const std::uint64_t packet_budget_total =
        g_decode_packet_budget_exhausted_total.load();
    const std::uint64_t read_budget_total =
        g_decode_read_budget_exhausted_total.load();
    const std::uint64_t streambuffer_pending_total =
        g_decode_streambuffer_pending_total.load();
    std::cout << "[debug] decode_rescheduled_total="
              << decode_rescheduled_total
              << " decode_packet_budget_exhausted_total="
              << packet_budget_total
              << " decode_read_budget_exhausted_total="
              << read_budget_total
              << " decode_streambuffer_pending_total="
              << streambuffer_pending_total << std::endl;
  }
}

void note_drain_rescheduled_debug(bool write_budget_exhausted) {
  if (write_budget_exhausted) {
    (void)g_drain_write_budget_exhausted_total.fetch_add(1U);
  }

  const std::uint64_t drain_rescheduled_total =
      g_drain_rescheduled_total.fetch_add(1U) + 1U;
  if ((drain_rescheduled_total % 100U) == 0U) {
    const std::uint64_t write_budget_total =
        g_drain_write_budget_exhausted_total.load();
    std::cout << "[debug] drain_rescheduled_total="
              << drain_rescheduled_total
              << " drain_write_budget_exhausted_total="
              << write_budget_total << std::endl;
  }
}

void note_inbound_socket_bytes_debug(std::size_t bytes_received) {
  if (bytes_received == 0U) {
    return;
  }

  const std::uint64_t previous_total =
      g_inbound_socket_bytes_total.fetch_add(bytes_received);
  const std::uint64_t updated_total = previous_total + bytes_received;
  if (updated_total % 20021 != 0U) {
    return;
  }

  std::cout << "[debug] inbound_socket_bytes_total=" << updated_total
            << std::endl;
}

void note_outbound_publish_debug(const WriteBuffer &frame) {
  if (frame.empty()) {
    return;
  }

  constexpr uint8_t k_publish_packet_type = 3U;
  const uint8_t packet_type =
      static_cast<uint8_t>((frame.front() >> 4U) & 0x0FU);
  if (packet_type != k_publish_packet_type) {
    return;
  }

  const std::uint64_t publish_total =
      g_outbound_publish_total.fetch_add(1U) + 1U;

  if ((publish_total % 1000U) == 0U) {
    std::cout << "[debug] outbound_publish_enqueued_total="
              << publish_total << std::endl;
  }
}

void note_socket_write_debug(std::size_t bytes_written) {
  if (bytes_written == 0U) {
    return;
  }

  const std::uint64_t bytes_total =
      g_socket_write_bytes_total.fetch_add(bytes_written) + bytes_written;
  if ((bytes_total % 812024U) == 0U) {
    std::cout << "[debug] outbound_socket_bytes_total="
              << bytes_total << std::endl;
  }
}

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
    note_outbound_publish_debug(frame);
  }
  session.clear_pending_write_frames();
  return true;
}

struct WriteDrainResult {
  bool success{false};
  bool write_budget_exhausted{false};
};

WriteDrainResult drain_socket_write_buffer(ConnectionSlot &slot,
                                           std::size_t write_budget_bytes) {
  std::size_t total_written = 0U;
  while (slot.write_size() > 0U) {
    if (total_written >= write_budget_bytes) {
      return WriteDrainResult{.success = true, .write_budget_exhausted = true};
    }

    const std::span<const uint8_t> chunk = slot.write_contiguous_bytes();
    if (chunk.empty()) {
      return WriteDrainResult{.success = true, .write_budget_exhausted = false};
    }

    std::size_t bytes_written = 0U;
    const IoResult write_result = nb_write(slot.fd(), chunk, &bytes_written);
    if (write_result == IoResult::Ok) {
      if (bytes_written == 0U) {
        return WriteDrainResult{.success = false,
                                .write_budget_exhausted = false};
      }
      note_socket_write_debug(bytes_written);
      (void)slot.pop_write_bytes(bytes_written);
      total_written += bytes_written;
      continue;
    }
    if (write_result == IoResult::WouldBlock) {
      return WriteDrainResult{.success = true, .write_budget_exhausted = false};
    }
    return WriteDrainResult{.success = false, .write_budget_exhausted = false};
  }
  return WriteDrainResult{.success = true, .write_budget_exhausted = false};
}

void submit_close(JobScheduler &scheduler, int fd) {
  scheduler.submit(ConnectionJob{.type = JobType::Close,
                                 .connection_fd = fd,
                                 .payload = CloseJobPayload{.immediate = false}});
}

bool prepare_session_takeover_close(ConnectionSession &session) {
  if (session.consume_session_takeover_request()) {
    session.arm_session_takeover_close(k_session_takeover_grace);
  }

  if (!session.is_session_takeover_close_due(std::chrono::steady_clock::now())) {
    return false;
  }

  session.clear_session_takeover_close_pending();
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

  std::unique_ptr<WebSocketTransport> ws_transport;
  if (payload.websocket_connection) {
    try {
      ws_transport = std::make_unique<WebSocketTransport>(*connection);
    } catch (...) {
      connection->close();
      return;
    }
  }

  if (set_nonblocking(payload.socket_handle) != IoResult::Ok) {
    connection->close();
    return;
  }

  auto session = std::make_unique<ConnectionSession>(
      std::move(connection), std::move(ws_transport),
      payload.websocket_connection, config);

  const std::size_t slot_write_capacity = std::max<std::size_t>(
      ConnectionSlot::k_default_write_capacity,
      static_cast<std::size_t>(config.write_queue_max_bytes) + 4096U);

  const int fd = static_cast<int>(payload.socket_handle);
  if (!table.add(
          fd,
          ConnectionSlot(payload.socket_handle,
                         ConnectionSlot::k_default_read_capacity,
                         slot_write_capacity),
          std::move(session))) {
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
  DecodeGuard decode_guard(session);
  if (!decode_guard.active()) {
    return;
  }

  ConnectionSlot &slot = entry->slot;

  if (session.phase() == ConnectionSession::Phase::Handshake &&
      std::chrono::steady_clock::now() - session.accepted_at() >=
          k_handshake_timeout) {
    submit_close(scheduler, fd);
    return;
  }

  bool close_after_flush = false;

  if (!close_after_flush &&
      session.phase() == ConnectionSession::Phase::Connected &&
      session.client_session() != nullptr) {
    auto &keep_alive_timer = session.client_session()->keep_alive_timer();
    if (keep_alive_timer.is_enabled() && keep_alive_timer.is_expired()) {
      session.disconnect_state().clean_disconnect = true;
      session.disconnect_state().reason_code = ReasonCode::KeepAliveTimeout;
      session.pending_write_frames().push_back(
          encode_disconnect_packet(ReasonCode::KeepAliveTimeout));
      session.set_phase(ConnectionSession::Phase::Closing);
      close_after_flush = true;
    }
  }

  std::array<uint8_t, k_decode_read_chunk_size> read_chunk{};
  std::size_t total_read = 0U;
  bool peer_closed = false;
  while (!close_after_flush && total_read < k_decode_read_budget_bytes) {
    if (session.is_websocket()) {
      WebSocketTransport *ws_transport = session.ws_transport();
      if (ws_transport == nullptr) {
        submit_close(scheduler, fd);
        return;
      }

      WsReadChunk ws_chunk = ws_transport->read_chunk();
      if (ws_chunk.timed_out) {
        break;
      }

      if (!ws_chunk.data.empty()) {
        session.stream_buffer().append(
            std::span<const uint8_t>(ws_chunk.data.data(), ws_chunk.data.size()));
        total_read += ws_chunk.data.size();
        note_inbound_socket_bytes_debug(ws_chunk.data.size());
      }

      if (ws_chunk.eof) {
        peer_closed = true;
        break;
      }

      if (ws_chunk.data.empty()) {
        break;
      }
      continue;
    }

    std::size_t bytes_read = 0U;
    const IoResult read_result = nb_read(
        slot.fd(), std::span<uint8_t>(read_chunk.data(), read_chunk.size()),
        &bytes_read);
    if (read_result == IoResult::Ok) {
      if (bytes_read == 0U) {
        peer_closed = true;
        break;
      }
      session.stream_buffer().append(
          std::span<const uint8_t>(read_chunk.data(), bytes_read));
      total_read += bytes_read;
      note_inbound_socket_bytes_debug(bytes_read);
      continue;
    }
    if (read_result == IoResult::WouldBlock) {
      break;
    }

    if (read_result == IoResult::Closed) {
      peer_closed = true;
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

  const bool packet_budget_exhausted =
      packets_processed >= k_decode_packet_budget;
  const bool read_budget_exhausted = total_read >= k_decode_read_budget_bytes;
  const bool streambuffer_has_packet =
      session.stream_buffer().has_complete_packet();
  // NOTE: peer_closed must NOT suppress the reschedule. When the peer sends
  // FIN after a burst, more complete packets may still sit in stream_buffer.
  // Suppressing reschedule here would silently drop those packets and trigger
  // the close path before they are processed. The rescheduled Decode handles
  // the EOF on its own read attempt and continues draining the buffer until
  // empty, then closes naturally.
  const bool should_reschedule_decode =
      !close_after_flush &&
      (packet_budget_exhausted || read_budget_exhausted ||
       streambuffer_has_packet);
  if (should_reschedule_decode) {
    scheduler.submit(ConnectionJob{.type = JobType::Decode,
                                   .connection_fd = fd,
                                   .payload = DecodeJobPayload{}});
    note_decode_rescheduled_debug(packet_budget_exhausted,
                                  read_budget_exhausted,
                                  streambuffer_has_packet);
  }

  // Only treat peer_closed as a definitive close-after-flush signal once the
  // stream buffer has been fully consumed; otherwise we let the rescheduled
  // Decode finish processing the remaining packets first.
  if (peer_closed && !streambuffer_has_packet) {
    close_after_flush = true;
  }

  if (!close_after_flush) {
    close_after_flush = prepare_session_takeover_close(session);
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
                       JobScheduler &scheduler, Broker &broker) {
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

  const WriteDrainResult write_drain_result =
      drain_socket_write_buffer(slot, k_drain_write_budget_bytes);
  if (!write_drain_result.success) {
    process_close_job(fd, table, reactor, broker);
    return;
  }

  if (slot.write_size() > 0U) {
    reactor.arm_write(fd);
    if (write_drain_result.write_budget_exhausted) {
      scheduler.submit(ConnectionJob{.type = JobType::Drain,
                                     .connection_fd = fd,
                                     .payload = DrainJobPayload{}});
      note_drain_rescheduled_debug(true);
    }
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
