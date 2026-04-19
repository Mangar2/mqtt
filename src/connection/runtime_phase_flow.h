#pragma once

/**
 * @file runtime_phase_flow.h
 * @brief Runtime packet loop for connected sessions.
 */

#include <atomic>

#include "connection/connection_flow_support.h"

namespace mqtt {

class Broker;
class ClientSession;
struct ConnectPacket;
struct ConnectResult;
class StreamBuffer;
class TcpConnection;
class WebSocketTransport;
class WriteQueue;

void run_connected_session_loop(
    TcpConnection &connection, WebSocketTransport *ws_transport,
    bool is_websocket, const ConnectPacket &connect_packet,
    const ConnectResult &connect_result,
    std::atomic<bool> &session_takeover_requested, StreamBuffer &stream_buffer,
    ClientSession &client_session, Broker &broker, WriteQueue &write_queue,
    RuntimeDisconnectState &disconnect_state,
    uint16_t inbound_receive_maximum);

} // namespace mqtt
