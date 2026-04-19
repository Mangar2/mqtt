#pragma once

/**
 * @file connect_phase_flow.h
 * @brief CONNECT and authentication phase orchestration.
 */

#include <atomic>
#include <functional>
#include <optional>

#include "broker/broker.h"
#include "data_model/packet/connect_packet.h"

namespace mqtt {

class StreamBuffer;
class TcpConnection;
class WebSocketTransport;
class WriteQueue;

[[nodiscard]] bool establish_connect_session(
    TcpConnection &connection, WebSocketTransport *ws_transport,
    bool is_websocket, Broker &broker, StreamBuffer &stream_buffer,
    WriteQueue &write_queue, std::optional<ConnectPacket> &connect_packet,
    ConnectResult &connect_result,
    std::atomic<bool> &session_takeover_requested,
    const std::function<void()> &stop_transport);

} // namespace mqtt
